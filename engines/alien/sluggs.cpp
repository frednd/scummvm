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

// Room 34, the road outside Sluggs' house, and the only hand that gives out the
// observatory keys.
//
// The room is the other end of the mailbox: the blast takes the park away
// ([0xa773]) and opens this scene off the town, where the house stands smoking
// and Sluggs is out on the road in front of it. Everything it does is the
// conversation, and the conversation is the layer the lift cannot reach -- the
// two rows tools/roomlogic.py found for room 34 are the item-use half of entry
// 3 (the keys and item 15 offered to him), and nothing in the tables ever runs
// `inv_add(31)`. The walkthroughs all take the keys here, so without the
// machine below the observatory door is shut for good: playtest report, "at the
// park / mailbox area it should be possible to talk to Sluggs to get the
// observatory keys".
//
// The talk arm is entry 3's plain-verb half (ovr_22_0e93:0x00e5): [0xa956] ==
// 0x4e25 with object 2 clicked ([0xa64c]) under verb 6 ([0xa824]). Its
// rectangle is registered only while [0xa76e] says Sluggs is still out there,
// which GAME.EXE ships set and which the room's own opening clears once the
// fourth conversation has been had (roominit.cpp, [0xa770]).
//
// What is said is picked by [0xa76f], a counter that stops at three:
//
//   0  the keys, which is the state machine below
//   1  seven lines, the eye and "you better leave"
//   2  six lines, coming back in
//   3  three lines, and [0xa770] -- after this one he is gone
//
// The last three are spoken straight from the arm, the way the original does
// it, and end in the state that waits for the last line to come down. The keys
// are a machine because there is an animation and an item in the middle of it
// ([0xa49f], ovr_22_0e93:0x0582 onwards):
//
//   3   eight lines, Ben first: "Hi Sluggs!" through to "...where did he put
//       them again.."
//   4   wait for that to finish, then take the cursor away
//   5   inv_add(31) -- the keys -- and fifty frames of him fetching them
//   6   0x62 ticks later, three more lines, Sluggs first: "There you go."
//   7   wait for those, and give the cursor back
//
// The conversation itself is DLGREQ:sub_0c4d1 and the pass DLGREQ:sub_0c432
// makes every frame: thirteen immediates that set a first outcome, how many
// lines follow it, whose turn it is ([0xa4a3]) and an anchor and colour for
// each of the two speakers. Each pass speaks the outcome standing in [0xa4a2]
// at whichever speaker's anchor, then moves both on, so a run of outcomes is
// alternating dialogue with no table behind it at all. sluggsSpeak() is that
// pass; the store's two-speaker banter is the same idea one line at a time
// (store.cpp).
static const int kSluggsRoom = 34;

static const byte kSluggs = 2;			///< the object his rectangle registers as
static const byte kVerbTalkTo = 6;

static const uint16 kSluggsHere = 0xa76e;	///< he is still out on the road
static const uint16 kStage = 0xa76f;		///< which conversation comes next, 0..3
static const uint16 kSluggsLeaving = 0xa770;	///< the last one has been had
static const byte kStageCount = 3;

static const byte kKeys = 31;			///< OBJ:sprite_add(0x1f), the keys

/// The four conversations, as (first outcome, how many lines) -- the arguments
/// the room hands DLGREQ:sub_0c4d1 at 0x0593, 0x0151, 0x0151 and 0x0186. Ben
/// opens every one of them.
static const byte kTalkLine[4] = { 1, 0x0d, 0x15, 0x1c };
static const byte kTalkCount[4] = { 8, 7, 6, 3 };

/// And the three lines he hands the keys over with, from state 6 (0x061e).
static const byte kKeysLine = 9;
static const byte kKeysCount = 3;

/// Where each of them speaks, and in what: [0xad24]..[0xad32], the same
/// thirteen immediates. Sluggs is the pale blue one; Ben answers in white.
static const int kSluggsX = 0x83, kSluggsY = 0x3a;
static const byte kSluggsInk[3] = { 0x2d, 0x2d, 0x3f };
static const int kBenX = 0x57, kBenY = 0x39;
static const byte kBenInk[3] = { 0x3f, 0x3f, 0x3f };

