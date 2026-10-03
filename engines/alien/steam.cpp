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
#include "graphics/paletteman.h"

#include "common/system.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Rooms 49 (engine room) and 50 (steam room), and the ladder between them.
//
// The ladder is object 1 in both rooms, a registered rectangle with a walk
// row (check_hotspots.py / check_walkgeom.py --sweep 49/50), and either room's
// tick answers a walk that ends ([0x9908] == 1) with the click that started it
// on object 1 ([0xa644] == 1):
//
//   room 50  ovr_32_0f81:0x06b2  submode 1 at once; room 49's own init plays
//                                the climb down (slot 0 backwards, mode 5)
//   room 49  ovr_31_0f85:0x06c0  the walker goes, the climb up plays on slot 0
//                                (eng_goup, 18 frames) and [0xa49f] = 5 takes
//                                submode 1 once [0xa4ea] reaches 1
//
// and each room's init answers the other's: [0xa49f] = 3 in room 49 gives the
// cursor back and turns him left once slot 0 is down to 1, [0xa49f] = 0x16 in
// room 50 does the same once STE_ladd on slot 2 is down to 2. The plays and
// placements themselves are lifted (roominit.cpp); the walker hidden under
// them is the [0xa94d] the inits clear, which is the port's hideCharacter().
//
// The steam ([0xa798], ships 1) never guards the ladder. What it guards is the
// right half of room 50: while it is up, its walk rows stop him at 220,142,
// and a left click past x 0xe1 latches [0xa644] = 0xc8 (0x0197) so the arrival
// speaks line 3. The valve that clears it is room 49's object 3 under Pull,
// registered only while the switch (object 4, Push) has [0xa799] up; both
// bodies are lifted (roomscripts.json blocks 165/166), and so is room 50's
// lever, object 6 under Pull (block 168). What the lift leaves out is the
// state each body hands to the room's tick, which is all this file keeps.
static const int kEngineRoom = 49;
static const int kBoilerRoom = 50;

static const uint16 kSteamOn = 0xa798;	///< ships 1: the right half of room 50 is too hot
static const uint16 kLeverUp = 0xa789;	///< ships 1: room 50's lever has not been pulled
static const uint16 kClickedObj = 0xa644;	///< the object the last left click was on

static const byte kLadderObj = 1;
static const byte kTooHot = 0xc8;	///< room 50's own [0xa644] value for the hot side
static const byte kLeverObj = 6;
static const byte kVerbPull = 13;

static const byte kLadderSubmode = 1;	///< transitions.cpp: 49 <-> 50, submode 1 both ways

static const byte kOutcomeTooHot = 3;

/// Room 49's ladder: eng_goup on slot 0, both ways.
static const uint kClimbSlot = 0;
static const int kClimbFrames = 0x12;
static const int kClimbRate = 4;
static const int kClimbMode = 1;
/// Room 50's: STE_ladd on slot 2, played by its init on the way up.
static const uint kBoilerLadderSlot = 2;
/// Room 50's lever: STE_BEVI on slot 1, Ben's own pull.
static const uint kLeverSlot = 1;

/// Room 49 after the valve: [0xa49f] = 0x1e waits [0xa49c] > 6, and 0x20
/// plays eng_BLOW on slot 5 with sample 4.
static const uint kBlowSlot = 5;
static const int kBlowFrames = 0x0b;
static const int kBlowRate = 2;
static const uint kBlowWait = 6;
static const uint kBlowSample = 4;
static const uint32 kBlowSampleRate = 0x2710;
static const byte kBlowVolume = 0x40;
static const int8 kBlowPanning = 0;
static const uint16 kBlowDelay = 1;

/// Room 49's light (ovr_31_0f85:0x050e). [0xa792] is how far palette entries
/// 0xe5..0xfe are mixed toward black (UTIL:sub_023f4, from the room's own
/// palette): the room's init zeroes it and every pass adds 7 up to 0xff, so
/// the room opens lit and goes dark. The switch, object 4 under Push, sets
/// [0xa799]; while it is up, a random draw (`& 0xfa0` == 0) starts a burst of
/// 0x28 passes, and on each of those a second draw (`& 5` == 0) puts the
/// level back to 0 -- the light catching and failing again.
static const uint16 kLightOn = 0xa799;
static const uint16 kLightLevel = 0xa792;
static const int kLightFirst = 0xe5;
static const int kLightCount = 0x1a;
static const int kLightStep = 7;
static const uint kBurstMask = 0xfa0;
static const uint kFlashMask = 5;
static const uint kBurstLength = 0x28;

