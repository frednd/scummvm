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

#include "common/system.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

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
//   0x0134  Pick up on obj 9, the handset on its hook: [0xa729] = 0 and
//           MIDAS:sub_19d5a(1), which plays PAR_BENP -- Ben lifting it, slot 2,
//           the list at ds:0x7008 in mode 8 -- with him out of sight; 0x64
//           gives him and the cursor back two frames before it ends
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
// Room 56, the chamber the booth lands in (ovr_38_0f92), is one more machine
// in the same [0xa49f] byte, entry 2's:
//
//   on entry  [0x33e6] is the chamber's state, 1 shut and 2 open, and the
//             opening draws it either way: LOGIC:sub_123d0 runs the shut
//             chamber's pulse (slot 3, TRA_CHAM, the 29-frame list at
//             ds:0x6d84), LOGIC:sub_123f1 the open one (slot 4, TRA_CHA2,
//             seven frames). Arriving from the park it is open, [0x33f2]
//             keeps the pad from reacting, and -- the first time only,
//             [0x33e0] == 0 -- the record loader is called directly with 15,
//             the ship in space (ALIESHP1.PCX), bypassing the scene dispatch.
//             Then he is put on the pad out of sight and the cursor goes
//             (0x0662).
//   0x3c      [0xa49c] > 0x50: the beam's hum, INPUT:sub_01e2e
//   0x3e      a tick later he is there ([0xa94d] = 1)
//   0x3f      [0xa49c] > 0xa: he walks off the pad to (0xb2, 0x74)
//   0x46      [0xa49c] > 0x2d: the pad is live again, everything from Earth
//             is taken off him (the list at ds:0x4438), the cursor comes
//             back, and the first time he says outcome 0xb
//
// The pad itself runs every tick (0x0aae): while [0x33f2] is clear, standing
// on it (sprite x 0xb9..0xc1, y under 0x13) counts [0x33f0] up to 0x64. At
// 0x28 the chamber opens; stepping off resets the count and shuts it. At 0x64
// the trip back runs: 0x64 takes the cursor, 0x65 waits 0x14 and hums, 0x69
// takes him away and 0x6b, 0x37 later, writes submode 100 -- the park.
//
// Both hums (INPUT:sub_01e2e) also flash the room: [0x33ea] = 5 starts four
// pulses of every palette entry but 0 mixed toward a pale blue (0x2d, 0x2d,
// 0x3f) by UTIL:sub_023f4, each starting at 0x100 and falling 0x46 a tick, a
// new one every second tick pair (0x07cc).
//
// From the jail (game mode 58) the room opens through CHARANIM:sub_14b1a:
// [0xa7b1] := 2, so the next arrest opens on the corridor, and Ben stood at
// (0xc6, 0x3b). The first time ([0x33e4] == 0) that is behind FROMJAIL.PCX,
// "Much, much later...", faded up, held 0x8c ticks and faded away, with him out
// of sight and the cursor gone for the room's machine:
//
//   9      [0xa49c] > 0x46: slot 6, TRA_SHAF's 0x74 frames of him climbing
//          out of the vent, and samples 3 and 4
//   0xa    the slot has run out ([0xa4f0]): he is himself again
//   0xb    [0xa49c] > 0xa: line 7 and the cursor
//
// After that he is simply there.
//
// What is not here is the wipe OBJ:sub_02f27 runs over the park arrival.
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
static const byte kHandsetObj = 9;
static const byte kVerbUse = 10;
static const byte kVerbPickUp = 1;

/// MIDAS:sub_19d5a(1): PAR_BENP, Ben lifting the handset (ds:0x7008).
static const uint kLiftSlot = 2;
static const byte kLiftFrames[] = { 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 31 };
static const int kLiftRate = 4;
static const int kLiftMode = 8;

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
static const byte kStepLifted = 0x64;
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

