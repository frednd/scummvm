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

// Room 26, the inside of Yodle's tree hut: the parrot.
//
// Playtest report: giving the moldy bread to the parrot did nothing.
// docs/room_scripts.md's Room 26 rows for object 6 are only ever
// `action_handled = 1` -- the reaction itself is a `call`, the same reason
// Yodle and Gameson needed their own files, and nothing in the port ever
// wrote [0xa752], the parrot's mood, past its new-game default of 1.
//
// ovr_1a_0eaf's entry 3 (item use) arms this room's [0xa49f] machine two
// ways on object 6:
//
//   any item, [0xa752] != 3     step 2  -- the brush-off, "not interested"
//   item 3 (bread), [0xa752]==1 step 0x1e -- overrides the above: the feed
//
// (When [0xa752] == 3 already -- fed -- item use instead queues outcome 28
// directly and never touches the machine; the lifted "any/any" row already
// covers that half.)
//
// The same byte also carries two sequences with nothing to do with this bug
// -- an ambient squawk (0x14-0x15) armed by a cutscene-position condition,
// and the room's own once-per-visit entrance ambience (0x32-0x3a) armed
// right after char_place -- both left alone here, the way opening.cpp left
// room 3's other [0xa49f] values alone.
//
// Every `queue_event`/`DIALOG:sub_0b63a` in both sequences shares one fixed
// anchor (0x95, 0x1a) -- sub_0b63a's own body decodes the same way
// opening.cpp already established [0xacf6]/[0xacf2]/[0xacf4] work, and the
// two ds:off "continuation" pointers it also writes are the original's
// coroutine-style resume mechanism, which the port's per-tick [0xad1c] poll
// already reproduces without them (no other room file ports them either).

static const int kForestRoom = 26;
static const byte kParrotObj = 6;
static const byte kBread = 3;

static const uint16 kMood = 0xa752;			///< 1 hungry, 2 eating, 3 fed
static const uint16 kFedTwitch = 0xa753;		///< cleared partway through the feed

static const int kAnchorX = 0x95, kAnchorY = 0x1a;

/// The brush-off, any item but the right one at the right moment.
static const byte kStepBrushOff = 2;
static const byte kBrushOffLine1 = 0x13;
static const byte kBrushOffLine2 = 0x15;

/// The feed, bread on a hungry parrot.
static const byte kStepFeedStart = 0x1e;
static const byte kFeedLine1 = 0x16, kFeedLine2 = 0x17, kFeedLine3 = 0x18;
static const byte kFeedLine4 = 0x1f, kFeedLine5 = 0x20;

/// [0xa49c] waits, in ticks, as the machine's own thresholds have them.
static const uint kWaitNotice = 0x46;
static const uint kWaitPeck = 0x1e;
static const uint kWaitSwallow = 0x96;
static const uint kWaitSettle = 0x3c;
static const uint kWaitSquawk = 0x14;

/// The anim slots the feed uses: 3 for the parrot itself, 4 for whatever the
/// second bank plays over it (MIDAS:anim_play_mode1's own arguments).
static const int kParrotSlot = 3;
static const int kOverlaySlot = 4;

/// Where Ben stands to watch, `OBJ:sub_07890(2, 0x7a, 0xb3)`.
static const int kWatchX = 0xb3, kWatchY = 0x7a;
static const int kWatchFacing = 2;

void AlienEngine::armForestParrot(int obj, byte verb, int item) {
	if (_room != kForestRoom || obj != kParrotObj)
		return;

	// The already-fed row (queue_event(28)) is the lifted table's own; this
	// file only answers the two live states.
	if (_script.flag(kMood) == 3)
		return;

	if (item == kBread && _script.flag(kMood) == 1) {
		_forestStep = kStepFeedStart;
		_forestWait = 0;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "forest: bread on the parrot, step 0x%02x", kStepFeedStart);
		return;
	}

	_forestStep = kStepBrushOff;
	_forestWait = 0;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "forest: the parrot is not interested, step %d", kStepBrushOff);
}

