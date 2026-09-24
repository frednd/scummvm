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
static const uint16 kBoardLeft = 0xa7dc;	///< objects 9 and 10, the board
static const uint16 kBoardRight = 0xa7dd;
static const uint16 kDeskOpen = 0xa7de;	///< object 14, once he is called

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

/// The print itself, the one real play in the sequence (0x0bf6).
static const byte kPrintFirst = 5, kPrintCount = 0x11, kPrintRate = 2;

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

/// MIDAS:sub_18962(5, 0, 0xc, 9, ds:0x6fb8): the board, mode 6 over this list.
static const uint kBoardSlot = 5;
static const byte kBoardFrames[] = { 1, 2, 1, 2, 1, 3, 1, 3, 1, 3, 1, 3 };
static const int kBoardRate = 9, kBoardMode = 6;

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

/// Every arrival answers the self-link.
void AlienEngine::startWaiting() {
	if (_room != kWaitingRoom)
		return;

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
		_anims.play(kButtonSlot, kPrintFirst, kPrintCount, kPrintRate, 1);
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
		_script.setFlag(kBoardLeft, 0);
		_script.setFlag(kBoardRight, 0);
		_script.setFlag(kDeskOpen, 1);
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
