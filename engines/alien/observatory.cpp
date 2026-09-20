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
#include "alien/anim.h"
#include "alien/play.h"
#include "alien/sfx.h"

namespace Alien {

// Room 19's fuse panel: the cover over the fuse box, and the only way into it.
//
// The observatory's light switch is dead until a fuse is in the box, and the
// box is behind a cover that has to be looked at once and then moved. That is
// three separate things the lifted tables could not carry, and with all three
// missing the port's observatory was a dead end: the switch turned nothing on,
// and the telescope room's two levers -- both guarded on the light *and* on the
// fuse -- could never do anything either. Reported from play as "pulling the
// switch or lever in the observatory does nothing".
//
// 1. **The cover's rectangle.** Registered by
//    `ovr_13_0eb3_room_19:0x04ea`, which is the one registration in the whole
//    game whose *x1* is computed: the box is 189,29..222,62 while the cover is
//    shut and 167,29..222,62 once it is open, because the open cover hangs to
//    the left of the hole. HotspotOp carried a scratch slot for the label, the
//    object, the verb and the outcomes but never for a coordinate, so
//    gen_hotspots.py skipped the site and room 19 shipped with no object 3 at
//    all. Fixed at the generator; the rectangle is lifted now, and its verb is
//    the other half of the same row -- [0x9927] is 5 (LOOK) until [0xa75b],
//    and 9 (MOVE) after it.
//
// 2. **Looking at it opens it.** Entry 3 at 0x0188: object 3 under verb 5
//    raises event 10 and hides the cursor, and then the *arrival* half at
//    0x00b9 -- [0xa956] == 0x4e2a with [0xacf6] == 10, the dialog unit's "a
//    line has come down" -- calls the cover routine, brings the cursor back
//    and sets [0xa75b]. So the first look is what opens the cover and what
//    turns its verb into MOVE. The queue_event and nothing else is what the
//    lift has of that.
//
// 3. **Moving it is a toggle.** Entry 3 tests object 3 under verb 9 twice:
//    at 0x0194 with [0xa758] == 1 it opens (the cover routine again), and at
//    0x01ad with action_handled still clear it closes. The lift saw only the
//    second branch, because the first one's body is a `call` to
//    ovr0eb3_sub_0068 rather than a run of opcodes -- so the port's table had
//    a row that shut a cover nothing could open. Both halves live here and the
//    pair is kept away from the table, the way room 23's Gameson is.
//
// The routine itself, ovr0eb3_sub_0068 and its sibling at 0x01c2, is four plays
// of slot 0 in mode 6 through ovr0eb3_sub_0000 -- a frame list out of the
// resident data segment, picked by whether the fuse is in:
//
//   | what      | fuse | 271a  | frames            |
//   |-----------|------|-------|-------------------|
//   | opening   | out  | 6ef0  | 1 2 3 4 5 4 5 4   |
//   | closing   | out  | 6ef8  | 4 3 2 1           |
//   | opening   | in   | 6efc  | 9 8 7 6 7 6 0     |
//   | closing   | in   | 6f04  | 6 7 8 9 1         |

/// The room this file is about, and the cover's object id.
static const int kObservatoryRoom = 19;
static const byte kPanelObject = 3;

/// [0xa758]: the cover is shut. Ships set, so the box starts hidden.
static const uint16 kPanelShut = 0xa758;
/// [0xa759]: the fuse is in the box.
static const uint16 kFuseIn = 0xa759;
/// [0xa75b]: the cover has been looked at, which is what turns its verb into
/// MOVE. Cleared for a new game at seg_main:0000:09f3.
static const uint16 kPanelSeen = 0xa75b;

/// The event the look raises, and which the open answers.
static const byte kPanelLookEvent = 10;

/// The two verbs the cover's one rectangle carries, either side of [0xa75b].
static const byte kVerbLookAt = 5;
static const byte kVerbMove = 9;

/// 271a:0x6ef0 and on: the four frame lists ovr0eb3_sub_0000 selects between.
static const byte kPanelOpen[] = { 1, 2, 3, 4, 5, 4, 5, 4 };
static const byte kPanelClose[] = { 4, 3, 2, 1 };
static const byte kPanelOpenFuse[] = { 9, 8, 7, 6, 7, 6, 0 };
static const byte kPanelCloseFuse[] = { 6, 7, 8, 9, 1 };

/// Both plays are slot 0, mode 6, three ticks a frame.
static const uint kPanelSlot = 0;
static const int kPanelRate = 3;
static const int kPanelMode = 6;

/// INPUT:sfx_play_delayed, as each half calls it: the cover coming off is a
/// different sample from the cover going back on, and the second one is held
/// seven ticks so that it lands under the end of the animation.
static const uint kOpenSample = 2;
static const uint32 kOpenRate = 0x2af8;
static const uint kCloseSample = 3;
static const uint32 kCloseRate = 0x2328;
static const byte kPanelVolume = 0x40;
static const uint16 kCloseDelay = 7;

/**
 * ovr0eb3_sub_0068: the cover comes off.
 *
 * The original sets action_handled here, which is what keeps the closing
 * branch below it from running on the same click. The port has no such
 * ordering to protect -- movePanel() is the only caller and it returns to a
 * dispatch that skips the table -- so the flag is left to the caller.
 */
void AlienEngine::openObservatoryPanel() {
	const bool fuse = _script.flag(kFuseIn) == 1;
	const byte *frames = fuse ? kPanelOpenFuse : kPanelOpen;
	const int count = fuse ? ARRAYSIZE(kPanelOpenFuse) : ARRAYSIZE(kPanelOpen);

	_anims.play(kPanelSlot, 0, count, kPanelRate, kPanelMode, frames);
	_sound.queue(kOpenSample, kOpenRate, kPanelVolume, 0, 0);
	_script.setFlag(kPanelShut, 0);
}

/**
 * ovr0eb3_entry3:0x01c2: the cover goes back on.
 *
 * The lifted row for this pair sets [0xa758] and plays the sample; the frames
 * are what it could not carry, so they are all this adds.
 */
void AlienEngine::closeObservatoryPanel() {
	const bool fuse = _script.flag(kFuseIn) == 1;
	const byte *frames = fuse ? kPanelCloseFuse : kPanelClose;
	const int count = fuse ? ARRAYSIZE(kPanelCloseFuse) : ARRAYSIZE(kPanelClose);

	_anims.play(kPanelSlot, 0, count, kPanelRate, kPanelMode, frames);
	_sound.queue(kCloseSample, kCloseRate, kPanelVolume, 0, kCloseDelay);
	_script.setFlag(kPanelShut, 1);
}

/**
 * MOVE on the cover, which entry 3 answers in two branches and the table in
 * neither.
 *
 * Returns true when the click is the room's, so that the lifted pair -- which
 * has the closing half without the opening one -- is not offered it.
 */
bool AlienEngine::armObservatoryPanel(int obj, byte verb) {
	if (_room != kObservatoryRoom || obj != kPanelObject || verb != kVerbMove)
		return false;

	if (_script.flag(kPanelShut) == 1)
		openObservatoryPanel();
	else
		closeObservatoryPanel();

	return true;
}

/**
 * LOOK at the cover raises event 10; when that line comes down the cover opens
 * itself, and from then on the rectangle carries MOVE.
 *
 * ovr_13_0eb3:0x00b9. The original hides the cursor for the length of the line
 * ([0xa948] := 0 at 0x018f, back at 0x00cc); the port's speech already gates
 * input while a line is up, so only the two writes that outlive it are here.
 */
void AlienEngine::stepObservatoryPanel() {
	if (!_observatoryLook || _room != kObservatoryRoom)
		return;
	if (_lastEvent != kPanelLookEvent || !speechDone())
		return;

	_observatoryLook = false;
	openObservatoryPanel();
	_script.setFlag(kPanelSeen, 1);
	rebuildHotspots();
}

/**
 * The other end of it: the look itself, which the lifted row raises the event
 * for. Arming is separate from answering because the answer waits for the line.
 */
void AlienEngine::armObservatoryLook(int obj, byte verb) {
	if (_room != kObservatoryRoom || obj != kPanelObject || verb != kVerbLookAt)
		return;
	if (_script.flag(kPanelSeen) == 1)
		return;

	_observatoryLook = true;
}

} // End of namespace Alien
