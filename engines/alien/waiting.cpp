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

// Room 54, the waiting room: the ticket machine, and the way in to Jack's room
// (room 60) beside it.
//
// tools/roomlogic.py lifts a handful of this room's ordinary bodies (the
// chairs' canned lines, the machine at object 13 giving up item 45 and later
// trading it for item 46) but nothing that reaches [0xa87e] itself, and the
// bodies it does lift could never run: **every flag this room registers its
// own hotspots on is written by its [0xa49f] machine and by nothing else**, so
// with the machine missing the port left them at their shipped values for
// ever. [0xa7e0] is the one that matters here -- it gates object 13, the slot
// the printed ticket sits in -- and it ships clear, so the lifted body behind
// it was dead code and items 45, 46 and 47 had no source at all (finding
// #115).
//
// The machine is the ticket machine, and it is one body and a counter, which
// is what "the machine pushed three times" in the walkthrough means:
//
//   [0xa7e1]  how many times the button has been pushed, held at 3
//   [0xa7e0]  a ticket is sitting in the slot, waiting to be taken
//   [0xa7e3]  the number has been called; the machine prints nothing more
//
// Object 12 is the button (a registered hotspot, verb 12, and the lifted body
// on it is only the refusal for pushing it with a ticket already out). Its
// other half, ovr_36_0fa6:0x0169, is the machine's arm, and it branches on the
// counter rather than on anything the player holds:
//
//   [0xa7e1] < 2   state 0x32, the loudspeaker calling a number that is not his
//   [0xa7e1] == 2  outcome 0x1c and state 0x64, the number that is
//   [0xa7e1] == 3  state 0x32 again, for ever
//
// and then increments it, stopping at 3.
//
// State 0x32 runs 0x64 ticks of the machine's own loop (slot 2, held by
// [0xa53c]) under outcome 0x18, then 0x33 waits for that line and 0x28 starts
// the button's play (10c9:sub_11d00, slot 4 under [0xa53e]). 0x34 prints --
// seventeen frames of WAI_BUTT -- and 0x35 puts outcome 0x19 up and sets
// [0xa7e0], which is what makes object 13 appear. Once [0xa7e3] stands the
// same push goes 0x3c -> 0x3d instead: outcome 0x1e, and no ticket.
//
// The called number, state 0x64, is a room exit into itself:
// transitions.cpp's { 54, 100, 54 } self-link, taken with [0xa7e2] set. The
// room comes back up with roominit.cpp's own char_place row (guarded on that
// flag) standing him at the desk, and the tick reads it as the cue for state
// 0x6d -- outcome 0x1d, and item 46 traded for item 47, the number he is
// actually holding when he is called.
//
// Its own way back (transitions.cpp: room 54, submode 1 -> room 57) is
// already armed generically, a kWalkSubmode row on a real hotspot (object 1).
// The way in to Jack's room is that same exit, taken over. The tick's tail
// (0x0e9b) looks at it after OBJ:sub_078dd has armed it -- [0xa881] set, and
// [0xa644] still naming object 1 -- and, while
//
//   [0xa7d7] == 1  the maintenance man's badge (item 44, the hallways' own
//                  machine, ovr_35_0f9e:0x1020 -- job 15's, not this one's)
//   [0xa7b4] == 0  ships 1; only the jail clears it, as the guard tells him
//                  "the Boss wants to see you" (CHARANIM:sub_15c32, 0x2300)
//   [0xa7e4] == 0  he has not been in there yet
//
// all hold, it drops the exit ([0xa881], walk_submode and game_submode back
// to 0) and starts state 0x96 instead: the board flickers through slot 5's
// frame list and a chime plays, "Wait a minute..." (0x22), he walks up to the
// board, "I think I have that figure in one of these tickets here... Here we
// go.." (0x23), he walks to the door at object 5, the door opens (slot 6),
// and 0x9c raises [0xa7e4] and leaves on game_submode 0x32 -- transitions.cpp's
// { 54, 50, 60 }.
//
// The door itself answers a walk that ends on it (0x0e56, [0x9908] == 1 with
// [0xa644] == 5): "The doors don't seem to open. I think I'll just have to
// wait for my turn." (0x28), or once he has been through, "I better not go in
// there anymore." (0x24).
static const int kWaitingRoom = 54;

