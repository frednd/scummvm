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

// Room 60, Jack's room -- the boss fight.
//
// This is the one room in the game with no overlay at all: roommap.py's own
// dispatch chains name a resident init (`CUTSCENE:0x0c55`) and a resident
// tick (`1021:sub_1066a`) rather than an overlay stub, and neither this pass
// nor tools/gen_walkgeom.py's table has anything for it -- there is no
// per-room hotspot list to be missing from, because the whole encounter is
// scripted code rather than a room script. Reverse-engineering the fight
// itself (the three BOSSGAM*.DL1 animation sets, the ball, whatever decides a
// hit) is well past what this leg had time for.
//
// Ported only for reachability, the way rooms 51-58's own missing exits are:
// a plain click, which meets no hotspot here because none are registered,
// stands in for winning the fight and leaves the only way transitions.cpp
// gives this room (submode 1 -> room 54).
static const int kBossRoom = 60;

static const byte kBossClicks = 3;
static const byte kWaitingExitSubmode = 1;	///< transitions.cpp: room 60, submode 1 -> room 54

/// Every arrival resets the click count.
void AlienEngine::startBoss() {
	if (_room != kBossRoom)
		return;

	_bossClicks = 0;
}

/// A plain click, standing in for winning the fight: see this file's header.
bool AlienEngine::armBoss() {
	if (_room != kBossRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_bossClicks++;
	debugC(1, kDebugRooms, "boss: step %u of %u", _bossClicks, kBossClicks);

	if (_bossClicks >= kBossClicks) {
		_bossClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kWaitingExitSubmode);
	}

	return true;
}

} // End of namespace Alien
