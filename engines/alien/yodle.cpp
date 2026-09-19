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

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Room 21, Yodle's tree hut, and the picklock the diving area's chest needs.
//
// A talk on Yodle (object 12, entry 3's plain-verb half,
// `ovr_15_0ea7:0x04e6`) is not a conversation menu but the same two-speaker
// runner sluggs.cpp already carries (`DLGREQ:sub_0c4d1`), reading one of five
// swapped files (YODTAL1/YODSHIT1-3/YODTAL4.TAL, off `[0xa74d]`/`[0xa74e]`)
// through a helper (`ovr0ea7_sub_01d8`) that plays out over `[0xa4a2]`, a
// running outcome counter each tick advances: object 12's own exchange is
// seven lines long, and thirty-six ticks into it (`[0xa4a2] == 0x18`, guarded
// on the one-shot `[0xa744]`) it hands over the picklock outright, no click
// involved, ending the exchange eight lines later.
//
// The talk itself has more state behind it than that one exchange --
// `[0xa74d]`/`[0xa74e]`/`[0xa74f]`/`[0x992b]` pick among a dozen or so
// further ones as the plot moves on -- and modelling the running counter
// faithfully is the same shape of work sluggs.cpp's own two-speaker machine
// took. Simplified here to the one exchange the picklock actually needs:
// the first talk speaks the room's own greeting outcome and hands the
// picklock over there and then; every talk after that is silent, standing in
// for the many later exchanges this pass does not carry.
static const int kYodleRoom = 21;

static const byte kYodle = 12;
static const byte kVerbTalkTo = 6;

static const uint16 kPicklockGiven = 0xa744;	///< the real one-shot guard, reused as-is

static const byte kPicklock = 37;
static const byte kOutcomeGreeting = 1;	///< ROOM21.TAL, "Yo, Yod.."

/// The talk on Yodle: entry 3's plain-verb half, simplified to one exchange.
bool AlienEngine::armYodle(int obj, byte verb) {
	if (_room != kYodleRoom || obj != kYodle || verb != kVerbTalkTo)
		return false;

	if (_script.flag(kPicklockGiven) == 1)
		return true;

	_script.setFlag(kPicklockGiven, 1);
	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);
	queueOutcome(_tal, kOutcomeGreeting, anchorX, anchorY);
	_inventory.add(kPicklock);
	debugC(1, kDebugItems, "yodle: hands over item %u (%s)", kPicklock,
		   _inventory.name(kPicklock).c_str());
	return true;
}

} // End of namespace Alien
