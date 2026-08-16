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
};

/** A room's bank loads in overlay order, or null with a count of zero. */
const AnimBank *animBanksForRoom(int room, uint &count);

} // End of namespace Alien

#endif