void AlienEngine::stepForestParrot() {
	if (_room != kForestRoom || !_forestStep)
		return;

	switch (_forestStep) {
	// --- The brush-off ---------------------------------------------------
	case kStepBrushOff:
		if (_forestWait++ < kWaitSquawk)
			break;
		queueOutcome(_tal, kBrushOffLine1, kAnchorX, kAnchorY);
		_forestStep = 3;
		break;

	case 3:
		queueOutcome(_tal, kBrushOffLine2, kAnchorX, kAnchorY);
		_forestStep = 5;
		break;

	case 5:
		if (!speechDone())
			break;
		_forestStep = 0;
		CursorMan.showMouse(true);
		break;

	// --- The feed ----------------------------------------------------------
	case kStepFeedStart:
		// The bird notices before it does anything else.
		if (_forestWait++ < kWaitNotice)
			break;
		_forestStep = 0x1f;
		_forestWait = 0;
		break;

	case 0x1f:
		_script.setFlag(kMood, 2);
		_forestStep = 0x20;
		_forestWait = 0;
		break;

	case 0x20:
		if (_forestWait++ < kWaitPeck)
			break;
		queueOutcome(_tal, kFeedLine1, kAnchorX, kAnchorY);
		_forestStep = 0x21;
		break;

	case 0x21:
		if (!speechDone())
			break;
		queueOutcome(_tal, kFeedLine2, kAnchorX, kAnchorY);
		_forestStep = 0x22;
		break;

	case 0x22:
		if (!speechDone())
			break;
		queueOutcome(_tal, kFeedLine3, kAnchorX, kAnchorY);
		_forestStep = 0x23;
		_forestWait = 0;
		break;

	case 0x23:
		if (_forestWait++ < kWaitSwallow)
			break;
		_anims.takeDown(kParrotSlot);
		_script.setFlag(kMood, 3);
		_anims.play(kParrotSlot, 0x6a, 0x18, 0, 1);
		_forestStep = 0x24;
		_forestWait = 0;
		break;

	case 0x24:
		// Not remaining(kParrotSlot) == 0: room 26's loop table
		// (animLoopsForRoom) carries this slot as an unconditional relaunch,
		// so AnimSlots::relaunch() restarts it the instant remaining hits 1
		// and it never reaches 0 -- exactly what left the cursor hidden and
		// Ben idling forever. A fixed wait sized to the play (count 0x18 at
		// rate 0, roughly a tick a frame) stands in for it instead.
		if (_forestWait++ < kWaitSwallow)
			break;
		_anims.play(kOverlaySlot, 0xa, 1, 2, 1);
		_forestStep = 0x25;
		_forestWait = 0;
		break;

	case 0x25:
		// Slot 4 is the same story (kWaitPeck covers its short count-1 play).
		if (_forestWait++ < kWaitPeck)
			break;
		_forestStep = 0x26;
		break;

	case 0x26:
		walkTo(kWatchX, kWatchY, kWatchFacing);
		_forestStep = 0x27;
		_forestWait = 0;
		break;

	case 0x27:
		if (_forestWait++ < kWaitSettle)
			break;
		queueOutcome(_tal, kFeedLine4, kAnchorX, kAnchorY);
		_forestStep = 0x28;
		break;

	case 0x28:
		if (!speechDone())
			break;
		_script.setFlag(kFedTwitch, 0);
		_anims.play(kParrotSlot, 0xf, 1, 1, 1);
		_forestStep = 0x29;
		_forestWait = 0;
		break;

	case 0x29:
		// Same unconditional-loop slot as 0x24/0x25 (kParrotSlot == slot 3);
		// the tick wait alone stands in for the play, which is one frame.
		if (_forestWait++ < kWaitSquawk)
			break;
		queueOutcome(_tal, kFeedLine5, kAnchorX, kAnchorY);
		_forestStep = 0x2a;
		break;

	case 0x2a:
		if (!speechDone())
			break;
		CursorMan.showMouse(true);
		_forestStep = 0;
		debugC(1, kDebugRooms, "forest: the parrot is fed, [0xa752] = 3");
		break;

	default:
		_forestStep = 0;
		break;
	}
}

} // End of namespace Alien