/// Room 56.
static const uint16 kShipVisited = 0x33e0;		///< counted up as the first arrival ends
static const uint16 kShipBeenHere = 0x33df;	///< set by every arrival from the park
static const uint16 kChamberState = 0x33e6;	///< 1 shut, 2 open
static const uint16 kPadCount = 0x33f0;		///< ticks stood on the pad
static const uint16 kPadOff = 0x33f2;			///< the pad ignores him while set

static const uint kShipRecord = 15;			///< ALIESHP1.PCX, the ship in space
static const byte kFromPark = kParkRoom;
static const byte kFromJail = 58;

static const uint kShutSlot = 3, kOpenSlot = 4, kShaftSlot = 6;
/// LOGIC:sub_123d0's list, ds:0x6d84: the shut chamber's pulse.
static const byte kShutFrames[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 10
};
static const int kShutRate = 2;
static const int kOpenFirst = 1, kOpenCount = 7, kOpenRate = 1;

/// Where the beam puts him, char_place(0, 0xbd, 0, 0x10, 3) at 0x066e, and
/// the walk off the pad that follows (0x09f7).
static const int16 kPadX = 0xbd, kPadY = 0x10;
static const int16 kOffPadX = 0xb2, kOffPadY = 0x74;
/// And the chamber's own approach point for a plain walk, which is the pad.
static const byte kChamberObj = 1;
static const int16 kPadWalkX = 0xc7, kPadWalkY = 0x50;
static const byte kOffPadFacing = 3;

/// The pad, as the tick at 0x0ab5 tests the sprite origin.
static const int16 kPadMinX = 0xb8, kPadMaxX = 0xc2, kPadMaxY = 0x13;
static const uint kPadOpen = 0x28, kPadGo = 0x64;

/// ds:0x4438: what cannot come aboard, OBJ:sprite_remove'd one by one.
static const byte kEarthItems[] = {
	1, 3, 4, 5, 6, 7, 8, 9, 11, 12, 14, 15, 16, 17, 18, 19, 20, 24, 25, 26,
	27, 28, 29, 31, 32, 33, 37, 38, 39
};

/// INPUT:sub_01e2e, the hum: three of sample 6.
static const TeleportSample kHumSample = { 6, 0x34bc, 0x40, 1 };
static const uint kHumCount = 3;

static const byte kLineAboard = 0x0b;		///< ROOM56.TAL, the first arrival

/// The hum's flashes: UTIL:sub_023f4(level, 0x2d, 0x2d, 0x3f, 1, 0xff).
static const byte kFlashCount = 5;
static const int kFlashFull = 0x100, kFlashFall = 0x46;
static const byte kFlashInk[3] = { 0x2d, 0x2d, 0x3f };
static const int kFlashFirst = 1, kFlashEntries = 0xff;

/// CHARANIM:sub_14b1a and the jail arrival's steps.
static const uint16 kJailView = 0xa7b1;
static const byte kJailCorridor = 2;
static const uint16 kOutOfJail = 0x33e4;		///< the vent has been climbed out of once
static const char *const kJailCard = "FROMJAIL.PCX";
static const uint kJailCardTicks = 0x8c;
static const int16 kVentX = 0xc6, kVentY = 0x3b;
static const byte kVentFacing = 4;
static const int kVentFrames = 0x74, kVentRate = 3;
static const TeleportSample kVentSamples[] = {
	{ 3, 0x2af8, 0x37, 1 }, { 4, 0x2af8, 0x37, 0x13b }
};
static const int8 kVentPanning = 0x14;
static const byte kLineOutOfJail = 7;

static const byte kShipVent = 9;
static const byte kShipVentOut = 0xa;
static const byte kShipVentDone = 0xb;
static const uint kVentWait = 0x46, kVentLineWait = 0x0a;

static const byte kShipBeamIn = 0x3c;
static const byte kShipAppear = 0x3e;
static const byte kShipStepOff = 0x3f;
static const byte kShipSettle = 0x46;
static const byte kShipLeave = 0x64;
static const byte kShipHum = 0x65;
static const byte kShipVanish = 0x69;
static const byte kShipGone = 0x6b;

static const uint kBeamInTicks = 0x50, kStepOffTicks = 0x0a, kSettleTicks = 0x2d;
static const uint kHumTicks = 0x14, kGoneShipTicks = 0x37;

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

