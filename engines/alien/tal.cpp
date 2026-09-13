/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/debug.h"
#include "common/file.h"
#include "common/path.h"
#include "common/stream.h"
#include "common/util.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/tal.h"

namespace Alien {

// The first offset word is 200 in every shipped file, which is where the text
// blob begins: 200 bytes of table, so 100 dialog slots.
static const uint16 kTextBlobStart = 200;

TalFile::TalFile() : _loaded(false) {
	for (uint i = 0; i < kEntryCount; i++)
		_overrides[i] = nullptr;
}

void TalFile::clear() {
	_name.clear();
	for (uint i = 0; i < kEntryCount; i++)
		_overrides[i] = nullptr;
	for (uint i = 0; i < kEntryCount; i++)
		_entries[i] = Entry();
	for (uint i = 0; i < kOutcomeCount; i++)
		_outcomes[i] = Outcome();
	for (uint t = 0; t < kChatTopics; t++)
		for (uint o = 0; o < kChatOptions; o++)
			_chat[t][o] = ChatOption();
	_loaded = false;
}

bool TalFile::load(const Common::Path &path, const AlienPack *pack) {
	clear();

	Common::File f;
	if (!f.open(path)) {
		warning("Alien::TalFile: could not open %s", path.toString().c_str());
		return false;
	}
	return loadStream(f, pack, AlienPack::talKey(path));
}

bool TalFile::loadStream(Common::SeekableReadStream &stream, const AlienPack *pack,
						 const Common::String &name) {
	clear();
	_name = AlienPack::talKey(name);

	uint32 size = (uint32)stream.size();
	if (size < kZone2Base + kTextBlobStart) {
		warning("Alien::TalFile: %u bytes is shorter than zone 1 plus the offset table", size);
		return false;
	}

	byte *data = new byte[size];
	if (stream.read(data, size) != size) {
		warning("Alien::TalFile: short read");
		delete[] data;
		return false;
	}

	for (uint code = 0; code < kOutcomeCount; code++) {
		const byte *rec = data + code * kOutcomeSize;
		if (!rec[0])
			continue;
		_outcomes[code].count = MIN<byte>(rec[0], kMaxOutcomeIds);
		memcpy(_outcomes[code].ids, rec + 1, _outcomes[code].count);
	}

	// An entry ends where the next offset in use begins. The shipped files
	// list the offsets in ascending order, but resolving the successor by
	// value rather than by slot costs nothing and does not rely on that.
	uint16 offsets[kEntryCount];
	for (uint i = 0; i < kEntryCount; i++)
		offsets[i] = READ_LE_UINT16(data + kZone2Base + i * 2);

	// The text stops where the conversation tree starts: the original's text
	// read is `filesize - 0x898 - 0x3fc` bytes long, so an entry that ran to the
	// end of the file would take the tree's bytes for lines.
	const uint32 blobEnd = size > kZone2Base + kChatTable ? size - kZone2Base - kChatTable
														  : size - kZone2Base;

	for (uint i = 0; i < kEntryCount; i++) {
		if (!offsets[i])
			continue;

		uint32 end = blobEnd;
		for (uint j = 0; j < kEntryCount; j++) {
			if (offsets[j] > offsets[i] && offsets[j] < end)
				end = offsets[j];
		}

		if (offsets[i] < kTextBlobStart || offsets[i] >= end || end > blobEnd) {
			warning("Alien::TalFile: entry %u has an out of range offset %u", i, offsets[i]);
			continue;
		}

		if (parseEntry(data, size, offsets[i], end, _entries[i]))
			_entries[i].present = true;
	}

	// The conversation tree, read from the end of the file the way OBJ:sub_04225
	// reads it: 51 topics of four five-byte options. Most rooms carry an
	// all-zero table -- only the ones with somebody to talk to fill it in.
	if (size >= kChatTable) {
		const byte *tree = data + size - kChatTable;
		for (uint t = 0; t < kChatTopics; t++) {
			for (uint o = 0; o < kChatOptions; o++) {
				const byte *rec = tree + t * kChatOptions * kChatRecord + o * kChatRecord;
				_chat[t][o].entry = rec[0];
				_chat[t][o].line = rec[1];
				_chat[t][o].lines = rec[2];
				_chat[t][o].reply = rec[3];
				_chat[t][o].next = rec[4];
			}
		}
	}

	delete[] data;
	_loaded = true;

	if (pack && pack->isLoaded() && !_name.empty())
		applyPack(*pack);

	debugC(1, kDebugResource, "TAL: %u bytes, %u dialog entries", size, usedEntries());
	return true;
}

/**
 * Lay the pack's rows for this file over what was read.
 *
 * Replacement lines and replacement chains are folded into the file here, so
 * everything downstream -- the renderer, the hover labels, the chat slices --
 * sees them without knowing a pack exists. The rest of an override is timing
 * and colour, which belong to the moment the line goes up rather than to the
 * file, and those are left for the caller to read back through textOverride().
 */
void TalFile::applyPack(const AlienPack &pack) {
	for (uint id = 0; id < kEntryCount; id++) {
		const AlienPack::TextOverride *ov = pack.text(_name, (byte)id);
		if (!ov)
			continue;

		// An override names the text it was written against. When the file has
		// moved under it the row is still applied -- the edit is what somebody
		// meant, and refusing it silently would be worse -- but the drift is
		// said out loud, because a duration written for one line is rarely
		// right for another.
		if (ov->hasHash && ov->hash != AlienPack::textHash(_entries[id].lines))
			warning("Alien::TalFile: %s entry %u has changed since its override was "
					"written; the override is being applied to the new text",
					_name.c_str(), id);

		_overrides[id] = ov;

		if (ov->has(AlienPack::kTextLines)) {
			_entries[id].lines.clear();
			for (uint i = 0; i < ov->lines.size(); i++)
				_entries[id].lines.push_back(ov->lines[i]);
			// The line count is also the layout selector, so a replacement that
			// is a different number of lines is drawn in the box that fits it.
			_entries[id].lineCount = (byte)ov->lines.size();
			_entries[id].present = true;
		}
	}

	for (uint code = 0; code < kOutcomeCount; code++) {
		const Common::Array<byte> *chain = pack.outcome(_name, (byte)code);
		if (!chain)
			continue;
		_outcomes[code].count = (byte)MIN<uint>(chain->size(), kMaxOutcomeIds);
		memset(_outcomes[code].ids, 0, sizeof(_outcomes[code].ids));
		for (uint i = 0; i < _outcomes[code].count; i++)
			_outcomes[code].ids[i] = (*chain)[i];
	}
}

const AlienPack::TextOverride *TalFile::textOverride(uint id) const {
	return id < kEntryCount ? _overrides[id] : nullptr;
}

bool TalFile::parseEntry(const byte *data, uint32 size, uint32 start, uint32 end, Entry &out) const {
	uint32 pos = kZone2Base + start;
	const uint32 stop = MIN<uint32>(kZone2Base + end, size);

	if (pos >= stop)
		return false;

	out.lineCount = data[pos++];

	while (pos < stop && out.lines.size() < kMaxLines) {
		uint len = data[pos++];
		// A ragged tail is kept as far as it goes rather than dropping the
		// whole entry; the shipped files tile their ranges exactly, so this
		// only guards against damaged data.
		len = MIN<uint32>(len, stop - pos);
		out.lines.push_back(Common::String((const char *)(data + pos), len));
		pos += len;
	}

	return true;
}

const TalFile::Entry &TalFile::entry(uint id) const {
	if (id >= kEntryCount || !_entries[id].present)
		return _empty;
	return _entries[id];
}

const TalFile::Outcome &TalFile::outcome(uint code) const {
	if (code >= kOutcomeCount)
		return _emptyOutcome;
	return _outcomes[code];
}

const TalFile::ChatOption &TalFile::chatOption(uint topic, uint option) const {
	if (topic >= kChatTopics || option >= kChatOptions)
		return _emptyOption;
	return _chat[topic][option];
}

uint TalFile::chatOptionCount(uint topic) const {
	// OBJ:sub_0456e counts an option in when any of its five bytes is non-zero,
	// and stops at the first gap the same way -- the options of a topic are
	// always filled from the top.
	uint n = 0;
	while (n < kChatOptions && chatOption(topic, n).present())
		n++;
	return n;
}

/**
 * Every room's dialog, and what the port believes about how it is spoken.
 *
 * tools/check_dialog.py prints the same lines out of TALFILES and the pack, so
 * the two can be diffed: this is the mirror the text layer never had. The text
 * itself is not printed -- it is CP850 and the harness would have to agree
 * about the encoding on both sides -- but its length and a hash of it are,
 * which is what catches a replacement line going astray.
 *
 * A file several rooms share is dumped once, under the first room that names
 * it, because that is what the engine would read on entering any of them.
 */
void AlienEngine::sweepDialog() {
	TalFile tal;
	Common::Array<Common::String> seen;

	for (int room = 0; room < StaticTables::kRoomCount; room++) {
		RoomAssets assets;
		Common::Path script;
		if (_overlays.readRoom(room, assets) && !assets.script.empty())
			script = Common::Path(assets.script);
		else
			script = Common::Path(Common::String::format("ROOM%d.TAL", room));

		const Common::String name = AlienPack::talKey(script);
		bool already = false;
		for (uint i = 0; i < seen.size(); i++)
			already |= seen[i] == name;
		if (already)
			continue;

		if (!Common::File::exists(script) || !tal.load(script, &_pack))
			continue;
		seen.push_back(name);

		debug("dialog room %2d %s: %u entries", room, name.c_str(), tal.usedEntries());

		for (uint id = 0; id < TalFile::kEntryCount; id++) {
			const TalFile::Entry &entry = tal.entry(id);
			if (!entry.present)
				continue;

			uint chars = 0;
			for (uint i = 0; i < entry.lines.size(); i++)
				chars += entry.lines[i].size();

			const AlienPack::TextOverride *ov = tal.textOverride(id);
			debug("text %s entry %3u type %u lines %u chars %4u ticks %5d "
				  "hash 0x%08x ov 0x%02x", name.c_str(), id, entry.lineCount,
				  entry.lines.size(), chars, speechTicksFor(tal, id),
				  AlienPack::textHash(entry.lines), ov ? ov->flags : 0);
		}

		for (uint code = 0; code < TalFile::kOutcomeCount; code++) {
			const TalFile::Outcome &chain = tal.outcome(code);
			if (!chain.count)
				continue;

			Common::String ids;
			for (uint i = 0; i < chain.count; i++)
				ids += Common::String::format(" %u", chain.ids[i]);
			debug("chain %s outcome %3u:%s", name.c_str(), code, ids.c_str());
		}
	}
}

uint TalFile::usedEntries() const {
	uint n = 0;
	for (uint i = 0; i < kEntryCount; i++)
		n += _entries[i].present ? 1 : 0;
	return n;
}

} // End of namespace Alien
