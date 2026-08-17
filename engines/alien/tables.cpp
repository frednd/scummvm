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
static const uint32 kUseVerb = 0x3218;
static const uint32 kWithVerb = 0x3223;

// The item tables, all indexed by item id: how many outcome codes the item has
// with bit 7 the wrap flag, the codes themselves, and where the item's icon sits
// in the icon page. See docs/inventory_system.md.
static const uint32 kTableItemArity = 0x2FEB;
static const uint32 kTableItemOutcomes = 0x3019;
static const uint32 kTableIconX = 0x30F6;
static const uint32 kTableIconY = 0x3178;
static const byte kItemCycleBit = 0x80;

StaticTables::StaticTables() : _loaded(false) {
	memset(_itemArity, 0, sizeof(_itemArity));
	memset(_itemOutcome, 0, sizeof(_itemOutcome));
	memset(_itemIconX, 0, sizeof(_itemIconX));
	memset(_itemIconY, 0, sizeof(_itemIconY));
}

/**
 * Read one Pascal ShortString out of an already positioned stream. Slots the
 * game never uses hold a zero length or blanks, both of which come back empty.
 */
static Common::String readName(Common::File &exe) {
	byte length = exe.readByte();
	if (length > kMaxNameLength)
		return Common::String();

	char buffer[kMaxNameLength + 1];
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

} // End of namespace Alien
