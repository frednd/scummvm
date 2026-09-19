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

// The four sequences room 3 runs on [0xa49f] that are not the opening
// (opening.cpp holds 0x64 and 0x6e, the monologue MAIN arms).
//
// Each is armed by a hotspot body and finishes in the room's own tick,
// ovr_03_0e57:0x0dc4-0x0e75. Two of them matter for more than the lab: they are
// the first sequences in the port where the room plays *the character's own
// action* on an animation slot, and so the first that turn the walker off while
// it runs. That is [0xa94d], which every room's tick tests before it calls the
// character's draw (`cmp [0xa94d], 1` at ovr_03_0e57:0x0c12 -> OBJ:sub_06466).
// Without it the slot's Ben and the walker are both on screen -- the "two Bens"
// of playtest report 3 -- and the port drew the walker unconditionally.
//
//   3     Armed by using the crowbar on obj 11, the boarded-up hole (0x00de).
//         The body plays LAB_PLAN's eighty-nine frames on slot 5 -- the plank
//         coming off -- and two delayed samples; this takes the walker and the
//         cursor away. When slot 5 is two frames from the end the walker comes
//         back and the machine goes to 5.
//   5     Ten frames later: the cursor comes back and outcome 0x1e is spoken.
//   0x1e  Armed by a plain verb on obj 12, the key behind the plank, whose
//         rectangle only registers once the plank body has cleared [0xa6dc]
//         (0x0293). The body plays LAB_TKEY's forty frames on slot 6; this
//         takes the walker and the cursor away. Two frames from the end both
//         come back and **the big key (item 9) is added to the inventory** --
//         the only place in the game that adds it, and the whole of playtest
//         report 9. It is not in the lifted script table because it is not in
//         a hotspot body at all: it is a step of this machine.
//   0x3c  Armed by using the floppy on obj 2, the terminal (0x0187), and only
//         while [0xa6e1] is still clear. The body plays a single frame and
//         takes the floppy; this takes the cursor (but not the walker), and
//         seventy frames later gives it back and runs the terminal's own
//         twenty-four frame animation.
//
// [0xa49c], which the room's tick increments on every tick pair (0x0c09) and
// two of these arms zero, is the counter behind the two frame counts.
static const int kLabRoom = 3;

static const byte kLabPlank = 11;		///< the boarded hole, under the crowbar
static const byte kLabKey = 12;			///< the key behind it, under a verb
static const byte kLabTerminal = 2;		///< the terminal, under the floppy

static const uint kPlankSlot = 5;
static const uint kKeySlot = 6;
static const uint kTerminalSlot = 4;

static const uint16 kTerminalUsed = 0xa6e1;
static const byte kBigKey = 9;
static const byte kPlankLine = 0x1e;

static const byte kStepPlank = 3;
static const byte kStepPlankSpeak = 5;
static const byte kStepKey = 0x1e;
static const byte kStepTerminal = 0x3c;

/// [0xa49c] > this before the step ends.
static const uint16 kPlankWait = 10;
static const uint16 kTerminalWait = 0x46;

// The room's other hook, ovr_03_0e57:0x0b3e, which its tick runs while the hole
// is still boarded up ([0xa6dc] == 1, tested at 0x0e7a). It is a proximity
// trigger and not a hotspot: a rectangle in *sprite* space that the character's
// own position is tested against every tick, creaking once as he steps into it
// (0x0b10) and once as he leaves (0x0b27). The bounds are exclusive, as the
// original's four `jle`/`jge` are, and the pair [0xa7a4]/[0xa7a5] it keeps the
// answer in is the edge -- zeroed where the room opens rather than inside the
// loop, so it survives from frame to frame.
//
// The rectangle is the floor **below** the approach point a click on the plank
// walks to, which is what playtest report 3 of 2026-09-06 was reading as the
// character being in the wrong place: see docs/playthrough_findings.md #72.
static const uint16 kHoleBoarded = 0xa6dc;
static const int kHoleX1 = 103, kHoleY1 = 65, kHoleX2 = 125, kHoleY2 = 110;
static const uint kHoleSample = 14;
static const uint32 kHoleEnterRate = 0xfa0, kHoleLeaveRate = 0x125c;
static const byte kHoleVolume = 0x32;

