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

// Room 22's arrival scene: the thing in the booth leaves, and the booth does
// not survive it.
//
// Playtest report: "There should be a cutscene going into Park / Teleport Area
// as well ... one where pumpkinman teleports and one where a small alien talks.
// And the phone booth should be 'normal' before and broken afterwards?"  All
// three are one scene, and none of it is a lifted cutscene: data/lift/
// cutscenes.json has no trigger for room 22 -- what plays here is the room's
// own [0xa49f] machine, ovr_16_0ea3 entry 2, which nothing carried over.
//
// The trigger, 0x0d47, tested on every pass of the room's own loop:
//
//   [0xa728] == 0     the once-only latch, an ordinary saved flag
//   [0xa8ec] < 0x19a  Ben has come far enough west across the park
//   [0xa0cc] == 0     with no scroll in flight
//
//   -> [0xa728] = 1, Ben walks to (305, 106) facing 4, [0xa49f] = 0x28,
//      [0xa948] = 0: the cursor goes away for the whole of it.
//
// And the machine itself, 0x0a8f to 0x0bee.  Slot 0 is PAR_TELE, the booth;
// slot 1 is PAR_BOCL, the wreck it is left as.  Argument order is push order
// for this compiler, as tools/gen_plates.py already notes for the stamp call.
//
//   0x28  play(slot 0, frame 1, 9 frames, rate 4), the talk flag [0x293c],
//         and DIALOG:sub_0b63a(outcome 50, x 132, y 67) -- the voice from
//         inside: "Helloo... You hear me? This piece of junk won't stay in one
//         piece much longer! ... Just keep the line open! I'm coming up!"
//   0x29  the line down ([0xad1c]): clears the talk flag, resets [0xa49c]
//   0x2a  [0xa49c] > 0x23
//   0x2b  stamp(slot 1, frame 1) -- the booth is a wreck from here on -- and
//         play(slot 0, frame 0xa, 0x10 frames, rate 3)
//   0x2c  slot 0 idle ([0xa4ea]): play(slot 0, frame 0x1b, 1 frame) holds it
//   0x2d  OBJ:sub_06e7d / sub_070b9, the sprite and hotspot refresh
//   0x2e  play(slot 0, frame 0x1a, 13 frames, rate 3)
//   0x2f  slot 0 idle: queue_event(51), "Wow! A teleport booth... Now I know
//         how to get to their ship!"
//   0x30  the line down: Ben walks to (172, 112) facing 4, [0xa49c] reset
//   0x31  [0xa49c] > 0x46: queue_event(52), "But first I'll have to find a way
//         to fix that piece of junk though."
//   0x32  the line down: the cursor comes back and [0xa49f] = 0
//
// The booth's two faces are split the same way the rest of the room is.  The
// room's authored plate is the *wrecked* one: plates.cpp stamps PAR_WIRE, the
// broken cables, unconditionally, PAR_PUM2 -- the pumpkin mask left lying there
// -- while [0x33b4] is 0, and PAR_RAD1 once the transistor is in.  The whole
// booth is stamped by the enter routine alone, at 0x07b3: 10c9:sub_1132a(0, 1)
// under [0xa728] == 0.  That call is not a plate step and tools/gen_roominit.py
// has no stamp op, so before this the port drew the wreck from the first visit
// onward, which is the report's third half.
//
// [0x293c] is not decoration: the room's tick relaunches slot 0 while that byte
// is set (anims.cpp), so the booth's own frames repeat for exactly as long as
// the line lasts and stop when the machine clears it. tools/gen_anims.py did not
// recognise the guard until this scene was ported and read the row as
// unconditional, which left the booth looping and state 0x2c waiting forever for
// a slot that never ran out.
//
// The camera is the room's as well (0x0d1a, every pass): while Ben's sprite is
// left of 0x124 the room holds it at the left edge ([0xa8e0] = 1, [0xa8e2] =
// 0), and past 0x123 it lets it go again -- unless the scene has taken it
// ([0xa8a4]), in which case it stays held until the last line. The trigger
// takes it, slides the bar away (OBJ:sub_02f9b) and waits in 0x28 for the pan
// to come to rest ([0xa0cc] and [0xa0c4] both zero) before the booth speaks,
// so the whole of it plays on the left half of the park where the booth is.
// Without that hold the port's camera stayed on Ben, off to the right, and the
// scene played out of sight with only its lines on the screen (playtest issue
// #29). The two [0xa49c] waits are counted in ticks of this hook.
//
// The ride between the park and room 56 is a different pair of machines in the
// same body (departure 0x96/0x98/0xaa, arrival 0xdc..0xdf, PAR_BENP), and it
// stays the delay teleport.cpp stands in with.
static const int kParkRoom = 22;

