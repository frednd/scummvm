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

#ifndef ALIEN_MAZETABLES_H
#define ALIEN_MAZETABLES_H

#include "common/scummsys.h"
#include "common/util.h"

namespace Alien {

/**
 * One cell of one of the two mazes.
 *
 * Rooms 43 and 44 are the only rooms in the game whose position is not the room
 * number. Both are one screen redrawn over and over from a cell index in
 * [0xa77c], and each keeps two tables in the resident data segment: what the
 * cell looks like, and what it joins. tools/gen_maze.py lifts both.
 *
 * `tile` is the seven pieces the corridor view is assembled from, left to right
 * and then the two foreground blocks at the bottom:
 *
 *   index   rect                    what it is
 *   0       0,13   64x142           far left column
 *   1       64,13  64x121           left of centre
 *   2       128,13 64x142           centre -- the far wall, torch or crystal door
 *   3       192,13 64x121           right of centre
 *   4       256,13 64x142           far right column
 *   5       64,134 64x21            foreground, bottom left
 *   6       192,134 64x21           foreground, bottom right
 *
 * The value names the plate the piece is copied from: 0 MAZBLK21.PCX, a wall,
 * 1 MAZBLK11.PCX, an opening, 2 MAZBLK31.PCX, a decor block (the crystal door's
 * mouth, the skeleton's alcove). `CHARANIM:sub_13ede` composes the screen that
 * way, and the hotspot program registers a direction's rectangle under the same
 * test, which is why an opening is both something to see and somewhere to go.
 *
 * `next` is the cell each direction leads to, indexed by direction 1..4 less
 * one -- 1 forward, 2 right, 3 back, 4 left -- or 0xff for a wall. The armed
 * submode picks the direction through `CHARANIM:sub_13b80`: submode 1 is left,
 * 2 and 4 forward, 5 right, 6 and 7 back.
 */
struct MazeCell {
	byte tile[7];
	byte next[4];
};

/// The cells of one maze, or null with a count of zero for any other room.
const MazeCell *mazeCells(int room, uint &count);

} // End of namespace Alien

#endif
