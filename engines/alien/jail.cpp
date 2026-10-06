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
// **The way out of the cell.** [0xa7b2] is the air shaft's cover, 1 -- shut --
// from the new game (seg_main:0x0b29) until the loose shackle is pulled. The
// shackle is object 3, registered only while the cover is on; its line (4,
// "This one seems to be a bit loose...") is the click's own outcome, and entry
// 3 arms [0xa49f] = 0x32 behind it (0x022d):
//
//   0x32   the line is down ([0xad1c])
//   0x33   JAIL_WAL's one frame held on slot 2 -- the cover off, the shaft
//          open -- sample 3, [0xa7b2] := 0
//   0x34   0x14 later: line 6, "How convenient...!", and the cursor back
//
// The open shaft is object 7, and the plate (10c9's room 58 routine) holds the
// same frame up on every later entry. A walk that ends on it with the click
// still latched on it ([0xa644] == 7, CHARANIM:sub_1514b) is the escape:
//
//   0x46   the cursor gone
//   0x47   Ben handed to slot 4, JAI_BESH's fifteen frames of the crawl
//   0x48   the slot has run out ([0xa4ee])
//   0x49   submode 0x64 -- transitions.cpp's room 58, submode 100 -> room 56
//
// **The LCD.** The ring (item 48) on object 21 in the corridor, the first
// time ([0xa7ab] == 0, 0x0103), is a lifted row: slot 5 plays the screen
// lighting up, frames 1..6 at rate 4, [0xa7ab] goes up and event 0x57 is
// spoken. The row's last write is the room's [0xa49f] = 0xc8, which the lift
// leaves behind; the tick (0x101a) waits for slot 5 to be two frames from its
// end ([0xa4ef] == 2) and runs the screen on as a loop over frames 4..6 at
// rate 6 -- the loop the plate starts on every later way in.
//
// The steps that wait on [0xa49c] wait on the animation frame it is counted
// on ([0xa5f8], 0x09c7), every second tick pair.
static const int kJailRoom = 58;

static const byte kShipExitSubmode = 100;	///< transitions.cpp: room 58, submode 100 -> room 56

static const uint16 kView = 0xa7b1;			///< 1 = the cell, 2 = the corridor
static const uint16 kForceField = 0xa7b0;		///< 1 = the field over the cells is up
static const uint16 kTalked = 0xa7c2;
static const uint16 kTalkedAgain = 0xa7c3;
static const uint16 kHasCard = 0xa7c4;
static const uint16 kPodReady = 0xa7d2;		///< ending.cpp's guard on room 59's machine
static const uint16 kShaftShut = 0xa7b2;		///< 1 = the air shaft's cover is still on
static const uint16 kClickedObj = 0xa644;		///< the object the last left click was on
static const byte kCell = 1;
static const byte kCorridor = 2;

static const byte kCard = 43;				///< OBJ:sprite_find_slot(0x2b)
static const char *const kCorridorTal = "ROOM58_2.TAL";

static const byte kVerbTalkTo = 6;
static const byte kVerbShackle = 5;			///< [0xa824], the shackle's rectangle verb
static const byte kYodleObj = 5, kUncleObj = 6;
static const byte kShackleObj = 3, kShaftObj = 7;

/// The steps of [0xa49f] this file runs, and the waits on [0xa49c].
static const byte kStepIdle = 0;
static const byte kStepShackle = 0x32;
static const byte kStepCoverOff = 0x33;
static const byte kStepShaftOpen = 0x34;
static const byte kStepShaft = 0x46;
static const byte kStepCrawl = 0x47;
static const byte kStepCrawling = 0x48;
static const byte kStepCrawled = 0x49;
static const byte kStepTalk = 0x6e;
static const byte kStepTalking = 0x82;
static const byte kStepArrive = 0x83;
static const byte kStepEscape = 0x84;
static const byte kStepLeave = 0x87;
static const byte kStepLeaving = 0x88;
static const byte kStepPod = 0x8c;
static const byte kStepLcd = 0xc8;
static const uint16 kArriveWait = 0xf;
static const uint16 kLeaveWait = 0x19;
static const uint16 kSettleWait = 0x5a;
static const uint16 kShaftLineWait = 0x14;

