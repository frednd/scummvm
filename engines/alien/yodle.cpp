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

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"

#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Room 21, Yodle's tree hut: the picklock, and the teleporter he builds.
//
// Yodle is the busiest talker in the game and none of him is in the lifted
// tables, for the reason room 23's hippie is not either -- every one of his
// bodies is a `call cs:<near>` into one of the overlay's own helpers, and a
// body that is a call is not an opcode the lift has. What the tables do carry
// for room 21 is the signboard, the puddle, the pipe and the matches; what
// they carry of the conversations is nothing at all, so without the machine
// below the picklock the diving area's chest needs is never handed out and the
// teleporter is never built.
//
// One rectangle, registered two ways (`ovr_15_0ea7:0x08c8`, and already in
// hotspots.cpp): object 12 "old man" until he has introduced himself
// ([0xa751]), object 1 "Yodle" after. Both only while [0xa74b] says he is out
// on the stairs.
//
// Everything he says is `DLGREQ:sub_0c4d1`, the two-speaker runner sluggs.cpp
// already carries: a run of consecutive outcome codes, alternating between two
// anchors and two inks, one line per line that comes down. The only exception
// is the conversation menu, which is `chat.cpp`'s, and whose per-option reply
// byte this room is the canonical reader of (entry 4, 0x05b0).
//
// The five arms, out of entry 3 (`0x04e6` for the verbs, `0x03df` for the
// items), and the helper each of them goes through:
//
//   obj 12, talk, never met        sub_01d8(1)  YODTAL1, the first meeting
//   obj  1, talk, met, no plans    sub_01d8(2)  YODSHIT1-3, the small talk
//   obj  1, talk, plans delivered  sub_01d8(4)  YODTAL4, the fetch-quest
//   item 29 blueprints             sub_01d8(3)  YODTAL4 60-61, or the meeting
//   item 34 radio / 30 gameson     sub_00f9 / sub_014f, YODTAL4 10-11 / 22-23
//
// Left out of this pass, deliberately: the door-knock machine ([0xa49f] 0x64
// and 0x6e) that answers Open on the hut door once he has gone inside, which
// is also the row whose `queue_event()` argument the lift dropped.
static const int kYodleRoom = 21;

static const byte kYodle = 1;			///< once he has introduced himself
static const byte kOldMan = 12;			///< and before that
static const byte kVerbTalkTo = 6;

/// The state block, all of it in the saved game.
static const uint16 kPicklockGiven = 0xa744;	///< the one-shot on the item
static const uint16 kHasGameson = 0xa747;		///< Yodle is holding the gameson
static const uint16 kHasRadio = 0xa748;			///< and the radio
static const uint16 kGamesonPending = 0xa749;	///< one is changing hands now
static const uint16 kRadioPending = 0xa74a;
static const uint16 kStage = 0xa74b;			///< 1 on the stairs, 2 gone inside
static const uint16 kSmallTalk = 0xa74c;		///< which of YODSHIT1-3 is next
static const uint16 kMet = 0xa74d;				///< the first meeting is over
static const uint16 kMetAlt = 0xa74e;			///< and the blueprints know it
static const uint16 kPlansGiven = 0xa74f;		///< the blueprints have arrived
static const uint16 kKnown = 0xa751;			///< "old man" becomes "Yodle"
static const uint16 kTeleporter = 0x33c2;		///< he is building it

static const byte kBlueprints = 29;
static const byte kGameson = 30;
static const byte kRadio = 34;
static const byte kPicklock = 37;

/// The thirteen immediates every call site pushes: his anchor and ink, then Ben's.
static const int kYodleX = 0xa5, kYodleY = 0x4e;
static const byte kYodleInk[3] = { 0x2d, 0x2d, 0x3f };
static const int kBenX = 0xc1, kBenY = 0x4e;
static const byte kBenInk[3] = { 0x3f, 0x3f, 0x3f };

/// Where the menu's replies are spoken, off the live scroll (0x0612).
static const int kReplyX = 0x6e, kReplyY = 0x4b;

