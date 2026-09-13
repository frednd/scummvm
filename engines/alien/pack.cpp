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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/stream.h"

#include "alien/detection.h"
#include "alien/pack.h"

namespace Alien {

static const char *const kPackFile = "ALIEN.DAT";
static const uint32 kMagic = MKTAG('A', 'L', 'P', 'K');
static const uint16 kVersion = 1;

static const uint32 kTagStrings = MKTAG('S', 'T', 'R', 'S');
static const uint32 kTagText = MKTAG('T', 'E', 'X', 'T');
static const uint32 kTagOutcomes = MKTAG('T', 'O', 'V', 'R');
static const uint32 kTagManifest = MKTAG('T', 'M', 'A', 'N');
static const uint32 kTagTune = MKTAG('T', 'U', 'N', 'E');
static const uint32 kTagEffects = MKTAG('E', 'F', 'F', 'X');
static const uint32 kTagProcs = MKTAG('C', 'P', 'R', 'C');
static const uint32 kTagRecords = MKTAG('C', 'R', 'E', 'C');
static const uint32 kTagSteps = MKTAG('C', 'S', 'T', 'P');
static const uint32 kTagArms = MKTAG('C', 'A', 'R', 'M');
static const uint32 kTagTriggers = MKTAG('C', 'T', 'R', 'G');
static const uint32 kTagRoomEffects = MKTAG('R', 'E', 'F', 'X');
static const uint32 kTagBlocks = MKTAG('R', 'S', 'C', 'R');
static const uint32 kTagRoomIndex = MKTAG('R', 'I', 'D', 'X');
static const uint32 kTagFlags = MKTAG('F', 'L', 'A', 'G');

/// One ScriptCond as the pack stores it: address, value, negate, kind.
static const uint kCondSize = 5;

/// One ScriptEffect as the pack stores it: the fields, then three guard slots.
static const uint kEffectSize = 17 + 3 * 5;

static ScriptCond readCond(const byte *p) {
	ScriptCond cond;
	cond.addr = READ_LE_UINT16(p);
	cond.value = p[2];
	cond.negate = p[3] != 0;
	cond.kind = p[4];
	return cond;
}

/// What an unset string index reads as.
static const uint16 kNoString = 0xFFFF;

AlienPack::AlienPack() : _loaded(false) {
}

/**
 * One effect pool, read whole.
 *
 * The cutscene procedures and the room scripts keep their own pools -- they are
 * lifted by different tools and their indices are each other's business -- but
 * a row is the same row in both, so it is read in one place.
 */
void AlienPack::readEffects(const byte *body, uint32 size, Common::Array<ScriptEffect> &out) {
	if (!body || size < 2)
		return;

	const byte *p = body;
	const byte *end = body + size;
	const uint16 count = READ_LE_UINT16(p);
	p += 2;

	for (uint i = 0; i < count && p + kEffectSize <= end; i++, p += kEffectSize) {
		ScriptEffect effect;
		effect.op = p[0];
		effect.argCount = p[1];
		for (uint a = 0; a < ARRAYSIZE(effect.args); a++)
			effect.args[a] = READ_LE_UINT16(p + 2 + a * 2);
		effect.dynArgs = p[14];
		effect.bias = p[15];
		effect.guardCount = p[16];
		for (uint g = 0; g < ARRAYSIZE(effect.guards); g++)
			effect.guards[g] = readCond(p + 17 + g * kCondSize);
		out.push_back(effect);
	}
}

Common::String AlienPack::talKey(const Common::String &name) {
	// The engine has a bare name in hand wherever it opens one of these, but a
	// cutscene record can spell a path, so the base name is what matches.
	const size_t slash = name.findLastOf("/\\");
	Common::String out(slash == Common::String::npos ? name
													 : Common::String(name.c_str() + slash + 1));
	out.toUppercase();
	return out;
}

Common::String AlienPack::talKey(const Common::Path &path) {
	return talKey(path.toString('/'));
}

uint32 AlienPack::textHash(const Common::Array<Common::String> &lines) {
	uint32 h = 0x811C9DC5;
	for (uint i = 0; i < lines.size(); i++) {
		if (i)
			h = (h ^ 0) * 0x01000193;
		const Common::String &line = lines[i];
		for (uint j = 0; j < line.size(); j++)
			h = (h ^ (byte)line[j]) * 0x01000193;
	}
	return h;
}

Common::String AlienPack::key(const Common::String &talName, byte index) const {
	return Common::String::format("%s/%u", talKey(talName).c_str(), index);
}

bool AlienPack::load() {
	// A run can point at a pack of its own -- the golden master uses that to
	// diff the engine against a lift-only build rather than against whatever
	// the working tree's edits currently say.
	if (ConfMan.hasKey("alienpack")) {
		const Common::FSNode node((Common::Path(ConfMan.get("alienpack"))));
		Common::SeekableReadStream *stream = node.exists() ? node.createReadStream() : nullptr;
		if (!stream) {
			warning("Alien::AlienPack: alienpack names %s, which cannot be read",
					ConfMan.get("alienpack").c_str());
			return false;
		}
		const bool ok = loadStream(*stream);
		delete stream;
		return ok;
	}

	Common::File f;
	if (!f.open(Common::Path(kPackFile))) {
		// Not an error: without a pack every override is absent, which is the
		// DOS behaviour.
		debugC(1, kDebugResource, "pack: no %s, running on the lifted defaults", kPackFile);
		return false;
	}
	return loadStream(f);
}

bool AlienPack::loadStream(Common::SeekableReadStream &stream) {
	_text.clear();
	_outcomes.clear();
	_tune.clear();
	_strings.clear();
	_effects.clear();
	_procs.clear();
	_records.clear();
	_steps.clear();
	_arms.clear();
	_triggers.clear();
	_roomEffects.clear();
	_blocks.clear();
	_rooms.clear();
	_flags.clear();
	_loaded = false;

	const uint32 size = (uint32)stream.size();
	if (size < 8) {
		warning("Alien::AlienPack: %u bytes is shorter than the header", size);
		return false;
	}

	byte *data = new byte[size];
	if (stream.read(data, size) != size) {
		warning("Alien::AlienPack: short read");
		delete[] data;
		return false;
	}

	if (READ_BE_UINT32(data) != kMagic) {
		warning("Alien::AlienPack: not a pack");
		delete[] data;
		return false;
	}

	const uint16 version = READ_LE_UINT16(data + 4);
	if (version != kVersion) {
		warning("Alien::AlienPack: pack version %u, this build reads %u", version, kVersion);
		delete[] data;
		return false;
	}

	const uint16 sections = READ_LE_UINT16(data + 6);
	if (8 + (uint32)sections * 12 > size) {
		warning("Alien::AlienPack: the table of contents runs past the end of the file");
		delete[] data;
		return false;
	}

	// A section the build does not know about is skipped rather than refused, so
	// a pack written by a newer generator still plays as far as it can.
	static const uint32 wanted[] = { kTagText, kTagOutcomes, kTagManifest, kTagTune,
									 kTagEffects, kTagProcs, kTagRecords, kTagSteps,
									 kTagArms, kTagTriggers, kTagRoomEffects,
									 kTagBlocks, kTagRoomIndex, kTagFlags };
	const byte *body[ARRAYSIZE(wanted)];
	uint32 bodySize[ARRAYSIZE(wanted)];
	for (uint i = 0; i < ARRAYSIZE(wanted); i++) {
		body[i] = nullptr;
		bodySize[i] = 0;
	}

	const byte *strs = nullptr;
	uint32 strsSize = 0;

	for (uint i = 0; i < sections; i++) {
		const byte *row = data + 8 + i * 12;
		const uint32 tag = READ_BE_UINT32(row);
		const uint32 offset = READ_LE_UINT32(row + 4);
		const uint32 length = READ_LE_UINT32(row + 8);
		if (offset > size || length > size - offset) {
			warning("Alien::AlienPack: a section runs past the end of the file");
			delete[] data;
			return false;
		}
		if (tag == kTagStrings) {
			strs = data + offset;
			strsSize = length;
			continue;
		}
		for (uint w = 0; w < ARRAYSIZE(wanted); w++) {
			if (tag == wanted[w]) {
				body[w] = data + offset;
				bodySize[w] = length;
			}
		}
	}

	if (!strs || strsSize < 4) {
		warning("Alien::AlienPack: the pack has no string table");
		delete[] data;
		return false;
	}

	Common::Array<Common::String> &strings = _strings;
	strings.clear();
	{
		const uint32 count = READ_LE_UINT32(strs);
		uint32 p = 4;
		for (uint32 i = 0; i < count && p + 2 <= strsSize; i++) {
			const uint16 length = READ_LE_UINT16(strs + p);
			p += 2;
			if (p + length > strsSize)
				break;
			strings.push_back(Common::String((const char *)(strs + p), length));
			p += length;
		}
		if (strings.size() != count) {
			warning("Alien::AlienPack: the string table is short: %u of %u",
					strings.size(), count);
			delete[] data;
			return false;
		}
	}

	// A string index the writer left unset, or one no table entry backs, reads
	// as empty rather than reaching past the array.
	struct Local {
		static Common::String at(const Common::Array<Common::String> &table, uint16 index) {
			if (index == kNoString || index >= table.size())
				return Common::String();
			return table[index];
		}
	};

	if (body[0] && bodySize[0] >= 2) {
		const byte *p = body[0];
		const byte *end = body[0] + bodySize[0];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 17 <= end; i++) {
			TextOverride ov;
			const Common::String name = Local::at(strings, READ_LE_UINT16(p));
			const byte entry = p[2];
			ov.flags = READ_LE_UINT16(p + 3);
			ov.duration = (int16)READ_LE_UINT16(p + 5);
			ov.ticksPerChar = (int8)p[7];
			ov.color[0] = p[8];
			ov.color[1] = p[9];
			ov.color[2] = p[10];
			ov.anchorX = (int16)READ_LE_UINT16(p + 11);
			ov.anchorY = (int16)READ_LE_UINT16(p + 13);
			ov.band = p[15];
			const byte lineCount = p[16];
			p += 17;
			for (uint l = 0; l < lineCount && p + 2 <= end; l++) {
				ov.lines.push_back(Local::at(strings, READ_LE_UINT16(p)));
				p += 2;
			}
			_text[key(name, entry)] = ov;
		}
	}