/**
 * A click body is about to run: start the machine if it is one of these four.
 *
 * Unlike the sewer's hook this runs **before** the body, because one of the
 * three arms is guarded on a flag the body itself sets ([0xa6e1]) and the
 * original reads it first. Nothing here depends on the body having run: the
 * animation plays belong to the body, and the state is not looked at until the
 * next tick either way.
 */
void AlienEngine::armLab(int obj, bool item) {
	if (_room != kLabRoom)
		return;

	// The two sections of the room's click dispatch: [0xa956] == 0x4e22 is the
	// item-use path and 0x4e25 the plain-verb one, which is what `item` is here.
	if (item && obj == kLabPlank) {
		_labStep = kStepPlank;
		// The play itself is in the click body; naming the slot here is what
		// lets showCharacter() take it down again with him.
		hideCharacter(kPlankSlot);
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "lab: the plank comes off, step %d", kStepPlank);
		return;
	}

	if (item && obj == kLabTerminal && _script.flag(kTerminalUsed) == 0) {
		_labStep = kStepTerminal;
		_labPos = 0;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "lab: the terminal, step 0x%02x", kStepTerminal);
		return;
	}

	if (!item && obj == kLabKey) {
		_labStep = kStepKey;
		hideCharacter(kKeySlot);
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "lab: the key, step 0x%02x", kStepKey);
		return;
	}
}

void AlienEngine::stepLab() {
	if (_room != kLabRoom)
		return;

	// ovr_03_0e57:0x0c09, which the room does whether or not anything is
	// running.
	_labPos++;

	switch (_labStep) {
	case kStepPlank:
		if (_anims.remaining(kPlankSlot) != 2)
			break;
		showCharacter();
		_labStep = kStepPlankSpeak;
		_labPos = 0;
		break;

	case kStepPlankSpeak: {
		if (_labPos <= kPlankWait)
			break;
		_labStep = 0;
		CursorMan.showMouse(true);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kPlankLine, anchorX, anchorY);
		debugC(1, kDebugRooms, "lab: outcome %d, the plank is off", kPlankLine);
		break;
	}

	case kStepKey:
		if (_anims.remaining(kKeySlot) != 2)
			break;
		CursorMan.showMouse(true);
		showCharacter();
		_labStep = 0;
		_inventory.add(kBigKey);
		debugC(1, kDebugRooms, "lab: item %d taken", kBigKey);
		break;

	case kStepTerminal:
		if (_labPos <= kTerminalWait)
			break;
		_labStep = 0;
		CursorMan.showMouse(true);
		_anims.play(kTerminalSlot, 2, 24, 2, 1);
		debugC(1, kDebugRooms, "lab: the terminal answers");
		break;

	default:
		break;
	}

	stepLabHole();
}

/**
 * The creak by the boarded-up hole: ovr_03_0e57:0x0b3e, run at the end of the
 * room's tick while the plank is still on.
 */
void AlienEngine::stepLabHole() {
	if (_script.flag(kHoleBoarded) != 1)
		return;

	const int x = _ben.spriteX(), y = _ben.spriteY();
	const bool inside = x > kHoleX1 && y > kHoleY1 && x < kHoleX2 && y < kHoleY2;
	if (inside == _labNearHole)
		return;

	_labNearHole = inside;
	_sound.queue(kHoleSample, inside ? kHoleEnterRate : kHoleLeaveRate, kHoleVolume, 0, 0);
	debugC(1, kDebugRooms, "lab: the hole %s at %d,%d", inside ? "creaks" : "settles", x, y);
}

} // End of namespace Alien
