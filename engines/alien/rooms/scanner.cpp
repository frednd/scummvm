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
#include "alien/rooms/scanner.h"

namespace Alien {

// Room 52, the security scanner between the ship's corridor and the
// transporter chamber.
//
// The room has no dispatch of its own: MAIN:sub_001eb stands in front of its
// overlay, and it is that routine, not anything clicked in the room, which is
// the scan. Every entry runs it:
//
//   [0xa79a] == 1              scanned already: the room, as it is
//   [0xa79a] == 0, no mask     [0xa7ad] < 2: the scan without the mask
//                              (CUTSCENE:sub_0e2a0(0)), [0x33e3] := 1, then
//                              the room -- whose init and tick are the arrest
//                              [0xa7ad] >= 2: straight to the jail, room 58,
//                              with [0xa7ae] and [0xa7b1] raised
//   [0xa79a] == 0, mask on     the scan with the mask (sub_0e2a0(1)),
//                              [0xa79a] := 1, [0x33e1] := 1, then the room
//
// [0xa79b] is the mask-is-on flag room 56 writes (input.cpp), [0xa7ad] the
// arrest count room 58 raises (jail.cpp, job 13).
//
// The scan itself is SEC_SCR1.DL2 or SEC_SCR2.DL2 over SEC_SCAN.PCX or
// SEC_SCA2.PCX, music slot 12: one frame every animation tick pair
// (CUTSCENE:sub_0e253 with [0xa4fa] = 0), each painted over what the last one
// left -- the frames are deltas, not cels, and MIDAS:snd_play copies their
// spans with rep movsw, colour 0 included: a zero is black that clears the
// last frame's pixels, not a hole -- until the file's frame count is
// reached ([0x8cd6], which the DL2 loader leaves) or both joystick buttons are
// held ([0xa900]/[0xa901], CUTSCENE:sub_0c550). Either way it leaves
// [0x33e2] := 1, which the room's init reads to put Ben at the scanner's far
// door when the robot is not waiting for him.
//
// With [0x33e3] standing the room's init (roominit.cpp) plays the robot --
// SEC_NORO frame 1, SEC_ROBO's forty frames -- and the overlay raises its own
// [0xa49f] machine with the cursor gone (ovr_34_0f96:0x05ee):
//
//   3     SEC_ROBO has run out ([0xa4f0]): outcome 0x1e at (0xa0, 0x32),
//         "You have been classified as an illegal alien..."
//   0xa   the line is down ([0xad1c]): [0xa7ae] := 1, [0xa7b1] := 1,
//         game_submode 0x6f -- transitions.cpp's room 52, submode 111 ->
//         room 58, the jail
static const int kScannerRoom = 52;
static const int kJailRoom = 58;

static const uint16 kScanned = 0xa79a;		///< the scan has passed him
static const uint16 kMaskOn = 0xa79b;
static const uint16 kArrests = 0xa7ad;		///< times he has been jailed
static const uint16 kJailArrival = 0xa7ae;
static const uint16 kJailGate = 0xa7b1;
static const uint16 kPassedLatch = 0x33e1;
static const uint16 kScanPlayed = 0x33e2;
static const uint16 kArrestLatch = 0x33e3;
static const uint16 kMachine = 0xa49f;

static const uint kRobotSlot = 6;			///< SEC_ROBO, [0xa4f0]
static const byte kArrestLine = 0x1e;
static const int kArrestX = 0xa0;
static const int kArrestY = 0x32;
static const byte kJailSubmode = 111;		///< transitions.cpp: room 52, submode 111 -> room 58

static const byte kStepArrest = 3;
static const byte kStepLine = 0xa;

static const uint kScanMusic = 12;

ScannerRoom::ScannerRoom(AlienEngine *vm) : Room(vm),
		_scannerArrest(false), _scannerStep(0) {
}

int ScannerRoom::redirect(int room) {
	if (room != kScannerRoom || _vm->_script.flag(kScanned) == 1)
		return room;

	if (_vm->_script.flag(kMaskOn) == 1) {
		playScannerScan(true);
		_vm->_script.setFlag(kScanned, 1);
		_vm->_script.setFlag(kPassedLatch, 1);
		debugC(1, kDebugRooms, "scanner: the mask passes the scan");
		return room;
	}

	if (_vm->_script.flag(kArrests) < 2) {
		playScannerScan(false);
		_vm->_script.setFlag(kArrestLatch, 1);
		_scannerArrest = true;
		debugC(1, kDebugRooms, "scanner: no mask, the robot is waiting");
		return room;
	}

	// 0x0227: jailed twice already, and the scan is not played a third time.
	_vm->_script.setFlag(kJailArrival, 1);
	_vm->_script.setFlag(kJailGate, 1);
	debugC(1, kDebugRooms, "scanner: arrest %u, straight to the jail",
		   _vm->_script.flag(kArrests) + 1);
	return kJailRoom;
}

/**
 * CUTSCENE:sub_0e2a0: the scan, a DL2 played frame by frame over its plate.
 *
 * Blocks, the way every scene out of the CUTSCENE segment does. Nothing of the
 * room is loaded yet -- the scan runs in front of the room's own dispatch -- so
 * there is nothing to put back afterwards: loadRoom() goes on to load the room.
 */
void ScannerRoom::playScannerScan(bool mask) {
	_vm->playDl2Clip(mask ? "SEC_SCR2.DL2" : "SEC_SCR1.DL2", mask ? "SEC_SCA2.PCX" : "SEC_SCAN.PCX",
				kScanMusic, mask ? "scan-2" : "scan-1");
	_vm->_script.setFlag(kScanPlayed, 1);
}

/// The overlay's own arm for [0x33e3], ovr_34_0f96:0x05ee; the init effects
/// before it already cleared the latch and put the robot up.
void ScannerRoom::start() {
	_scannerStep = 0;
	if (_vm->_room != kScannerRoom || !_scannerArrest)
		return;

	_scannerArrest = false;
	// 0x05e4: and the camera held at the room's left edge for the rest of
	// the visit.
	_vm->_scrollHold = 0;
	_scannerStep = kStepArrest;
	_vm->_script.setFlag(kMachine, kStepArrest);
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "scanner: the robot comes for him");
}

/// ovr_34_0f96:0x0853, the room's [0xa49f] machine.
void ScannerRoom::tickPair() {
	if (_vm->_room != kScannerRoom || !_scannerStep)
		return;

	switch (_scannerStep) {
	case kStepArrest:
		if (_vm->_anims.remaining(kRobotSlot) != 0)
			break;
		_vm->queueOutcome(_vm->_tal, kArrestLine, kArrestX, kArrestY, false);
		_scannerStep = kStepLine;
		_vm->_script.setFlag(kMachine, kStepLine);
		break;

	case kStepLine:
		if (_vm->_speech || _vm->_queueNext < _vm->_queueCount)
			break;
		_scannerStep = 0;
		_vm->_script.setFlag(kMachine, 0);
		_vm->_script.setFlag(kJailArrival, 1);
		_vm->_script.setFlag(kJailGate, 1);
		debugC(1, kDebugRooms, "scanner: arrested, to the jail");
		_vm->takeExit(kJailSubmode);
		// The jail's own machine is what gives the cursor back in the
		// original; until that is ported (job 13) the port does it here.
		CursorMan.showMouse(true);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