/// MIDAS:sub_196ac, room 21's own pose dispatcher, by the number it writes into
/// [0xa52d]. It was read as a sample player until the manual playthrough's
/// issue #33 asked why Yodle never moved: every case is a play on one of the
/// room's banks, and only three of them queue sounds as well (INPUT:sub_01e7a).
/// Poses 1 and 3-6 are gestures nothing in the game asks for.
static const byte kPoseIdle = 0;		///< sub_18810, frame 1 of YODLE1 once
static const byte kPoseTalk = 2;		///< 0x6ca8, the loop the tick relaunches
static const byte kPosePlans = 0x14;	///< 0x6ce2, the blueprints taken
static const byte kPoseGameson = 0x15;	///< 0x6cf8, the gameson taken
static const byte kPoseRadio = 0x16;	///< 0x6d04, the radio taken
static const byte kPosePicklock = 0x17;	///< 0x6d12, the picklock held out
static const byte kPoseInside = 0x1e;	///< YOD_GOIN, up the stairs and in

/// The banks those poses play on (the overlay's load order, 0x0bd4-0x0c89).
static const uint kWaterSlot = 0;		///< vesi.dl1
static const uint kYodleSlot = 3;		///< YODLE1.dl1
static const uint kInsideSlot = 4;		///< YOD_GOIN.dl1
static const uint kHandSlot = 6;		///< yod_thro.dl1

/// The lists in the data segment, one-based frame numbers.
static const byte kTalkFrames[] = { 6, 7, 8, 9, 10, 11, 9, 10, 8, 9, 10, 11, 12, 13, 14 };
static const byte kPlansFrames[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 8, 9, 8, 10, 11, 12, 13, 14, 15, 16, 17, 1
};
static const byte kGamesonFrames[] = { 1, 2, 3, 4, 18, 19, 20, 21, 22, 16, 17, 1 };
static const byte kRadioFrames[] = { 1, 2, 3, 4, 23, 24, 25, 26, 27, 28, 16, 17, 1 };
static const byte kPicklockFrames[] = { 1, 2, 3, 4, 4, 4, 4, 3, 2, 1 };

static const int kListMode = 7;			///< MIDAS:sub_18a2b
static const int kOnceMode = 4;			///< MIDAS:sub_18810
static const int kKeepMode = 1;			///< MIDAS:anim_play_mode1
static const byte kInsideFrames = 0x1c;

/// The puddle: [0xa5eb] runs 1..0x1e through bank 0, drawn straight to the
/// frame buffer by the room's tick (0x0e92) rather than by a slot.
static const byte kWaterFrames = 0x1e;

/// INPUT:sub_01e7a's three cases: sample, rate, volume, delay. The panning is
/// -0x28 on every one.
struct YodleSound {
	uint sample;
	uint32 rate;
	byte volume;
	uint16 delay;
};
static const int8 kYodlePanning = -0x28;
static const YodleSound kSoundsTake1[] = { { 1, 15000, 0x40, 0x23 } };
static const YodleSound kSoundsTake2[] = { { 2, 30000, 0x40, 0x12 } };
static const YodleSound kSoundsInside[] = {
	{ 4, 8000, 0x19, 0x1c }, { 2, 33000, 0x40, 0x3a }, { 2, 33000, 0x40, 0x3a },
	{ 4, 13000, 0x40, 0x2d }, { 3, 13000, 0x40, 0x50 }
};

/// LOGIC:sub_12418 and the byte it keeps: the room's own file, which the
/// conversations swap out and the overlay's bodies swap back in.
static const uint16 kRoomFileLoaded = 0xa742;
static const char *const kRoomScript = "ROOM21.TAL";

/// 0x10b2: the story so far is told under a black screen.
static const uint32 kStoryMillis = 2000;

/// The files the overlay loads out of its own literal pool (0x199, 0xab3).
enum {
	kFileTal1 = 0, kFileShit1, kFileShit2, kFileShit3,
	kFileTal4, kFileCon1, kFileCon2, kFileCount
};

static const char *const kScripts[kFileCount] = {
	"YODTAL1.TAL", "YODSHIT1.TAL", "YODSHIT2.TAL", "YODSHIT3.TAL",
	"YODTAL4.TAL", "YODCON1.TAL", "yodcon2.tal"
};

