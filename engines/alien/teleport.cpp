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

// Room 22, the park, and room 56, the transporter chamber at the other end.
//
// tools/roomlogic.py already lifted the repair itself: using the transistor
// (item 35) on either half of the teleporter (objects 6/7) sets [0x33b3] to 1
// and takes the part (roomscripts.json blocks 113/114). What it does not
// cover is the transporter's own [0xa49f] machine
// (ovr_16_0ea3_entry2:0x0a87 onward): a two-stage animation gated on [0x33b3]
// and a destination flag ([0x33b6]) that a further chain of `game_submode`
// 101-104 (`LOGIC:sub_1265c`) picks among -- Jack's ship is only one of the
// places it can send Ben, and getting there also depends on state this pass
// does not carry (the pumpkin mask, the panel codes).
//
// Room 56's own return trip is the mirror image: entry2's arrival cutscene
// (from the jail escape, `CHARANIM:sub_14b1a`) ends by planting Ben in the
// right spot and counting [0x33f0] up to 0x64 before writing `game_submode`
// to 100 directly (0x0a9f) -- position-driven, not a click on any object.
//
// Both simplified to a single hop each way: clicking the repaired teleporter
// in room 22 sends Ben straight to room 56 (the only destination this leg
// needs), and three plain clicks in room 56 send him back, the same
// substitution shore.cpp makes for its door.
static const int kParkRoom = 22;
static const int kShipRoom = 56;

static const uint16 kTeleportRepaired = 0x33b3;	///< set by the transistor's own lifted row

static const byte kTeleportObjA = 6;
static const byte kTeleportObjB = 7;

static const byte kToShipSubmode = 111;	///< transitions.cpp: room 22, submode 111 -> room 56
static const byte kToParkSubmode = 100;	///< transitions.cpp: room 56, submode 100 -> room 22

static const uint kTeleportDelay = 20;	///< ticks stood in for the original's two-stage animation
static const uint kReturnClicks = 3;

/// Every arrival resets room 56's return click count.
void AlienEngine::startTeleport() {
	if (_room != kShipRoom)
		return;

	_teleportReturnClicks = 0;
}

/// The repaired teleporter in room 22: a plain click sends Ben to the ship.
bool AlienEngine::armTeleportPark(int obj, byte verb) {
	if (_room != kParkRoom || (obj != kTeleportObjA && obj != kTeleportObjB))
		return false;

	if (_script.flag(kTeleportRepaired) != 1)
		return false;

	_teleportStep = kTeleportDelay;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "teleport: repaired and fired up, off to room %d", kShipRoom);
	return true;
}

/**
 * The return trip out of room 56, standing in for the position-driven
 * arrival cutscene the same way room 41's door does for its own machine.
 */
bool AlienEngine::armTeleportReturn() {
	if (_room != kShipRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_teleportReturnClicks++;
	debugC(1, kDebugRooms, "teleport: step %u of %u back to the park", _teleportReturnClicks, kReturnClicks);

	if (_teleportReturnClicks >= kReturnClicks) {
		_teleportReturnClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kToParkSubmode);
	}

	return true;
}

/// The delay standing in for the room 22 end's two-stage animation.
void AlienEngine::stepTeleport() {
	if (_room != kParkRoom || !_teleportStep)
		return;

	if (--_teleportStep)
		return;

	takeExit(kToShipSubmode);
}

} // End of namespace Alien
