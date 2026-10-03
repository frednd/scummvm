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

// Room 58, the jail.
//
// [0xa7b1] picks which of two views the room opens on: 1 is the cell Ben is
// thrown into after the scanner (scanner.cpp), 2 the corridor outside it,
// where the new game's state leaves it and where Ben comes in from room 51.
// Two prisoners sit behind the bars in both: Yodle in slot 0 (JAIL_YOD.DL1)
// and the uncle in slot 1 (JAIL_UNC.DL1), each with a pose byte MIDAS keeps
// for it -- sub_19d1e writes [0xa52a] and plays Yodle's lists, sub_19caa
// writes [0xa52b] and plays the uncle's. The prologue puts both on pose 1.
//
// The corridor is where the escape pod is reached from. [0xa7b0] is the
// force field over the cells: the new game raises it (seg_main:0x0b1f) and
// only the maintenance man's network terminal in rooms 53/57, the "FORCE
// FIELD HOLDING" panel at 15f3:sub_17373, takes it down (0x191a). With it down,
// the prologue (0x08c2) holds the cursor, turns the uncle to face the bars
// (pose 4) and sets [0xa49f] = 0x83; after 0xf the room calls
// 10c9:sub_11d3b, which loads ROOM58_2.TAL and picks one of six DLGREQ runs,
// Ben first, from how far the talk has got ([0xa7c2], [0xa7c3]) and whether
// he holds item 43, the Boss' escape pod card -- raising [0xa7c4] if he does:
//
//   first time        no card: 0x50, 13 lines    card: 0x64, 11 lines
//   after that        no card: 0x78, 5           card: 0x82, 5
//   talked to before  no card: 1, 11             card: 0x14, 9
//   (with the field still up, [0xa7b0] = 1)
//
// State 0x84 runs the conversation; when it is over, [0xa7c4] decides the
// way out: 0x8c raises [0xa7d2] -- the pod is ready, ending.cpp -- and
// leaves by submode 5 for room 59; 0x87 walks him back to 0x185,0x91 and,
// 0x19 later, submode 1 takes him out to room 51.
//
// Talking to either prisoner in the corridor (entry 3, objects 5 and 6 under
// verb 6) is state 0x6e: once the uncle's idle list is on frame 0xc or past
// it, the same sub_11d3b runs with state 0x82 in place of whatever it chose,
// so the talk never leads out. With the field still up it speaks out of the
// room's own file, 0x64 for 6 lines, or 0x3c for 4 once that has been heard.
//
// DLGREQ:sub_0c384 answers handler 0x3a on each of the uncle's lines with
// pose 2, his talking list; the room's tick turns it to pose 3 as the line
// comes down, and 0x5a after a talk ends ([0xa7bd] = 3) back to pose 1.
//
// The guard, his conversation in the corridor, the force field and the red
// card he leaves in the slot are jailguard.cpp's.
//
// What is still simplified: the cell view's own machine (job 13c), and the way
// back to room 56 below, which is a plain click in the cell with nothing held
// and no hotspot under it, standing in for whatever the original actually
// gates it on.
static const int kJailRoom = 58;

static const byte kEscapeClicks = 3;
static const byte kShipExitSubmode = 100;	///< transitions.cpp: room 58, submode 100 -> room 56

static const uint16 kView = 0xa7b1;			///< 1 = the cell, 2 = the corridor
static const uint16 kForceField = 0xa7b0;		///< 1 = the field over the cells is up
static const uint16 kTalked = 0xa7c2;
static const uint16 kTalkedAgain = 0xa7c3;
static const uint16 kHasCard = 0xa7c4;
static const uint16 kPodReady = 0xa7d2;		///< ending.cpp's guard on room 59's machine
static const byte kCorridor = 2;

static const byte kCard = 43;				///< OBJ:sprite_find_slot(0x2b)
static const char *const kCorridorTal = "ROOM58_2.TAL";

static const byte kVerbTalkTo = 6;
static const byte kYodleObj = 5, kUncleObj = 6;

/// The steps of [0xa49f] this file runs, and the waits on [0xa49c].
static const byte kStepIdle = 0;
static const byte kStepTalk = 0x6e;
static const byte kStepTalking = 0x82;
static const byte kStepArrive = 0x83;
static const byte kStepEscape = 0x84;
static const byte kStepLeave = 0x87;
static const byte kStepLeaving = 0x88;
static const byte kStepPod = 0x8c;
static const uint16 kArriveWait = 0xf;
static const uint16 kLeaveWait = 0x19;
static const uint16 kSettleWait = 0x5a;