/// The first meeting: DLGREQ(1, 1, 7), and then the rest of the file by hand.
static const byte kMeetFirst = 1, kMeetCount = 7;
static const byte kMeetRest = 8;			///< where state 5 picks it up (0x10cb)
static const byte kMeetLast = 0x21;			///< and where it lets go (0x10ee)
static const byte kPicklockLine = 0x18;		///< the line it hands the item on

/// YODCON2: the twelve lines that end in "find me the blueprints".
static const byte kTheoryFirst = 1, kTheoryCount = 12;

/// YODTAL4's four banks, and the line that closes a handover.
static const byte kPlansFirst = 0x3c, kPlansCount = 2;
static const byte kRadioFirst = 0x0a, kGamesonFirst = 0x16, kHandCount = 2;
static const byte kThanksBoth = 0x46, kThanksNeither = 0x47;
static const byte kThanksGameson = 0x48, kThanksRadio = 0x49;
static const byte kSporty = 0x64;			///< 0x13ac, once he is inside

/// The small talk, by [0xa74c]: the file, its run, and the state it ends in.
static const byte kSmallFile[3] = { kFileShit1, kFileShit2, kFileShit3 };
static const byte kSmallCount[3] = { 5, 6, 2 };

/// [0xa49f], as the room writes it.
static const byte kStepMeet = 0x03;			///< the seven lines of the opening
static const byte kStepMeetGap = 0x04;		///< 0x10b2, the beat between halves
static const byte kStepMeetRest = 0x05;		///< the rest of YODTAL1, by hand
static const byte kStepMenu = 0x06;
static const byte kStepMenuWait = 0x07;
static const byte kStepTheory = 0x32;
static const byte kStepTheoryDone = 0x08;
static const byte kStepPlans = 0x09;
static const byte kStepPlansDone = 0x0a;
static const byte kStepHandOver = 0x0b;
static const byte kStepThanks = 0x0c;
static const byte kStepInside = 0x0d;
static const byte kStepInsideDone = 0x0e;
static const byte kStepSmallMenu = 0x82;	///< YODSHIT1 opens a tree of its own
static const byte kStepSmallWait = 0x83;
static const byte kStepSmallDone = 0x85;	///< YODSHIT2 and 3 do not

static const uint kInsideTicks = 0x87;		///< 0x139f

/// OBJ:sub_0461f's shape: the file every line is read out of from here on.
void AlienEngine::loadYodleScript(byte which) {
	if (which >= kFileCount)
		return;

	const Common::Path path(kScripts[which]);
	if (!Common::File::exists(path)) {
		debugC(1, kDebugRooms, "yodle: %s is missing", kScripts[which]);
		return;
	}

	_tal.load(path, &_pack);
	_script.setFlag(kRoomFileLoaded, 0);
	debugC(1, kDebugChat, "yodle: speaking out of %s", kScripts[which]);
}

/**
 * LOGIC:sub_12418: ROOM21.TAL, unless it is the file already in.
 *
 * The overlay names no roomNN.tal of its own -- its literal pool is the seven
 * Yodle files, and the room's file is spelled in LOGIC -- so the port's room
 * loader, which takes the overlay's last .TAL literal, opened the room on
 * yodcon2.tal. Every line the lifted rows speak here came out of the wrong
 * file, which is why the signboard said nothing (manual playthrough #31).
 * The room calls this as it opens (0x0c93) and in front of each body that
 * speaks out of the room's own file.
 */
void AlienEngine::loadYodleRoomScript() {
	if (_script.flag(kRoomFileLoaded) != 0)
		return;

	const Common::Path path(kRoomScript);
	if (!Common::File::exists(path))
		return;

	_tal.load(path, &_pack);
	_script.setFlag(kRoomFileLoaded, 1);
	debugC(1, kDebugChat, "yodle: speaking out of %s", kRoomScript);
}

/**
 * The entry 3 arms that call LOGIC:sub_12418 first, which the port has to
 * answer before the lifted row runs rather than after: the item half's
 * anything on him (0x03bb, 0x03ce), anything on the hut door once he is inside
 * (0x044e) and anything on the puddle (0x04af), and the plain half's look at
 * the signboard (0x0598).
 */
