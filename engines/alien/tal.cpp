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

#include "alien/detection.h"
#include "alien/tal.h"

namespace Alien {

// The first offset word is 200 in every shipped file, which is where the text
// blob begins: 200 bytes of table, so 100 dialog slots.
static const uint16 kTextBlobStart = 200;

TalFile::TalFile() : _loaded(false) {
}

void TalFile::clear() {
	for (uint i = 0; i < kEntryCount; i++)
		_entries[i] = Entry();
	for (uint i = 0; i < kOutcomeCount; i++)
		_outcomes[i] = Outcome();
	_loaded = false;
}

bool TalFile::load(const Common::Path &path) {
	clear();

	Common::File f;
	if (!f.open(path)) {
		warning("Alien::TalFile: could not open %s", path.toString().c_str());
		return false;
	}
	return loadStream(f);
}

bool TalFile::loadStream(Common::SeekableReadStream &stream) {
	clear();

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

	const uint32 blobEnd = size - kZone2Base;

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

	delete[] data;
	_loaded = true;

	debugC(1, kDebugResource, "TAL: %u bytes, %u dialog entries", size, usedEntries());
	return true;
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

uint TalFile::usedEntries() const {
	uint n = 0;
	for (uint i = 0; i < kEntryCount; i++)
		n += _entries[i].present ? 1 : 0;
	return n;
}

} // End of namespace Alien
