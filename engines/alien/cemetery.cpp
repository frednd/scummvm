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
// The other half is the exit itself. walkgeom.cpp's room 32 rows carry the
// zone `0,0..43,94 -> 43,94 facing 1` (entry 0's `walk_zone(0,0,0x2b,0x5e,
// 0x2b,0x5e,1)`) with no `kWalkSubmode` row after it, because the original
// does not arm it that way: entry 2's own walk-blocked handler
// (0x0c35-0x0c79) tests [0xa644], the latch that zone sets, and only writes
// `game_submode := 3` (0x0b8e) once [0xa72e] is already 0. Answer the statue
// right and the cave is open; walk there first and the statue turns you back
// with outcome 14 instead.
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

/// entry 0's walk_zone target (0x2b,0x5e) facing 1, and the submode it arms
/// once the statue has opened.
static const int kCaveX = 0x2b, kCaveY = 0x5e;
static const byte kCaveFacing = 1;
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
		openChat((uint)_script.flag(kTopic));
		debugC(1, kDebugChat, "cemetery: the statue's topic is %u", (uint)_script.flag(kTopic));
	} else if (verb == kVerbLookAt) {
		// 0x00b8: the look queues its own line through the generic outcome
		// path: taking the cursor away and waiting for it is all this room
		// adds (0x0046, the tail of which reopens topic 0).
		CursorMan.showMouse(false);
		_cemeteryLookWait = true;
	}
}

/**
 * The exit the statue guards: room 32's own hook alongside armCliff, since
 * the zone that reaches it has no `kWalkSubmode` row for the click dispatch to
 * arm on its own (walkgeom.cpp, this file's header).
 */
void AlienEngine::armCemeteryExit(const WalkTarget &target) {
	if (_room != kCemeteryRoom || target.x != kCaveX || target.y != kCaveY
			|| target.facing != kCaveFacing)
		return;

	if (_script.flag(kPhraseNeeded) != 0) {
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeNotYet, anchorX, anchorY);
		debugC(1, kDebugRooms, "cemetery: the statue still wants the phrase");
		return;
	}

	_armed = kCaveSubmode;
	_armedX = kCaveX;
	_armedY = kCaveY;
	_armedFacing = kCaveFacing;
	debugC(1, kDebugRooms, "cemetery: the cave is open");
}

/// The pick the menu hands back: right on topic 2, or one more wrong answer.
void AlienEngine::cemeteryStatuePick() {
	const uint topic = _chatPickTopic;
	const uint choice = _chatPickChoice;
	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	if (topic == kCorrectTopic && choice == kCorrectChoice) {
		_script.setFlag(kPhraseNeeded, 0);
		_anims.setLoopFlag(kRewardSlot, 0);
		_anims.play(kRewardSlot, 1, kRewardFrames, kStatueRate, kStatueMode);
		_anims.setLoopFlag(kOpenSlot, 0);
		_anims.play(kOpenSlot, 1, kOpenFrames, kStatueRate, kStatueMode);
		queueOutcome(_tal, kOutcomeSolved, anchorX, anchorY);
		debugC(1, kDebugRooms, "cemetery: the phrase is right, the statue opens");
		return;
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
		_script.setFlag(kTopic, 0);
		openChat(0);
		CursorMan.showMouse(true);
	}

	if (_chatPickNew) {
		_chatPickNew = false;
		cemeteryStatuePick();
	}
}

} // End of namespace Alien