void AlienEngine::yodleRoomScriptFor(int obj, byte verb, byte item) {
	if (_room != kYodleRoom)
		return;

	static const byte kHutDoor = 6, kPuddle = 3, kSignboard = 2;
	static const byte kVerbLookAt = 5;

	bool reload = false;
	if (item != Inventory::kNoItem)
		reload = obj == kYodle || obj == kOldMan || obj == kPuddle ||
				 (obj == kHutDoor && _script.flag(kStage) == 2);
	else
		reload = obj == kSignboard && verb == kVerbLookAt;

	if (reload)
		loadYodleRoomScript();
}

/// INPUT:sub_01e7a, which three of the poses call.
void AlienEngine::yodleSounds(byte which) {
	const YodleSound *list = nullptr;
	uint count = 0;
	switch (which) {
	case 1:
		list = kSoundsTake1;
		count = ARRAYSIZE(kSoundsTake1);
		break;
	case 2:
		list = kSoundsTake2;
		count = ARRAYSIZE(kSoundsTake2);
		break;
	case 3:
		list = kSoundsInside;
		count = ARRAYSIZE(kSoundsInside);
		break;
	default:
		return;
	}

	for (uint i = 0; i < count; i++)
		_sound.queue(list[i].sample, list[i].rate, list[i].volume, kYodlePanning, list[i].delay);
}

/**
 * MIDAS:sub_196ac: [0xa52d], and the play that goes with it.
 *
 * Yodle himself is painted into the room's plate; every pose is drawn over
 * him and runs out back onto it. The tick relaunches slot 3 only while the
 * byte says 2 (0x0fe6, the room's row in the loop table), so any other pose
 * lets the talk loop finish the list it is on and stop.
 */
void AlienEngine::yodlePose(byte pose) {
	_anims.setLoopFlag(kYodleSlot, pose == kPoseTalk ? 1 : 0);

	switch (pose) {
	case kPoseIdle:
		_anims.play(kYodleSlot, 1, 1, 0, kOnceMode);
		break;
	case kPoseTalk:
		_anims.play(kYodleSlot, 0, ARRAYSIZE(kTalkFrames), 4, kListMode, kTalkFrames);
		break;
	case kPosePlans:
		_anims.play(kHandSlot, 0, ARRAYSIZE(kPlansFrames), 4, kListMode, kPlansFrames);
		break;
	case kPoseGameson:
		_anims.play(kHandSlot, 0, ARRAYSIZE(kGamesonFrames), 3, kListMode, kGamesonFrames);
		yodleSounds(1);
		yodleSounds(2);
		break;
	case kPoseRadio:
		_anims.play(kHandSlot, 0, ARRAYSIZE(kRadioFrames), 3, kListMode, kRadioFrames);
		yodleSounds(1);
		yodleSounds(2);
		break;
	case kPosePicklock:
		_anims.play(kHandSlot, 0, ARRAYSIZE(kPicklockFrames), 4, kListMode, kPicklockFrames);
		break;
	case kPoseInside:
		_anims.play(kInsideSlot, 1, kInsideFrames, 3, kKeepMode);
		yodleSounds(3);
		break;
	default:
		return;
	}

	debugC(3, kDebugRooms, "yodle: pose 0x%02x", pose);
}

/// [0xa5eb]: one frame of the puddle every second tick pair (0x0e44).
void AlienEngine::stepYodleWater() {
	if (_room != kYodleRoom)
		return;

	_yodleWaterDue = !_yodleWaterDue;
	if (_yodleWaterDue)
		return;

	if (++_yodleWater > kWaterFrames)
		_yodleWater = 1;
	_dirty = true;
}

/// 0x0e92: MIDAS:sub_180d4 on the frame buffer, over the slots and under Ben.
void AlienEngine::drawYodleWater(Graphics::Surface &dest) const {
	if (_room != kYodleRoom || _cutscene)
		return;
	_anims.drawBankFrame(kWaterSlot, _yodleWater, dest, _scrollX, _clipBottom);
}

/**
 * DLGREQ:sub_0c432 / sub_0c384, one pass: the line standing in [0xa4a2], by
 * whoever's turn it is.
 */