/// 0x0e03: anim_play_mode2(2, 1, 1, 0), JAIL_WAL's frame held.
static const uint kCoverSlot = 2;
/// 0x0e68: anim_play_mode1(4, 1, 0x10, 3), the crawl; frame 0x10 is the
/// terminator that takes the last one down.
static const uint kCrawlSlot = 4;
static const int kCrawlFrames = 0x10, kCrawlRate = 3;

/// The cover coming off, sfx_play_delayed(3, 0, 0x3a98, 0x37, -0x32, 1).
static const uint kCoverSample = 3;
static const uint32 kCoverRateHz = 0x3a98;
static const byte kCoverVolume = 0x37;
static const int8 kCoverPanning = -0x32;
static const uint16 kCoverDelay = 1;

/// The LCD: the ring on object 21, and 0x1025's anim_play_mode1(5, 4, 3, 6).
static const byte kItemRing = 48;
static const byte kLcdObj = 21;
static const uint16 kLcdLit = 0xa7ab;
static const uint kLcdSlot = 5;
static const int kLcdLoopFirst = 4, kLcdLoopCount = 3, kLcdLoopRate = 6;

static const byte kOutcomeShaftOpen = 6;	///< "How convenient...!"

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

/// [0xa7be]: the room's own clock beside [0xa49c], kept where a save holds it.
static const uint16 kJailClockPos = 0xa7be;
static const int kHoldMode = 2;