static const uint16 kTicketOut = 0xa7e0;	///< a ticket is in the slot
static const uint16 kPushCount = 0xa7e1;	///< how often the button was pushed
static const uint16 kCalledReload = 0xa7e2;	///< the self-link is being taken
static const uint16 kNumberCalled = 0xa7e3;	///< his number has been called
// The waiting room's two other customers, and what the first leaves behind.
// Each flag is a hotspot set and a figure: [0xa7dc] the one on the bench
// (object 9, WAIT_AL1 on slot 0), [0xa7dd] the one standing (object 10,
// WAIT_AL2 on slot 1), and [0xa7de] the bench once his number has been called
// and both have gone (object 14, WAIT_AL1's last frames).
static const uint16 kBenchAlien = 0xa7dc;
static const uint16 kStandingAlien = 0xa7dd;
static const uint16 kBenchLeft = 0xa7de;

static const int kButton = 12;			///< object 12, verb 12 -- "Push"
static const byte kPushVerb = 12;

// Item 45 is the first number and comes out of the lifted body on object 13;
// these two are the trades the machine itself makes.
static const byte kTicketSecond = 46;
static const byte kTicketThird = 47;

static const byte kMaxPushes = 3;		///< [0xa7e1] is held here

// The room's own outcomes, out of ROOM54.TAL.
static const byte kLineAnother = 0x18;	///< a number that is not his
static const byte kLinePrinted = 0x19;	///< the ticket drops into the slot
static const byte kLineCalled = 0x1c;	///< and this one is
static const byte kLineDesk = 0x1d;		///< what he says standing at the desk
static const byte kLineDead = 0x1e;		///< the machine, once he is called

static const uint kMachineSlot = 2;		///< WAI_MACH, looped by [0xa53c]
static const uint kButtonSlot = 4;		///< WAI_BUTT, looped by [0xa53e]

/// 10c9:sub_11d00 and sub_11d17, the two one-frame plays that start and stop
/// the button: both are shared code rather than anything room 54 owns.
static const byte kButtonDownFrame = 3, kButtonUpFrame = 1;

/// The print itself, the one real play in the sequence (0x0bf6): MIDAS's
/// arguments go slot, first frame, count, rate in push order, so this is five
/// frames of the machine from its seventeenth, not the button's.
static const byte kPrintFirst = 0x11, kPrintCount = 5, kPrintRate = 4;

// [0xa49c] waits, as the machine's own thresholds have them.
static const uint16 kAnnounceWait = 0x64;
static const uint16 kPrintWait = 0x0a;
static const uint16 kTicketWait = 0x0f;
static const uint16 kDeadWait = 0x19;

// The states, by the value [0xa49f] carries.
static const byte kStepIdle = 0;
static const byte kStepWhirr = 0x28;		///< the button goes down
static const byte kStepAnnounce = 0x32;	///< the loudspeaker
static const byte kStepAnnounceDone = 0x33;
static const byte kStepPrint = 0x34;		///< the ticket is printed
static const byte kStepTicket = 0x35;		///< and lands in the slot
static const byte kStepDeadStart = 0x3c;	///< nothing comes out any more
static const byte kStepDead = 0x3d;
static const byte kStepCall = 0x64;		///< his number, and the self-link
static const byte kStepDesk = 0x6d;		///< back up again, at the desk
static const byte kStepDeskDone = 0x6e;

static const byte kCallSubmode = 100;		///< transitions.cpp: room 54 -> room 54

