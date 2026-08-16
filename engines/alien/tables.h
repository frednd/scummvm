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

#ifndef ALIEN_TABLES_H
#define ALIEN_TABLES_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Alien {

/**
 * The static tables the original keeps in data segment 0x271A of GAME.EXE.
 *
 * There is no separate resource file for these: the executable itself is the
 * table store, and the detection entry pins it by MD5, so the offsets can be
 * hardcoded. Only the two room plate tables are read for now; the same segment
 * also holds the music and SFX module paths and the verb names, which land
 * with the systems that need them.
 */
class StaticTables {
public:
	/// The tables have 60 slots; the game itself only reaches 60 rooms.
	static const int kRoomCount = 60;

	/// Verb codes run 1..14; slot 0 of the table is empty.
	static const int kVerbCount = 15;

	StaticTables();

	bool load();
	bool isLoaded() const { return _loaded; }

	/// Main background plate for a room, empty if the room has none.
	const Common::String &background(int room) const;

	/// Second plate: the B state, a wide room's right half or the close-up.
	const Common::String &secondPlate(int room) const;

	/// True when the room has a background of its own and can be entered.
	bool hasRoom(int room) const { return !background(room).empty(); }

	/// The status-line word for a verb code: "Look at", "Pick up", ...
	const Common::String &verb(int code) const;

	/// What the status line reads when nothing interactive is under the cursor.
	const Common::String &walkVerb() const { return _walkVerb; }

private:
	Common::String _background[kRoomCount];
	Common::String _secondPlate[kRoomCount];
	Common::String _verb[kVerbCount];
	Common::String _walkVerb;
	Common::String _empty;
	bool _loaded;
};

} // End of namespace Alien

#endif
