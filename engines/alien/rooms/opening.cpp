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

#include "backends/graphics/graphics.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/charanim.h"
#include "alien/detection.h"
#include "alien/rooms/opening.h"

namespace Alien {

// How the game opens: the monologue in the uncle's lab.
//
// The flag [0x7dc4] is set at startup and cleared only on restoring a save,
// so the flag means "this is a new game and nothing has been played yet". Room 3's opening subroutine reads it at the very end of its work
// (ovr_03_0e57, 0x0afd): it hides the cursor and starts the room's [0xa49f]
// machine at 0x64.
//
// That machine is room 3's per-frame entry, 0x0dc4-0x0e75, and it carries five
// unrelated sequences on the one byte. Only the two steps below belong to the
// opening; the others are armed by the room's own hotspot bodies -- 3 and 5 by
// the cupboard, 0x1e by the drawer, 0x3c by the terminal -- and wait on
// animation-slot states the port does not model yet. They are left alone here
// rather than half-written.
//
//   0x64  speak outcome 0x50                                     -> 0x6e
//   0x6e  the line has come down ([0xad1c] == 1): clear [0x7dc4],
//         give the cursor back                                   -> 0
//
// The character's own placement is not part of this: the room's opening effects
// stand him at (135, 58) unconditionally, which roominit.cpp already carries.
static const int kOpeningRoom = 3;
static const byte kOpeningLine = 0x50;

OpeningRoom::OpeningRoom(AlienEngine *vm) : Room(vm),
		_openingStep(0), _openingPending(true) {
}

void OpeningRoom::startOpening() {
	if (_vm->_room != kOpeningRoom || !_openingPending)
		return;

	_openingStep = 0x64;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "opening: room %d, step 0x64", kOpeningRoom);
}

void OpeningRoom::stepOpening() {
	if (!_openingStep)
		return;

	switch (_openingStep) {
	case 0x64: {
		int anchorX, anchorY;
		_vm->characterAnchor(anchorX, anchorY);
		_vm->queueOutcome(_vm->_tal, kOpeningLine, anchorX, anchorY);
		_openingStep = 0x6e;
		break;
	}

	case 0x6e:
		// [0xad1c], the pulse the dialog unit leaves when a line clears and
		// nothing in the chain follows it.
		if (!_vm->_speech && _vm->_queueNext >= _vm->_queueCount) {
			_openingPending = false;
			_openingStep = 0;
			CursorMan.showMouse(true);
			debugC(1, kDebugRooms, "opening: done, the cursor is back");
		}
		break;

	default:
		break;
	}
}

/// Drop the opening, cursor and all: a restored game has already had it.
void OpeningRoom::cancelOpening() {
	if (_openingStep)
		CursorMan.showMouse(true);
	_openingStep = 0;
	_openingPending = false;
}

} // End of namespace Alien
