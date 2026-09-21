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

// Room 58, the jail.
//
// tools/roomlogic.py already lifts the door out the way the walkthrough
// takes it: using any of the three security cards (items 41/42/43) on object
// 13 plays the anim and clears [0xa7ba] (roomscripts.json), which is what the
// room's own kWalkSubmode row (object 10 -> submode 1 -> room 51) is already
// guarded on -- an ordinary generic exit, no hand port needed.
//
// transitions.cpp gives the room two further exits neither the lift nor the
// walk geometry carries: submode 5 -> room 59, a second, more direct way to
// the ending this pass leaves for the route already ported through room 53's
// maintenance man (corridor.cpp), and submode 100 -> room 56, back to the
// ship's transporter chamber -- the clock at [0xa7be], the two other
// CHARANIM calls and the four `kOpUnsupported` reads the plan named for this
// room all belong to one or the other, and resolving which is which was past
// this leg's time. Only the second is ported, the same way room 52's scan
// and room 54's number board are: a plain click with nothing held and no
// hotspot under it, standing in for whatever the original actually gates it
// on.
static const int kJailRoom = 58;

static const byte kEscapeClicks = 3;
static const byte kShipExitSubmode = 100;	///< transitions.cpp: room 58, submode 100 -> room 56

/// Every arrival resets the click count.
void AlienEngine::startJail() {
	if (_room != kJailRoom)
		return;

	_jailClicks = 0;
}

/// A plain click, standing in for the real exit machine: see this file's
/// header.
bool AlienEngine::armJailExit() {
	if (_room != kJailRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	// The player picks up the second security card from the slot during the escape.
	if (!_inventory.has(42)) {
		_inventory.add(42);
		debugC(1, kDebugRooms, "jail: picked up security card (item 42)");
	}

	_jailClicks++;
	debugC(1, kDebugRooms, "jail: step %u of %u back to the ship", _jailClicks, kEscapeClicks);

	if (_jailClicks >= kEscapeClicks) {
		_jailClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kShipExitSubmode);
	}

	return true;
}

} // End of namespace Alien
