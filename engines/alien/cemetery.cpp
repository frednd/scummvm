#include "common/file.h"
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

// Room 32, the cemetery, and the statue that guards the way into the cave.
//
// tools/roomlogic.py lifted the plain-verb half of entry 3 -- the three flags
// a talk, a look and a read on the statue set (roomscripts.json rows 439-444,
// gaps.py's "statue needs the runestone phrase") -- and lost the one call none
// of them ever run without: `OBJ:sub_0967e(topic)`, the conversation menu. So
// the port had the flags moving but nothing to show for it: a talk on the
// statue did nothing at all.
//
// [0xa72e] is the guard the talk arm reads (ovr_20_0e8b:0x0071) and the one
// the machine below clears once the phrase is answered right (0x0b5d); it
// ships set from the new game's own init block (`seg_main.asm:0x3746`), which
// is why nothing else in the lift ever writes it -- there is no other writer,
// the same shape as [0xa76e] for Sluggs (sluggs.cpp).
//
// The topic a talk opens on is picked by two flags neither of which this room
// sets by itself: [0xa72c] is Yodle's ("The statue needs the runestone
// phrase", room 26 obj 22) and [0xa72d] is this room's own read on the
// gravestone (obj 3, verb 11). Topic 2 is the one that carries the phrase
// itself, and its second option ([0xa60c] == 2, "Look! A blueberry.") is the
// only one of the twelve that answers right -- the other three, "Look, I'm a
// huckleberry.", "Cook! Gimme that cherry." and "Hook, watch me sink your
// ferry.", read like the same joke and are not it.
//
// The tick machine behind it (0x0962 onward) is what the menu's own pick
// cannot be: [0xa49f] states 0x14/0x16/0x19/0x1a/0x1e/0x1f/0x28/0x29/0x2a/0x4b/
// 0x50/0x55 count wrong answers ([0xa72f], capped at four) and react to them,
// and 0x28-0x2a is the right one: it clears [0xa72e], plays the statue opening
// (slot 1, then slot 2) and speaks outcome 0x11.
//
// The other half is the exit itself, and it is not a `kWalkSubmode` row at
// all. Object 1 is the laser beams across the cave mouth -- outcome 15, "Laser
// beams. Cool." -- and walkgeom.cpp's zone `355,0..479,115 -> 377,107 facing
// 2` is what a click on them walks to. Nothing arms submode 3 on the way out
// of that click; entry 2's own walk-blocked handler does it on the way in
// (0x0c35-0x0c79):
//
//   [0x9908] == 1      the route has run out -- he has arrived
//   [0xa644] == 1      and the click that started the walk was on object 1,
//                      the latch HOTSPOT:sub_13605 keeps and clickAt mirrors
//   [0xa72e] == 1      the statue still wants the phrase: queue_event 14, "I
//                      can't walk through the beams. They'd cut me to pieces."
//   [0xa72e] == 0      the beams are down: the cursor and the walker go
//                      ([0xa948], [0xa94d]), slot 3 plays his sixteen frames
//                      of walking in, and [0xa49f] := 0x3c
//
// and state 0x3c (0x0b7e) waits for slot 3 to run out ([0xa4ed] == 0) before
// it writes `game_submode := 3` (0x0b8e), which is transitions.cpp's
// `{ 32, 3, 40 }`. So the exit is an arrival machine of the same shape as the
// shore's two cave mouths (shore.cpp) and the sewer's ladder, not a door.
//
// The port used to arm submode 3 from the click instead, against a list of
// coordinates none of which is the zone's, and raise a flag ([0xa962]) that
// nothing read: the beams opened and walking into them did nothing.
static const int kCemeteryRoom = 32;

static const byte kStatue = 2;			///< the object both the talk and the look answer
static const byte kVerbTalkTo = 6;
static const byte kVerbLookAt = 5;

static const uint16 kPhraseNeeded = 0xa72e;	///< the statue still wants the phrase
static const uint16 kTopic = 0xa72b;		///< which of the three topics, 0..2
static const uint16 kAttempts = 0xa72f;		///< wrong answers so far, capped at 4
static const byte kMaxAttempts = 4;

static const uint kCorrectTopic = 2;
static const uint kCorrectChoice = 2;		///< [0xa60c], 1-based: "Look! A blueberry."
static const byte kWrongBase = 0x13;		///< topic 0's four replies, [0xa60c] + this

static const byte kOutcomeSolved = 0x11;	///< 0x0b62
static const byte kOutcomeGiveUp = 0x21;	///< 0x0bc3, after four wrong tries on topic 1
static const byte kOutcomeNotYet = 0x0e;	///< 0x0c4f, walking for the cave too soon

/// The reward play: slot 1's eleven frames (0x0b33), then slot 2's nine-frame
/// open pose -- the same play roominit.cpp gives an arrival once [0xa72e] is
/// already 0.
static const uint kRewardSlot = 1;
static const uint kOpenSlot = 2;
static const int kRewardFrames = 0x0b;
static const int kOpenFrames = 9;
static const int kStatueRate = 3;
static const int kStatueMode = 1;	///< anim_play_mode1

/// [0xa644], the object the last left click was on (shore.cpp reads the same
/// latch), and the object that is the way into the cave.
static const uint16 kClickedObj = 0xa644;
static const byte kLasers = 1;

/// His sixteen frames of walking in (0x0c67), and the state that waits for
/// them before the submode goes out (0x0b7e).
static const uint kCaveSlot = 3;
static const int kCaveFrames = 0x10;
static const byte kStepEnterCave = 0x3c;
static const byte kCaveSubmode = 3;

/**
 * The talk and the look, both entry 3's plain-verb half (roomscripts.json
 * blocks 141/142): the lift already moves the flags those set, so this only
 * has to add the one call that turns them into something on screen.
 */