// The call in to Jack's room, 0x0e9b and states 0x96..0x9c.
static const uint16 kClickedObj = 0xa644;	///< the object the last left click was on
static const uint16 kBadge = 0xa7d7;		///< the maintenance man's badge
static const uint16 kNotSummoned = 0xa7b4;	///< ships 1; the jail's guard clears it
static const uint16 kBeenInside = 0xa7e4;	///< he has been in Jack's room

static const int kWayOut = 1;			///< object 1, submode 1 -> room 57
static const byte kWayOutSubmode = 1;
static const int kJackDoor = 5;			///< object 5, the door itself

static const byte kStepNotice = 0x96;		///< the board flickers
static const byte kStepWaitMinute = 0x97;
static const byte kStepToBoard = 0x98;
static const byte kStepFigure = 0x99;
static const byte kStepToDoor = 0x9a;
static const byte kStepDoorOpen = 0x9b;
static const byte kStepEnter = 0x9c;

static const byte kLineWaitMinute = 0x22;	///< "Wait a minute..."
static const byte kLineFigure = 0x23;		///< "I think I have that figure..."
static const byte kLineNotYet = 0x28;		///< the door, before his turn
static const byte kLineNotAgain = 0x24;	///< the door, after it

static const uint16 kWaitMinuteWait = 0x14;
static const uint16 kToBoardWait = 0x1e;
static const uint16 kFigureWait = 0x46;
static const uint16 kDoorWait = 0x37;

/// MIDAS:sub_18962(9, 0, 0xc, 5, ds:0x6fb8): the board, WAI_NUMB, mode 6 over
/// this list. Slot 5 is the fan, which the board had been playing over.
static const uint kBoardSlot = 9;
static const byte kBoardFrames[] = { 1, 2, 1, 2, 1, 3, 1, 3, 1, 3, 1, 3 };
static const int kBoardRate = 5, kBoardMode = 6;

// The alarm's aftermath (states 0xc8..0xd2): the first time the room comes up
// after room 57's alarm ([0x33f6], corridor.cpp) the aliens in it scatter
// (slot 10, WAI_MONS, a lifted opening row under the same flag), and Ben is
// walked out of their way in two legs once each play has run out.
static const uint16 kAlarmPlayed = 0x33f6;
static const uint kAlarmSlot = 10;
static const byte kAlarmFirst = 4, kAlarmCount = 0xf, kAlarmRate = 3;
static const int kAlarmX1 = 0x5a, kAlarmY1 = 0x88, kAlarmFacing1 = 1;
static const int kAlarmX2 = 0x50, kAlarmY2 = 0x7e, kAlarmFacing2 = 4;
static const uint16 kAlarmWait = 0x28;

// The two customers' conversations (ovr_0fa6:0x00e5 and 0x003e, the tick at
// 0x0a63): talk to one, or hold an item out to him, and once Ben's own line is
// down he answers through DLGREQ:sub_0c275 -- which in this room puts him in his
// talking pose first, by [0xa7df] -- and goes back to waiting. The answers are
// outcomes 0x11/0x12 to a word and 0x15/0x16 to an item, and an item gets
// "0x17" from Ben after it (dosbox graphics parity, finding #161).
static const byte kStepBenchTalk = 3;
static const byte kStepBenchAnswer = 4;
static const byte kStepBenchDone = 5;
static const byte kStepStandingTalk = 0x0a;
static const byte kStepStandingAnswer = 0x0b;
static const byte kStepStandingDone = 0x0c;
static const byte kStepBenchItem = 0x14;
static const byte kStepBenchItemDone = 0x15;
static const byte kStepStandingItem = 0x1e;
static const byte kStepStandingItemDone = 0x1f;

static const int kBenchObject = 9, kStandingObject = 10;
static const byte kTalkVerb = 6;
static const uint16 kSpeaker = 0xa7df;			///< 0 the one on the bench, 1 the one standing

/// DLGREQ:sub_0c250 for each: anchor and ink.
static const int kBenchX = 0x76, kBenchY = 0x46;
static const byte kBenchInk[3] = { 0x3a, 0x3a, 0x3a };
static const int kStandingX = 0xca, kStandingY = 0x28;
static const byte kStandingInk[3] = { 0x3f, 0x28, 0x0f };

