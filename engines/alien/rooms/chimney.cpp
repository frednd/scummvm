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
#include "alien/rooms/chimney.h"

namespace Alien {

// Room 14, the chimney (ovr_0e_0e83:0x0616). Every 160 animation frames -- some
// nine seconds -- four frames of MAF_CHIM.DL1 play and [0xa740] is set to 0x19,
// which the room's own hotspot program then counts back down one per frame. It
// is not a decoration: while [0xa740] is above zero the program writes 5 rather
// than 4 into the scratch temporary [0x9926] (ovr_0e_0e83:0x0410), so the
// chimney hotspot answers with a different outcome for those 25 frames.
static const int kChimneyRoom = 14;
static const uint16 kChimneyClock = 0xa73e;
static const uint16 kChimneyLimit = 0x00a0;
static const uint16 kChimneyFlag = 0xa740;
static const byte kChimneyReload = 0x19;

/// The overlay zeroes its clock as it opens, so a stay is counted afresh.
void ChimneyRoom::reset(int room) {
	if (room == kChimneyRoom)
		_vm->setStateWord(kChimneyClock, 0);
}

/// Every animation frame.
bool ChimneyRoom::roomClock() {
	if (_vm->_room != kChimneyRoom || (_vm->_tick & 3) != 0)
		return false;
	_vm->setStateWord(kChimneyClock, _vm->stateWord(kChimneyClock) + 1);

	// The countdown the puff loaded, one per frame. Its last step matters as
	// much as the puff does: it is what puts the chimney's outcome back.
	bool changed = false;
	const byte held = _vm->_script.flag(kChimneyFlag);
	if (held > 0) {
		_vm->_script.setFlag(kChimneyFlag, held - 1);
		changed = held == 1;
	}

	if (_vm->stateWord(kChimneyClock) > kChimneyLimit) {
		_vm->_script.setFlag(kChimneyFlag, kChimneyReload);
		_vm->setStateWord(kChimneyClock, 0);
		_vm->_anims.play(0, 1, 4, 4, 1);
		changed = true;
		debugC(1, kDebugRooms, "clock: room %d, the chimney puffs", kChimneyRoom);
	}
	return changed;
}

} // End of namespace Alien
