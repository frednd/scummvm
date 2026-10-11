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

#include "alien/detection.h"
#include "alien/tables.h"

namespace Alien {

// Data segment 0x271A, the same block the font metrics come from.
static const uint32 kDataSegment = 0x29F70;

// Both tables are 60 Pascal strings of 13 bytes each, indexed by room - 1.
static const uint32 kTableBackground = 0x2940;
static const uint32 kTableSecondPlate = 0x2C4C;
static const uint32 kRoomStride = 13;
static const uint kMaxNameLength = kRoomStride - 1;

// The verb names, same stride, indexed by verb code. "Walk to" sits just in
// front of the table and is the default hover text, with "Swim to", "USE" and
// "WITH" between them on an 11-byte stride of their own.
static const uint32 kTableVerbs = 0x3221;
static const uint32 kWalkVerb = 0x3202;
static const uint32 kSwimVerb = 0x320d;
static const uint32 kUseVerb = 0x3218;
static const uint32 kWithVerb = 0x3223;

// The SFX side of the audio tables: a bank selector indexed by the room, and the
// bank file names as Pascal strings on a 256-byte stride -- the tail of a
// MIDASmodule struct the game keeps in the executable. The music names sit in the
// parallel table at 0x0008 and land with the replayer.
static const uint32 kTableSfxBank = 0x1D97;
static const uint32 kTableMusicNames = 0x0008;
static const uint32 kTableSlotModule = 0x0B7E;
static const uint32 kTableSlotOrder = 0x0B8C;
static const uint32 kTableSfxNames = 0x0B9A;
static const uint32 kModuleStride = 0x100;
static const uint kMaxPathLength = 20;

// The item tables, all indexed by item id: how many outcome codes the item has
// with bit 7 the wrap flag, the codes themselves, and where the item's icon sits
// in the icon page.
static const uint32 kTableItemArity = 0x2FEB;
static const uint32 kTableItemOutcomes = 0x3019;
static const uint32 kTableIconX = 0x30F6;
static const uint32 kTableIconY = 0x3178;
static const byte kItemCycleBit = 0x80;

// 10c9:sub_119e2's refusals, indexed by its own counter at DS:0x30E2.
static const uint32 kTableRefusals = 0x30E4;

/**
 * The item-use tables 10c9:sub_11aae walks, one per room, each pushed by the
 * room's click entry as a DS offset. A table is a count and then records of
 * `n, item, object, n outcome codes`; item 0 takes any item. Room 15 reads a
 * second table while [0xa6d6] is 1 (ovr_0f_0e5b:0x0128), the shared hallway
 * overlay picks one by handler code (ovr_35_0f9e:0x0104) and the maze rooms
 * share theirs.
 */
struct UseTable {
	int room;
	uint16 table;
	bool needsA6d6;
};

static const UseTable kUseTables[] = {
	{  3, 0x67c4, false }, {  6, 0x678a, false }, {  7, 0x688a, false },
	{  8, 0x681c, false }, { 11, 0x6946, false }, { 13, 0x680e, false },
	{ 14, 0x69b4, false }, { 15, 0x68fe, false }, { 15, 0x6924, true  },
	{ 17, 0x6998, false }, { 18, 0x68d4, false }, { 19, 0x6950, false },
	{ 21, 0x6a26, false }, { 22, 0x6a8c, false }, { 25, 0x6a00, false },
	{ 26, 0x6980, false }, { 27, 0x6940, false }, { 28, 0x695e, false },
	{ 32, 0x6a9a, false }, { 34, 0x6992, false }, { 35, 0x6936, false },
	{ 43, 0x69a6, false }, { 44, 0x69a6, false }, { 45, 0x69a6, false },
	{ 46, 0x6a38, false }, { 48, 0x6a5e, false }, { 49, 0x6a7c, false },
	{ 50, 0x6a82, false }, { 51, 0x6ad6, false }, { 52, 0x6ae0, false },
	{ 53, 0x6b0e, false }, { 54, 0x6b30, false }, { 56, 0x6aa8, false },
	{ 57, 0x6afe, false }, { 59, 0x6b08, false },
};

StaticTables::StaticTables() : _loaded(false) {
	memset(_musicSlotModule, 0, sizeof(_musicSlotModule));
	memset(_musicSlotOrder, 0, sizeof(_musicSlotOrder));
	memset(_itemArity, 0, sizeof(_itemArity));
	memset(_itemOutcome, 0, sizeof(_itemOutcome));
	memset(_itemIconX, 0, sizeof(_itemIconX));
	memset(_itemIconY, 0, sizeof(_itemIconY));
}

/**
 * Read one Pascal ShortString out of an already positioned stream. Slots the
 * game never uses hold a zero length or blanks, both of which come back empty.
 */
static Common::String readName(Common::File &exe, uint limit = kMaxNameLength) {
	byte length = exe.readByte();
	if (length > limit)
		return Common::String();

	char buffer[kMaxPathLength + 1];
	if (exe.read(buffer, length) != length)
		return Common::String();
	buffer[length] = '\0';

	Common::String name(buffer);
	name.trim();
	return name;
}

bool StaticTables::load() {
	Common::File exe;
	if (!exe.open(Common::Path("GAME.EXE"))) {
		warning("Alien::StaticTables: could not open GAME.EXE");
		return false;
	}

	int rooms = 0;
	for (int i = 0; i < kRoomCount; i++) {
		exe.seek(kDataSegment + kTableBackground + i * kRoomStride);
		_background[i] = readName(exe);
		exe.seek(kDataSegment + kTableSecondPlate + i * kRoomStride);
		_secondPlate[i] = readName(exe);

		if (exe.eos() || exe.err()) {
			warning("Alien::StaticTables: GAME.EXE is too short for the room tables");
			return false;
		}

		if (!_background[i].empty())
			rooms++;
	}

	for (int i = 0; i < kVerbCount; i++) {
		exe.seek(kDataSegment + kTableVerbs + i * kRoomStride);
		_verb[i] = readName(exe);
	}

	exe.seek(kDataSegment + kWalkVerb);
	_walkVerb = readName(exe);
	exe.seek(kDataSegment + kSwimVerb);
	_swimVerb = readName(exe);
	exe.seek(kDataSegment + kUseVerb);
	_useVerb = readName(exe);
	exe.seek(kDataSegment + kWithVerb);
	_withVerb = readName(exe);

	exe.seek(kDataSegment + kTableItemArity);
	if (exe.read(_itemArity, sizeof(_itemArity)) != sizeof(_itemArity))
		return false;
	exe.seek(kDataSegment + kTableItemOutcomes);
	if (exe.read(_itemOutcome, sizeof(_itemOutcome)) != sizeof(_itemOutcome))
		return false;

	exe.seek(kDataSegment + kTableSfxBank);
	if (exe.read(_sfxBank, sizeof(_sfxBank)) != sizeof(_sfxBank))
		return false;

	exe.seek(kDataSegment + kTableSlotModule);
	if (exe.read(_musicSlotModule, sizeof(_musicSlotModule)) != sizeof(_musicSlotModule))
		return false;
	exe.seek(kDataSegment + kTableSlotOrder);
	if (exe.read(_musicSlotOrder, sizeof(_musicSlotOrder)) != sizeof(_musicSlotOrder))
		return false;

	for (int i = 0; i < kMusicCount; i++) {
		exe.seek(kDataSegment + kTableMusicNames + i * kModuleStride);
		_musicName[i] = readName(exe, kMaxPathLength);
	}

	for (int i = 0; i < kSfxBankCount; i++) {
		exe.seek(kDataSegment + kTableSfxNames + i * kModuleStride);
		_sfxName[i] = readName(exe, kMaxPathLength);
	}

	exe.seek(kDataSegment + kUseBlockStart);
	if (exe.read(_useBlock, sizeof(_useBlock)) != sizeof(_useBlock))
		return false;
	exe.seek(kDataSegment + kTableRefusals);
	if (exe.read(_refusal, sizeof(_refusal)) != sizeof(_refusal))
		return false;

	for (int i = 0; i < kItemCount; i++) {
		exe.seek(kDataSegment + kTableIconX + i * 2);
		_itemIconX[i] = exe.readUint16LE();
		exe.seek(kDataSegment + kTableIconY + i * 2);
		_itemIconY[i] = exe.readUint16LE();
	}

	debugC(1, kDebugResource, "room tables: %d rooms with a plate of their own, "
		   "verb 5 is \"%s\", item 1 has %d outcomes", rooms, _verb[5].c_str(),
		   itemArity(1));

	_loaded = rooms > 0;
	return _loaded;
}

const Common::String &StaticTables::background(int room) const {
	if (room < 1 || room > kRoomCount)
		return _empty;
	return _background[room - 1];
}

const Common::String &StaticTables::secondPlate(int room) const {
	if (room < 1 || room > kRoomCount)
		return _empty;
	return _secondPlate[room - 1];
}

const Common::String &StaticTables::verb(int code) const {
	if (code < 0 || code >= kVerbCount)
		return _empty;
	return _verb[code];
}

byte StaticTables::itemArity(int item) const {
	if (item < 1 || item >= kItemCount)
		return 0;
	return _itemArity[item] & ~kItemCycleBit;
}

bool StaticTables::itemCycles(int item) const {
	if (item < 1 || item >= kItemCount)
		return false;
	return (_itemArity[item] & kItemCycleBit) != 0;
}

byte StaticTables::itemOutcome(int item, int counter) const {
	// The original indexes one flat run, so the arithmetic is kept literal
	// rather than folded into a per-item record.
	if (item < 1 || item >= kItemCount || counter < 0 || counter > kItemOutcomes)
		return 0;
	return _itemOutcome[item * kItemOutcomes + counter];
}

int StaticTables::itemIconX(int item) const {
	if (item < 1 || item >= kItemCount)
		return 0;
	return _itemIconX[item];
}

int StaticTables::itemIconY(int item) const {
	if (item < 1 || item >= kItemCount)
		return 0;
	return _itemIconY[item];
}

byte StaticTables::sfxBank(int room) const {
	if (room < 0 || room >= kSfxRoomCount)
		return kSfxBankNone;
	return _sfxBank[room];
}

const Common::String &StaticTables::sfxName(int bank) const {
	if (bank < 0 || bank >= kSfxBankCount)
		return _empty;
	return _sfxName[bank];
}

const Common::String &StaticTables::musicName(int module) const {
	if (module < 0 || module >= kMusicCount)
		return _empty;
	return _musicName[module];
}

byte StaticTables::musicSlotModule(int slot) const {
	if (slot < 0 || slot >= kMusicSlotCount)
		return 0;
	return _musicSlotModule[slot];
}

byte StaticTables::musicSlotOrder(int slot) const {
	if (slot < 0 || slot >= kMusicSlotCount)
		return 0;
	return _musicSlotOrder[slot];
}

byte StaticTables::answerFrom(uint16 table, byte item, byte obj,
								Common::RandomSource &rnd) const {
	uint p = table - kUseBlockStart;
	if (p >= kUseBlockSize)
		return 0;
	const uint count = _useBlock[p++];
	for (uint i = 0; i < count && p + 3 <= kUseBlockSize; i++) {
		const byte n = _useBlock[p];
		const byte want = _useBlock[p + 1];
		if (_useBlock[p + 2] == obj && (want == item || (want == 0 && item > 0)) && n) {
			const uint pick = p + 3 + rnd.getRandomNumber(n - 1);
			return pick < kUseBlockSize ? _useBlock[pick] : 0;
		}
		p += n + 3;
	}
	return 0;
}

byte StaticTables::useAnswer(int room, byte item, byte obj, byte flagA6d6,
							 Common::RandomSource &rnd) const {
	for (uint i = 0; i < ARRAYSIZE(kUseTables); i++) {
		const UseTable &t = kUseTables[i];
		if (t.room != room || (t.needsA6d6 && flagA6d6 != 1))
			continue;
		const byte code = answerFrom(t.table, item, obj, rnd);
		if (code)
			return code;
	}
	return 0;
}

} // End of namespace Alien
