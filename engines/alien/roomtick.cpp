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
// Four of the five are the same machine and are here. The counter is a word in
// the state block, kept there at its own address so a save holds what the
// original's does (dosbox state parity). Rooms 8, 14 and 18 zero theirs as the
// overlay opens, so it measures time in *this* visit and loadRoom zeroes it the
// same way; room 13's is zeroed only by MAIN for a new game
// (seg_main.asm:0x07e2), so the basement's drip keeps its phase across visits.
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

// Room 8, the library: the owl (ovr_08_0e67:0x0d93). Sit in the library long
// enough and the owl on the shelf wakes up -- twelve frames of LIB_OWL.DL1 --
// and [0x33ac] goes to 1, which is the whole point: the owl's hotspot has three
// arms and only the one guarded on [0x33ac] == 1 carries the talk verb
// (hotspots.cpp, obj 4, the arm whose verb comes from the scratch temporary).
// Until the latch is set the owl can only be looked at.
//
// 0x5dc tick pairs is about 43 seconds.
static const int kOwlRoom = 8;
static const uint16 kOwlClock = 0xa710;
static const uint16 kOwlLimit = 0x5dc;
static const uint16 kOwlLatch = 0x33ac;

// Room 14, the chimney (ovr_0e_0e83:0x0616). Every 160 animation frames -- some
// nine seconds -- four frames of MAF_CHIM.DL1 play and [0xa740] is set to 0x19,
// which the room's own hotspot program then counts back down one per frame. It
// is not a decoration: while [0xa740] is above zero the program writes 5 rather
// than 4 into the scratch temporary [0x9926] (ovr_0e_0e83:0x0410), so the
// chimney hotspot answers with a different outcome for those 25 frames.
static const int kChimneyRoom = 14;
static const uint16 kChimneyClock = 0xa73e;
static const uint16 kChimneyLimit = 0x00a0;
static const uint16 kChimneyFlag = 0xa740;
static const byte kChimneyReload = 0x19;

// Room 18, the sitting room (ovr_12_0e6b:0x08b7). A UFO crosses the window --
// UFOFLYBY.DL1, all 78 frames and the erase after them -- once, after 0x157c
// animation frames, something over five minutes in the one room. [0xa71e] both
// gates the counter and remembers that it has happened, so it is a once a game
// event; MAIN clears it for a new game (seg_main.asm:0x092b).
static const int kUfoRoom = 18;
static const uint16 kUfoClock = 0xa71c;
static const uint16 kUfoLimit = 0x157c;
static const uint16 kUfoFlag = 0xa71e;

// Room 13, the basement (ovr_0d_0e6f:0x08f4). Every 0x12d tick pairs, some
// eight seconds, slot 4 -- BAS_WATE.DL1 -- plays its seventeen frames once,
// through MIDAS:sub_18810. Nothing reads the counter or the slot; it is the
// water.
static const int kDripRoom = 13;
static const uint16 kDripClock = 0xa6ce;
static const uint16 kDripLimit = 0x012c;

// Room 7, the bedroom in the dark (ovr_07_0e63:0x0cca, LOGIC:sub_126af). While
// the light is off ([0xa6fa] == 0) every frame he spends where he was the
// frame before counts in [0x33aa], and any step puts it back to zero. At
// 0x1068 -- about a minute -- he says "Standing here in the dark is giving me
// the creeps" (queue_event 42), and at 0x15e0 "I want to get out of here"
// (43), which also starts the count again. The port never said either.
static const int kDarkRoom = 7;
static const uint16 kDarkLight = 0xa6fa;
static const uint16 kDarkClock = 0x33aa;
static const uint16 kDarkFirst = 0x1068;
static const uint16 kDarkSecond = 0x15e0;
static const byte kDarkLine1 = 42;
static const byte kDarkLine2 = 43;

// Room 40, the cave entrance (ovr_28_0ebb:0x04a7). Every frame [0xa78c] says
// whether his sprite is right of 0x88, and entry 0 reads it to choose where a
// click on the hole (obj 4) walks him: from the left, 0xae,0x7f facing right;
// from the right, 0x75,0x91 facing left (0x01e5-0x021c, a walk_to_object on
// [0x7dbc]/[0x7dbe]/[0x7db8], which the walk geometry lift cannot carry). The
// port walked him to the click.
static const int kCaveRoom = 40;
static const uint16 kCaveRight = 0xa78c;
static const int kCaveSideX = 0x88;
static const byte kCaveHole = 4;

