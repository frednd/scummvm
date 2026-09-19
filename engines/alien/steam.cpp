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

// Rooms 49 (engine room) and 50 (steam room), and the valve between them.
//
// [0xa798] ships true from the new-game init block (seg_main.asm:0x0a52) and
// is the steam itself: while it is set, room 50's own walk geometry (its
// [0xa798]-guarded kWalkZone/kWalkSnapDown rows, already generic) turns Ben
// back before the pit. tools/roomlogic.py's own tables name object 3, verb 13
// (ovr0f85_entry3:0x0079) as what clears it, and object 6, verb 13 in room 50
// (ovr0f81_entry3:0x0092, once the steam is off) as the door back -- but
// neither object is among either room's registered hotspots
// (check_hotspots.py --sweep 49/50 lists only 1/2/4/5 and 1/2/3/4/6, the
// second 6 being the dead-end row roomscripts.json block 166 already lifts),
// so the click that is meant to reach them is armed by something entry 1
// registers at runtime rather than the static table, the same shape of gap
// room 41's three doors and room 46's key hotspot are (shore.cpp, diving.cpp).
//
// Ported the same way those are: a plain click with nothing held and no
// registered hotspot under it counts as a step, in place of resolving the
// runtime rectangle, and the exit follows after a short delay standing in for
// each room's own [0xa49f] cutscene.
static const int kEngineRoom = 49;
static const int kBoilerRoom = 50;

static const uint16 kSteamOn = 0xa798;	///< ships 1: the pit blocks the crossing
static const uint16 kDoorShut = 0xa789;	///< ships 1: the room 50 door is still there to open

static const byte kEngineExitSubmode = 1;	///< transitions.cpp: room 49, submode 1 -> room 50
static const byte kBoilerExitSubmode = 1;	///< transitions.cpp: room 50, submode 1 -> room 49

static const uint kValveClicks = 3;
static const uint kExitDelay = 20;	///< ticks stood in for the original's cutscene length

/// Every arrival resets the click count taken toward whichever end's valve.
void AlienEngine::startSteam() {
	if (_room != kEngineRoom && _room != kBoilerRoom)
		return;

	_steamClicks = 0;
}

/// The valve in room 49: three plain clicks turn the steam off and start the
/// walk through to room 50.
bool AlienEngine::armSteamValve() {
	if (_room != kEngineRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	if (_script.flag(kSteamOn) == 0)
		return false;

	_steamClicks++;
	debugC(1, kDebugRooms, "steam: valve, step %u of %u", _steamClicks, kValveClicks);

	if (_steamClicks >= kValveClicks) {
		_steamClicks = 0;
		_script.setFlag(kSteamOn, 0);
		_steamStep = kExitDelay;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "steam: valve turned, the pit to room 50 is clear");
	}

	return true;
}

/// The door at the room 50 end: only openable once the steam is off.
bool AlienEngine::armSteamDoor() {
	if (_room != kBoilerRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	if (_script.flag(kSteamOn) != 0 || _script.flag(kDoorShut) == 0)
		return false;

	_steamClicks++;
	debugC(1, kDebugRooms, "steam: door, step %u of %u", _steamClicks, kValveClicks);

	if (_steamClicks >= kValveClicks) {
		_steamClicks = 0;
		_script.setFlag(kDoorShut, 0);
		_steamStep = kExitDelay;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "steam: door opened, the way back to room 49 is clear");
	}

	return true;
}

/// The delay standing in for both rooms' [0xa49f] cutscenes.
void AlienEngine::stepSteam() {
	if (!_steamStep)
		return;

	if (--_steamStep)
		return;

	if (_room == kEngineRoom)
		takeExit(kEngineExitSubmode);
	else if (_room == kBoilerRoom)
		takeExit(kBoilerExitSubmode);
}

} // End of namespace Alien
