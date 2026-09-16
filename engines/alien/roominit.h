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

#ifndef ALIEN_ROOMINIT_H
#define ALIEN_ROOMINIT_H

#include "alien/roomscripts.h"

namespace Alien {

/**
 * What a room sets up as it opens: the frame each animation slot starts on.
 *
 * A room's overlay runs this out of the subroutine its per-frame entry calls
 * first -- the same routine that loads the DL1 banks -- and the plays there sit
 * under the puzzle-state guards that decide whether the door was left open or
 * the shelf emptied. tools/gen_roominit.py lifts them into the same ScriptEffect
 * form the room scripts use, so RoomScript runs them through the interpreter it
 * already has.
 *
 * A room absent from the table sets no slot at all, which is right for the rooms
 * whose plate already draws everything in its opening state.
 */
const ScriptEffect *roomInitEffects(int room, uint &count);

/**
 * A frame list an opening plays, as the modes that read one index it.
 *
 * Modes 6, 7 and 8 do not step a frame: they step a cursor into a far table of
 * one byte per tick and draw the byte (MIDAS:snd_func_1482 at 0x15dc). An
 * opening effect carries where its own list starts and the play's frame count
 * is how long it is. The pool is the rooms' own, separate from the cutscenes'
 * (cutscenes.h) because the openings are a table of their own.
 */
const byte *roomInitFrameList(uint first, uint count);

} // End of namespace Alien

#endif
