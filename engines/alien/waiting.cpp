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

#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Room 54, the waiting room, and the ticket machine and number board between
// it and Jack's room.
//
// tools/roomlogic.py lifts a handful of this room's ordinary bodies (the
// chairs' canned lines, the pass machine at object 13 giving up item 45 and
// later trading it for item 46) but nothing that reaches [0xa87e] itself: the
// number board (objects 9/10, already lifted as far as queuing outcome 20)
// is the real gate on submode 50 -> room 60, and this pass did not resolve
// which ticket number the board wants or how the two line up.
//
// Its own way back (transitions.cpp: room 54, submode 1 -> room 57) is
// already armed generically, a kWalkSubmode row on a real hotspot (object 1).
// Only the call in to Jack's room is not: ported the same way room 52's scan
// is, a plain click with nothing held and no hotspot under it standing in for
// holding the right number when it is called.
static const int kWaitingRoom = 54;

static const byte kBoardClicks = 3;
static const byte kJackExitSubmode = 50;	///< transitions.cpp: room 54, submode 50 -> room 60

/// Every arrival resets the click count.
void AlienEngine::startWaiting() {
	if (_room != kWaitingRoom)
		return;

	_waitingClicks = 0;
}

/// A plain click, standing in for holding the right number when it is called.
bool AlienEngine::armWaitingBoard() {
	if (_room != kWaitingRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_waitingClicks++;
	debugC(1, kDebugRooms, "waiting: step %u of %u until the number is called", _waitingClicks,
		   kBoardClicks);

	if (_waitingClicks >= kBoardClicks) {
		_waitingClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kJackExitSubmode);
	}

	return true;
}

} // End of namespace Alien
