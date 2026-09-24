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

// Rooms 51, 53, 55 and 57, the four floors of the alien ship -- and the
// maintenance man in 53/57 who is the only way out of them toward the ending.
//
// The four are joined by an elevator, not by doorways. Every one of them
// carries the same four rows in the transition chain (transitions.cpp: 101->53,
// 102->57, 103->51, 104->55), and LOGIC:sub_1265c, which each room calls as its
// frame loop ends, picks among them from [0xa79e] once [0xa79f] says the panel
// has been used. What writes both is the resident CHARANIM:sub_150f4 (room 51:
// sub_151bc, the same body), called from every room's tick: a walk that ends
// ([0x9908] == 1) with the click that started it on object 2 ([0xa644] == 2)
// opens the panel, 15f3:sub_16895 (elevator.cpp), and raises [0xa79f] and
// [0xa881] as it returns. Object 2 is a registered rectangle with a walk row
// in all four rooms (check_hotspots.py / check_walkgeom.py --sweep), so the
// arrival test below is all the room side needs.
//
// Each room's prologue then answers [0xa7a1], which the panel raises on its
// way out: the lifted opening already plays the doors and puts Ben in front of
// them, but the prologue's flag writes are hand-owned by rule (gen_roominit.py,
// plays_of), and without them [0xa7a1] was never cleared and every later way
// in replayed the elevator. Room 55 also answers [0xa7a0], the no-card trip.
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

/// Object 2 in all four rooms: the elevator door (sub_150f4 / sub_151bc).
static const byte kElevatorObj = 2;
/// [0xa644], the object the last left click was on (cemetery.cpp).
static const uint16 kClickedObj = 0xa644;

/// [0xa7a1]: the panel has just been used, so this room opens at its elevator.
static const uint16 kElevatorArrival = 0xa7a1;
/// Room 51's pair of door flags and the other three rooms' pair, raised as the
/// doors are drawn open (ovr_33_0faa:0x03ad, ovr_35_0f9e:0x0857/0x0d7c,
/// ovr_37_0f9a:0x03c4) so the rooms' door ticks do not open them again.
static const uint16 kDoorA = 0xa7a2;
static const uint16 kDoorAWas = 0xa7a3;
static const uint16 kDoorB = 0xa7a4;
static const uint16 kDoorBWas = 0xa7a5;
/// [0xa7a0]: the panel was left with no card; room 55 speaks line 5
/// (ovr_37_0f9a:0x041d, answered at the top of entry 3 as [0xa956] == 5).
static const uint16 kNoCardLine = 0xa7a0;
static const byte kOutcomeNoCard = 5;

static const byte kMaintenanceMan = 3;
static const byte kVerbTalkTo = 6;

static const uint16 kForceField = 0xa7d6;		///< ships 1: the field is up
static const uint16 kTalkDone = 0xa7d9;		///< no writer but the man's own talk

static const byte kWinExitSubmode = 2;			///< transitions.cpp: room 53, submode 2 -> room 59
static const uint kWinExitDelay = 20;

static bool isElevatorRoom(int room) {
	return room == kLobbyRoom || room == kHallwayRoomA || room == kHallwayRoomB ||
		   room == kCorridorRoom;
}

/// The prologue's [0xa7a1] and [0xa7a0] arms, the flag writes the lift leaves
/// to hand code (see this file's header).
void AlienEngine::startCorridor() {
	if (!isElevatorRoom(_room))
		return;

	if (_script.flag(kElevatorArrival) == 1) {
		_script.setFlag(kElevatorArrival, 0);
		if (_room == kLobbyRoom) {
			_script.setFlag(kDoorA, 1);
			_script.setFlag(kDoorAWas, 1);
		} else {
			_script.setFlag(kDoorB, 1);
			_script.setFlag(kDoorBWas, 1);
		}
		debugC(1, kDebugRooms, "corridor: out of the elevator into room %d", _room);
	}

	if (_room == kCorridorRoom && _script.flag(kNoCardLine) == 1) {
		_script.setFlag(kNoCardLine, 0);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeNoCard, anchorX, anchorY);
		debugC(1, kDebugRooms, "corridor: back without a card");
	}
}

/// CHARANIM:sub_150f4 / sub_151bc: a walk that ended at the elevator door.
void AlienEngine::corridorArrival() {
	if (!isElevatorRoom(_room))
		return;
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;
	if (_script.flag(kClickedObj) != kElevatorObj)
		return;

	// 0x158a: the latch is spent as the panel opens.
	_script.setFlag(kClickedObj, 0);
	playElevatorPanel();
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
	corridorArrival();

	if (_room != kHallwayRoomA || !_corridorStep)
		return;

	if (--_corridorStep)
		return;

	takeExit(kWinExitSubmode);
}

} // End of namespace Alien