/// PAL_SMOK, the eleven frames of him standing in the road smoking, which the
/// room's opening starts and its tick keeps going (roominit.cpp, anims.cpp).
static const uint kIdleSlot = 0;

/// And PAL_ANIM, which is everything he does with his hands.
static const uint kSluggsSlot = 1;

/// MIDAS:sub_19a34's two cases: the pose byte it writes is [0xa52b], and the
/// room's tick relaunches the talking loop only while that byte says 1.
static const byte kPoseIdle = 0, kPoseTalk = 1;

/// Case 1: sixteen frames off the list at the data segment's 0x6d1c, mode 7.
static const byte kTalkFrames[] = {
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 0
};
static const int kTalkRate = 4;

/// Case 0: one frame of the idle bank, held (mode 2, 0x19a3b).
static const int kIdleFrame = 1, kIdleRate = 1;

/// The fifty frames of state 5, off the list at 0x1e14 -- mode 8, and the one
/// play in the room the talking loop must not pick up.
static const byte kKeyFrames[] = {
	1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 4, 5, 6, 7, 8, 9, 10, 11, 10, 9, 8,
	9, 10, 11, 10, 9, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, 26, 27, 1
};
static const int kKeyRate = 4;

static const int kPoseMode = 2, kLoopMode = 7, kKeyMode = 8;

/// [0xa49c] > this before the keys are spoken for (0x0617).
static const uint16 kKeysWait = 0x62;

static const byte kStepKeys = 3;		///< the eight lines that lead to them
static const byte kStepHandOver = 4;	///< they have been asked for
static const byte kStepFetch = 5;		///< and fetched
static const byte kStepGiven = 6;		///< "There you go."
static const byte kStepDone = 7;
static const byte kStepLine = 0x15;		///< every other conversation ends here

/**
 * MIDAS:sub_19a34, the room's own two poses.
 *
 * The port's loop table carries room 34's two relaunches, and the second of
 * them is guarded on the pose byte the way the original's is (anims.cpp): the
 * talking loop repeats while the pose says so, and the fifty frames of state 5
 * run once because it does not.
 */
void AlienEngine::sluggsPose(byte pose) {
	if (pose == kPoseTalk) {
		_anims.setLoopFlag(kSluggsSlot, 1);
		_anims.play(kSluggsSlot, 0, ARRAYSIZE(kTalkFrames), kTalkRate, kLoopMode,
					kTalkFrames);
	} else {
		_anims.setLoopFlag(kSluggsSlot, 0);
		_anims.play(kIdleSlot, kIdleFrame, 1, kIdleRate, kPoseMode);
	}

	debugC(3, kDebugRooms, "sluggs: pose %u", pose);
}

/**
 * DLGREQ:sub_0c432, one pass: the line standing in [0xa4a2], by whoever's turn
 * it is.
 *
 * The pass that finds nothing left to say is the one that ends the
 * conversation, which is what [0xad40] counting down past the last line does in
 * the original.
 */
void AlienEngine::sluggsSpeak() {
	if (!_sluggsLeft) {
		_sluggsSpeaking = false;
		return;
	}

	const bool sluggs = _sluggsSpeaker == 0;
	const byte *ink = sluggs ? kSluggsInk : kBenInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, _sluggsLine, sluggs ? kSluggsX : kBenX,
				 sluggs ? kSluggsY : kBenY);

	// DLGREQ:sub_0c384's own half of it: handler 0x22 starts the talking loop
	// on every line Sluggs takes, and the room stops it again as the line comes
	// down.
	if (sluggs) {
		sluggsPose(kPoseTalk);
		_sluggsTalking = true;
	}

	debugC(1, kDebugRooms, "sluggs: %s says outcome 0x%02x",
		   sluggs ? "Sluggs" : "Ben", _sluggsLine);

	_sluggsSpeaker = _sluggsSpeaker ? 0 : 1;
	_sluggsLine++;
	_sluggsLeft--;
}

/// DLGREQ:sub_0c4d1: set the run up, and speak the first line of it.
void AlienEngine::sluggsTalk(byte speaker, byte line, byte count) {
	_sluggsSpeaker = speaker;
	_sluggsLine = line;
	_sluggsLeft = count;
	_sluggsSpeaking = true;
	CursorMan.showMouse(false);
	sluggsSpeak();
}