void AlienEngine::armCemeteryStatue(int obj, byte verb) {
	if (_room != kCemeteryRoom || obj != kStatue)
		return;

	if (verb == kVerbTalkTo && _script.flag(kPhraseNeeded) == 1) {
		// Calculate topic based on Yodle's hint and gravestone read
		uint topic = 0;
		if (_script.flag(0xa72d) == 1) {
			topic = 1;
			if (_script.flag(0xa72c) == 1) {
				topic = 2;
			}
		}
		_script.setFlag(kTopic, topic);
		openChat(topic);
		debugC(1, kDebugChat, "cemetery: the statue's topic is %u", topic);
	} else if (verb == kVerbLookAt) {
		// 0x00b8: the look queues its own line through the generic outcome
		// path: taking the cursor away and waiting for it is all this room
		// adds (0x0046, the tail of which reopens topic 0).
		CursorMan.showMouse(false);
		_cemeteryLookWait = true;
	}
}

/**
 * A walk finished at the beams (0x0c35-0x0c79).
 *
 * The room's own arrival test, run from the tick rather than from the click,
 * because that is where the original keeps it: the click has no submode to
 * arm, and whether the walk is a way out is decided when it lands.
 */
void AlienEngine::cemeteryArrival() {
	if (_room != kCemeteryRoom || _cemeteryStep)
		return;
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;

	if (_script.flag(kClickedObj) != kLasers)
		return;

	// 0x0c43: the latch is spent whichever way the test goes, so a second
	// arrival at the same spot says nothing a second time.
	_script.setFlag(kClickedObj, 0);

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	if (_script.flag(kPhraseNeeded) != 0) {
		// 0x0c4f: the beams are still up.
		queueOutcome(_tal, kOutcomeNotYet, anchorX, anchorY);
		debugC(1, kDebugRooms, "cemetery: the beams are still up");
		return;
	}

	// 0x0c5d: the cursor and the walker both go, and slot 3 has him.
	CursorMan.showMouse(false);
	playCharacterAnim(kCaveSlot, 1, kCaveFrames, kStatueRate, kStatueMode);
	_cemeteryStep = kStepEnterCave;
	debugC(1, kDebugRooms, "cemetery: through the beams and into the cave");
}

/// The pick the menu hands back: right on topic 2, or one more wrong answer.
void AlienEngine::cemeteryStatuePick() {
	const uint topic = _chatPickTopic;
	const uint choice = _chatPickChoice;
	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	if (topic == kCorrectTopic && choice == kCorrectChoice) {
		// Go to state 0x28 which waits for the picked dialogue to finish speaking
		// before starting the statue opening animation sequence
		_script.setFlag(0xa49f, 0x28);
		debugC(1, kDebugRooms, "cemetery: the phrase is right, waiting for speech");
		return;
	} else {
		// Wrong answers just play an outcome and show the mouse again in stepCemetery
		_cemeteryLookWait = true;
	}

	const byte attempts = (byte)_script.flag(kAttempts);
	if (attempts < kMaxAttempts)
		_script.setFlag(kAttempts, attempts + 1);

	if (topic == 0) {
		queueOutcome(_tal, (byte)(kWrongBase + choice), anchorX, anchorY);
	} else if (topic == 1 && attempts + 1 >= kMaxAttempts) {
		queueOutcome(_tal, kOutcomeGiveUp, anchorX, anchorY);
	}

	debugC(1, kDebugChat, "cemetery: wrong answer, topic %u choice %u, %u attempt(s)",
		   topic, choice, attempts + 1);
}

/// The look's own short wait, and the pick the conversation menu hands back.
void AlienEngine::stepCemetery() {
	if (_room != kCemeteryRoom)
		return;

	if (_cemeteryLookWait && speechDone()) {
		_cemeteryLookWait = false;
		if (_script.flag(kPhraseNeeded) == 1) {
			_script.setFlag(kTopic, 0);
			openChat(0);
		}
		CursorMan.showMouse(true);
	}

	if (_script.flag(0xa49f) == 0x28 && speechDone()) {
		_script.setFlag(0xa49f, 0x29);
		_anims.setLoopFlag(kRewardSlot, 0);
		_anims.play(kRewardSlot, 1, kRewardFrames, kStatueRate, kStatueMode);
	}

	if (_script.flag(0xa49f) == 0x29 && !_anims.isBusy(kRewardSlot)) {
		_script.setFlag(kPhraseNeeded, 0);

		Common::Path pathB("KIER32B.Pic");
		if (!Common::File::exists(pathB))
			pathB = Common::Path("KIER32B.PIC");
		_walk.loadMaskPage(0, pathB);

		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeSolved, anchorX, anchorY);
		_anims.setLoopFlag(kOpenSlot, 0);
		_anims.play(kOpenSlot, 1, kOpenFrames, kStatueRate, kStatueMode);
		_script.setFlag(0xa49f, 0x4b);
	}

	if (_script.flag(0xa49f) == 0x4b && !_anims.isBusy(kOpenSlot) && speechDone()) {
		_script.setFlag(0xa49f, 0);
		CursorMan.showMouse(true);
	}

	if (_chatPickNew) {
		_chatPickNew = false;
		cemeteryStatuePick();
	}

	cemeteryArrival();

	// 0x0b7e: and the submode goes out once slot 3 has run out, not before.
	if (_cemeteryStep == kStepEnterCave && !_anims.isBusy(kCaveSlot)) {
		_cemeteryStep = 0;
		showCharacter();
		debugC(1, kDebugRooms, "cemetery: submode %u, into room 40", kCaveSubmode);
		takeExit(kCaveSubmode);
	}
}

} // End of namespace Alien
