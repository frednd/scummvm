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

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/rooms/ufo.h"

namespace Alien {

// Room 18, Ben's room (ovr_12_0e6b:0x08b7). A UFO crosses the window --
// UFOFLYBY.DL1, all 78 frames and the erase after them -- once, after 0x157c
// animation frames, something over five minutes in the one room. [0xa71e] both
// gates the counter and remembers that it has happened, so it is a once a game
// event; it is cleared for a new game.
static const int kUfoRoom = 18;
static const uint16 kUfoClock = 0xa71c;
static const uint16 kUfoLimit = 0x157c;
static const uint16 kUfoFlag = 0xa71e;

/// The overlay zeroes its clock as it opens, so a stay is counted afresh.
void UfoRoom::reset(int room) {
	if (room == kUfoRoom)
		_vm->setStateWord(kUfoClock, 0);
}

bool UfoRoom::roomClock() {
	// Unlike the other two the compare sits outside the gate, and both the
	// count and the fire are held off once the flag is up.
	if (_vm->_room != kUfoRoom || _vm->_script.flag(kUfoFlag))
		return false;
	if ((_vm->_tick & 3) == 0)
		_vm->setStateWord(kUfoClock, _vm->stateWord(kUfoClock) + 1);
	if (_vm->stateWord(kUfoClock) <= kUfoLimit)
		return false;
	_vm->_script.setFlag(kUfoFlag, 1);
	_vm->_anims.play(3, 1, 0x4f, 4, 1);
	debugC(1, kDebugRooms, "clock: room %d, the UFO flies by", kUfoRoom);
	return true;
}

} // End of namespace Alien
