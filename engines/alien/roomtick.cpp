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

#include "common/events.h"
#include "common/system.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// The room clocks: what a room does on its own, with nobody touching it.
//
// Every scene overlay's entry 2 is a per-frame body, and most of what it holds
// the port already runs from somewhere else -- the animation stepper, the sound
// queue, the hotspot pass, the status line. Five rooms carry something of their
// own there as well: a counter that runs while the player is in the room and
// fires once it is full. They are what makes a room feel inhabited rather than
// painted, and the port ran none of them.
//
// Four of the five are the same machine, and each is its room's roomClock()
// (library.cpp, basement.cpp, chimney.cpp, ufo.cpp), beside room 7's count in
// the dark (bedroom.cpp) and room 40's side flag (cave.cpp). The counter is a word in
// the state block, kept there at its own address so a savegame holds what the
// original's does. Rooms 8, 14 and 18 zero theirs as the overlay opens, so it
// measures time in *this* visit and loadRoom zeroes it the same way; room 13's
// is zeroed only on a new game, so the basement's drip keeps its phase across visits.
//
//   room  counter   gate         fires at  and then
//   ----  --------  -----------  --------  ----------------------------------
//    8    [0xa710]  tick pair    == 0x5dc  latch [0x33ac] = 1, slot 12
//   13    [0xa6ce]  tick pair    >  0x012c slot 4, 17 frames once
//   14    [0xa73e]  frame (/4)   >  0x00a0 flag [0xa740] = 0x19, slot 0
//   18    [0xa71c]  frame (/4)   >  0x157c flag [0xa71e] = 1, slot 3
//
// The fourth, room 58's [0xa7be] (ovr_3a_101d:0x09cb), is not a clock of this
// shape: it feeds the jail's larger machine, which also runs [0xbefa] outside
// the state block and three CHARANIM calls, and none of that is modelled yet.
//
// The counters are words inside the state block that RoomScript holds as bytes;
// stateWord() and setStateWord() put the two halves together.

uint16 AlienEngine::stateWord(uint16 addr) const {
	return _script.flag(addr) | _script.flag(addr + 1) << 8;
}

void AlienEngine::setStateWord(uint16 addr, uint16 value) {
	_script.setFlag(addr, value & 0xff);
	_script.setFlag(addr + 1, value >> 8);
}

void AlienEngine::stepRoomClock() {
	// Whether this tick moved something a hotspot guard reads.
	bool changed = false;
	for (Room *r : _rooms)
		changed = r->roomClock() || changed;
	if (!changed)
		return;

	// The clock has moved a byte a guard reads, and in the original entry 1
	// runs again on the very next frame -- which is how the owl's talk arm
	// appears without the player having clicked anything. The port registers
	// its rectangles only when something moves the state, so this is one of
	// those moments.
	rebuildHotspots();
	_hover = -1;
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	updateHover(mouse.x, mouse.y);
	_dirty = true;
}

} // End of namespace Alien