/// [0xa803] = 4 at ovr_31_0f85:0x0659: off the ladder, he faces left.
static const int kFacingLeft = 4;

/// The [0xa49f] steps of both rooms.
static const byte kStepDownArrived = 3;	///< room 49: in from room 50
static const byte kStepClimbingUp = 5;	///< room 49: on the way to room 50
static const byte kStepUpArrived = 0x16;	///< room 50: in from room 49
static const byte kStepLever = 0x33;	///< room 50 (its own 3): the lever pull
static const byte kStepBlowWait = 0x1e;	///< room 49: the valve has just turned
static const byte kStepBlow = 0x20;

/// The arrival plays that each room's init starts: hide the walker under them
/// and take the cursor away until they finish.
void AlienEngine::startSteam() {
	_steamStep = 0;
	_steamPos = 0;
	_steamWas = _script.flag(kSteamOn);
	_steamBurst = 0;
	_steamRelight = false;

	if (_room == kEngineRoom) {
		// ovr_31_0f85:0x0308, and the palette sub_023f4 mixes from.
		_script.setFlag(kLightLevel, 0);
		memcpy(_steamLight, _palette + kLightFirst * 3, sizeof(_steamLight));
	}

	if (_room == kEngineRoom && _mode == kBoilerRoom) {
		// ovr_31_0f85:0x045a
		CursorMan.showMouse(false);
		hideCharacter(kClimbSlot);
		_steamStep = kStepDownArrived;
		_steamRelight = true;
		debugC(1, kDebugRooms, "steam: down the ladder from room 50");
	} else if (_room == kBoilerRoom && _mode == kEngineRoom) {
		// ovr_32_0f81:0x0459
		CursorMan.showMouse(false);
		hideCharacter(kBoilerLadderSlot);
		_steamStep = kStepUpArrived;
		_steamRelight = true;
		debugC(1, kDebugRooms, "steam: up the ladder from room 49");
	}
}

/// Room 50's entry 0 (0x0197): a left click on the hot side while the steam
/// is up latches its own value over the clicked object.
void AlienEngine::steamClick(int roomX, int y) {
	if (_room != kBoilerRoom || _script.flag(kSteamOn) != 1 || _heldItem != Inventory::kNoItem)
		return;
	if (roomX > 0xe1 && y < 0xa1)
		_script.setFlag(kClickedObj, kTooHot);
}

/// The click body has just run: room 50's lever plays Ben pulling it.
void AlienEngine::armSteam(int obj, byte verb) {
	if (_room != kBoilerRoom || obj != kLeverObj || verb != kVerbPull)
		return;

	// ovr_32_0f81:0x00b5: the body clears [0xa789] as it starts the play, and
	// otherwise only speaks, so a running slot 1 is the one sign it pulled.
	if (_script.flag(kLeverUp) != 0 || _anims.remaining(kLeverSlot) <= 2)
		return;

	// The walker only, not the slot: the original sets [0xa94d] back with two
	// frames to go and lets the play run out, and mode 1 leaves its last frame,
	// the lever down without him (frames 27 and 28), in the background. Handing
	// the slot to showCharacter() took it down there instead, and the plate's
	// lever came back up under him.
	hideCharacter();
	_steamStep = kStepLever;
	debugC(1, kDebugRooms, "steam: the lever is pulled");
}

/// Room 49's light, once a pass of its loop; the port gives it every tick,
/// the same as room 46's water (diving.cpp).
void AlienEngine::stepSteamLight() {
	if (_room != kEngineRoom)
		return;

	const byte was = _script.flag(kLightLevel);
	int level = was;

	if (_script.flag(kLightOn) == 1) {
		if ((_rnd.getRandomNumber(0xffff) & kBurstMask) == 0)
			_steamBurst = kBurstLength;
		if (_steamBurst > 0) {
			_steamBurst--;
			if ((_rnd.getRandomNumber(0xffff) & kFlashMask) == 0)
				level = 0;
		}
	}

	level = MIN(level + kLightStep, 0xff);
	_script.setFlag(kLightLevel, (byte)level);
	if (level == was)
		return;

	// UTIL:sub_023f4: (source * (0x100 - level) + black * level) >> 8.
	for (int i = 0; i < kLightCount * 3; i++)
		_palette[kLightFirst * 3 + i] = (byte)((_steamLight[i] * (0x100 - level)) >> 8);
	g_system->getPaletteManager()->setPalette(_palette + kLightFirst * 3, kLightFirst, kLightCount);
}