/// [0xa7bd] = 3, the clock the tick settles the uncle back to idle on.
static const byte kClockSettle = 3;

/// 0x0eb5: [0xa54b], the frame slot 1 last drew, at 0xc or past it.
static const int kTalkFrame = 0xc;

/// OBJ:sub_07890(0x185, 0x91, 2), 0x0f39.
static const int kLeaveX = 0x185, kLeaveY = 0x91, kLeaveFacing = 2;
static const byte kLobbySubmode = 1;		///< transitions.cpp: room 58, submode 1 -> room 51
static const byte kPodSubmode = 5;			///< transitions.cpp: room 58, submode 5 -> room 59

/// 10c9:0x11d2: DLGREQ:sub_0c4d1(1, line, count, 0xf7, 0x3e, 0x35 x3, Ben's
/// box, 0x3f x3) -- Ben first, the uncle in grey at 0xf7,0x3e.
static const int kUncleX = 0xf7, kUncleY = 0x3e;
static const byte kUncleInk[3] = { 0x35, 0x35, 0x35 };
static const byte kBenInk[3] = { 0x3f, 0x3f, 0x3f };

static const uint kYodleSlot = 0, kUncleSlot = 1;

/// MIDAS:sub_19caa's four poses for the uncle, [0xa52b], and sub_19d1e's two
/// for Yodle, [0xa52a].
static const byte kPoseIdle = 1, kPoseTalk = 2, kPoseSettle = 3, kPoseFace = 4;

/// The lists both play in mode 8 at rate 4, from the data segment: the
/// uncle's at 0x6f0a and 0x6f4c, Yodle's at 0x6f5c and 0x6f98.
static const byte kUncleIdle[] = {
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 2, 3, 4, 5, 6, 6, 6, 6, 6, 6, 7, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 10, 11, 12, 13, 13, 13, 13, 13, 13, 1, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1, 0
};
static const byte kUncleTalk[] = {
	14, 15, 16, 17, 18, 19, 20, 21, 18, 19, 17, 15, 14, 21, 14
};
static const byte kYodleIdle[] = {
	1, 2, 3, 4, 5, 6, 7, 6, 7, 6, 7, 6, 7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 8, 8,
	1, 1, 1, 1, 1, 1, 8, 1, 1, 1, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11,
	11, 11, 10, 10, 10, 1, 1, 8, 1, 1, 1, 1, 1, 1, 9
};
static const byte kYodleTalk[] = {
	9, 1, 10, 11, 9, 12, 14, 13, 15, 12, 15, 11, 9, 10
};
static const int kListRate = 4, kListMode = 8;

/// Cases 3 and 4 are mode 2 holds: frames 0x11..0x12 at 0x5a, and frame 0xe.
static const int kSettleFirst = 0x11, kSettleCount = 2, kSettleRate = 0x5a;
static const int kFaceFrame = 0xe;
static const int kHoldMode = 2;

/// The prologue's arms: both prisoners' idle poses (0x0778, 0x079a), and the
/// way in with the field down (0x08c2).
void AlienEngine::startJail() {
	if (_room != kJailRoom)
		return;

	_jailClicks = 0;
	_jailStep = kStepIdle;
	_jailPos = 0;
	_jailLeft = 0;
	_jailSpeaking = false;
	_jailClock = 0;

	jailYodlePose(kPoseIdle);
	startJailGuard();
	jailUnclePose(kPoseIdle);

	// The char_place beside it is a lifted row (roominit.cpp).
	if (_script.flag(kView) == kCorridor && _script.flag(kForceField) == 0) {
		CursorMan.showMouse(false);
		_jailStep = kStepArrive;
		jailUnclePose(kPoseFace);
		debugC(1, kDebugRooms, "jail: in with the force field down");
	}
}

