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

// Room 52, the security scanner between the park teleporter and the jail.
//
// tools/roomlogic.py has no rows at all for this room -- its own dispatch is
// a resident MAIN routine rather than the overlay directly (roommap.py:
// "MAIN:sub_001eb"), the same shape of gap room 43/44's plate lookup was, and
// the pumpkin mask the walkthrough scans through it is a puzzle this pass did
// not resolve: which item, which guard, and the animation that plays while
// the scan runs.
//
// Its two ordinary exits (transitions.cpp: room 52, submode 1 -> room 56,
// submode 2 -> room 55) are already armed generically -- both are
// kWalkSubmode rows on real, registered hotspots (objects 3 and 1). Only the
// scanner's own way through, submode 111 -> room 58, is not: ported the same
// way room 41's door and room 46's swim are, a plain click with nothing held
// and no hotspot under it standing in for the mask and the scan.
static const int kScannerRoom = 52;

static const byte kScanClicks = 3;
static const byte kJailExitSubmode = 111;	///< transitions.cpp: room 52, submode 111 -> room 58

/// Every arrival resets the click count.
void AlienEngine::startScanner() {
	if (_room != kScannerRoom)
		return;

	_scannerClicks = 0;
}

/// A plain click, standing in for the mask and the scan: see this file's
/// header.
bool AlienEngine::armScanner() {
	if (_room != kScannerRoom || _heldItem != Inventory::kNoItem || _hover >= 0)
		return false;

	_scannerClicks++;
	debugC(1, kDebugRooms, "scanner: step %u of %u through to the jail", _scannerClicks, kScanClicks);

	if (_scannerClicks >= kScanClicks) {
		_scannerClicks = 0;
		CursorMan.showMouse(false);
		takeExit(kJailExitSubmode);
	}

	return true;
}

} // End of namespace Alien
