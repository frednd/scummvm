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

// Room 46, underwater between the cavern pool and the shore.
//
// Two of its three mechanics are ordinary item outcomes tools/roomlogic.py
// cannot see because they are guarded on a `[0xa49f]` machine or on a flag
// with no writer of its own: the picklock opens the chest (object 1, entry 3's
// item-use half, `ovr_2e_0ec3:0x0040`) in two lines and a six-frame play, and
// [0xa788] -- true from the new game's own init block, the same shape as
// [0xa72e] and [0xa76e] -- hands the old key over on a plain click on object 2
// once the chest is open (`0x0087`).
//
// The third is the swim itself, and it is not simplified the way the answer
// to the first two is exact: the real trigger is a walk-arrival latch
// (`[0xa644]` tested against object ids 4 and 5 at `0x06a7`/`0x06ae`) behind a
// `[0xa49f]` machine of its own (states 0x14/0x15) that only then arms
// submode 10 -- the same shape as room 32's cave zone, but resolving exactly
// which walk-to-object id the original's KIERRA geometry assigns object 5 is
// more than this pass had time for. Standing in for it: any plain click that
// is not on the chest or the key, with an empty hand, counts as a stroke
// toward the far side, the way room 43's turns do.
static const int kDivingRoom = 46;

static const byte kChestObj = 1;
static const byte kPicklock = 37;
static const byte kKeyObj = 2;
static const byte kOldKey = 38;

static const uint16 kKeyReady = 0xa788;	///< the chest is open and the key is in it

static const uint kChestSlot = 1;
static const int kChestFrames = 6;
static const int kChestRate = 3;
static const int kChestMode = 1;

static const byte kOutcomeChestOpens = 9;	///< two lines: "I wonder..." / "That did it."
static const byte kOutcomeOldKey = 2;

static const byte kStepChestOpening = 3;
static const byte kStepChestDone = 4;

static const uint kSwimClicks = 3;			///< standing in for the real arrival latch
static const byte kStepSwimming = 0x14;
static const uint16 kSwimWait = 60;
static const byte kSwimExitSubmode = 10;	///< transitions.cpp: room 46, submode 10 -> room 41

/// Every arrival resets the swim count and the machine.
void AlienEngine::startDiving() {
	if (_room != kDivingRoom)
		return;

	_divingStep = 0;
	_divingClicks = 0;
}

/// The picklock, used on the chest: entry 3's item-use arm.
bool AlienEngine::armDiving(int obj, byte item) {
	if (_room != kDivingRoom)
		return false;

	if (obj == kChestObj && item == kPicklock) {
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeChestOpens, anchorX, anchorY);
		_divingStep = kStepChestOpening;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "diving: the picklock finds the catch");
		return true;
	}

	if (obj == kKeyObj && item == Inventory::kNoItem && _script.flag(kKeyReady) == 1) {
		_script.setFlag(kKeyReady, 0);
		_inventory.add(kOldKey);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeOldKey, anchorX, anchorY);
		debugC(1, kDebugItems, "diving: the chest holds item %u (%s)", kOldKey,
			   _inventory.name(kOldKey).c_str());
		return true;
	}

	return false;
}

/**
 * The swim to the far side: a plain click standing in for the real
 * walk-arrival latch (see this file's header). Consumes the click the way
 * room 43's turns do, so it must never see one meant for the chest or the
 * key -- both are hovered objects by the time this runs, which is what
 * `_hover` is for.
 */
bool AlienEngine::armDivingSwim() {
	if (_room != kDivingRoom || _divingStep || _heldItem != Inventory::kNoItem)
		return false;

	if (_hover >= 0) {
		const int obj = _spots[_hover].obj;
		if (obj == kChestObj || obj == kKeyObj)
			return false;
	}

	// Object 2's rect only registers with [0xa785] in a state this port's
	// swim never sets (see this file's header), so a plain click claims the
	// key straight away once the chest has one waiting, rather than leaving
	// it behind a rectangle nothing here ever shows.
	if (_script.flag(kKeyReady) == 1) {
		armDiving(kKeyObj, Inventory::kNoItem);
		return true;
	}

	_divingClicks++;
	debugC(1, kDebugRooms, "diving: stroke %u of %u", _divingClicks, kSwimClicks);

	if (_divingClicks >= kSwimClicks) {
		_divingStep = kStepSwimming;
		_divingPos = 0;
		CursorMan.showMouse(false);
	}

	return true;
}

/// The chest's own finish, and the wait before the far bank.
void AlienEngine::stepDiving() {
	if (_room != kDivingRoom || !_divingStep)
		return;

	switch (_divingStep) {
	case kStepChestOpening:
		if (!speechDone())
			break;
		_anims.setLoopFlag(kChestSlot, 0);
		_anims.play(kChestSlot, 1, kChestFrames, kChestRate, kChestMode);
		_script.setFlag(kKeyReady, 1);
		CursorMan.showMouse(true);
		_divingStep = kStepChestDone;
		break;

	case kStepChestDone:
		_divingStep = 0;
		break;

	case kStepSwimming:
		if (_divingPos++ < kSwimWait)
			break;
		_divingStep = 0;
		takeExit(kSwimExitSubmode);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
