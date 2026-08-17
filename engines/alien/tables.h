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

	/// The icon page holds an 8 by 8 grid of cells, so item ids stop at 64.
	static const int kItemCount = 65;

	/// Only the first 48 cells were ever drawn, and INVENTOR.TAL names exactly
	/// those, so an id past this one is not an item the game hands out.
	static const int kItemInUse = 48;

	/// How many outcome codes an item's record holds.
	static const int kItemOutcomes = 4;

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

	/// The two words the item-use line is built from: "USE <item> WITH <object>".
	const Common::String &useVerb() const { return _useVerb; }
	const Common::String &withVerb() const { return _withVerb; }

	/**
	 * How many of an item's four outcome codes are in use, and whether the
	 * rotation wraps round to the first or stops on the last. The original packs
	 * both into one byte at DS:0x2FEB, indexed by item id, with bit 7 the wrap.
	 *
	 * The record runs out at item 45: the outcome table starts where item 46's
	 * byte would be, so items 46 to 48 read their arity out of the head of that
	 * table. The overlap is the original's own, not a mistake here.
	 */
	byte itemArity(int item) const;
	bool itemCycles(int item) const;

	/// One of an item's outcome codes, by the item's own click counter (1..arity).
	byte itemOutcome(int item, int counter) const;

	/// Where the item's 35 by 22 icon sits in the icon page.
	int itemIconX(int item) const;
	int itemIconY(int item) const;

private:
	Common::String _background[kRoomCount];
	Common::String _secondPlate[kRoomCount];
	Common::String _verb[kVerbCount];
	Common::String _walkVerb;
	Common::String _useVerb;
	Common::String _withVerb;
	Common::String _empty;

	byte _itemArity[kItemCount];

	/// The outcome codes as one run, indexed the way the original does it:
	/// `item * 4 + counter` from a base four bytes in front of item 1's record,
	/// so a counter of zero reads the tail of the item before.
	byte _itemOutcome[(kItemCount + 1) * kItemOutcomes];

	uint16 _itemIconX[kItemCount];
	uint16 _itemIconY[kItemCount];

	bool _loaded;
};

} // End of namespace Alien

#endif