/// Every arrival puts room 22's machine down, and opens room 56 (0x0462).
void AlienEngine::startTeleport() {
	_teleportStep = 0;
	_teleportWait = 0;
	_shipStep = 0;
	_shipWait = 0;

	if (_room != kShipRoom)
		return;

	_script.setFlag(kPadOff, 0);

	// The vent is the jail arrival's own play ([0xa49f] 9, 0x093c), which
	// roominit.cpp lifts as an opening play on every way in; the machine
	// below plays it when it is due.
	_anims.takeDown(kShaftSlot);
	_shipFlashes = 0;
	_shipFlashDue = false;

	// 0x0591: the chamber stands open for an arrival through it.
	const bool fromPark = _mode == kFromPark;
	_script.setFlag(kChamberState, 1);
	if (fromPark) {
		_script.setFlag(kChamberState, 2);
		_script.setFlag(kPadOff, 1);
	}
	shipChamber(fromPark);

	if (_mode == kFromJail)
		shipFromJail();

	if (!fromPark)
		return;

	// 0x0669: on the pad, out of sight, and no cursor until he is off it.
	// roominit.cpp has already stood him there.
	_script.setFlag(kShipBeenHere, 1);
	_ben.placeSprite(kPadX, kPadY, kOffPadFacing);
	hideCharacter();
	CursorMan.showMouse(false);
	_shipStep = kShipBeamIn;
	debugC(1, kDebugRooms, "ship: beamed in from the park");
}

/// CHARANIM:sub_14b1a, room 56's opening after the climb through the shaft.
void AlienEngine::shipFromJail() {
	_script.setFlag(kJailView, kJailCorridor);
	_ben.placeSprite(kVentX, kVentY, kVentFacing);

	if (_script.flag(kOutOfJail) == 1)
		return;

	showStill(kJailCard, kJailCardTicks);
	hideCharacter();
	CursorMan.showMouse(false);
	_shipStep = kShipVent;
	_shipWait = 0;
	_script.setFlag(kOutOfJail, 1);
	debugC(1, kDebugRooms, "ship: much, much later, out of the vent");
}

/// INPUT:sub_01e2e's half of the flash: [0x33ea] = 5 and the rest cleared.
void AlienEngine::startShipFlash() {
	memcpy(_shipFlashSource, _palette, sizeof(_shipFlashSource));
	_shipFlashes = kFlashCount;
	_shipFlashCount = 0;
	_shipFlashLevel = 0;
	_shipFlashDue = false;
}

/// The room's tick, 0x07cc: every master tick, a mix is pushed if one is due,
/// and while flashes are left the level falls and a new pulse starts every
/// second tick pair. The last mix is at the level the last pulse fell to,
/// which the source puts back.
void AlienEngine::stepShipFlash() {
	if (_room != kShipRoom || (!_shipFlashes && !_shipFlashDue))
		return;

	if (_shipFlashDue) {
		const int level = _shipFlashLevel;
		for (int i = kFlashFirst * 3; i < (kFlashFirst + kFlashEntries) * 3; i++) {
			const int ink = kFlashInk[i % 3] * 255 / 63;
			_palette[i] = (byte)((_shipFlashSource[i] * (0x100 - level) + ink * level) >> 8);
		}
		g_system->getPaletteManager()->setPalette(_palette + kFlashFirst * 3, kFlashFirst,
												  kFlashEntries);
		_shipFlashDue = false;
	}

	if (!_shipFlashes)
		return;

	_shipFlashDue = true;
	if (_shipFlashLevel > 0)
		_shipFlashLevel = MAX(_shipFlashLevel - kFlashFall, 0);
	if ((_tick & 1) == 0)
		_shipFlashCount++;
	if (_shipFlashCount == 2) {
		_shipFlashes--;
		if (_shipFlashes) {
			_shipFlashLevel = kFlashFull;
			_shipFlashCount = 0;
		}
	}
}