static const byte kLineBenchWord = 0x11, kLineStandingWord = 0x12;
static const byte kLineBenchItem = 0x15, kLineStandingItem = 0x16;
static const byte kLineNoThanks = 0x17;

static const byte kStepAlarm = 0xc8;
static const byte kStepAlarmLeg = 0xcd;
static const byte kStepAlarmDone = 0xd2;

/// sfx_play_delayed(3, 0, 0x4650, 0x40, 0, 1), the chime that goes with it.
static const uint kChimeSample = 3;
static const uint32 kChimeRate = 0x4650;
static const byte kChimeVolume = 0x40;

/// OBJ:sub_07890, the two places he is walked to: in front of the board, and
/// the door (the same point object 5's own walk row names).
static const int kBoardX = 0xa0, kBoardY = 0x7b, kBoardFacing = 1;
static const int kDoorX = 0x11c, kDoorY = 0x70, kDoorFacing = 2;

/// anim_play_mode1(6, 1, 0xb, 2), the door opening -- the same eleven frames
/// the room plays backwards when he comes back out (0x08d2) -- and
/// INPUT:sub_01d38(1), the shared door sound: sfx_play_delayed(1, 0, 0x2af8,
/// 0x37, 0x32, 1).
static const uint kDoorSlot = 6;
static const int kDoorFrames = 0xb, kDoorRate = 2;
static const uint kDoorSample = 1;
static const uint32 kDoorSampleRate = 0x2af8;
static const byte kDoorVolume = 0x37;
static const int8 kDoorPanning = 0x32;

static const byte kJackExitSubmode = 0x32;	///< transitions.cpp: room 54, submode 50 -> room 60

/// MIDAS:sub_19ba0 and sub_19b64, the two figures' poses: a mode 8 play over
/// a list in the data segment, slot 0 for the one on the bench and slot 1 for
/// the one standing. The room's tick keeps both slots looping (anims.cpp), so a
/// pose runs until another replaces it. (The pose numbers they keep at
/// [0xa52a] and [0xa52b] are read by nothing in this room.)
struct WaitingPose {
	uint slot;
	byte pose;
	int rate;
	const byte *frames;
	int count;
};

static const byte kBenchIdle[] = {			// ds:0x6e7a
	1, 6, 1, 6, 7, 6, 1, 6, 7, 6, 7, 1, 7, 6, 6
};
static const byte kBenchWait[] = {			// ds:0x6e8a
	1, 1, 1, 1, 1, 2, 1, 1, 1, 1, 2, 1, 1, 1, 3, 4, 5, 1, 3, 4, 5, 1, 3, 4, 5,
	1, 3, 4, 5, 1, 2, 1, 1, 1, 1, 1, 1, 1
};
static const byte kBenchGone[] = {			// ds:0x6eb0
	8, 9, 10, 10, 10, 10, 9, 8, 8, 8, 1
};
static const byte kStandingWait[] = {		// ds:0x6e38
	1, 2, 3, 2, 1, 2, 3, 2, 1, 2, 3, 2, 1, 2, 3, 2, 1, 2, 3, 2, 7, 1, 1, 1, 1,
	1, 1, 7, 1, 1, 2, 3, 2, 1, 2, 3, 2, 1, 1, 1, 1, 7, 1, 1, 1, 0
};
static const byte kStandingTalk[] = {		// ds:0x6e66
	4, 5, 6, 5, 4, 5, 6, 5, 4, 5, 6, 5, 4, 5, 4, 7, 5, 6, 5, 0
};

static const WaitingPose kWaitingPoses[] = {
	{ 0, 1, 3, kBenchIdle, ARRAYSIZE(kBenchIdle) },
	{ 0, 2, 3, kBenchWait, ARRAYSIZE(kBenchWait) },
	{ 0, 3, 4, kBenchGone, ARRAYSIZE(kBenchGone) },
	{ 1, 1, 4, kStandingWait, ARRAYSIZE(kStandingWait) },
	{ 1, 2, 3, kStandingTalk, ARRAYSIZE(kStandingTalk) },
};