/**
 * The talk verb on Sluggs: the arm at the top of entry 3's plain-verb half.
 *
 * Returns true when the room has taken the click, the way room 8's owl does:
 * the lifted rows for room 34 are the item-use pair and neither of them wants
 * a bare talk.
 */
bool AlienEngine::armSluggs(int obj, byte verb, bool item) {
	if (_room != kSluggsRoom || item || obj != kSluggs || verb != kVerbTalkTo)
		return false;

	// The rectangle is only registered while he is out there, but a click that
	// was already in flight when the last conversation ended would still reach
	// this.
	if (_script.flag(kSluggsHere) != 1)
		return false;

	const byte stage = MIN<byte>(_script.flag(kStage), kStageCount);

	// 0x0109: the keys are a machine, the other three are spoken from here.
	if (stage == 0) {
		_sluggsStep = kStepKeys;
		_sluggsPos = 0;
		CursorMan.showMouse(false);
	} else {
		if (stage == kStageCount)
			_script.setFlag(kSluggsLeaving, 1);
		sluggsTalk(1, kTalkLine[stage], kTalkCount[stage]);
		_sluggsStep = kStepLine;
		_sluggsPos = 0;
	}

	// 0x01ab: and the counter moves on, stopping at the last conversation.
	if (stage < kStageCount)
		_script.setFlag(kStage, stage + 1);

	debugC(1, kDebugRooms, "sluggs: conversation %u, step 0x%02x", stage, _sluggsStep);
	return true;
}

/// The room's [0xa49f] machine, and the pass the conversation makes beside it.
void AlienEngine::stepSluggs() {
	if (_room != kSluggsRoom || !_sluggsStep)
		return;

	// [0xad1b], the frame a line comes down: the talking loop stops with it
	// (0x06c3). The port has no such pulse, so the flag the line was started
	// with stands in for it, as the store's does.
	if (_sluggsTalking && speechDone()) {
		_sluggsTalking = false;
		sluggsPose(kPoseIdle);
	}

	// A conversation keeps itself going: one line per line that came down.
	if (_sluggsSpeaking && speechDone())
		sluggsSpeak();

	// [0xa49c], which LOGIC advances whether or not a machine is reading it.
	_sluggsPos++;

	switch (_sluggsStep) {
	case kStepKeys:
		// 0x0582: the eight lines that ask for them.
		sluggsTalk(1, kTalkLine[0], kTalkCount[0]);
		_sluggsStep = kStepHandOver;
		_sluggsPos = 0;
		break;

	case kStepHandOver:
		// 0x05b8: and when they are over, the cursor stays away for the rest.
		if (_sluggsSpeaking || !speechDone())
			break;
		CursorMan.showMouse(false);
		_sluggsStep = kStepFetch;
		_sluggsPos = 0;
		break;

	case kStepFetch:
		// 0x05da: the keys themselves, and the fifty frames he finds them in.
		_inventory.add(kKeys);
		// [0xa52b] = 0xc8 at 0x05ef: any value but the talking pose, so that
		// the tick leaves this play alone rather than repeating it.
		_anims.setLoopFlag(kSluggsSlot, 0);
		_anims.play(kSluggsSlot, 0, ARRAYSIZE(kKeyFrames), kKeyRate, kKeyMode,
					kKeyFrames);
		_sluggsStep = kStepGiven;
		_sluggsPos = 0;
		debugC(1, kDebugItems, "sluggs: hands over item %u (%s)", kKeys,
			   _inventory.name(kKeys).c_str());
		break;

	case kStepGiven:
		// 0x0610: "There you go.", and the two lines that end the scene.
		if (_sluggsPos <= kKeysWait)
			break;
		sluggsTalk(0, kKeysLine, kKeysCount);
		_sluggsStep = kStepDone;
		_sluggsPos = 0;
		break;

	case kStepDone:
	case kStepLine:
		// 0x0648 and 0x066a, which are the same state twice over: the cursor
		// comes back once the last line has come down.
		if (_sluggsSpeaking || !speechDone())
			break;
		_sluggsStep = 0;
		setTextColor(0x3f, 0x3f, 0x3f);
		uploadTextColor();
		CursorMan.showMouse(true);
		debugC(1, kDebugRooms, "sluggs: the conversation is over");
		break;

	default:
		break;
	}
}

} // End of namespace Alien