static const uint16 kSceneSeen = 0xa728;	///< the once-only latch, 0x0d47

/// The booth: slot 0's first frame is it whole, with its occupant standing in
/// it; slot 1's is the wreck the scene leaves.
static const uint kBoothSlot = 0;
static const uint kWreckSlot = 1;
static const int kWholeFrame = 1;
static const int kWreckFrame = 1;

/// The trigger's own test, 0x0d4e, and where it puts Ben.
static const int kTriggerX = 0x19a;
static const int kStartX = 305;
static const int kStartY = 106;
static const int kStartFacing = 4;

/// 0x0d1a: left of this the camera is held at the room's left edge.
static const int kHoldBelowX = 0x124;

/// And where he walks to for the second line, 0x0b97.
static const int kLookX = 172;
static const int kLookY = 112;

/// ROOM22.TAL outcome codes: zone 1 sends 50 on to entry 20.
static const byte kLineVoice = 50;
static const byte kLineBooth = 51;
static const byte kLineBroken = 52;

/// Where the voice from inside the booth is anchored, 0x0ab3.
static const int kVoiceX = 132;
static const int kVoiceY = 67;

/// [0xa49f]'s steps in this room, by the original's own numbers.
static const byte kStepVoice = 0x28;
static const byte kStepVoiceDone = 0x29;
static const byte kStepHold = 0x2a;
static const byte kStepWreck = 0x2b;
static const byte kStepSettle = 0x2c;
static const byte kStepRefresh = 0x2d;
static const byte kStepAfter = 0x2e;
static const byte kStepSpeak = 0x2f;
static const byte kStepWalk = 0x30;
static const byte kStepSecond = 0x31;
static const byte kStepEnd = 0x32;

/// The two [0xa49c] waits, 0x0aed and 0x0bb4, as ticks of this hook.
static const uint kHoldTicks = 0x24;
static const uint kSecondTicks = 0x47;

/// The plays the machine issues, all of them on the booth's own slot.
static const int kOpenFirst = 1, kOpenCount = 9, kOpenRate = 4, kOpenMode = 4;
static const int kGoFirst = 0xa, kGoCount = 0x10, kGoRate = 3, kGoMode = 4;
static const int kRestFrame = 0x1b;
static const int kAfterFirst = 0x1a, kAfterCount = 13, kAfterRate = 3, kAfterMode = 1;

/**
 * 0x07b3: the booth is whole until the scene has played, and only the enter
 * routine ever draws it that way.
 */
void AlienEngine::startPark() {
	if (_room != kParkRoom)
		return;

	_parkStep = 0;
	_parkWait = 0;
	_parkLock = false;			// 0x0599

	if (_script.flag(kSceneSeen) != 0)
		return;

	_anims.stamp(kBoothSlot, kWholeFrame, _background);
	debugC(1, kDebugRooms, "park: the booth is whole, with its occupant in it");
}