static const int kPoseMode = 8;

void AlienEngine::playWaitingPose(uint slot, byte pose) {
	for (uint i = 0; i < ARRAYSIZE(kWaitingPoses); i++) {
		const WaitingPose &p = kWaitingPoses[i];
		if (p.slot == slot && p.pose == pose) {
			_anims.play(slot, 0, p.count, p.rate, kPoseMode, p.frames);
			return;
		}
	}
}

/// DLGREQ:sub_0c275 for room 54: the customer named by [0xa7df] takes his
/// talking pose and says the line, in the colour and place sub_0c250 gave him.
void AlienEngine::waitingCustomerSays(byte code) {
	const bool standing = _script.flag(kSpeaker) == 1;
	playWaitingPose(standing ? 1 : 0, standing ? 2 : 1);
	const byte *ink = standing ? kStandingInk : kBenchInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, code, standing ? kStandingX : kBenchX, standing ? kStandingY : kBenchY,
				 false);
	debugC(1, kDebugRooms, "waiting: the %s customer says outcome 0x%02x",
		   standing ? "standing" : "bench", code);
}

/**
 * Entry 3's arms for the two customers (ovr_0fa6:0x003e, 0x00e5): a word or an
 * item to either starts his conversation, with Ben's own line -- the table's --
 * going first.
 */
bool AlienEngine::armWaitingCustomers(int obj, byte verb, byte item) {
	if (_room != kWaitingRoom)
		return false;

	// Ben's own line -- 0x14 holding an item out, 0x33 for the bench he has
	// left -- is the lifted table's (the same entry 3), so only the machine is
	// started here.
	if (obj != kBenchObject && obj != kStandingObject)
		return false;
	if (!item && verb != kTalkVerb)
		return false;

	const bool standing = obj == kStandingObject;
	if (item) {
		_waitingStep = standing ? kStepStandingItem : kStepBenchItem;
	} else {
		_waitingStep = standing ? kStepStandingTalk : kStepBenchTalk;
	}
	_waitingPos = 0;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "waiting: %s the %s customer, step 0x%02x", item ? "an item to" : "a word with",
		   standing ? "standing" : "bench", _waitingStep);
	return true;
}

/// Every arrival answers the self-link.
void AlienEngine::startWaiting() {
	if (_room != kWaitingRoom)
		return;

	// 10c9:sub_11855, from the room's open (ovr_36_0fa6:0x093d): whoever is
	// still waiting takes up their pose. The port never drew either of them
	// (dosbox graphics parity, finding #161).
	if (_script.flag(kBenchAlien) == 1)
		playWaitingPose(0, 2);
	if (_script.flag(kStandingAlien) == 1)
		playWaitingPose(1, 1);
	if (_script.flag(kBenchLeft) == 1)
		playWaitingPose(0, 3);

	// 0x0909: the node ring and the mask are loaded only while the standing
	// customer is there to walk round. Once he has gone the room has no nodes
	// and every walk is a straight line -- through where he stood, which the
	// mask still blocks (trace parity, pod-card and password).
	if (_script.flag(kStandingAlien) != 1)
		_walk.dropNodes();

	_waitingStep = kStepIdle;
	_waitingPos = 0;

	// ovr_36_0fa6:0x08df, the tick's own first test after the reload: the flag
	// state 0x64 left behind, which roominit.cpp has already read to stand him
	// at the desk. Clearing it here is that body's own first act.
	if (_script.flag(kCalledReload) == 1) {
		_script.setFlag(kCalledReload, 0);
		_waitingStep = kStepDesk;
		CursorMan.showMouse(false);

		debugC(1, kDebugRooms, "waiting: his number was called, step 0x%02x", kStepDesk);
	}

	// 0x0986: the alarm's once, cleared as it is answered.
	if (_script.flag(kAlarmPlayed) == 1) {
		_script.setFlag(kAlarmPlayed, 0);
		_waitingStep = kStepAlarm;
		CursorMan.showMouse(false);

		debugC(1, kDebugRooms, "waiting: after the alarm, step 0x%02x", kStepAlarm);
	}
}

