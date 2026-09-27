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
// and takes the part (roomscripts.json blocks 113/114). What sets it off is not
// the teleporter but the phone beside it: the number the entity gives Ben, item
// 40, dialled on the open line. None of that is a lifted row -- the table has
// the (5, item 40) key as a bare action_handled and the plain use of the phone
// as its refusal alone -- so the two click hooks and the [0xa49f] machine they
// start are the room's own, here:
//
//   0x0043  item 40 on obj 5 or obj 1, handset off ([0xa729] == 0): 0x14
//   0x00e5  Use on obj 5 (the phone, only registered with the handset off):
//           item 40 carried -> 0x14; otherwise the first use is state 5, the
//           alien mumble (outcome 1), and every later one outcome 0x19.
//           [0x33b2] counts the uses, stopping at 1.
//
//   5..8   outcome 1, the line open with nobody on it
//   0x14   outcome 2 -- "I hope the number the entity gave me..." and the
//          number itself -- or 0x3b once [0x33b5] says it has been dialled
//          before; the cursor goes
//   0x16   the line down: 0x96
//   0x96   play(slot 5, frame 1, 0x1b frames, rate 2), INPUT:sub_01e07's two
//          samples, [0xa49c] reset
//   0x98   [0xa49c] > 0x1e: the booth fires only when it is repaired
//          ([0x33b3] == 1) *and* the telescope has saved the ship's
//          co-ordinates ([0x33b6] == 2, telescope.cpp). Then [0xa94d] = 0,
//          play(slot 3, frame 1, 0xe frames, rate 3), INPUT:sub_01de0's two
//          samples, 0xaa. Otherwise outcome 0x1a, "Hmm... nothing happened."
//   0xaa   [0xa49c] > 0x32: game_submode 0x6f, room 56
//
// The [0xa49c] waits are counted in ticks of this hook, as park.cpp counts its
// own.
//
// Room 56's own return trip is the mirror image: entry2's arrival cutscene
// (from the jail escape, `CHARANIM:sub_14b1a`) ends by planting Ben in the
// right spot and counting [0x33f0] up to 0x64 before writing `game_submode`
// to 100 directly (0x0a9f) -- position-driven, not a click on any object.
// That end stays simplified: three plain clicks in room 56 send him back, the
// same substitution shore.cpp makes for its door.
static const int kParkRoom = 22;
static const int kShipRoom = 56;

static const uint16 kTeleportRepaired = 0x33b3;	///< set by the transistor's own lifted row
static const uint16 kTeleportDest = 0x33b6;		///< telescope.cpp: 2 is the ship
static const uint16 kPhoneUses = 0x33b2;		///< plain uses of the phone, stopping at 1
static const uint16 kDialledBefore = 0x33b5;	///< the number has been dialled once
static const uint16 kHandsetDown = 0xa729;		///< 1 while the handset is on its hook

static const byte kNumberItem = 40;		///< "phone number"
static const byte kPhoneObj = 5;
static const byte kBoothObj = 1;
static const byte kVerbUse = 10;

static const byte kDestShip = 2;

static const byte kToShipSubmode = 111;	///< transitions.cpp: room 22, submode 111 -> room 56
static const byte kToParkSubmode = 100;	///< transitions.cpp: room 56, submode 100 -> room 22

/// ROOM22.TAL outcome codes.
static const byte kLineMumble = 1;			///< the open line, first use
static const byte kLineNumber = 2;			///< "I hope the number..." + the number
static const byte kLineStillOpen = 0x19;	///< the open line, every later use
static const byte kLineNothing = 0x1a;		///< "Hmm... nothing happened."
static const byte kLineNumberAgain = 0x3b;	///< the number alone

/// [0xa49f]'s steps in this room, by the original's own numbers.
static const byte kStepMumble = 8;			///< 5..7 collapse into the line itself
static const byte kStepDial = 0x14;
static const byte kStepDialDone = 0x16;
static const byte kStepSpark = 0x96;
static const byte kStepFire = 0x98;
static const byte kStepGone = 0xaa;

/// The two [0xa49c] waits, 0x0c35 and 0x0c97, as ticks of this hook.
static const uint kFireTicks = 0x1f;
static const uint kGoneTicks = 0x33;

/// The plays, push order: slot, first frame, frame count, rate.
static const uint kSparkSlot = 5;
static const int kSparkFirst = 1, kSparkCount = 0x1b, kSparkRate = 2;
static const uint kBeamSlot = 3;
static const int kBeamFirst = 1, kBeamCount = 0xe, kBeamRate = 3;
static const int kPlayMode = 1;			///< MIDAS:anim_play_mode1

/// INPUT:sub_01e07 and sub_01de0: two sfx_play_delayed each.
struct TeleportSample {
	uint sample;
	uint32 rate;
	byte volume;
	uint16 delay;
};
static const TeleportSample kSparkSamples[] = {
	{ 2, 0x6784, 0x23, 0 },
	{ 2, 0x6978, 0x23, 0x18 },
};
static const TeleportSample kBeamSamples[] = {
	{ 1, 0x3a98, 0x40, 1 },
	{ 1, 0x3a98, 0x40, 3 },
};

static const uint kReturnClicks = 3;

static const int kCorridorRoom = 55;
static const uint16 kChamberView = 0xa7a6;		///< room 56's view, 1..3; 2 shows the card
static const uint16 kCardThere = 0xa7a7;		///< the security card (item 41) still lies there
static const uint16 kFromCorridor = 0xa7a8;		///< room 55 was entered since the last visit

