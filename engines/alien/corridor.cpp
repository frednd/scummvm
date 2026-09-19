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

// Rooms 51, 53, 55 and 57, the four-way junction between the park teleporter
// and the ship's hallways -- and the maintenance man in 53/57 who is the only
// way out of it toward the ending.
//
// Every one of the four rooms carries the same four-destination table in its
// walk geometry (transitions.cpp: 101->53, 102->57, 103->51, 104->55), but no
// row of any of their kWalkObject entries arms one: `LOGIC:sub_1265c`
// (`seg_logic.asm:0x1265c`) picks among them from `[0xa79e]`/`[0xa79f]`, which
// nothing in any of the four overlays' own hotspot tables writes -- the real
// choice is made by walking into one of four unlabelled doorways this pass
// did not resolve which object is which.
//
// Ported as a fixed cycle rather than the real four-way choice: a plain click
// with nothing held and no hotspot under it, in any of the four rooms, always
// leaves by the same one of its four real destinations (51->55, 55->53,
// 53->57, 57->51), the way room 41's door and room 46's swim already stand in
// for their own multi-state guards. Clicking round the cycle enough times
// reaches every room; it is never the fastest way between two of them.
//
// Room 53 (and, since the two rooms share one overlay, room 57 as well)
// also carries the one thing this leg actually needs: object 3, verb 6
// ("talk to") is a real, registered hotspot (check_hotspots.py --sweep 53:
// 333,46..402,128) -- the maintenance man, whose conversation
// (`ovr_35_0f9e:0x0155`, `OBJ:sub_0967e` over a topic counter at [0xa7d8])
// is the only writer either room has for [0xa7d9], one of the two guards
// (with [0xa7d6], the force field, both already the "ships true" pattern
// room 32's statue and Yodle's picklock use) on the win exit itself
// (transitions.cpp: room 53, submode 2 -> room 59 -- already an ordinary
// kWalkSubmode row, `checkExit` just never sees it fire while either flag is
// still set). Simplified to one exchange the same way Yodle's is: the first
// talk in room 53 clears both flags and leaves toward the ending after a
// short delay, standing in for the real multi-topic conversation and the walk
// to object 6's own hotspot (which, like the steam valve's, is not among
// either room's registered rectangles).
static const int kLobbyRoom = 51;
static const int kHallwayRoomA = 53;
static const int kHallwayRoomB = 57;
static const int kCorridorRoom = 55;

static const byte kNextClicks = 3;

static const byte kLobbyNextSubmode = 104;		///< transitions.cpp: room 51, submode 104 -> room 55
static const byte kCorridorNextSubmode = 101;	///< transitions.cpp: room 55, submode 101 -> room 53
static const byte kHallwayANextSubmode = 102;	///< transitions.cpp: room 53, submode 102 -> room 57
static const byte kHallwayBNextSubmode = 103;	///< transitions.cpp: room 57, submode 103 -> room 51

static const byte kMaintenanceMan = 3;
static const byte kVerbTalkTo = 6;

static const uint16 kForceField = 0xa7d6;		///< ships 1: the field is up
static const uint16 kTalkDone = 0xa7d9;		///< no writer but the man's own talk

static const byte kWinExitSubmode = 2;			///< transitions.cpp: room 53, submode 2 -> room 59
static const uint kWinExitDelay = 20;

/// Every arrival resets the click count toward whichever room is next.
void AlienEngine::startCorridor() {
	if (_room != kLobbyRoom && _room != kHallwayRoomA && _room != kHallwayRoomB &&
		_room != kCorridorRoom)
		return;

	_corridorClicks = 0;
}

/// A plain click, standing in for the real four-way choice: see this file's
/// header. Consumes the click the way room 41's door does, so it must never
/// take one meant for the room's own rectangles.
bool AlienEngine::armCorridorNext() {
	if (_heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	byte submode;
	switch (_room) {
	case kLobbyRoom:
		submode = kLobbyNextSubmode;
		break;
	case kCorridorRoom:
		submode = kCorridorNextSubmode;
		break;
	case kHallwayRoomA:
		submode = kHallwayANextSubmode;
		break;
	case kHallwayRoomB:
		submode = kHallwayBNextSubmode;
		break;
	default:
		return false;
	}

	_corridorClicks++;
	debugC(1, kDebugRooms, "corridor: step %u of %u round the cycle", _corridorClicks, kNextClicks);

	if (_corridorClicks >= kNextClicks) {
		_corridorClicks = 0;
		CursorMan.showMouse(false);
		takeExit(submode);
	}

	return true;
}

/// The maintenance man: a real hotspot, simplified to one exchange.
bool AlienEngine::armCorridorMan(int obj, byte verb) {
	if ((_room != kHallwayRoomA && _room != kHallwayRoomB) || obj != kMaintenanceMan ||
		verb != kVerbTalkTo)
		return false;

	if (_script.flag(kTalkDone) != 0)
		return true;

	_script.setFlag(kTalkDone, 1);

	// Room 57 shares this hotspot but has no exit that either flag guards
	// (transitions.cpp has no submode 2 out of it), so only room 53's talk
	// starts the walk out.
	if (_room != kHallwayRoomA)
		return true;

	_script.setFlag(kForceField, 0);
	_corridorStep = kWinExitDelay;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "corridor: the maintenance man clears the field, off to room 59");
	return true;
}

/// The delay standing in for the walk to object 6's own hotspot.
void AlienEngine::stepCorridor() {
	if (_room != kHallwayRoomA || !_corridorStep)
		return;

	if (--_corridorStep)
		return;

	takeExit(kWinExitSubmode);
}

} // End of namespace Alien
