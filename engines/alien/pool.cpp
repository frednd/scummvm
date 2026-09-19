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

// Room 48, the ship's upper level, and the pool the diving suit goes into.
//
// tools/roomlogic.py lifted the room's one script row -- the old key opening
// the panel on object 2 (item 38, roomscripts.json block 162) -- complete
// with its animation and sound, because that body is an ordinary outcome
// chain. What it has nothing for is object 1, the pool itself: the diving
// suit answers it with `[0xa49f]` states 3/4/5/7 (ovr_30_0ebf:0x0511 onward),
// which is a machine, not a row.
//
// [0x33dc] is whether the dive has been made before: the first time speaks
// outcome 0x11 ("I hope it's not freezing down there...") and waits for it;
// every time after skips straight to the plunge. Either way the same play
// follows -- slot 3, eighteen frames, mode 1 -- and the room ends itself once
// the last frame is up, arming submode 2 into room 46 the way `game_submode`
// is written directly rather than through an approach point (matching
// sluggs.cpp and maze.cpp's own exits).
//
// The original also answers walking into the pool without the suit (a
// `[0x9908]`/`[0xa644]` LOS-blocked check further down, `sprite_find_slot(28)`
// deciding whether outcome 0x10's "I'd need a diving suit" plays instead of
// starting the same machine) -- left unanswered here, the way room 40's pipe
// and matches are: the item use alone is the way every walkthrough takes it.
static const int kPoolRoom = 48;

static const byte kPoolObj = 1;
static const byte kSuit = 28;

static const uint16 kFirstDive = 0x33dc;

static const uint kDiveSlot = 3;
static const int kDiveFrames = 0x12;
static const int kDiveRate = 3;
static const int kDiveMode = 1;

static const byte kOutcomeFirstDive = 0x11;
static const byte kPoolExitSubmode = 2;	///< transitions.cpp: room 48, submode 2 -> room 46

static const byte kStepStart = 3;
static const byte kStepWaitLine = 4;
static const byte kStepPlain = 5;
static const byte kStepDiving = 7;

/// The diving suit, used on the pool: entry 3's one machine-shaped arm.
bool AlienEngine::armPool(int obj, byte item) {
	if (_room != kPoolRoom || obj != kPoolObj || item != kSuit)
		return false;

	_poolStep = kStepStart;
	debugC(1, kDebugRooms, "pool: the suit goes on");
	return true;
}

/// The room's own [0xa49f] machine: the line the first dive gets, the play,
/// and the exit it ends in.
void AlienEngine::stepPool() {
	if (_room != kPoolRoom || !_poolStep)
		return;

	switch (_poolStep) {
	case kStepStart: {
		// 0x0511: which of the two follows depends on whether this is the
		// first time, and either way the flag is set before the play starts.
		const bool first = _script.flag(kFirstDive) == 0;
		_script.setFlag(kFirstDive, 1);
		CursorMan.showMouse(false);

		if (first) {
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kOutcomeFirstDive, anchorX, anchorY);
			_poolStep = kStepWaitLine;
		} else {
			_poolStep = kStepPlain;
		}
		break;
	}

	case kStepWaitLine:
		// 0x0542: the line comes down before the plunge.
		if (!speechDone())
			break;
		playCharacterAnim(kDiveSlot, 1, kDiveFrames, kDiveRate, kDiveMode);
		_poolStep = kStepDiving;
		break;

	case kStepPlain:
		// 0x0562: every dive after the first skips straight to it.
		playCharacterAnim(kDiveSlot, 1, kDiveFrames, kDiveRate, kDiveMode);
		_poolStep = kStepDiving;
		break;

	case kStepDiving:
		// 0x057f: the last frame of the plunge is the room's own exit.
		if (_anims.remaining(kDiveSlot) != 1)
			break;
		_poolStep = 0;
		takeExit(kPoolExitSubmode);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
