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

// Room 54, the waiting room, and the ticket machine and number board between
// it and Jack's room.
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
// Only the call in to Jack's room is not, and that one is still a stand-in:
// the original hijacks the same object-1 arrival (0x0e9b) and only when
// [0xa7d7] stands -- the maintenance man's badge, item 44, which is granted by
// the hallways' own machine and is job 15's, not this one's. Until then the
// walk in is ported the way room 52's scan is, a plain click with nothing held
// and no hotspot under it.
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

static const byte kBoardClicks = 3;
static const byte kJackExitSubmode = 50;	///< transitions.cpp: room 54, submode 50 -> room 60

/// Every arrival resets the click count, and answers the self-link.
void AlienEngine::startWaiting() {
	if (_room != kWaitingRoom)
		return;

	_waitingClicks = 0;
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
	if (_room != kWaitingRoom || _waitingStep == kStepIdle)
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

	default:
		break;
	}
}

/// A plain click, standing in for holding the right number when it is called.
bool AlienEngine::armWaitingBoard() {
	if (_room != kWaitingRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;
	// Not while the ticket machine owns the room.
	if (_waitingStep != kStepIdle)
		return false;

	_waitingClicks++;
	debugC(1, kDebugRooms, "waiting: step %u of %u until the number is called", _waitingClicks,
		   kBoardClicks);

	if (_waitingClicks >= kBoardClicks) {
		_waitingClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kJackExitSubmode);
	}

	return true;
}

} // End of namespace Alien