/// LOGIC:sub_123f1 and sub_123d0: the chamber open, or shut and pulsing.
void AlienEngine::shipChamber(bool open) {
	if (open) {
		_script.setFlag(kChamberState, 2);
		_anims.stop(kShutSlot);
		_anims.play(kOpenSlot, kOpenFirst, kOpenCount, kOpenRate, kPlayMode);
	} else {
		_script.setFlag(kChamberState, 1);
		_anims.stop(kOpenSlot);
		_anims.play(kShutSlot, 0, ARRAYSIZE(kShutFrames), kShutRate, 6, kShutFrames);
	}
	_dirty = true;
}

/**
 * Entry 0's two approach points for the chamber (0x01d7-0x021b): a plain walk
 * goes onto the pad, and a right click or an item in hand ([0x8d0f], or
 * [0xa825] with [0x8d0e]) stops in front of it. The lift keeps both rows and
 * the second always wins, so the switch on the button is put back here.
 */
void AlienEngine::shipWalkTarget(byte obj, bool action, WalkTarget &target) {
	if (_room != kShipRoom || obj != kChamberObj || action)
		return;

	target.x = kPadWalkX;
	target.y = kPadWalkY;
	target.facing = kOffPadFacing;
}

/// 0x046b: the first arrival from the park opens on the ship itself.
void AlienEngine::shipArrivalScene(int room) {
	if (room != kShipRoom || _mode != kFromPark || _script.flag(kShipVisited) != 0)
		return;

	debugC(1, kDebugCutscene, "ship: record %u, the ship in space", kShipRecord);
	playCutsceneRecord(kShipRecord);
}