void AlienEngine::stepSteam() {
	if (_room != kEngineRoom && _room != kBoilerRoom)
		return;

	// [0xa49c], which LOGIC advances on every tick pair.
	_steamPos++;

	// The climb brings him in under the light of the room he left. The room
	// opens on that level, and the upload gate only pushes a change of it
	// (lighting.cpp), which a character standing still at the ladder never
	// makes: he kept the other room's brightness until his first step, then
	// dropped to this one's (finding #127). The port gives him this
	// room's level at once; the ladder is at the near end of both rooms, so it
	// is never room 49's far-side palette.
	if (_steamRelight) {
		_steamRelight = false;
		uploadCharPalette(false);
	}

	// Room 49's valve body turns the steam off; its machine follows on.
	const byte steam = _script.flag(kSteamOn);
	if (_room == kEngineRoom && _steamWas == 1 && steam == 0 && !_steamStep) {
		_steamStep = kStepBlowWait;
		_steamPos = 0;
		debugC(1, kDebugRooms, "steam: valve turned, step 0x%02x", kStepBlowWait);
	}
	_steamWas = steam;

	switch (_steamStep) {
	case kStepDownArrived:
		// ovr_31_0f85:0x0643
		if (_anims.remaining(kClimbSlot) != 1)
			return;
		showCharacter();
		CursorMan.showMouse(true);
		_ben.faceTo(kFacingLeft);
		_steamStep = 0;
		debugC(1, kDebugRooms, "steam: at the foot of the ladder");
		return;

	case kStepClimbingUp:
		// ovr_31_0f85:0x0669
		if (_anims.remaining(kClimbSlot) != 1)
			return;
		_steamStep = 0;
		debugC(1, kDebugRooms, "steam: submode %d, up to room 50", kLadderSubmode);
		takeExit(kLadderSubmode);
		return;

	case kStepUpArrived:
		// ovr_32_0f81:0x0697
		if (_anims.remaining(kBoilerLadderSlot) != 2)
			return;
		showCharacter();
		CursorMan.showMouse(true);
		_steamStep = 0;
		debugC(1, kDebugRooms, "steam: at the top of the ladder");
		return;

	case kStepLever:
		// ovr_32_0f81:0x0680
		if (_anims.remaining(kLeverSlot) != 2)
			return;
		showCharacter();
		_steamStep = 0;
		return;

	case kStepBlowWait:
		// ovr_31_0f85:0x0685
		if (_steamPos <= kBlowWait)
			return;
		_steamStep = kStepBlow;
		return;

	case kStepBlow:
		// ovr_31_0f85:0x0697
		_sound.queue(kBlowSample, kBlowSampleRate, kBlowVolume, kBlowPanning, kBlowDelay);
		_anims.play(kBlowSlot, 1, kBlowFrames, kBlowRate, 1);
		_steamStep = 0;
		return;

	default:
		break;
	}

	// The arrival tests, which run only while no step owns the room.
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;

	const byte obj = _script.flag(kClickedObj);
	if (!obj)
		return;

	if (obj == kLadderObj) {
		_script.setFlag(kClickedObj, 0);
		if (_room == kBoilerRoom) {
			// ovr_32_0f81:0x06b2
			debugC(1, kDebugRooms, "steam: submode %d, down to room 49", kLadderSubmode);
			takeExit(kLadderSubmode);
		} else {
			// ovr_31_0f85:0x06c0
			CursorMan.showMouse(false);
			playCharacterAnim(kClimbSlot, 1, kClimbFrames, kClimbRate, kClimbMode);
			_steamStep = kStepClimbingUp;
			debugC(1, kDebugRooms, "steam: up the ladder");
		}
		return;
	}

	if (_room == kBoilerRoom && obj == kTooHot) {
		// ovr_32_0f81:0x06e9
		_script.setFlag(kClickedObj, 0);
		if (steam == 1) {
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kOutcomeTooHot, anchorX, anchorY);
		}
	}
}

} // End of namespace Alien
