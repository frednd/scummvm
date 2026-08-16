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

StaticTables::StaticTables() : _loaded(false) {
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

	debugC(1, kDebugResource, "room tables: %d rooms with a plate of their own", rooms);

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

} // End of namespace Alien
