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

#ifndef ALIEN_HOTSPOTS_H
#define ALIEN_HOTSPOTS_H

#include "common/scummsys.h"
#include "common/util.h"

#include "alien/roomscripts.h"

namespace Alien {

/**
 * One registered interactive rectangle.
 *
 * The original has no hotspot resource. Entry 1 of each room overlay calls one
 * of five routines in segment 1336 as it runs, passing the rectangle, the
 * object id, the verb and up to four outcome codes; the routine hit-tests the
 * cursor there and then. The port lifts entry 1 out of the disassembly as the
 * program below -- see tools/gen_hotspots.py, which writes hotspots.cpp -- runs
 * it into an array of these, and hit-tests the whole room in one pass.
 *
 * Registration order matters: the overlay registers from the back of the room
 * forward, so where two rectangles overlap the one registered last is the one
 * the original leaves in its globals, and the table keeps that order.
 *
 * The verb is the literal status-line word, and the label is a slot number in
 * the room's NAMEROOM/<lang>/R<n>.TAL. Rectangles are not clipped to the
 * screen: wide rooms register against the full plate, which is up to 640 px.
 */
struct Hotspot {
	int16 x1;
	int16 y1;
	int16 x2;
	int16 y2;
	byte label;						///< dialog slot in the room's label file
	byte obj;						///< object id the room script branches on
	byte verb;						///< 1..14, index into the verb table
	byte outcomeCount;				///< 1..4, how many of the codes are in use
	byte outcomes[4];

	bool contains(int x, int y) const {
		return x >= x1 && x <= x2 && y >= y1 && y <= y2;
	}
};

/** What one step of a room's registration program does. */
enum HotspotOpKind {
	kHotspotSet = 0,		///< a scratch global takes a value
	kHotspotRegister		///< a rectangle joins the room's table
};

/**
 * One step of entry 1.
 *
 * A rectangle can be conditional -- the trapdoor is a hotspot only once it is
 * open -- so every step carries the puzzle flags the overlay tests around it.
 * A field can also be computed: the overlay writes a byte into one of the
 * compiler temporaries it shares with the resident units and pushes that,
 * which is how one cupboard says a different line depending on what is inside.
 * kHotspotSet is such a write and `varOf` names the temporary a field reads.
 */
struct HotspotOp {
	byte kind;
	byte guardCount;
	ScriptCond guards[3];

	byte var;					///< kHotspotSet: the slot written
	byte value;					///< kHotspotSet: what it takes

	int16 x1, y1, x2, y2;
	byte label;
	byte obj;
	byte verb;
	byte outcomeCount;
	byte outcomes[4];

	/// Per field -- label, obj, verb, outcomes 1..4 -- the slot supplying it
	/// plus one, or zero when the overlay pushed an immediate.
	byte varOf[7];
};

/**
 * A room's registration program in overlay order, or null with a count of zero
 * for the one room that registers nothing at all.
 */
const HotspotOp *hotspotProgramForRoom(int room, uint &count);

/// How many scratch slots the programs share, and what each one is in the
/// original's data segment. An address inside the state block is a real flag
/// the overlay reads rather than a temporary, so a slot starts from the state.
uint hotspotVarCount();
uint16 hotspotVarAddr(uint index);

} // End of namespace Alien

#endif