void AlienEngine::caveWalkTarget(byte obj, WalkTarget &target) {
	if (_room != kCaveRoom)
		return;
	const bool right = _ben.spriteX() >= kCaveSideX;
	_script.setFlag(kCaveRight, right ? 1 : 0);
	if (obj != kCaveHole)
		return;

	target.x = right ? 0x75 : 0xae;
	target.y = right ? 0x91 : 0x7f;
	target.facing = right ? 4 : 2;
}

uint16 AlienEngine::stateWord(uint16 addr) const {
	return _script.flag(addr) | _script.flag(addr + 1) << 8;
}

void AlienEngine::setStateWord(uint16 addr, uint16 value) {
	_script.setFlag(addr, value & 0xff);
	_script.setFlag(addr + 1, value >> 8);
}

void AlienEngine::resetRoomClock(int room) {
	switch (room) {
	case kOwlRoom:
		setStateWord(kOwlClock, 0);
		break;
	case kChimneyRoom:
		setStateWord(kChimneyClock, 0);
		break;
	case kUfoRoom:
		setStateWord(kUfoClock, 0);
		break;
	default:
		break;
	}
}

void AlienEngine::stepRoomClock() {
	// The two gates the rooms count on, in the port's terms: the tick pair is
	// [0xa5fc] and the animation frame is [0xa5f8], which is the pair divided by
	// two again.
	const bool pair = (_tick & 1) == 0;
	const bool frame = (_tick & 3) == 0;

	// Whether this tick moved something a hotspot guard reads.
	bool changed = false;

	if (_room == kCaveRoom)
		_script.setFlag(kCaveRight, _ben.spriteX() >= kCaveSideX ? 1 : 0);

	switch (_room) {
	case kDarkRoom: {
		if (_script.flag(kDarkLight) != 0)
			return;
		// [0xa8f0]/[0xa8f2], the sprite as it was: the count is "still", not
		// "idle", so turning on the spot does not reset it.
		const bool still = _ben.spriteX() == _darkLastX && _ben.spriteY() == _darkLastY;
		_darkLastX = _ben.spriteX();
		_darkLastY = _ben.spriteY();
		if (!still) {
			setStateWord(kDarkClock, 0);
			return;
		}
		// The original counts every frame and this runs every other one; both
		// limits are even, so two at a time lands on them.
		const uint16 count = stateWord(kDarkClock) + 2;
		setStateWord(kDarkClock, count);
		if (count != kDarkFirst && count != kDarkSecond)
			return;
		int x, y;
		characterAnchor(x, y);
		queueOutcome(_tal, count == kDarkFirst ? kDarkLine1 : kDarkLine2, x, y);
		if (count == kDarkSecond)
			setStateWord(kDarkClock, 0);
		debugC(1, kDebugRooms, "clock: room %d, line %d in the dark", kDarkRoom,
			   count == kDarkFirst ? kDarkLine1 : kDarkLine2);
		return;
	}

	case kOwlRoom:
		if (!pair)
			return;
		// The compare is `jne`, not `jbe`: the counter is zeroed on the way
		// past, so the owl wakes once per stay however long the stay is.
		setStateWord(kOwlClock, stateWord(kOwlClock) + 1);
		if (stateWord(kOwlClock) != kOwlLimit)
			return;
		setStateWord(kOwlClock, 0);
		_anims.play(12, 1, 12, 3, 1);
		_script.setFlag(kOwlLatch, 1);
		changed = true;
		debugC(1, kDebugRooms, "clock: room %d, the owl wakes", kOwlRoom);
		break;

	case kDripRoom:
		if (!pair)
			return;
		setStateWord(kDripClock, stateWord(kDripClock) + 1);
		if (stateWord(kDripClock) <= kDripLimit)
			return;
		setStateWord(kDripClock, 0);
		_anims.play(4, 1, 0x11, 2, 4);
		return;

	case kChimneyRoom: {
		if (!frame)
			return;
		setStateWord(kChimneyClock, stateWord(kChimneyClock) + 1);

		// The countdown the puff loaded, one per frame. Its last step matters
		// as much as the puff does: it is what puts the chimney's outcome back.
		const byte held = _script.flag(kChimneyFlag);
		if (held > 0) {
			_script.setFlag(kChimneyFlag, held - 1);
			changed = held == 1;
		}

		if (stateWord(kChimneyClock) > kChimneyLimit) {
			_script.setFlag(kChimneyFlag, kChimneyReload);
			setStateWord(kChimneyClock, 0);
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
			setStateWord(kUfoClock, stateWord(kUfoClock) + 1);
		if (stateWord(kUfoClock) <= kUfoLimit)
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
	rebuildHotspots();
	_hover = -1;
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	updateHover(mouse.x, mouse.y);
	_dirty = true;
}

} // End of namespace Alien
