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
// queue, the hotspot pass, the status line. Four rooms carry something of their
// own there as well: a counter that runs while the player is in the room and
// fires once it is full. They are what makes a room feel inhabited rather than
// painted, and the port ran none of them.
//
// Three of the four are the same machine and are here. The counter is a word in
// the state block, and the room zeroes it as its overlay opens, so it measures
// time in *this* visit and a room left and re-entered starts again -- which is
// why the engine keeps one counter rather than one per room, and resets it in
// loadRoom.
//
//   room  counter   gate         fires at  and then
//   ----  --------  -----------  --------  ----------------------------------
//    8    [0xa710]  tick pair    == 0x5dc  latch [0x33ac] = 1, slot 12
//   14    [0xa73e]  frame (/4)   >  0x00a0 flag [0xa740] = 0x19, slot 0
//   18    [0xa71c]  frame (/4)   >  0x157c flag [0xa71e] = 1, slot 3
//
// The fourth, room 58's [0xa7be] (ovr_3a_101d:0x09cb), is not a clock of this
// shape: it feeds the jail's larger machine, which also runs [0xbefa] outside
// the state block and three CHARANIM calls, and none of that is modelled yet.
//
// The counters live at word addresses inside the state block that RoomScript
// holds as bytes, so they are kept here as a `uint16` instead of as flags.
// Nothing else in the game reads those bytes.

// Room 8, the library: the owl (ovr_08_0e67:0x0d93). Sit in the library long
// enough and the owl on the shelf wakes up -- twelve frames of LIB_OWL.DL1 --
// and [0x33ac] goes to 1, which is the whole point: the owl's hotspot has three
// arms and only the one guarded on [0x33ac] == 1 carries the talk verb
// (hotspots.cpp, obj 4, the arm whose verb comes from the scratch temporary).
// Until the latch is set the owl can only be looked at.
//
// 0x5dc tick pairs is about 43 seconds.
static const int kOwlRoom = 8;
static const uint16 kOwlLimit = 0x5dc;
static const uint16 kOwlLatch = 0x33ac;

// Room 14, the chimney (ovr_0e_0e83:0x0616). Every 160 animation frames -- some
// nine seconds -- four frames of MAF_CHIM.DL1 play and [0xa740] is set to 0x19,
// which the room's own hotspot program then counts back down one per frame. It
// is not a decoration: while [0xa740] is above zero the program writes 5 rather
// than 4 into the scratch temporary [0x9926] (ovr_0e_0e83:0x0410), so the
// chimney hotspot answers with a different outcome for those 25 frames.
static const int kChimneyRoom = 14;
static const uint16 kChimneyLimit = 0x00a0;
static const uint16 kChimneyFlag = 0xa740;
static const byte kChimneyReload = 0x19;

// Room 18, the sitting room (ovr_12_0e6b:0x08b7). A UFO crosses the window --
// UFOFLYBY.DL1, all 78 frames and the erase after them -- once, after 0x157c
// animation frames, something over five minutes in the one room. [0xa71e] both
// gates the counter and remembers that it has happened, so it is a once a game
// event; MAIN clears it for a new game (seg_main.asm:0x092b).
static const int kUfoRoom = 18;
static const uint16 kUfoLimit = 0x157c;
static const uint16 kUfoFlag = 0xa71e;

void AlienEngine::stepRoomClock() {
	// The two gates the rooms count on, in the port's terms: the tick pair is
	// [0xa5fc] and the animation frame is [0xa5f8], which is the pair divided by
	// two again.
	const bool pair = (_tick & 1) == 0;
	const bool frame = (_tick & 3) == 0;

	// Whether this tick moved something a hotspot guard reads.
	bool changed = false;

	switch (_room) {
	case kOwlRoom:
		if (!pair)
			return;
		// The compare is `jne`, not `jbe`: the counter is zeroed on the way
		// past, so the owl wakes once per stay however long the stay is.
		if (++_roomClock != kOwlLimit)
			return;
		_roomClock = 0;
		_anims.play(12, 1, 12, 3, 1);
		_script.setFlag(kOwlLatch, 1);
		changed = true;
		debugC(1, kDebugRooms, "clock: room %d, the owl wakes", kOwlRoom);
		break;

	case kChimneyRoom: {
		if (!frame)
			return;
		_roomClock++;

		// The countdown the puff loaded, one per frame. Its last step matters
		// as much as the puff does: it is what puts the chimney's outcome back.
		const byte held = _script.flag(kChimneyFlag);
		if (held > 0) {
			_script.setFlag(kChimneyFlag, held - 1);
			changed = held == 1;
		}

		if (_roomClock > kChimneyLimit) {
			_script.setFlag(kChimneyFlag, kChimneyReload);
			_roomClock = 0;
			_anims.play(0, 1, 4, 4, 1);
			changed = true;
			debugC(1, kDebugRooms, "clock: room %d, the chimney puffs", kChimneyRoom);
		}
		break;
	}

	case kUfoRoom:
		// Unlike the other two the compare sits outside the gate, and both the
		// count and the fire are held off once the flag is up.
		if (_script.flag(kUfoFlag))
			return;
		if (frame)
			_roomClock++;
		if (_roomClock <= kUfoLimit)
			return;
		_script.setFlag(kUfoFlag, 1);
		_anims.play(3, 1, 0x4f, 4, 1);
		changed = true;
		debugC(1, kDebugRooms, "clock: room %d, the UFO flies by", kUfoRoom);
		break;

	default:
		return;
	}

	if (!changed)
		return;

	// The clock has moved a byte a guard reads, and in the original entry 1
	// runs again on the very next frame -- which is how the owl's talk arm
	// appears without the player having clicked anything. The port registers
	// its rectangles only when something moves the state, so this is one of
	// those moments.
	_script.buildHotspots(_room, _spots);
	_hover = -1;
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	updateHover(mouse.x, mouse.y);
	_dirty = true;
}

} // End of namespace Alien
