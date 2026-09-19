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

// Room 41, the shore, and the way into the maze.
//
// All three of this room's doors are guarded on [0xa77f] (0, 1 or 2), which
// nothing in the lift writes -- entry 3 tests it directly
// (ovr_29_0f89:0x005f, 0x007c, 0x0105) and a hover-label helper re-registers
// one of three rectangles from it (0x0096, through 10c9:sub_11aae), rather
// than through a `kWalkSubmode` row. The diving suit on object 3 starts its
// own [0xa49f] machine when [0xa77f] is 0 (0x0091), and the pick-axe on
// objects 6/7/8 answers a different one when it is 1 (0x0066) -- neither
// ported here, since the way into the diving area is already open from the
// other end (diving.cpp) and the door the game needs to leave through is the
// one to the maze.
//
// What this file ports is only that door: room 43 (transitions.cpp: room 41,
// submode 3 -> room 43), stood in for the same way room 46's swim is -- a
// plain click, not on one of the room's own rectangles, counts as a step
// toward it, in place of resolving which of [0xa77f]'s three states the door
// is actually in.
static const int kShoreRoom = 41;

static const uint kMazeDoorClicks = 3;
static const byte kMazeDoorSubmode = 3;	///< transitions.cpp: room 41, submode 3 -> room 43

/// Every arrival resets the click count.
void AlienEngine::startShore() {
	if (_room != kShoreRoom)
		return;

	_shoreClicks = 0;
}

/**
 * A plain click, standing in for the real door: see this file's header.
 * Consumes the click the way room 46's swim does, so it must never take one
 * meant for the room's own rectangles.
 */
bool AlienEngine::armShoreDoor() {
	if (_room != kShoreRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_shoreClicks++;
	debugC(1, kDebugRooms, "shore: step %u of %u toward the maze", _shoreClicks, kMazeDoorClicks);

	if (_shoreClicks >= kMazeDoorClicks) {
		_shoreClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kMazeDoorSubmode);
	}

	return true;
}

} // End of namespace Alien