	if (body[1] && bodySize[1] >= 2) {
		const byte *p = body[1];
		const byte *end = body[1] + bodySize[1];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 4 <= end; i++) {
			const Common::String name = Local::at(strings, READ_LE_UINT16(p));
			const byte code = p[2];
			const byte ids = p[3];
			p += 4;
			Common::Array<byte> chain;
			for (uint l = 0; l < ids && p < end; l++)
				chain.push_back(*p++);
			_outcomes[key(name, code)] = chain;
		}
	}

	// The manifest is folded into the overrides it belongs to rather than kept
	// on its own: it is only ever read alongside one.
	if (body[2] && bodySize[2] >= 2) {
		const byte *p = body[2];
		const byte *end = body[2] + bodySize[2];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 7 <= end; i++) {
			const Common::String name = Local::at(strings, READ_LE_UINT16(p));
			const byte entry = p[2];
			const uint32 hash = READ_LE_UINT32(p + 3);
			p += 7;
			if (_text.contains(key(name, entry))) {
				TextOverride &ov = _text[key(name, entry)];
				ov.hash = hash;
				ov.hasHash = true;
			}
		}
	}

	if (body[3] && bodySize[3] >= 2) {
		const byte *p = body[3];
		const byte *end = body[3] + bodySize[3];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 6 <= end; i++) {
			_tune[Local::at(strings, READ_LE_UINT16(p))] = (int32)READ_LE_UINT32(p + 2);
			p += 6;
		}
	}

	// The cutscene tables. A name field is a pointer into the string table
	// rather than a copy, which is what CutsceneRecord takes; the table is
	// complete by now and nothing appends to it afterwards, so the pointers hold
	// for as long as the pack does.
	struct Named {
		static const char *at(const Common::Array<Common::String> &table, uint16 index) {
			if (index == kNoString || index >= table.size())
				return nullptr;
			return table[index].c_str();
		}
	};

	readEffects(body[4], bodySize[4], _effects);

	if (body[5] && bodySize[5] >= 2) {
		const byte *p = body[5];
		const byte *end = body[5] + bodySize[5];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 6 <= end; i++, p += 6) {
			CutsceneProc proc;
			proc.addr = READ_LE_UINT16(p);
			proc.first = READ_LE_UINT16(p + 2);
			proc.count = READ_LE_UINT16(p + 4);
			_procs.push_back(proc);
		}
	}

	if (body[6] && bodySize[6] >= 2) {
		const byte *p = body[6];
		const byte *end = body[6] + bodySize[6];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 60 <= end; i++, p += 60) {
			CutsceneRecord rec;
			rec.pcx = Named::at(strings, READ_LE_UINT16(p));
			rec.tal = Named::at(strings, READ_LE_UINT16(p + 2));
			rec.music = (int16)READ_LE_UINT16(p + 4);
			for (uint b = 0; b < ARRAYSIZE(rec.banks); b++)
				rec.banks[b] = Named::at(strings, READ_LE_UINT16(p + 6 + b * 2));
			rec.mainProc = READ_LE_UINT16(p + 26);
			for (uint s = 0; s < ARRAYSIZE(rec.subProcs); s++)
				rec.subProcs[s] = READ_LE_UINT16(p + 28 + s * 2);
			for (uint n = 0; n < ARRAYSIZE(rec.points); n++)
				rec.points[n] = (int16)READ_LE_UINT16(p + 42 + n * 2);
			for (uint c = 0; c < ARRAYSIZE(rec.colors); c++)
				rec.colors[c] = p[50 + c];
			rec.firstStep = READ_LE_UINT16(p + 56);
			rec.stepCount = READ_LE_UINT16(p + 58);
			_records.push_back(rec);
		}
	}

	if (body[7] && bodySize[7] >= 2) {
		const byte *p = body[7];
		const byte *end = body[7] + bodySize[7];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 2 <= end; i++, p += 2) {
			CutsceneStep step;
			step.op = p[0];
			step.arg = p[1];
			_steps.push_back(step);
		}
	}

	if (body[8] && bodySize[8] >= 2) {
		const byte *p = body[8];
		const byte *end = body[8] + bodySize[8];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 12 + 2 * kCondSize <= end; i++) {
			CutsceneArm arm;
			arm.id = p[0];
			arm.latch = READ_LE_UINT16(p + 1);
			arm.guardCount = p[3];
			for (uint g = 0; g < ARRAYSIZE(arm.guards); g++)
				arm.guards[g] = readCond(p + 4 + g * kCondSize);
			const byte *tail = p + 4 + 2 * kCondSize;
			arm.recordCount = tail[0];
			arm.records[0] = tail[1];
			arm.records[1] = tail[2];
			arm.first = READ_LE_UINT16(tail + 3);
			arm.count = READ_LE_UINT16(tail + 5);
			arm.studio = tail[7] != 0;
			p = tail + 8;
			_arms.push_back(arm);
		}
	}

	if (body[9] && bodySize[9] >= 2) {
		const byte *p = body[9];
		const byte *end = body[9] + bodySize[9];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 3 + 3 * kCondSize <= end; i++) {
			CutsceneTrigger trigger;
			trigger.room = p[0];
			trigger.scene = p[1];
			trigger.guardCount = p[2];
			for (uint g = 0; g < ARRAYSIZE(trigger.guards); g++)
				trigger.guards[g] = readCond(p + 3 + g * kCondSize);
			p += 3 + 3 * kCondSize;
			_triggers.push_back(trigger);
		}
	}

	// The room scripts: the gating layer, in the same shapes.
	readEffects(body[10], bodySize[10], _roomEffects);

	if (body[11] && bodySize[11] >= 2) {
		const byte *p = body[11];
		const byte *end = body[11] + bodySize[11];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 11 + 4 * kCondSize <= end; i++) {
			ScriptBlock block;
			block.obj = (int16)READ_LE_UINT16(p);
			block.verb = (int16)READ_LE_UINT16(p + 2);
			block.item = (int16)READ_LE_UINT16(p + 4);
			block.condCount = p[6];
			for (uint c = 0; c < ARRAYSIZE(block.conds); c++)
				block.conds[c] = readCond(p + 7 + c * kCondSize);
			const byte *tail = p + 7 + 4 * kCondSize;
			block.first = READ_LE_UINT16(tail);
			block.count = READ_LE_UINT16(tail + 2);
			p = tail + 4;
			_blocks.push_back(block);
		}
	}

	if (body[12] && bodySize[12] >= 2) {
		const byte *p = body[12];
		const byte *end = body[12] + bodySize[12];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 7 <= end; i++, p += 7) {
			RoomBlocks row;
			row.room = p[0];
			row.first = READ_LE_UINT16(p + 1);
			row.count = READ_LE_UINT16(p + 3);
			_rooms.push_back(row);
		}
	}

	if (body[13] && bodySize[13] >= 2) {
		const byte *p = body[13];
		const byte *end = body[13] + bodySize[13];
		const uint16 count = READ_LE_UINT16(p);
		p += 2;
		for (uint i = 0; i < count && p + 3 <= end; i++, p += 3) {
			ScriptFlagInit flag;
			flag.addr = READ_LE_UINT16(p);
			flag.value = p[2];
			_flags.push_back(flag);
		}
	}

	delete[] data;
	_loaded = true;

	debugC(1, kDebugResource, "pack: %u bytes, %u text override%s, %u outcome override%s, "
		   "%u tunable%s", size, _text.size(), _text.size() == 1 ? "" : "s",
		   _outcomes.size(), _outcomes.size() == 1 ? "" : "s",
		   _tune.size(), _tune.size() == 1 ? "" : "s");
	debugC(1, kDebugResource, "pack: %u cutscene record%s, %u step%s, %u arm%s, "
		   "%u trigger%s, %u procedure%s over %u effect%s",
		   _records.size(), _records.size() == 1 ? "" : "s",
		   _steps.size(), _steps.size() == 1 ? "" : "s",
		   _arms.size(), _arms.size() == 1 ? "" : "s",
		   _triggers.size(), _triggers.size() == 1 ? "" : "s",
		   _procs.size(), _procs.size() == 1 ? "" : "s",
		   _effects.size(), _effects.size() == 1 ? "" : "s");
	return true;
}

