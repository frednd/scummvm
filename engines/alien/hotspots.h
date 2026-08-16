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

namespace Alien {

/**
 * One registered interactive rectangle.
 *
 * The original has no hotspot resource. Each room overlay calls one of five
 * routines in segment 1336 as it runs, passing the rectangle, the object id,
 * the verb and up to four outcome codes as immediate arguments; the routine
 * hit-tests the cursor there and then. The port lifts those arguments out of
 * the overlay disassembly instead -- see tools/gen_hotspots.py, which writes
 * hotspots.cpp -- and hit-tests the whole room's table in one pass.
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

/**
 * The decoded hotspots of a room, in registration order. Returns null and a
 * count of zero for a room whose overlay registers nothing this table could
 * decode.
 */
const Hotspot *hotspotsForRoom(int room, uint &count);

} // End of namespace Alien

#endif