/// The two click hooks at 0x0043 and 0x00e5: the phone, and the number on it.
bool AlienEngine::armTeleportPhone(int obj, byte verb, byte item) {
	if (_room != kParkRoom || _teleportStep)
		return false;

	// 0x0134: the handset off its hook.
	if (!item && obj == kHandsetObj && verb == kVerbPickUp &&
		_script.flag(kHandsetDown) == 1) {
		_script.setFlag(kHandsetDown, 0);
		hideCharacter();
		_anims.play(kLiftSlot, 0, ARRAYSIZE(kLiftFrames), kLiftRate, kLiftMode, kLiftFrames);
		CursorMan.showMouse(false);
		_teleportStep = kStepLifted;
		debugC(1, kDebugRooms, "teleport: the handset comes off its hook");
		return true;
	}

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

/// Room 22's [0xa49f] machine from 0x0a04, the phone's half of it.
void AlienEngine::stepTeleport() {
	if (_room != kParkRoom || !_teleportStep)
		return;

	switch (_teleportStep) {
	case kStepLifted:
		// 0x0bf5: [0xa4ec], two frames of the lift still to go.
		if (_anims.remaining(kLiftSlot) > 2)
			break;
		_teleportStep = 0;
		showCharacter();
		CursorMan.showMouse(true);
		break;

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

/// Room 56's machine (0x092e) and the pad under it (0x0aae).
void AlienEngine::stepShip() {
	if (_room != kShipRoom)
		return;

	// [0xa49c] moves on the animation frame (0x0715), every second tick pair.
	if ((_tick & 3) == 0)
		_shipWait++;

	switch (_shipStep) {
	case kShipVent:
		CursorMan.showMouse(false);
		if (_shipWait <= kVentWait)
			break;
		_anims.play(kShaftSlot, 1, kVentFrames, kVentRate, 1);
		for (uint i = 0; i < ARRAYSIZE(kVentSamples); i++)
			_sound.queue(kVentSamples[i].sample, kVentSamples[i].rate, kVentSamples[i].volume,
						 kVentPanning, kVentSamples[i].delay);
		_shipStep = kShipVentOut;
		debugC(1, kDebugRooms, "ship: out of the vent, %d frames", kVentFrames);
		break;

	case kShipVentOut:
		if (_anims.remaining(kShaftSlot) != 0)
			break;
		showCharacter();
		debugC(1, kDebugRooms, "ship: standing by the vent at %d,%d", _ben.walkX(), _ben.walkY());
		_shipStep = kShipVentDone;
		_shipWait = 0;
		break;

	case kShipVentDone: {
		if (_shipWait <= kVentLineWait)
			break;
		CursorMan.showMouse(true);
		_shipStep = 0;
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kLineOutOfJail, anchorX, anchorY);
		break;
	}

	case kShipBeamIn:
		// A scene handing the room back gives the cursor back with it.
		CursorMan.showMouse(false);
		if (_shipWait <= kBeamInTicks)
			break;
		for (uint i = 0; i < kHumCount; i++)
			_sound.queue(kHumSample.sample, kHumSample.rate, kHumSample.volume, 0,
						 kHumSample.delay);
		startShipFlash();
		_shipStep = kShipAppear;
		_shipWait = 0;
		break;

	case kShipAppear:
		if (_shipWait <= 0)
			break;
		showCharacter();
		_shipStep = kShipStepOff;
		_shipWait = 0;
		break;

	case kShipStepOff:
		if (_shipWait <= kStepOffTicks)
			break;
		walkTo(kOffPadX, kOffPadY, kOffPadFacing);
		debugC(1, kDebugRooms, "ship: off the pad from %d,%d, walking %d", _ben.walkX(),
			   _ben.walkY(), _ben.isWalking());
		_shipStep = kShipSettle;
		_shipWait = 0;
		break;

	case kShipSettle: {
		if (_shipWait <= kSettleTicks)
			break;
		_script.setFlag(kPadOff, 0);
		for (uint i = 0; i < ARRAYSIZE(kEarthItems); i++)
			if (_inventory.has(kEarthItems[i]))
				_inventory.remove(kEarthItems[i]);
		CursorMan.showMouse(true);
		_shipStep = 0;
		if (_script.flag(kShipVisited) == 0) {
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kLineAboard, anchorX, anchorY);
		}
		_script.setFlag(kShipVisited, _script.flag(kShipVisited) + 1);
		debugC(1, kDebugRooms, "ship: aboard at %d,%d, the Earth things left behind",
			   _ben.walkX(), _ben.walkY());
		break;
	}

	case kShipLeave:
		CursorMan.showMouse(false);
		_shipStep = kShipHum;
		_shipWait = 0;
		break;

	case kShipHum:
		if (_shipWait <= kHumTicks)
			break;
		for (uint i = 0; i < kHumCount; i++)
			_sound.queue(kHumSample.sample, kHumSample.rate, kHumSample.volume, 0,
						 kHumSample.delay);
		startShipFlash();
		_shipStep = kShipVanish;
		_shipWait = 0;
		break;

	case kShipVanish:
		if (_shipWait <= 0)
			break;
		hideCharacter();
		_shipStep = kShipGone;
		_shipWait = 0;
		break;

	case kShipGone:
		if (_shipWait <= kGoneShipTicks)
			break;
		_shipStep = 0;
		CursorMan.showMouse(true);
		showCharacter();
		takeExit(kToParkSubmode);
		return;

	default:
		break;
	}

	// 0x0aae: the pad, whenever the arrival is not holding it off.
	if (_script.flag(kPadOff) == 0) {
		const int x = _ben.spriteX(), y = _ben.spriteY();
		if (x > kPadMinX && x < kPadMaxX && y < kPadMaxY) {
			if (_script.flag(kPadCount) < kPadGo)
				_script.setFlag(kPadCount, _script.flag(kPadCount) + 1);
		} else {
			_script.setFlag(kPadCount, 0);
			if (_script.flag(kChamberState) == 2)
				shipChamber(false);
		}
	}
	if (_script.flag(kPadCount) == kPadOpen) {
		_script.setFlag(kPadCount, kPadOpen + 1);
		shipChamber(true);
	}
	if (_script.flag(kPadCount) == kPadGo) {
		_script.setFlag(kPadCount, kPadGo + 1);
		_shipStep = kShipLeave;
		debugC(1, kDebugRooms, "ship: on the pad long enough, back to the park");
	}
}

} // End of namespace Alien