const AlienPack::TextOverride *AlienPack::text(const Common::String &talName, byte entry) const {
	if (!_loaded)
		return nullptr;
	Common::HashMap<Common::String, TextOverride>::const_iterator it = _text.find(key(talName, entry));
	return it == _text.end() ? nullptr : &it->_value;
}

const Common::Array<byte> *AlienPack::outcome(const Common::String &talName, byte code) const {
	if (!_loaded)
		return nullptr;
	Common::HashMap<Common::String, Common::Array<byte> >::const_iterator it =
		_outcomes.find(key(talName, code));
	return it == _outcomes.end() ? nullptr : &it->_value;
}

const ScriptBlock *AlienPack::blocksForRoom(int room, uint &count) const {
	count = 0;
	for (uint i = 0; i < _rooms.size(); i++) {
		if (_rooms[i].room != room)
			continue;
		if (_rooms[i].first + _rooms[i].count > _blocks.size())
			return nullptr;
		count = _rooms[i].count;
		return count ? &_blocks[_rooms[i].first] : nullptr;
	}
	return nullptr;
}

int AlienPack::tunable(const char *name, int fallback) const {
	if (!_loaded)
		return fallback;
	Common::HashMap<Common::String, int>::const_iterator it = _tune.find(Common::String(name));
	return it == _tune.end() ? fallback : it->_value;
}

} // End of namespace Alien