void AlienEngine::yodleSpeak() {
	if (!_yodleLeft) {
		_yodleSpeaking = false;
		return;
	}

	// 0x1103: the picklock is handed over as the line it is taken on comes up,
	// no click involved, and the latch keeps it to one.
	if (_yodleLine == kPicklockLine && _script.flag(kPicklockGiven) == 0) {
		_script.setFlag(kPicklockGiven, 1);
		_inventory.add(kPicklock);
		yodlePose(kPosePicklock);
		debugC(1, kDebugItems, "yodle: hands over item %u (%s)", kPicklock,
			   _inventory.name(kPicklock).c_str());
	}

	const bool yodle = _yodleSpeaker == 0;
	const byte *ink = yodle ? kYodleInk : kBenInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, _yodleLine, yodle ? kYodleX : kBenX,
				 yodle ? kYodleY : kBenY);

	// DLGREQ:sub_0c384's handler 0x15 arm: every line he takes while he is out
	// on the stairs starts the talking loop, and the room stops it again as
	// the line comes down.
	if (yodle && _script.flag(kStage) == 1) {
		yodlePose(kPoseTalk);
		_yodleTalking = true;
	}

	debugC(1, kDebugChat, "yodle: %s says outcome 0x%02x",
		   yodle ? "Yodle" : "Ben", _yodleLine);

	_yodleSpeaker = _yodleSpeaker ? 0 : 1;
	_yodleLine++;
	_yodleLeft--;
}

/// DLGREQ:sub_0c4d1: set the run up, and speak the first line of it.
void AlienEngine::yodleTalk(byte speaker, byte line, byte count) {
	_yodleSpeaker = speaker;
	_yodleLine = line;
	_yodleLeft = count;
	_yodleSpeaking = true;
	CursorMan.showMouse(false);
	yodleSpeak();
}

/// Whether the run that is going has said its last line and seen it come down.
bool AlienEngine::yodleRunDone() const {
	return !_yodleSpeaking && speechDone();
}

/**
 * ovr0ea7_sub_0000: which bank of YODTAL4 the check-in is read out of.
 *
 * [0x992b] is the first code and [0x992a] the count, and what picks them is
 * what Ben is carrying against what Yodle already has. The two `pending` bytes
 * it sets are what state 0x0c then takes out of the inventory.
 */
void AlienEngine::yodleCheckIn(byte &first, byte &count) {
	_script.setFlag(kGamesonPending, 0);
	_script.setFlag(kRadioPending, 0);

	const bool hasGameson = _script.flag(kHasGameson) == 1;
	const bool hasRadio = _script.flag(kHasRadio) == 1;
	const bool bringsRadio = _inventory.has(kRadio);
	const bool bringsGameson = _inventory.has(kGameson);

	first = 1;
	count = (hasGameson && !hasRadio) ? 2 : 4;
	if (!hasGameson && hasRadio) {
		first = 3;
		count = 2;
	}

	// 0x0042: the radio alone, then the gameson alone, then both.
	if (bringsRadio && !bringsGameson) {
		first = 0x0a;
		count = hasGameson ? 2 : count;
		_script.setFlag(kRadioPending, 1);
	} else if (!bringsRadio && bringsGameson) {
		first = 0x14;
		if (hasRadio) {
			first = 0x16;
			count = 2;
		}
		_script.setFlag(kGamesonPending, 1);
	} else if (bringsRadio && bringsGameson) {
		first = 0x1e;
		_script.setFlag(kGamesonPending, 1);
		_script.setFlag(kRadioPending, 1);
	}
}

/**
 * ovr0ea7_sub_01d8, the helper all four talk arms go through.
 */