/// MIDAS:sub_19caa: the uncle's pose byte and the list or hold it plays.
void AlienEngine::jailUnclePose(byte pose) {
	_jailUncle = pose;

	// A port addition (anim.h's setHold()), as sluggs.cpp has for the same
	// shape: the tick pair between two poses never shows him blank.
	_anims.setHold(kUncleSlot, true);

	switch (pose) {
	case kPoseIdle:
		_anims.play(kUncleSlot, 0, ARRAYSIZE(kUncleIdle), kListRate, kListMode, kUncleIdle);
		_anims.setLoopFlag(kUncleSlot, 1);
		break;
	case kPoseTalk:
		_anims.play(kUncleSlot, 0, ARRAYSIZE(kUncleTalk), kListRate, kListMode, kUncleTalk);
		_anims.setLoopFlag(kUncleSlot, 1);
		break;
	case kPoseSettle:
		_anims.play(kUncleSlot, kSettleFirst, kSettleCount, kSettleRate, kHoldMode);
		_anims.setLoopFlag(kUncleSlot, 0);
		break;
	case kPoseFace:
		_anims.play(kUncleSlot, kFaceFrame, 1, 0, kHoldMode);
		_anims.setLoopFlag(kUncleSlot, 0);
		break;
	default:
		break;
	}
}

/// MIDAS:sub_19d1e: Yodle's, whose slot the room relaunches unguarded.
void AlienEngine::jailYodlePose(byte pose) {
	_jailYodle = pose;
	_anims.setHold(kYodleSlot, true);

	if (pose == kPoseIdle)
		_anims.play(kYodleSlot, 0, ARRAYSIZE(kYodleIdle), kListRate, kListMode, kYodleIdle);
	else if (pose == kPoseTalk)
		_anims.play(kYodleSlot, 0, ARRAYSIZE(kYodleTalk), kListRate, kListMode, kYodleTalk);
}

/// 10c9:sub_11d3b: which conversation, out of which file, and the step it
/// leaves [0xa49f] on.
void AlienEngine::jailConversation() {
	byte line = 0, count = 0;
	const bool card = _inventory.has(kCard);

	if (_script.flag(kForceField) == 1) {
		_jailStep = kStepTalk;
		line = 0x64;
		count = 6;
		if (_script.flag(kTalked) == 1) {
			line = 0x3c;
			count = 4;
		}
		_script.setFlag(kTalked, 1);
	} else {
		_jailStep = kStepEscape;
		_tal.load(Common::Path(kCorridorTal), &_pack);

		// [0xa602], the "one branch has spoken" latch the three tests share.
		bool chosen = false;
		if (_script.flag(kTalked) == 0) {
			chosen = true;
			if (!card) {
				line = 0x50;
				count = 0xd;
				_script.setFlag(kTalked, 1);
				_script.setFlag(kTalkedAgain, 1);
			} else {
				line = 0x64;
				count = 0xb;
				_script.setFlag(kHasCard, 1);
			}
		}
		if (_script.flag(kTalkedAgain) == 1 && !chosen) {
			chosen = true;
			if (!card) {
				line = 0x78;
				count = 5;
			} else {
				line = 0x82;
				count = 5;
				_script.setFlag(kHasCard, 1);
			}
		}
		if (_script.flag(kTalked) == 1 && _script.flag(kTalkedAgain) == 0 && !chosen) {
			if (!card) {
				line = 1;
				count = 0xb;
				_script.setFlag(kTalkedAgain, 1);
			} else {
				line = 0x14;
				count = 9;
				_script.setFlag(kHasCard, 1);
			}
		}
	}

	// DLGREQ:sub_0c4d1 takes the run and the cursor; Ben's lines are anchored
	// on his box as it stands now (0x11e9: the midpoint of [0xa97a]/[0xa97e]).
	CursorMan.showMouse(false);
	characterAnchor(_jailBenX, _jailBenY);
	_jailSpeaker = 1;
	_jailLine = line;
	_jailLeft = count;
	_jailSpeaking = true;
	debugC(1, kDebugRooms, "jail: conversation 0x%02x, %u lines%s", line, count,
		   _script.flag(kHasCard) ? ", with the card" : "");
	jailSpeak();
}

/// DLGREQ:sub_0c432, one pass (boss.cpp has the same runner).
void AlienEngine::jailSpeak() {
	if (!_jailLeft) {
		_jailSpeaking = false;
		return;
	}

	const bool uncle = _jailSpeaker == 0;
	const byte *ink = uncle ? kUncleInk : kBenInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, _jailLine, uncle ? kUncleX : _jailBenX, uncle ? kUncleY : _jailBenY,
				 !uncle);

	// DLGREQ:sub_0c384, handler 0x3a.
	if (uncle)
		jailUnclePose(kPoseTalk);

	_jailSpeaker = _jailSpeaker ? 0 : 1;
	_jailLine++;
	_jailLeft--;
}