/// [0xad1c]: the line the machine put up has come down and nothing follows it.
bool AlienEngine::waitingLineDone() const {
	return !_speech && _queueNext >= _queueCount;
}

void AlienEngine::speakWaiting(byte code) {
	queueOutcome(_tal, code, _ben.walkX(), _ben.walkY() - Walker::kWalkPointY);
}

/**
 * Object 12, the button: ovr_36_0fa6:0x0169, the half the lift cannot see.
 *
 * The lifted body is the other half and runs first, the refusal for pushing it
 * while a ticket is already sitting in the slot ([0xa7e0] == 1, outcome 0x1f);
 * this one only answers while it is not. The counter is bumped either way, and
 * held at three, which is why the fourth push and every one after it goes back
 * to the loudspeaker.
 */
bool AlienEngine::armWaitingButton(int obj, byte verb) {
	if (_room != kWaitingRoom || obj != kButton || verb != kPushVerb)
		return false;
	if (_script.flag(kTicketOut) == 1)
		return false;

	const byte pushes = _script.flag(kPushCount);

	CursorMan.showMouse(false);
	if (pushes == 2) {
		// His number, and with it the walk to the desk.
		speakWaiting(kLineCalled);
		_waitingStep = kStepCall;
	} else {
		_waitingStep = kStepAnnounce;
		_waitingPos = 0;
		// [0xa53c]: the machine's own loop, running while it thinks.
		_anims.setLoopFlag(kMachineSlot, 1);
		// 10c9:sub_11d17, the button back up.
		_anims.setLoopFlag(kButtonSlot, 0);
		_anims.play(kButtonSlot, kButtonUpFrame, 1, 0, 1);
	}

	if (pushes < kMaxPushes)
		_script.setFlag(kPushCount, pushes + 1);

	debugC(1, kDebugRooms, "waiting: push %u, step 0x%02x", pushes + 1, _waitingStep);
	return true;
}

/**
 * One step of the machine, on the tick pair the room's own entry 2 runs on.
 */