void AlienEngine::yodleArm(byte which) {
	switch (which) {
	case 1:
		// 0x01dc: the first meeting, and the name that comes with it. The bar
		// slides away for it (OBJ:sub_02f9b) and is not back until he has
		// asked for the blueprints.
		CursorMan.showMouse(false);
		hideBar();
		loadYodleScript(kFileTal1);
		_script.setFlag(kKnown, 1);
		_yodleStep = kStepMeet;
		_yodlePos = 0;
		yodleTalk(1, kMeetFirst, kMeetCount);
		break;

	case 2: {
		// 0x0252: the small talk, one file further along each time.
		const byte which2 = MIN<byte>((byte)_script.flag(kSmallTalk), 2);
		hideBar();
		loadYodleScript(kSmallFile[which2]);
		_yodleStep = which2 == 0 ? kStepSmallMenu : kStepSmallDone;
		_yodlePos = 0;
		yodleTalk(1, 1, kSmallCount[which2]);
		if (which2 < 2)
			_script.setFlag(kSmallTalk, which2 + 1);
		break;
	}

	case 3:
		// 0x0326: the blueprints, which state 9 speaks and takes.
		loadYodleScript(kFileTal4);
		_yodleStep = kStepPlans;
		_yodlePos = 0;
		break;

	case 4: {
		// 0x034a: the check-in, out of whichever bank fits what he is owed.
		loadYodleScript(kFileTal4);
		byte first = 1, count = 4;
		yodleCheckIn(first, count);
		_yodleStep = kStepHandOver;
		_yodlePos = 0;
		yodleTalk(0, first, count);
		break;
	}

	default:
		break;
	}

	debugC(1, kDebugChat, "yodle: arm %u, step 0x%02x", which, _yodleStep);
}

/**
 * Entry 3's two halves, as far as Yodle is concerned.
 *
 * Returns true when the room has taken the click. The item rows the lift does
 * have for room 21 -- the matches, the puddle, the pipe, the signboard -- are
 * left to it; these are the five it has none of.
 */
bool AlienEngine::armYodle(int obj, byte verb, int item) {
	if (_room != kYodleRoom)
		return false;

	const bool held = item != Inventory::kNoItem;

	// 0x04e6: the plain verbs. His rectangle is only registered while he is out
	// on the stairs, but a click in flight as he goes inside would still reach
	// this.
	if (!held && verb == kVerbTalkTo && _script.flag(kStage) == 1) {
		if (obj == kOldMan && _script.flag(kMet) == 0) {
			yodleArm(1);
			return true;
		}

		if (obj == kYodle && _script.flag(kMet) == 1) {
			yodleArm(_script.flag(kPlansGiven) == 1 ? 4 : 2);
			return true;
		}
	}

	// 0x03df: and the three items, all of them on him.
	if (!held || obj != kYodle)
		return false;

	if (item == kBlueprints) {
		// He will not take them from a stranger: the first meeting happens
		// instead, and the blueprints stay in the bag.
		yodleArm(_script.flag(kMetAlt) == 1 ? 3 : 1);
		return true;
	}

	if (_script.flag(kPlansGiven) != 1)
		return false;

	if (item == kRadio) {
		// 0x00f9
		loadYodleScript(kFileTal4);
		_script.setFlag(kRadioPending, 1);
		_yodleStep = kStepHandOver;
		_yodlePos = 0;
		yodleTalk(0, kRadioFirst, kHandCount);
		return true;
	}

	if (item == kGameson) {
		// 0x014f
		loadYodleScript(kFileTal4);
		_script.setFlag(kGamesonPending, 1);
		_yodleStep = kStepHandOver;
		_yodlePos = 0;
		yodleTalk(0, kGamesonFirst, kHandCount);
		return true;
	}

	return false;
}

/**
 * The menu's pick, which this room reads more of than any other: the option's
 * own reply byte (entry 4, 0x05e9), spoken in his ink over the scroll.
 */
void AlienEngine::yodlePick() {
	if (!_chatPickReply)
		return;

	const TalFile::Entry &entry = _tal.entry(_chatPickReply);
	if (!entry.present)
		return;

	_yodleReplyEntry = entry;
	_yodleReplyTicks = speechTicksFor(_tal, _chatPickReply);
	_yodleReply = _chatPickReply;
	_yodleAnswer = true;
}