/// The prologue's arms: both prisoners' idle poses (0x0778, 0x079a), and the
/// way in with the field down (0x08c2).
void AlienEngine::startJail() {
	if (_room != kJailRoom)
		return;

	_jailStep = kStepIdle;
	_jailPos = 0;
	_jailLeft = 0;
	_jailSpeaking = false;
	_jailClock = 0;

	// The prologue's first writes (0x06bf-0x06ce): the next of the cell talk's
	// lines, 0x78 on, and the two states the guard's patrol keeps. The port
	// runs neither machine from these bytes yet, but a save carries them
	// (dosbox state parity, guard-gone).
	_script.setFlag(0xa7aa, 0x78);
	_script.setFlag(0xa7bd, 0);
	_script.setFlag(0xa7c0, 0);

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

/// Entry 3, 0x021a: the loose shackle. Its line is the click's own outcome;
/// the machine waits for it to come down.
bool AlienEngine::armJailShackle(int obj, byte verb) {
	if (_room != kJailRoom || obj != kShackleObj || verb != kVerbShackle)
		return false;

	_jailStep = kStepShackle;
	CursorMan.showMouse(false);
	return true;
}

/// Entry 3, 0x0103: the ring on the LCD the first time. The row lights the
/// screen and raises [0xa7ab]; this is its last write, [0xa49f] = 0xc8, so it
/// has to read the flag before the row runs.
void AlienEngine::armJailLcd(int obj, byte item) {
	if (_room != kJailRoom || obj != kLcdObj || item != kItemRing)
		return;
	if (_script.flag(kLcdLit) != 0)
		return;

	_jailStep = kStepLcd;
}

/// CHARANIM:sub_1514b's last test: a walk in the cell that ended with the click
/// that started it still latched on the open shaft.
void AlienEngine::jailShaftArrival() {
	if (_jailStep != kStepIdle || _script.flag(kView) != kCell)
		return;
	if (_script.flag(kClickedObj) != kShaftObj)
		return;
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;

	_script.setFlag(kClickedObj, 0);
	CursorMan.showMouse(false);
	_jailStep = kStepShaft;
	debugC(1, kDebugRooms, "jail: into the air shaft");
}

/// Entry 2's loop: the pose settles and [0xa49f].
/**
 * CHARANIM:sub_1505d (0x0c0d), every tick of room 58: the corridor door shuts
 * behind Ben. [0xa7a2] says whether his sprite is in the doorway (x past
 * 0x233, y above 0x4f) and [0xa7a3] what it said last tick; walking out of it
 * with the door open plays the door shut and sets [0xa7ba], which takes the
 * way out (object 10, walkgeom) away until a card opens it again.
 */
void AlienEngine::stepJailDoor() {
	static const uint16 kInDoorway = 0xa7a2, kWasInDoorway = 0xa7a3, kDoorShut = 0xa7ba;
	static const int kDoorwayX = 0x233, kDoorwayY = 0x4f;
	static const uint kDoorSlot = 3, kDoorFrames = 0x16, kDoorRate = 2;
	static const uint kDoorSample = 1;
	static const uint32 kDoorSampleRate = 0x2af8;
	static const byte kDoorVolume = 0x37;
	static const int8 kDoorPan = 0x32;
	static const uint16 kDoorShutDelay = 2;
	static const int kPlayBackward = 3;	///< anim_play_mode3

	_script.setFlag(kWasInDoorway, _script.flag(kInDoorway));
	const bool inDoorway = _ben.spriteX() > kDoorwayX && _ben.spriteY() < kDoorwayY;
	_script.setFlag(kInDoorway, inDoorway ? 1 : 0);
	if (inDoorway || _script.flag(kInDoorway) == _script.flag(kWasInDoorway)
			|| _script.flag(kDoorShut) != 0)
		return;

	_anims.play(kDoorSlot, kDoorFrames, kDoorFrames, kDoorRate, kPlayBackward);
	_sound.queue(kDoorSample, kDoorSampleRate, kDoorVolume, kDoorPan, kDoorShutDelay);
	_script.setFlag(kDoorShut, 1);
	debugC(1, kDebugRooms, "jail: the corridor door shuts behind him");
}

void AlienEngine::stepJail() {
	if (_room != kJailRoom)
		return;

	stepJailDoor();

	// [0xa49c] and [0xa7be] both move on the animation frame (0x09c0).
	const bool frame = (_tick & 3) == 0;

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
	if (frame)
		setStateWord(kJailClockPos, stateWord(kJailClockPos) + 1);
	if (_jailClock == kClockSettle && stateWord(kJailClockPos) > kSettleWait) {
		jailUnclePose(kPoseIdle);
		_jailClock = 0;
	}

	if (_jailSpeaking && speechDone())
		jailSpeak();

	jailShaftArrival();

	if (_jailStep == kStepIdle)
		return;

	if (frame)
		_jailPos++;

	switch (_jailStep) {
	case kStepShackle:
		if (!speechDone())
			break;
		_jailStep = kStepCoverOff;
		break;

	case kStepCoverOff:
		_anims.play(kCoverSlot, 1, 1, 0, 2);
		_sound.queue(kCoverSample, kCoverRateHz, kCoverVolume, kCoverPanning, kCoverDelay);
		_script.setFlag(kShaftShut, 0);
		rebuildHotspots();
		_jailStep = kStepShaftOpen;
		_jailPos = 0;
		debugC(1, kDebugRooms, "jail: the shackle pulls the shaft's cover off");
		break;

	case kStepShaftOpen:
		if (_jailPos <= kShaftLineWait)
			break;
		{
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kOutcomeShaftOpen, anchorX, anchorY);
		}
		CursorMan.showMouse(true);
		_jailStep = kStepIdle;
		break;

	case kStepShaft:
		_jailStep = kStepCrawl;
		break;

	case kStepCrawl:
		playCharacterAnim(kCrawlSlot, 1, kCrawlFrames, kCrawlRate, 1);
		_jailStep = kStepCrawling;
		break;

	case kStepCrawling:
		if (_anims.remaining(kCrawlSlot) != 0)
			break;
		_jailStep = kStepCrawled;
		break;

	case kStepCrawled:
		_jailStep = kStepIdle;
		debugC(1, kDebugRooms, "jail: through the shaft, back to the ship");
		takeExit(kShipExitSubmode);
		break;
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
		setStateWord(kJailClockPos, 0);
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

	case kStepLcd:
		// 0x101a: [0xa4ef] == 2, the lighting up two frames from its end.
		if (_anims.remaining(kLcdSlot) != 2)
			break;
		_anims.play(kLcdSlot, kLcdLoopFirst, kLcdLoopCount, kLcdLoopRate, 1);
		_jailStep = kStepIdle;
		debugC(1, kDebugRooms, "jail: the LCD runs on");
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

} // End of namespace Alien
