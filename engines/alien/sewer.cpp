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

// The sewer hatch, which is what unlocks the front door of the house.
//
// The hall's front door (room 15, obj 7) arms submode 6 -- the link that leads
// to room 27 and the scene the mansion raises as it opens -- but only while
// [0xa6d6] is clear, and [0xa6d6] ships as 1 (seg_main.asm:0x07f9). The one
// writer that clears it in the whole game is the arm of scene 6
// (seg_cutscene.asm:0x19b4), and the only room that raises scene 6 is the sewer
// itself, on the way *back into* it: the trigger's guards are game_mode == 35
// and game_submode == 20 (ovr_23_0e7b:0x049f).
//
// Nothing walks into submode 20. It is armed by the room's own [0xa49f]
// machine, which the hatch starts:
//
//   ovr_23_0e7b:0x00e3  obj 2 under Push: the hatch opens on slot 6, [0xa777]
//                       goes to 1 -- and if scene 6 has not played yet
//                       ([0x339a] == 0) the cursor goes away and the machine
//                       starts at 0x64.
//   ovr_23_0e7b:0x07e8  0x64: once slot 6 has run out ([0xa4f0] == 0), raise
//                       loop_flag and set game_submode to 20, which ends the
//                       room and re-enters it through the chain.
//
// That is the half ported here, because it needs nothing the port does not
// already model: an animation slot's remaining count, and takeExit.
//
// The room's other four states are not here. 3 -> 4 and 7 are the arrival and
// departure animations shared with room 27, and 0x32 -> 0x37 is the ride that
// carries Ben in: it runs on [0x33be], which ships as 1, and while it is set
// the room's own tick drives Ben's position from a table in the overlay's data
// ([0x4362], counted by [0x4380]..[0x4385]) into [0xa8e4], a character global
// the port does not model at all. [0x33be] is also the guard on the hatch's own
// hotspot, so until that ride is understood the hatch cannot actually be
// clicked -- the sequence below is correct and unreachable, the same way the
// jail's machine in roomtick.cpp is left alone rather than half-written.
static const int kSewerRoom = 35;
static const byte kSewerHatch = 2;
static const byte kSewerPush = 12;
static const uint kSewerHatchSlot = 6;
static const uint16 kSewerHatchOpen = 0xa777;
static const uint16 kSewerSceneLatch = 0x339a;
static const byte kSewerLeaving = 0x64;
static const byte kSewerSubmode = 20;

/// The click body has just run: start the machine if it opened the hatch.
void AlienEngine::armSewerHatch(int obj, byte verb) {
	if (_room != kSewerRoom || obj != kSewerHatch || verb != kSewerPush)
		return;

	// [0xa777] is what the body sets as the hatch swings open; pushing it shut
	// again clears the flag and arms nothing.
	if (_script.flag(kSewerHatchOpen) != 1 || _script.flag(kSewerSceneLatch) != 0)
		return;

	_sewerStep = kSewerLeaving;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "sewer: the hatch is open, step 0x%02x", kSewerLeaving);
}

void AlienEngine::stepSewer() {
	if (_sewerStep != kSewerLeaving || _anims.remaining(kSewerHatchSlot) != 0)
		return;

	_sewerStep = 0;
	debugC(1, kDebugRooms, "sewer: submode %d, which raises scene 6", kSewerSubmode);
	takeExit(kSewerSubmode);
}

} // End of namespace Alien