/// The room's [0xa49f] machine, and the pass the conversation makes beside it.
void AlienEngine::stepYodle() {
	if (_room != kYodleRoom)
		return;

	// [0xad1b], the frame a line comes down: he stops talking with it (0x14f3).
	// The port has no such pulse, so the flag the line was started with stands
	// in for it, as Sluggs' does.
	if (_yodleTalking && speechDone()) {
		_yodleTalking = false;
		yodlePose(kPoseIdle);
	}

	if (!_yodleStep)
		return;

	// A run keeps itself going: one line per line that came down.
	if (_yodleSpeaking && speechDone())
		yodleSpeak();

	// The menu moves on by itself, so the pick is read the frame it is made.
	if (_chatPickNew) {
		_chatPickNew = false;
		yodlePick();
	}

	// And the reply, once the option's own line is off the screen (0x03f6).
	if (_yodleAnswer && speechDone()) {
		_yodleAnswer = false;
		setTextColor(kYodleInk[0], kYodleInk[1], kYodleInk[2]);
		uploadTextColor();
		speakEntry(_yodleReplyEntry, _scrollX + kReplyX, kReplyY, _yodleReplyTicks);
		// 0x0e00: and he talks it, while he is out on the stairs.
		if (_script.flag(kStage) == 1) {
			yodlePose(kPoseTalk);
			_yodleTalking = true;
		}
		debugC(1, kDebugChat, "yodle: answers the menu with dialog %u", _yodleReply);
	}

	// [0xa49c], which LOGIC advances whether or not a state is reading it.
	_yodlePos++;

	switch (_yodleStep) {
	case kStepMeet:
		// 0x1089: the seven lines of the opening.
		if (!yodleRunDone())
			break;
		_yodleStep = kStepMeetGap;
		break;

	case kStepMeetGap:
		// 0x10b2: Ben tells him the story so far, and the game skips it -- the
		// screen fades out (UTIL:sub_021f9), stays black for two seconds
		// (TPRT:sub_252f8(0x7d0)) and fades back in (util_func_2aa). Then the
		// two lines it steps the counter back over.
		fadeOut();
		yodleStoryWait();
		fadeIn();
		_yodleStep = kStepMeetRest;
		yodleTalk(1, kMeetRest, kMeetLast - kMeetRest + 1);
		break;

	case kStepMeetRest:
		// 0x10da: the rest of YODTAL1, the picklock in the middle of it.
		if (!yodleRunDone())
			break;
		_script.setFlag(kMet, 1);
		_script.setFlag(kMetAlt, 1);
		_yodleStep = kStepMenu;
		break;

	case kStepMenu:
		// 0x1179: and it ends in the conversation menu.
		loadYodleScript(kFileCon1);
		openChat(0);
		_yodleStep = kStepMenuWait;
		break;

	case kStepMenuWait:
		// 0x11ac: [0xa60f], the menu closing behind the option taken.
		if (!_chat.isFinished() || _yodleAnswer || !speechDone())
			break;
		_yodleStep = kStepTheory;
		break;

	case kStepTheory:
		// 0x11bf: the teleport-booth theory, and the blueprints he asks for.
		CursorMan.showMouse(false);
		loadYodleScript(kFileCon2);
		_yodleStep = kStepTheoryDone;
		yodleTalk(1, kTheoryFirst, kTheoryCount);
		break;

	case kStepTheoryDone:
	case kStepSmallDone:
		// 0x1211 and 0x14b9: the cursor comes back with the last line, and the
		// bar with it (OBJ:sub_030f4 at 0x1229 and 0x14cc).
		if (!yodleRunDone())
			break;
		_yodleStep = 0;
		CursorMan.showMouse(true);
		showBar();
		break;

	case kStepPlans:
		// 0x122e: "You've found them!", and the drawing changes hands.
		yodleTalk(1, kPlansFirst, kPlansCount);
		_yodleStep = kStepPlansDone;
		yodlePose(kPosePlans);
		_inventory.remove(kBlueprints);
		debugC(1, kDebugItems, "yodle: takes item %u (%s)", kBlueprints,
			   _inventory.name(kBlueprints).c_str());
		break;

	case kStepPlansDone:
		// 0x1269
		if (!yodleRunDone())
			break;
		_yodleStep = 0;
		_script.setFlag(kPlansGiven, 1);
		CursorMan.showMouse(true);
		break;

	case kStepHandOver: {
		// 0x1286: whatever was brought goes across as the asking ends.
		if (!yodleRunDone())
			break;

		CursorMan.showMouse(false);
		_yodleStep = kStepThanks;

		if (_script.flag(kGamesonPending) == 1) {
			_inventory.remove(kGameson);
			yodlePose(kPoseGameson);
			_script.setFlag(kHasGameson, 1);
			_script.setFlag(kGamesonPending, 0);
			debugC(1, kDebugItems, "yodle: takes item %u (%s)", kGameson,
				   _inventory.name(kGameson).c_str());
		}

		if (_script.flag(kRadioPending) == 1) {
			_inventory.remove(kRadio);
			yodlePose(kPoseRadio);
			_script.setFlag(kHasRadio, 1);
			_script.setFlag(kRadioPending, 0);
			debugC(1, kDebugItems, "yodle: takes item %u (%s)", kRadio,
				   _inventory.name(kRadio).c_str());
		}

		// 0x12ec: and what he says next is what he is still waiting for.
		const bool gameson = _script.flag(kHasGameson) == 1;
		const bool radio = _script.flag(kHasRadio) == 1;
		byte line = kThanksNeither;
		if (gameson && radio)
			line = kThanksBoth;
		else if (radio)
			line = kThanksRadio;
		else if (gameson)
			line = kThanksGameson;

		yodleTalk(0, line, 1);
		break;
	}

	case kStepThanks:
		// 0x1352: with both of them in hand he goes inside to build it.
		if (!yodleRunDone())
			break;

		_yodleStep = 0;
		CursorMan.showMouse(true);

		if (_script.flag(kHasGameson) == 1 && _script.flag(kHasRadio) == 1) {
			_script.setFlag(kTeleporter, 1);
			yodlePose(kPoseInside);
			_script.setFlag(kStage, 2);
			CursorMan.showMouse(false);
			_yodleStep = kStepInside;
			_yodlePos = 0;
			rebuildHotspots();
			debugC(1, kDebugRooms, "yodle: goes inside to build the teleporter");
		}
		break;

	case kStepInside: {
		// 0x1398: the door shuts, and Ben watches him go.
		if (_yodlePos <= kInsideTicks)
			break;
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		setTextColor(kBenInk[0], kBenInk[1], kBenInk[2]);
		uploadTextColor();
		queueOutcome(_tal, kSporty, anchorX, anchorY);
		_yodleStep = kStepInsideDone;
		break;
	}

	case kStepInsideDone:
		// 0x13b8
		if (!speechDone())
			break;
		_yodleStep = 0;
		CursorMan.showMouse(true);
		break;

	case kStepSmallMenu:
		// 0x1487: YODSHIT1 has a little tree of its own behind it.
		if (!yodleRunDone())
			break;
		openChat(0);
		_yodleStep = kStepSmallWait;
		break;

	case kStepSmallWait:
		// 0x14a6. The menu's own close brings the bar back in the original
		// (OBJ:sub_0ab84's tail); here the bar was already down when the menu
		// opened, so the menu does not own it and it is put back here.
		if (!_chat.isFinished() || _yodleAnswer || !speechDone())
			break;
		_yodleStep = 0;
		CursorMan.showMouse(true);
		showBar();
		break;

	default:
		break;
	}
}

/// The room has opened: nothing of his is left running from the last visit.
void AlienEngine::startYodle() {
	if (_room != kYodleRoom)
		return;

	_yodleStep = 0;
	_yodlePos = 0;
	_yodleSpeaking = false;
	_yodleLeft = 0;
	_yodleAnswer = false;
	_yodleReply = 0;
	_yodleTalking = false;

	// 0x0d36 and 0x0ad9: the puddle starts on its first frame, and the room's
	// file is the one to speak out of (0x0c93) -- whatever a Yodle file the
	// loader took from the literal pool.
	_yodleWater = 1;
	_yodleWaterDue = false;
	_script.setFlag(kRoomFileLoaded, 0);
	loadYodleRoomScript();
}

/**
 * TPRT:sub_252f8(0x7d0): two seconds with the game loop stopped, the screen
 * black. Events are drained so the window keeps answering.
 */
void AlienEngine::yodleStoryWait() {
	const uint32 until = g_system->getMillis() + kStoryMillis;
	while (!shouldQuit() && g_system->getMillis() < until) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
		}
		g_system->updateScreen();
		g_system->delayMillis(10);
	}
}

} // End of namespace Alien