/**
 * Room 56 is drawn, walked and clicked in one of three views picked by
 * [0xa7a6] -- the room's own plates, floor rectangles and hotspots all switch
 * on it -- and only view 2 shows the security card (obj 8). MAIN starts it at
 * 3. Room 55's entry 2 raises [0xa7a8] on every arrival (ovr_37_0f9a:0x027f),
 * and room 56's entry 2 hands that to CHARANIM:sub_146a1 (ovr_38_0f92:0x0494)
 * before anything is drawn: back from the corridor with the card still there
 * the view is 2, and once it is taken each trip toggles between 1 and 2. The
 * teleporter from the park never raises [0xa7a8], so the first arrival is view
 * 3 and the card is out of sight until Ben has been to the corridor.
 */
void AlienEngine::advanceChamberView(int room) {
	if (room == kCorridorRoom) {
		_script.setFlag(kFromCorridor, 1);
		return;
	}

	if (room != kShipRoom || _script.flag(kFromCorridor) != 1)
		return;

	_script.setFlag(kFromCorridor, 0);
	if (_script.flag(kCardThere) == 1) {
		_script.setFlag(kChamberView, 2);
	} else {
		byte view = _script.flag(kChamberView) + 1;
		_script.setFlag(kChamberView, view > 2 ? 1 : view);
	}
	debugC(1, kDebugRooms, "teleport: room 56 in view %u", _script.flag(kChamberView));
}

/// Every arrival puts room 22's machine down and resets room 56's click count.
void AlienEngine::startTeleport() {
	_teleportStep = 0;
	_teleportWait = 0;
	_teleportReturnClicks = 0;
}

/// The two click hooks at 0x0043 and 0x00e5: the phone, and the number on it.
bool AlienEngine::armTeleportPhone(int obj, byte verb, byte item) {
	if (_room != kParkRoom || _teleportStep)
		return false;

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	if (item) {
		// 0x0043: only the number, only on the phone or the booth, and only
		// with the line open.
		if (item != kNumberItem || (obj != kPhoneObj && obj != kBoothObj) ||
			_script.flag(kHandsetDown) != 0)
			return false;
	} else {
		// 0x00e5
		if (obj != kPhoneObj || verb != kVerbUse)
			return false;

		const bool first = _script.flag(kPhoneUses) == 0;
		if (_script.flag(kPhoneUses) < 1)
			_script.setFlag(kPhoneUses, 1);

		if (!_inventory.has(kNumberItem)) {
			if (first) {
				queueOutcome(_tal, kLineMumble, anchorX, anchorY);
				CursorMan.showMouse(false);
				_teleportStep = kStepMumble;
			} else {
				queueOutcome(_tal, kLineStillOpen, anchorX, anchorY);
			}
			return true;
		}
	}

	// 0x0a4e: the number is dialled.
	const byte line = _script.flag(kDialledBefore) == 1 ? kLineNumberAgain : kLineNumber;
	queueOutcome(_tal, line, anchorX, anchorY);
	_script.setFlag(kDialledBefore, 1);
	CursorMan.showMouse(false);
	_teleportStep = kStepDialDone;
	debugC(1, kDebugRooms, "teleport: the number is dialled, outcome %u", line);
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

/// Room 22's [0xa49f] machine from 0x0a04, the phone's half of it.
void AlienEngine::stepTeleport() {
	if (_room != kParkRoom || !_teleportStep)
		return;

	switch (_teleportStep) {
	case kStepMumble:
		// 0x0a36
		if (!speechDone())
			break;
		_teleportStep = 0;
		CursorMan.showMouse(true);
		break;

	case kStepDialDone:
		// 0x0a80
		if (!speechDone())
			break;
		_teleportStep = kStepSpark;
		break;

	case kStepSpark:
		// 0x0c12
		_anims.play(kSparkSlot, kSparkFirst, kSparkCount, kSparkRate, kPlayMode);
		for (uint i = 0; i < ARRAYSIZE(kSparkSamples); i++)
			_sound.queue(kSparkSamples[i].sample, kSparkSamples[i].rate,
						 kSparkSamples[i].volume, 0, kSparkSamples[i].delay);
		_teleportWait = kFireTicks;
		_teleportStep = kStepFire;
		_dirty = true;
		break;

	case kStepFire: {
		// 0x0c35
		if (_teleportWait && --_teleportWait)
			break;

		if (_script.flag(kTeleportRepaired) != 1 || _script.flag(kTeleportDest) != kDestShip) {
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kLineNothing, anchorX, anchorY);
			_teleportStep = 0;
			CursorMan.showMouse(true);
			debugC(1, kDebugRooms, "teleport: nothing happens, [0x33b3] = %u, [0x33b6] = %u",
				   _script.flag(kTeleportRepaired), _script.flag(kTeleportDest));
			break;
		}

		// 0x0c5c: Ben goes with the beam.
		playCharacterAnim(kBeamSlot, kBeamFirst, kBeamCount, kBeamRate, kPlayMode);
		for (uint i = 0; i < ARRAYSIZE(kBeamSamples); i++)
			_sound.queue(kBeamSamples[i].sample, kBeamSamples[i].rate,
						 kBeamSamples[i].volume, 0, kBeamSamples[i].delay);
		_teleportWait = kGoneTicks;
		_teleportStep = kStepGone;
		debugC(1, kDebugRooms, "teleport: the booth fires, off to room %d", kShipRoom);
		break;
	}

	case kStepGone:
		// 0x0c97
		if (_teleportWait && --_teleportWait)
			break;
		_teleportStep = 0;
		takeExit(kToShipSubmode);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
