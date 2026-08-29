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

#ifndef ALIEN_OCCLUSION_H
#define ALIEN_OCCLUSION_H

#include "alien/roomscripts.h"

namespace Alien {

/**
 * What hides the character, as tools/gen_occlusion.py lifts it.
 *
 * The original sorts nothing. A room's tick draws the character and then calls
 * OBJ:sub_03400 once per foreground object with a fixed rectangle; that routine
 * intersects the rectangle with the bounding box the character blit just left
 * behind and, where the two overlap, stamps the room's foreground sheet back
 * over him. Depth is authored, one rectangle at a time, and a room with no
 * rectangle has nothing the character can walk behind.
 *
 * The sheet is the room's MSCR<n>.PCX: a 320-wide page of foreground pieces
 * with palette index 0 everywhere else, which is what makes the stamp
 * transparent. Source coordinates address that sheet; destination coordinates
 * are room space, so a wide room's run past 320 into the half plate B supplies.
 */
struct OcclusionRect {
	byte room;
	int16 srcX, srcY;		///< into the room's foreground sheet
	int16 dstX, dstY;		///< room space, where the character was drawn
	int16 width, height;

	/**
	 * Apply only while the character's Y (`[0xa8ee]`) is above this, or -1 for
	 * a rectangle the tick never guards. It is how a low object stops hiding
	 * him once he has walked in front of it.
	 */
	int16 benYBelow;

	uint16 firstGuard;		///< into the table's puzzle-state guards
	byte guardCount;
};

/** The rectangles of one room, in the order its tick draws them. */
const OcclusionRect *occlusionRects(int room, uint &count);

/** The puzzle-state guards on one rectangle; all of them must hold. */
const ScriptCond *occlusionGuards(const OcclusionRect &rect, uint &count);

uint occlusionRectCount();

} // End of namespace Alien

#endif