/// The trigger at 0x0d47 and the machine at 0x0a8f, one step to a tick.
void AlienEngine::stepPark() {
	if (_room != kParkRoom)
		return;

	// 0x0d1a: the left half of the park holds the camera on itself, unless the
	// scene already has it.
	if (!_parkLock) {
		if (_ben.spriteX() < kHoldBelowX)
			_scrollHold = 0;
		else
			_scrollHold = -1;
	}

	if (!_parkStep) {
		// 0x0d47: far enough west, no pan in flight, and the latch has not
		// been burnt. The x is the sprite's, [0xa8ec].
		if (_script.flag(kSceneSeen) != 0 || _ben.spriteX() >= kTriggerX || _scrollVel != 0)
			return;

		_script.setFlag(kSceneSeen, 1);
		_scrollHold = 0;
		_parkLock = true;
		walkTo(kStartX, kStartY, kStartFacing);
		CursorMan.showMouse(false);
		hideBar();
		_parkStep = kStepVoice;
		debugC(1, kDebugRooms, "park: the scene starts, step 0x%02x", kStepVoice);
		return;
	}

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	switch (_parkStep) {
	case kStepVoice:
		// 0x0a93: not before the camera has come to rest on the booth.
		if (_scrollVel != 0 || _scrollX != 0)
			break;
		// 0x0aa1: the booth opens and whatever is in it shouts.
		_anims.play(kBoothSlot, kOpenFirst, kOpenCount, kOpenRate, kOpenMode);
		_anims.setLoopFlag(kBoothSlot, 1);
		queueOutcome(_tal, kLineVoice, kVoiceX, kVoiceY, false);
		_parkStep = kStepVoiceDone;
		_dirty = true;
		break;

	case kStepVoiceDone:
		// 0x0acb
		if (!speechDone())
			break;
		_anims.setLoopFlag(kBoothSlot, 0);
		_parkWait = kHoldTicks;
		_parkStep = kStepHold;
		break;

	case kStepHold:
		// 0x0aed
		if (_parkWait && --_parkWait)
			break;
		_parkStep = kStepWreck;
		break;

	case kStepWreck:
		// 0x0b00: the wreck goes into the plate, and the booth fires.
		_anims.stamp(kWreckSlot, kWreckFrame, _background);
		_anims.play(kBoothSlot, kGoFirst, kGoCount, kGoRate, kGoMode);
		_parkStep = kStepSettle;
		_dirty = true;
		debugC(1, kDebugRooms, "park: the booth is wrecked");
		break;

	case kStepSettle:
		// 0x0b27: one frame left standing where the booth was.
		if (_anims.remaining(kBoothSlot))
			break;
		_anims.play(kBoothSlot, kRestFrame, 1, 0, kGoMode);
		_parkStep = kStepRefresh;
		break;

	case kStepRefresh:
		// 0x0b4c
		rebuildHotspots();
		_parkStep = kStepAfter;
		break;

	case kStepAfter:
		// 0x0b5d
		_anims.play(kBoothSlot, kAfterFirst, kAfterCount, kAfterRate, kAfterMode);
		_parkStep = kStepSpeak;
		break;

	case kStepSpeak:
		// 0x0b76
		if (_anims.remaining(kBoothSlot))
			break;
		queueOutcome(_tal, kLineBooth, anchorX, anchorY);
		_parkStep = kStepWalk;
		break;

	case kStepWalk:
		// 0x0b97: Ben goes over to what is left of it.
		if (!speechDone())
			break;
		walkTo(kLookX, kLookY, kStartFacing);
		_parkWait = kSecondTicks;
		_parkStep = kStepSecond;
		break;

	case kStepSecond:
		// 0x0bb4
		if (_parkWait && --_parkWait)
			break;
		queueOutcome(_tal, kLineBroken, anchorX, anchorY);
		_parkStep = kStepEnd;
		break;

	case kStepEnd:
		// 0x0bce: the room is the player's again.
		if (!speechDone())
			break;
		_parkStep = 0;
		_parkLock = false;
		CursorMan.showMouse(true);
		showBar();
		_scrollHold = -1;
		_dirty = true;
		debugC(1, kDebugRooms, "park: the scene is over");
		break;

	default:
		break;
	}
}

} // End of namespace Alien
