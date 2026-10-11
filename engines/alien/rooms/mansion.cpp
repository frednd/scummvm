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

// The other end of the sewer's manhole. Room 27's open, arriving from there
// (game_mode 35, ovr_1b_0e7f:0x03c1), plays the climb out on slot 1 -- the
// whole of it the first time, [0xa77a], its tail after -- and puts the
// character down at 200,72, all of which the lifted init carries. What it does
// not carry is the rest of that arm: [0xa948] = 0, [0xa94d] = 0 and [0xa49f]
// = 3, the machine whose one step (0x05eb) gives the character and the cursor
// back once slot 1 has one frame left. Without it the walker stood at the top
// of the manhole while the climb played under him -- two of him (#8).
static const int kMansionRoom = 27;
static const byte kFromSewer = 35;
static const uint kManholeSlot = 1;
static const byte kMansionClimbing = 3;

void AlienEngine::enterMansion(int room) {
	if (room != kMansionRoom || _mode != kFromSewer)
		return;

	hideCharacter();
	CursorMan.showMouse(false);
	_mansionStep = kMansionClimbing;
	debugC(1, kDebugRooms, "mansion: up through the manhole");
}

void AlienEngine::stepMansion() {
	if (_room != kMansionRoom || _mansionStep != kMansionClimbing)
		return;
	// The original waits for exactly one frame left; a count already past it
	// (a slot the scene zeroed) must not strand him either.
	if (_anims.remaining(kManholeSlot) > 1)
		return;

	_mansionStep = 0;
	showCharacter();
	CursorMan.showMouse(true);
	debugC(1, kDebugRooms, "mansion: out of the manhole");
}

} // End of namespace Alien