void AlienEngine::stepWaitingMachine() {
	if (_room != kWaitingRoom)
		return;

	waitingArrival();

	if (_waitingStep == kStepIdle)
		return;

	// [0xa49c], which LOGIC:sub_11f79 advances on every tick pair whether or
	// not a machine is running.
	_waitingPos++;

	switch (_waitingStep) {
	case kStepBenchTalk:
	case kStepStandingTalk:
	case kStepBenchItem:
	case kStepStandingItem: {
		// 0x0a63..0x0b5b: Ben's line down, then the answer.
		if (!waitingLineDone())
			break;
		const bool standing = _waitingStep == kStepStandingTalk || _waitingStep == kStepStandingItem;
		const bool word = _waitingStep == kStepBenchTalk || _waitingStep == kStepStandingTalk;
		_script.setFlag(kSpeaker, standing ? 1 : 0);
		waitingCustomerSays(word ? (standing ? kLineStandingWord : kLineBenchWord)
								 : (standing ? kLineStandingItem : kLineBenchItem));
		_waitingStep = word ? (standing ? kStepStandingDone : kStepBenchDone)
							: (standing ? kStepStandingItemDone : kStepBenchItemDone);
		break;
	}

	case kStepBenchDone:
	case kStepStandingDone:
	case kStepBenchItemDone:
	case kStepStandingItemDone: {
		// Back to waiting: sub_19ba0(2) or sub_19b64(1), and the cursor.
		if (!waitingLineDone())
			break;
		const bool standing = _waitingStep == kStepStandingDone || _waitingStep == kStepStandingItemDone;
		playWaitingPose(standing ? 1 : 0, standing ? 1 : 2);
		if (_waitingStep == kStepBenchItemDone || _waitingStep == kStepStandingItemDone)
			speakWaiting(kLineNoThanks);
		_waitingStep = kStepIdle;
		CursorMan.showMouse(true);
		break;
	}

	case kStepAnnounce:
		if (_waitingPos <= kAnnounceWait)
			break;
		speakWaiting(kLineAnother);
		_waitingStep = kStepAnnounceDone;
		_waitingPos = 0;
		break;

	case kStepAnnounceDone:
		if (!waitingLineDone())
			break;
		_anims.setLoopFlag(kMachineSlot, 0);
		_waitingStep = kStepWhirr;
		break;

	case kStepWhirr:
		// The original waits on [0xa4ec] here, a byte only room 11 ever writes
		// and only ever to zero, so the wait is over as soon as it is reached.
		// 10c9:sub_11d00, the button going down.
		_anims.setLoopFlag(kButtonSlot, 1);
		_anims.play(kButtonSlot, kButtonDownFrame, 1, 4, 1);
		_waitingPos = 0;
		_waitingStep = _script.flag(kNumberCalled) == 0 ? kStepPrint : kStepDeadStart;
		break;

	case kStepPrint:
		if (_waitingPos <= kPrintWait)
			break;
		_anims.play(kMachineSlot, kPrintFirst, kPrintCount, kPrintRate, 1);
		_waitingPos = 0;
		_waitingStep = kStepTicket;
		break;

	case kStepTicket:
		if (_waitingPos <= kTicketWait)
			break;
		speakWaiting(kLinePrinted);
		// The whole point of the sequence: object 13's hotspot is registered
		// on this flag, so the ticket becomes something that can be taken.
		_script.setFlag(kTicketOut, 1);
		// The table is a program over the room's flags and is only run when
		// something asks for it, so the rectangle appears with the ticket.
		rebuildHotspots();
		CursorMan.showMouse(true);
		_waitingStep = kStepIdle;
		break;

	case kStepDeadStart:
		_waitingPos = 0;
		_waitingStep = kStepDead;
		break;

	case kStepDead:
		if (_waitingPos <= kDeadWait)
			break;
		speakWaiting(kLineDead);
		CursorMan.showMouse(true);
		_waitingStep = kStepIdle;
		break;

	case kStepCall:
		if (!waitingLineDone())
			break;
		// The original grabs the screen here (GFX:sub_263ea over a buffer of
		// its own, then CHARANIM:sub_14ba9) because it is about to reload the
		// room under the player; the port's takeExit does that work itself.
		_script.setFlag(kNumberCalled, 1);
		_script.setFlag(kBenchAlien, 0);
		_script.setFlag(kStandingAlien, 0);
		_script.setFlag(kBenchLeft, 1);
		_script.setFlag(kCalledReload, 1);
		_waitingStep = kStepIdle;
		takeExit(kCallSubmode);
		break;

	case kStepDesk:
		speakWaiting(kLineDesk);
		// The third number is the second one taken back and stamped: an
		// inv_replace in the original, and the only place item 47 comes from.
		_inventory.remove(kTicketSecond);
		_inventory.add(kTicketThird);
		_waitingStep = kStepDeskDone;
		break;

	case kStepDeskDone:
		if (!waitingLineDone())
			break;
		CursorMan.showMouse(true);
		_waitingStep = kStepIdle;
		break;

	case kStepAlarm:
		// [0xa4f4]: slot 10's frames left.
		if (_anims.remaining(kAlarmSlot) != 0)
			break;
		walkTo(kAlarmX1, kAlarmY1, kAlarmFacing1);
		_anims.play(kAlarmSlot, kAlarmFirst, kAlarmCount, kAlarmRate, 1);
		_waitingStep = kStepAlarmLeg;
		break;

	case kStepAlarmLeg:
		if (_anims.remaining(kAlarmSlot) != 0)
			break;
		walkTo(kAlarmX2, kAlarmY2, kAlarmFacing2);
		_waitingPos = 0;
		_waitingStep = kStepAlarmDone;
		break;

	case kStepAlarmDone:
		if (_waitingPos <= kAlarmWait)
			break;
		CursorMan.showMouse(true);
		_waitingStep = kStepIdle;
		debugC(1, kDebugRooms, "waiting: the alarm's aftermath is over at %d,%d", _ben.walkX(), _ben.walkY());
		break;

	case kStepNotice:
		_anims.play(kBoardSlot, 0, ARRAYSIZE(kBoardFrames), kBoardRate, kBoardMode, kBoardFrames);
		_sound.queue(kChimeSample, kChimeRate, kChimeVolume, 0, 1);
		_waitingPos = 0;
		_waitingStep = kStepWaitMinute;
		break;

	case kStepWaitMinute:
		if (_waitingPos <= kWaitMinuteWait)
			break;
		speakWaiting(kLineWaitMinute);
		_waitingPos = 0;
		_waitingStep = kStepToBoard;
		break;

	case kStepToBoard:
		if (_waitingPos <= kToBoardWait)
			break;
		walkTo(kBoardX, kBoardY, kBoardFacing);
		_waitingPos = 0;
		_waitingStep = kStepFigure;
		break;

	case kStepFigure:
		if (_waitingPos <= kFigureWait)
			break;
		speakWaiting(kLineFigure);
		_waitingStep = kStepToDoor;
		break;

	case kStepToDoor:
		if (!waitingLineDone())
			break;
		walkTo(kDoorX, kDoorY, kDoorFacing);
		_waitingPos = 0;
		_waitingStep = kStepDoorOpen;
		break;

	case kStepDoorOpen:
		if (_waitingPos <= kDoorWait)
			break;
		_anims.play(kDoorSlot, 1, kDoorFrames, kDoorRate, 1);
		_sound.queue(kDoorSample, kDoorSampleRate, kDoorVolume, kDoorPanning, 1);
		_waitingStep = kStepEnter;
		break;

	case kStepEnter:
		// [0xa4f0], slot 6's frames left.
		if (_anims.remaining(kDoorSlot) != 0)
			break;
		// The original wipes the screen here too (OBJ:sub_02f27); the port's
		// room change does without it, the same as maze.cpp's.
		_script.setFlag(kBeenInside, 1);
		_waitingStep = kStepIdle;
		_waitingPos = 0;
		CursorMan.showMouse(true);
		takeExit(kJackExitSubmode);
		break;

	default:
		break;
	}
}

