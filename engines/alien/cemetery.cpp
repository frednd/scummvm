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
static const uint kLeaveChoice = 4;			///< every topic's last option says goodbye
static const byte kWrongBase = 0x13;		///< topic 0's replies, [0xa60c] + this

static const uint16 kLooked = 0xa72a;		///< the look has been had; the verb is TALK now
static const uint16 kGravestoneRead = 0xa72d;
static const uint16 kRunestoneRead = 0xa72c;	///< the rune stone in Yodle's hut

static const byte kOutcomeSolved = 0x11;	///< 0x0b62
static const byte kOutcomeGiveUp = 0x21;	///< 0x0bc3, the fourth wrong try on topic 1
static const byte kOutcomeTryAgain = 0x10;	///< 0x0bed, the ones before it
static const byte kOutcomeClose = 0x0b;		///< 0x0c06, topic 2's wrong answers
static const byte kOutcomeNotYet = 0x0e;	///< 0x0c4f, walking for the cave too soon

/// [0xa49f], as entry 3 and the tick (0x0962-0x0c0d) write it.
enum {
	kStateIdle = 0,
	kStateBack = 3,			///< back from the crystal: a moment, then...
	kStateBackLine = 4,		///< ...the empty line and "...Uh oh.."
	kStateBackDown = 5,		///< ...and the bar back once it is down
	kStateAsked = 0x14,		///< the menu is up, waiting for a pick
	kStateClose = 0x16,		///< topic 2, wrong: wait for his line
	kStateTalk = 0x19,		///< topic 0: wait for his line
	kStateTalkReply = 0x1a,	///< ...then 0x1e counts, then the statue's answer
	kStateSpell = 0x1e,		///< topic 1: wait for his line
	kStateCloseZap = 0x1f,	///< topic 2, after 10 counts: the zap
	kStateRight = 0x28,		///< the phrase: wait for his line
	kStateMouth = 0x29,		///< slot 1, the mouth
	kStateOpen = 0x2a,		///< slot 2, the way opens
	kStateLook = 0x46,		///< the look's line, then topic 0
	kStateSpellZap = 0x4b,	///< topic 1 after the zap
	kStateSpellLine = 0x50,
	kStateCloseLine = 0x55
};

/// The waits, in [0xa49c].
static const uint16 kTalkReplyAt = 0x1e;
static const uint16 kCloseZapAt = 0x0a;
static const uint16 kAfterZapAt = 0x0f;

/// The zap for a wrong spell: slot 4 (CEM_DOBU) three frames at once, the
/// sample, and 0x0f counts later slot 0 (CEM_LADO) runs its eleven frames
/// ([bp-1]/[bp-4] at 0x0c0d).
static const uint kZapSlot = 4;
static const int kZapFrames = 3;
static const uint kBeamSlot = 0;
static const int kBeamFrames = 0x0b;
static const uint kZapSample = 1;
static const uint32 kZapRate = 0x2710;
static const uint kMouthSample = 2;
static const uint32 kMouthRate = 0x2ee0;
static const byte kStatueVolume = 0x40;
static const int8 kStatuePan = 0x28;

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

void AlienEngine::setCemeteryState(byte state) {
	_script.setFlag(0xa49f, state);
	_cemeteryPos = 0;
}

/// The way back from the crystal entry (room 45).
static const int kCrystalRoom = 45;
static const uint16 kBackWait = 5;
static const byte kOutcomeBackEmpty = 0x0a;	///< an empty entry
static const byte kOutcomeBack = 3;			///< "...Uh oh.."

/**
 * Entry 2's arrival from room 45 (0x046b, 0x06b8): [0xa637] = 1 opens the room
 * with the bar already off the screen, the cursor goes, and [0xa49f] 3 runs the
 * rest.
 */
void AlienEngine::startCemetery() {
	if (_room != kCemeteryRoom || _mode != kCrystalRoom)
		return;

	_barHidden = true;
	_inventory.setBarAway(true);
	CursorMan.showMouse(false);
	setCemeteryState(kStateBack);
	debugC(1, kDebugRooms, "cemetery: back from the crystal");
}

