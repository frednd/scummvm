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

#include "common/file.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Rooms 43/44/45, the maze between the shore and the crystal chamber --
// ported here as a simplified straight-line walk rather than the real
// self-routing engine.
//
// `disasm/ovr_2b_0f8d_...asm` is one overlay standing in for three room
// numbers, and unlike every other room in the game its position is not the
// room number at all: [0xa77c] is a maze-cell index, `imul di, ax, 7` into a
// seven-byte-stride per-cell table (two of them -- 0x6450 for room 43's own
// view, 0x65dc for room 44's -- one per physical maze section), each cell's
// bytes gating which of up to six directional arrows entry 1/2 register
// (`HOTSPOT:sub_13731`/`sub_1387d`/`sub_138f6`) and which background/DL1 set
// the room's own init swaps in. Reaching cell 0x23 while showing as room 43
// runs a seven-step [0xa49f] cutscene of its own (0x722-0x7ec): Ben turns,
// walks to the wall, and finds the pick-axe (`ROOM43.TAL` outcomes 22 "What
// the..." and 23 "Wow..."), then arms submode 5 -- which `seg_main.asm`
// rewrites to 10 before the transition chain runs, landing on room 45
// (`transitions.cpp`: room 43 submode 10 -> room 45).
//
// `transitions.cpp` also settles which of the two maze rooms matters: room 41
// (the shore) submode 3 opens on room 43, and both of room 44's own exits
// (submodes 1 and 2) lead straight back to room 41. So 44 is a loop -- a wrong
// turn out of 43's cell graph that returns to the entrance rather than a
// second leg of forward progress -- and room 45 has no tick entry at all
// (`docs/rooms.md`: "Room 45 is the only one with an init but no tick"), so it
// asks nothing further of the port once it can be reached.
//
// Modelling the real per-cell table -- both rooms' worth of it, the six
// direction rectangles per cell, and the turn-by-turn route a walkthrough
// gives as compass directions rather than cell numbers -- is a job of its own
// (docs/playthrough_findings.md finding #7's "bigger lift than the guard/effect
// tables roomlogic.py already lifts"). This is the simplified stand-in: room
// 44 is left unvisited, and room 43 counts plain clicks instead of resolving
// which of the cell table's arrows was under the cursor, ending in the same
// two lines and the same axe the original does.
static const int kMazeRoom = 43;

/// However many turns the real maze takes to cross, this is not: a click
/// count standing in for it.
static const uint kMazeSteps = 6;

static const byte kAxe = 39;				///< OBJ:sprite_add, item 39, "pick-axe"
static const byte kOutcomeFound = 22;		///< ROOM43.TAL, "What the..."
static const byte kOutcomeWow = 23;		///< ROOM43.TAL, "Wow..."
static const byte kMazeExitSubmode = 10;	///< transitions.cpp: room 43, submode 10 -> room 45

static const byte kStepFound = 1;
static const byte kStepWow = 2;

/// Every arrival resets the click count: the real room only keeps [0xa77c]
/// across a re-entry the simplified path never takes.
void AlienEngine::startMaze() {
	if (_room != kMazeRoom)
		return;

	_mazeClicks = 0;
	_mazeStep = 0;

	// The generic room open reads one script name out of the shared overlay's
	// own manifest and gets room 45's rather than 43's -- the overlay serves
	// three room numbers and the reader was never written to tell them apart.
	// Load the right one by hand, the way hippie.cpp corrects for the six
	// files room 23's conversation swaps between.
	const Common::Path script("room43.tal");
	if (Common::File::exists(script))
		_tal.load(script, &_pack);
}

/**
 * One click, one turn: the simplified stand-in for resolving which of the
 * cell table's directional arrows the cursor was over.
 *
 * Returns true once the maze has taken the click, the way the owl and Sluggs
 * do, so it is never also read as a walk.
 */
bool AlienEngine::armMaze() {
	if (_room != kMazeRoom || _mazeStep)
		return false;

	_mazeClicks++;
	debugC(1, kDebugRooms, "maze: turn %u of %u", _mazeClicks, kMazeSteps);

	if (_mazeClicks >= kMazeSteps) {
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		_inventory.add(kAxe);
		queueOutcome(_tal, kOutcomeFound, anchorX, anchorY);
		_mazeStep = kStepFound;
		debugC(1, kDebugItems, "maze: finds item %u (%s)", kAxe, _inventory.name(kAxe).c_str());
	}

	return true;
}

/// The two lines the original speaks over finding the axe, then the door.
void AlienEngine::stepMaze() {
	if (_room != kMazeRoom || !_mazeStep || !speechDone())
		return;

	if (_mazeStep == kStepFound) {
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeWow, anchorX, anchorY);
		_mazeStep = kStepWow;
	} else if (_mazeStep == kStepWow) {
		_mazeStep = 0;
		takeExit(kMazeExitSubmode);
	}
}

} // End of namespace Alien
