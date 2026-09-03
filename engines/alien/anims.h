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

#ifndef ALIEN_ANIMS_H
#define ALIEN_ANIMS_H

#include "common/scummsys.h"
#include "common/util.h"

namespace Alien {

/**
 * One DL1 bank a room loads into one animation slot.
 *
 * The slot number is not the room's manifest order: slot numbers have gaps, and
 * a few rooms load two banks into the same slot, the second overwriting the
 * first. Both the slot and the bank name are immediate arguments to
 * MIDAS:load_anim_bank as the overlay opens, and tools/gen_anims.py lifts them
 * out of the disassembly into the generated table.
 */
struct AnimBank {
	byte slot;
	const char *name;

	/**
	 * The one name the original does not store whole. Room 7's overlay keeps
	 * its closet as two Pascal literals, `MAK_CLO` and `.DL1`, writes the
	 * character between them into [0xa6c9] -- 'S', or '2' when [0xa700] is
	 * zero -- and concatenates the three just before the load. Both files
	 * ship. So `flag` is that guard's address, zero on every ordinary row,
	 * and `alt` is the name loaded instead while the flag holds `value`.
	 */
	uint16 flag;
	byte value;
	const char *alt;
};

/** A room's bank loads in overlay order, or null with a count of zero. */
const AnimBank *animBanksForRoom(int room, uint &count);

/**
 * One slot a room's tick keeps looping.
 *
 * Nothing in a slot says "repeat": MIDAS's eight play routines all count a
 * range down and stop. What makes the computer cursor blink and the light
 * around the hall door pulse is the room's own tick calling
 * MIDAS:snd_func_112d(slot) every frame, which re-issues that slot's last play
 * -- same mode, same first frame, same count, same rate -- as soon as the slot
 * has one frame left. So the loop lives in the room, and this is the lift of
 * those calls (tools/gen_anims.py).
 *
 * `flag` is the byte of the original's [0xa53a] array the call sits behind,
 * where a room turns one of its loops on and off, or 0 where the call is made
 * every frame regardless.
 */
struct AnimLoop {
	byte slot;
	uint16 flag;
};

/** A room's looping slots, or null with a count of zero. */
const AnimLoop *animLoopsForRoom(int room, uint &count);

} // End of namespace Alien

#endif