/**
 * Entry 3's statue arms (0x0063-0x00c7). The rectangle carries LOOK until
 * [0xa72a] and TALK after it (entry 1, 0x02ce), so the first thing a player
 * ever does to it is the look.
 */
void AlienEngine::armCemeteryStatue(int obj, byte verb) {
	if (_room != kCemeteryRoom || obj != kStatue)
		return;

	if (verb == kVerbTalkTo && _script.flag(kPhraseNeeded) == 1) {
		// 0x0078: the topic. The rune stone in Yodle's hut alone is enough for
		// the phrase; the gravestone alone gets the spells. The port used to
		// ask for both before offering the phrase, so a player who had read
		// only the rune stone never saw it (manual playthrough #55).
		byte topic = 0;
		if (_script.flag(kRunestoneRead) == 0 && _script.flag(kGravestoneRead) == 1)
			topic = 1;
		if (_script.flag(kRunestoneRead) == 1)
			topic = 2;
		_script.setFlag(kTopic, topic);
		setCemeteryState(kStateAsked);
		openChat(topic);
		debugC(1, kDebugChat, "cemetery: the statue's topic is %u", topic);
	} else if (verb == kVerbLookAt) {
		// 0x00b8: the look's own line goes through the generic outcome path;
		// this room takes the cursor away until it is down and then opens
		// topic 0 (state 0x46).
		_script.setFlag(kLooked, 1);
		setCemeteryState(kStateLook);
		CursorMan.showMouse(false);
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

/**
 * State 0x15: the pick the menu hands back. Every option in ROOM32.TAL's tree
 * ends the conversation (next 255), so the menu is gone by now and nothing
 * here opens it again -- the port used to reopen topic 0 after every pick,
 * which is why "I have to go" could not end it (manual playthrough #55).
 */
void AlienEngine::cemeteryStatuePick() {
	if (_script.flag(0xa49f) != kStateAsked)
		return;

	const uint topic = _script.flag(kTopic);
	const uint choice = _chatPickChoice;

	if (topic == 0) {
		// 0x09d0: small talk, and goodbye is just goodbye.
		if (choice < kLeaveChoice)
			setCemeteryState(kStateTalk);
		else
			setCemeteryState(kStateIdle);
	} else if (topic == 1) {
		// 0x09e3: a spell. The count goes up on the goodbye too, and since
		// the state is left at 0x15 the original counts it again on every
		// pass until the cap -- a farewell there uses up every try.
		const byte attempts = (byte)_script.flag(kAttempts);
		if (choice < kLeaveChoice) {
			_script.setFlag(kAttempts, MIN<byte>(attempts + 1, kMaxAttempts));
			setCemeteryState(kStateSpell);
		} else {
			_script.setFlag(kAttempts, kMaxAttempts);
			setCemeteryState(kStateIdle);
		}
	} else if (choice == kCorrectChoice) {
		// 0x0a14: the phrase.
		setCemeteryState(kStateRight);
		CursorMan.showMouse(false);
	} else {
		setCemeteryState(kStateClose);
	}

	debugC(1, kDebugChat, "cemetery: topic %u choice %u -> state 0x%02x, %u attempt(s)",
		   topic, choice, _script.flag(0xa49f), _script.flag(kAttempts));
}

/// The zap a wrong spell or a near miss gets (0x0a93, 0x0ac9).
void AlienEngine::cemeteryZap() {
	_anims.play(kZapSlot, 1, kZapFrames, 0, kStatueMode);
	_sound.queue(kZapSample, kZapRate, kStatueVolume, kStatuePan, 0);
	_cemeteryZapPos = 0;
}

/// The statue's [0xa49f] machine, 0x0962-0x0c2c, and the cave's arrival.
void AlienEngine::stepCemetery() {
	if (_room != kCemeteryRoom)
		return;

	// [0xa49c] and the zap's own count both move on the animation gate.
	if ((_tick & 3) == 0) {
		_cemeteryPos++;
		if (_cemeteryZapPos >= 0)
			_cemeteryZapPos++;
	}

	if (_chatPickNew) {
		_chatPickNew = false;
		cemeteryStatuePick();
	}

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	switch (_script.flag(0xa49f)) {
	case kStateBack:
		// 0x0969
		if (_cemeteryPos <= kBackWait)
			break;
		queueOutcome(_tal, kOutcomeBackEmpty, anchorX, anchorY);
		setCemeteryState(kStateBackLine);
		break;

	case kStateBackLine:
		// 0x0988: queued straight after it, so it is the one that is said.
		queueOutcome(_tal, kOutcomeBack, anchorX, anchorY);
		setCemeteryState(kStateBackDown);
		break;

	case kStateBackDown:
		// 0x099b: OBJ:sub_030f4 and sub_03c77 put the bar back.
		if (!speechDone())
			break;
		setCemeteryState(kStateIdle);
		showBar();
		CursorMan.showMouse(true);
		break;

	case kStateLook:
		if (!speechDone())
			break;
		_script.setFlag(kTopic, 0);
		setCemeteryState(kStateAsked);
		openChat(0);
		CursorMan.showMouse(true);
		break;

	case kStateTalk:
		if (speechDone())
			setCemeteryState(kStateTalkReply);
		break;

	case kStateTalkReply:
		if (_cemeteryPos <= kTalkReplyAt)
			break;
		queueOutcome(_tal, (byte)(kWrongBase + _chatPickChoice), anchorX, anchorY);
		setCemeteryState(kStateIdle);
		break;

	case kStateSpell:
		if (!speechDone())
			break;
		setCemeteryState(kStateSpellZap);
		cemeteryZap();
		break;

	case kStateSpellZap:
		if (_script.flag(kAttempts) == kMaxAttempts)
			queueOutcome(_tal, kOutcomeGiveUp, anchorX, anchorY);
		setCemeteryState(kStateSpellLine);
		break;

	case kStateSpellLine:
		if (_cemeteryPos <= kAfterZapAt)
			break;
		setCemeteryState(kStateIdle);
		if (_script.flag(kAttempts) != kMaxAttempts)
			queueOutcome(_tal, kOutcomeTryAgain, anchorX, anchorY);
		break;

	case kStateClose:
		if (speechDone())
			setCemeteryState(kStateCloseZap);
		break;

	case kStateCloseZap:
		if (_cemeteryPos <= kCloseZapAt)
			break;
		cemeteryZap();
		setCemeteryState(kStateCloseLine);
		break;

	case kStateCloseLine:
		if (_cemeteryPos <= kAfterZapAt)
			break;
		setCemeteryState(kStateIdle);
		queueOutcome(_tal, kOutcomeClose, anchorX, anchorY);
		break;

	case kStateRight:
		if (speechDone())
			setCemeteryState(kStateMouth);
		break;

	case kStateMouth:
		if (_anims.isBusy(kRewardSlot))
			break;
		_sound.queue(kMouthSample, kMouthRate, kStatueVolume, kStatuePan, 0);
		_anims.setLoopFlag(kRewardSlot, 0);
		_anims.play(kRewardSlot, 1, kRewardFrames, kStatueRate, kStatueMode);
		setCemeteryState(kStateOpen);
		break;

	case kStateOpen: {
		if (_anims.isBusy(kRewardSlot))
			break;
		_script.setFlag(kPhraseNeeded, 0);

		queueOutcome(_tal, kOutcomeSolved, anchorX, anchorY);
		_anims.setLoopFlag(kOpenSlot, 0);
		_anims.play(kOpenSlot, 1, kOpenFrames, kStatueRate, kStatueMode);
		setCemeteryState(kStateIdle);
		// The cursor went at 0x0a1e and the machine never gives it back; the
		// dialog unit does when the line comes down, which the port's
		// speech does not, so it comes back with the line here.
		_cemeteryCursorOwed = true;
		break;
	}

	default:
		break;
	}

	// Slot 2 is one of the room's always-relaunched loops, so it never reads
	// as finished: the line is the whole wait.
	if (_cemeteryCursorOwed && speechDone()) {
		_cemeteryCursorOwed = false;
		CursorMan.showMouse(true);
	}

	// 0x0c0d: fifteen counts after a zap the beams run their eleven frames.
	if (_cemeteryZapPos > (int)kAfterZapAt) {
		_cemeteryZapPos = -1;
		_anims.play(kBeamSlot, 1, kBeamFrames, kStatueRate, kStatueMode);
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