/// Entry 3: talking to either prisoner from the corridor.
bool AlienEngine::armJailTalk(int obj, byte verb) {
	if (_room != kJailRoom || verb != kVerbTalkTo || (obj != kYodleObj && obj != kUncleObj))
		return false;
	if (_script.flag(kView) != kCorridor)
		return false;

	_jailClock = 0;
	_jailStep = kStepTalk;
	CursorMan.showMouse(false);
	return true;
}

/// Entry 2's loop: the pose settles and [0xa49f].
void AlienEngine::stepJail() {
	if (_room != kJailRoom)
		return;

	// The guard, his machine and the field, which run before the room's own
	// [0xa49f] in the tick (jailguard.cpp).
	stepJailGuard();

	// 0x106b and 0x1080: a line down puts a talking prisoner back.
	if (speechDone()) {
		if (_jailUncle == kPoseTalk)
			jailUnclePose(kPoseSettle);
		if (_jailYodle == kPoseTalk)
			jailYodlePose(kPoseIdle);
	}

	// 0x1037: [0xa7be], counted by LOGIC alongside [0xa49c].
	if (_jailClock == kClockSettle && ++_jailClockPos > kSettleWait) {
		jailUnclePose(kPoseIdle);
		_jailClock = 0;
	}

	if (_jailSpeaking && speechDone())
		jailSpeak();

	if (_jailStep == kStepIdle)
		return;

	_jailPos++;

	switch (_jailStep) {
	case kStepTalk:
		if (_anims.shownFrame(kUncleSlot) < kTalkFrame)
			break;
		jailUnclePose(kPoseFace);
		jailConversation();
		_jailStep = kStepTalking;
		break;

	case kStepTalking:
		// [0xad3f], the run over and its last line down.
		if (_jailSpeaking || !speechDone())
			break;
		CursorMan.showMouse(true);
		_jailStep = kStepIdle;
		_jailClock = kClockSettle;
		_jailClockPos = 0;
		break;

	case kStepArrive:
		if (_jailPos <= kArriveWait)
			break;
		jailConversation();
		break;

	case kStepEscape:
		if (_jailSpeaking || !speechDone())
			break;
		_jailStep = _script.flag(kHasCard) == 1 ? kStepPod : kStepLeave;
		break;

	case kStepLeave:
		walkTo(kLeaveX, kLeaveY, kLeaveFacing);
		_jailStep = kStepLeaving;
		_jailPos = 0;
		break;

	case kStepLeaving:
		if (_jailPos <= kLeaveWait)
			break;
		_jailStep = kStepIdle;
		takeExit(kLobbySubmode);
		break;

	case kStepPod:
		_script.setFlag(kPodReady, 1);
		debugC(1, kDebugRooms, "jail: the pod is ready, off to room 59");
		_jailStep = kStepIdle;
		takeExit(kPodSubmode);
		break;

	default:
		break;
	}
}

/**
 * Room 58 has no walk mask or node ring: ovr_3a_101d names no KIERRA file,
 * like the hallways (corridor.cpp). Entry 0's clamps, one set a view, keep Ben
 * on the floor, so the click it resolved is walked to in a straight line.
 */
bool AlienEngine::jailWalkTo(int x, int y, int arrivalFacing) {
	if (_room != kJailRoom)
		return false;

	straightWalkTo(x, y, arrivalFacing);
	return true;
}

/// A plain click in the cell, standing in for the real exit machine: see this
/// file's header. The corridor has real ways out (object 10 to room 51, and
/// the pod), so this stays out of it.
bool AlienEngine::armJailExit() {
	if (_room != kJailRoom || _script.flag(kView) == kCorridor ||
		_heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_jailClicks++;
	debugC(1, kDebugRooms, "jail: step %u of %u back to the ship", _jailClicks, kEscapeClicks);

	if (_jailClicks >= kEscapeClicks) {
		_jailClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kShipExitSubmode);
	}

	return true;
}

} // End of namespace Alien