/**
 * 0x0e9b: the way out through object 1, taken over while his number is up.
 *
 * checkExit() asks this before it takes an exit the arrival has fired, which
 * is the point the original's tick tail sees it: armed, and not yet taken.
 */
bool AlienEngine::hijackWaitingExit(byte submode) {
	if (_room != kWaitingRoom || submode != kWayOutSubmode || _waitingStep != kStepIdle)
		return false;
	if (_script.flag(kNotSummoned) != 0 || _script.flag(kBadge) != 1 ||
		_script.flag(kBeenInside) != 0 || _script.flag(kClickedObj) != kWayOut)
		return false;

	_script.setFlag(kClickedObj, 0);
	CursorMan.showMouse(false);
	_waitingStep = kStepNotice;
	_waitingPos = 0;

	debugC(1, kDebugRooms, "waiting: his number is up, step 0x%02x", kStepNotice);
	return true;
}

/// 0x0e56: a walk that ends on the door to Jack's room.
void AlienEngine::waitingArrival() {
	if (_waitingStep != kStepIdle)
		return;
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;
	if (_script.flag(kClickedObj) != kJackDoor)
		return;

	_script.setFlag(kClickedObj, 0);
	const byte line = _script.flag(kBeenInside) == 1 ? kLineNotAgain : kLineNotYet;
	speakWaiting(line);
	debugC(1, kDebugRooms, "waiting: the door to Jack's room, outcome 0x%02x", line);
}

} // End of namespace Alien
