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
#include "alien/detection.h"
#include "alien/play.h"
#include "alien/sfx.h"

#include "graphics/cursorman.h"

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
//    pair is kept away from the table, the way room 23's hippie is.
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

/// The circuit breaker, object 1, and the byte it toggles.
static const byte kBreakerObject = 1;
static const byte kVerbUse = 10;
static const uint16 kPowerOn = 0xa75a;

/// [0xa4ef]: the frames left on slot 5, the hand on the breaker.
static const uint16 kBreakerLeft = 0xa4ef;

/// The two lines the machine speaks as Ben comes back, by which way it went.
static const byte kPowerOnEvent = 0x0b;		///< "There. The power is on."
static const byte kPowerOffEvent = 0x0c;	///< "There. The power is switched off."

/**
 * Use on the breaker: the body both ways round (0x024d, 0x02b4) plays the hand
 * on slot 5 with Ben drawn into it, so it takes the walker away ([0xa94d] = 0
 * at 0x0285, 0x02c2) and arms [0xa49f] 0x14. The lifted rows have the plays
 * and the flag, not the two byte writes -- which is Ben on screen twice and no
 * line at the end of it (manual playthrough #38).
 */
void AlienEngine::armObservatoryBreaker(int obj, byte verb) {
	if (_room != kObservatoryRoom || obj != kBreakerObject || verb != kVerbUse)
		return;
	if (!_anims.isBusy(5))
		return;

	hideCharacter();
	_observatoryBreaker = true;
	debugC(1, kDebugRooms, "observatory: the breaker, power %s",
		   _script.flag(kPowerOn) ? "on" : "off");
}

/// 0x0ad0: one frame left on the hand, and Ben is back to say which way it went.
void AlienEngine::stepObservatoryBreaker() {
	if (!_observatoryBreaker || _room != kObservatoryRoom)
		return;
	if (_script.flag(kBreakerLeft) > 1)
		return;

	_observatoryBreaker = false;
	showCharacter();

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);
	queueOutcome(_tal, _script.flag(kPowerOn) ? kPowerOnEvent : kPowerOffEvent,
				 anchorX, anchorY);
}

// The stairs, both ways. Up is the room's own exit: the walk geometry arms
// submode 1 on the stairs, and the loop answers the arrival at 0x0b0e by
// clearing it, so the shared exit test never sees it, and starting [0xa49f]
// 0x64. Fifteen counts of [0xa49c] later Ben is taken away and OBSGOUP climbs
// on slot 4 (0x0b3e); its seventeenth frame is what writes game_submode 1
// (0x0b63). The lift has the play but not the arrival that starts it, so the
// port cut straight to the telescope room (manual playthrough #50).
//
// Down is the slip, BENSLIP on slot 3: entry 2 at 0x07b8, coming back from
// room 28 while [0xa75c] is clear and the fuse cover is still shut. That is
// why a normal game never shows it -- the cover has to be open to fit the fuse
// that powers the telescope, and nothing makes a player close it again
// (manual playthrough #56). It happens once: [0xa75c] is set as it starts.

/// The room the stairs lead to, and the submode the geometry arms for them.
static const int kTelescopeRoom = 28;
static const byte kStairsSubmode = 1;

/// [0xa75c]: the slip has played.
static const uint16 kSlipped = 0xa75c;

/// The slots: BENSLIP on 3, OBSGOUP on 4.
static const uint kSlipSlot = 3;
static const uint kClimbSlot = 4;

/// [0xa49f] as the loop writes it.
static const byte kStepSlipFall = 1;		///< 0x07c6, frames 1-19 playing
static const byte kStepSlipLie = 2;			///< 0x0a65, down on the floor
static const byte kStepSlipLine = 3;		///< 0x0a89, the line said
static const byte kStepSlipUp = 4;			///< 0x0aae, frames 19-37 playing
static const byte kStepStairs = 0x64;		///< 0x0b39, at the foot of the stairs

/// The waits, in [0xa49c].
static const uint16 kSlipLineAt = 0x28;
static const uint16 kSlipUpAt = 0x50;
static const uint16 kClimbAt = 0x0f;

/// The plays: slot 3 once in two halves of 19, slot 4 kept, 17 frames.
static const int kSlipFrames = 0x13;
static const int kSlipRate = 3;
static const int kSlipMode = 4;
static const int kClimbFrames = 0x11;
static const int kClimbRate = 2;
static const int kClimbMode = 1;

/// DIALOG:sub_0b63a(0x0f, 0xcc, 0x5b): "Doh." and "Oh man, oh man... Game over?"
static const byte kSlipLine = 0x0f;

/// The port's own fall (manual playthrough #60, not in the original): every
/// seventh trip down from room 28 he takes the stairs the fast way again, and
/// says a line of his own. The line is outcome 20, which ROOM19.TAL ships
/// pointing at nothing; data/edits aims it at entry 20, empty in the shipped
/// file, and fills that in. A pack without the edit has him fall and say
/// nothing, which is the most that can go wrong.
static const byte kEveryFall = 7;
static const byte kEncoreLine = 20;

/// CHARANIM:sub_13bce at 0x07d6: where he lands, facing front.
static const int kSlipX = 0xc1, kSlipY = 0x28;
static const int kFacingFront = 3;
static const int kSlipLineX = 0xcc, kSlipLineY = 0x5b;

/// INPUT:sfx_play_delayed at 0x07f2 and 0x0804: the thump, twice.
static const uint kSlipSample = 1;
static const uint32 kSlipRateA = 0x1770, kSlipRateB = 0x0fa0;
static const byte kSlipVolumeA = 0x40, kSlipVolumeB = 0x19;
static const uint16 kSlipDelayA = 0x20, kSlipDelayB = 0x36;

/// Entry 2 at 0x0799. The placement at 193,40 facing front and the first half
/// of the play are lifted rows (roominit.cpp); the rest is the walker taken
/// away, the cursor, the sounds and the latch.
void AlienEngine::startObservatory() {
	_observatoryStep = 0;
	_observatoryPos = 0;

	if (_room != kObservatoryRoom || _mode != kTelescopeRoom || _restoring)
		return;

	const bool original = _script.flag(kSlipped) == 0 && _script.flag(kPanelShut) == 1;

	// The count runs on every trip down, the original's slip included.
	_stairsDescents = (byte)((_stairsDescents + 1) % kEveryFall);
	const bool encore = !original && _stairsDescents == 0;
	if (!original && !encore)
		return;

	if (encore) {
		// The lifted rows only place him and start the play under the
		// original's own guard, so the encore does both itself.
		_ben.placeSprite(kSlipX, kSlipY, kFacingFront);
		_anims.play(kSlipSlot, 1, kSlipFrames, kSlipRate, kSlipMode);
		updateScroll(true);
	}

	// A port addition (setHold()): mode 4 takes its last frame away, and he
	// lies on the floor between the two halves for the whole of the line.
	hideCharacter(kSlipSlot);
	_anims.setHold(kSlipSlot, true);
	CursorMan.showMouse(false);
	_sound.queue(kSlipSample, kSlipRateA, kSlipVolumeA, 0, kSlipDelayA);
	_sound.queue(kSlipSample, kSlipRateB, kSlipVolumeB, 0, kSlipDelayB);
	if (original)
		_script.setFlag(kSlipped, 1);
	_observatoryEncore = encore;
	_observatoryStep = kStepSlipFall;
	debugC(1, kDebugRooms, "observatory: down the stairs the fast way%s",
		   encore ? " (the port's seventh trip)" : "");
}

/// 0x0b0e: the arrival at the stairs, taken before the shared exit sees it.
bool AlienEngine::hijackObservatoryStairs(byte submode) {
	if (_room != kObservatoryRoom || submode != kStairsSubmode)
		return false;

	CursorMan.showMouse(false);
	_observatoryStep = kStepStairs;
	_observatoryPos = 0;
	debugC(1, kDebugRooms, "observatory: at the stairs");
	return true;
}

/// The loop's [0xa49f] states for both, 0x0a57 to 0x0b6f.
void AlienEngine::stepObservatoryStairs() {
	if (_room != kObservatoryRoom || !_observatoryStep)
		return;

	// [0xa49c] moves on the animation gate ([0xa5f8], 0x088d), every other
	// tick pair.
	if ((_tick & 3) == 0)
		_observatoryPos++;

	switch (_observatoryStep) {
	case kStepSlipFall:
		if (_anims.isBusy(kSlipSlot))
			return;
		_observatoryStep = kStepSlipLie;
		_observatoryPos = 0;
		return;

	case kStepSlipLie:
		if (_observatoryPos < kSlipLineAt)
			return;
		queueOutcome(_tal, _observatoryEncore ? kEncoreLine : kSlipLine, kSlipLineX, kSlipLineY);
		_observatoryStep = kStepSlipLine;
		_observatoryPos = 0;
		return;

	case kStepSlipLine:
		if (_observatoryPos < kSlipUpAt)
			return;
		// The encore's line is longer than the original's, and he stays down
		// until he has said it.
		if (_observatoryEncore && !speechDone())
			return;
		playCharacterAnim(kSlipSlot, kSlipFrames, kSlipFrames, kSlipRate, kSlipMode);
		_observatoryStep = kStepSlipUp;
		return;

	case kStepSlipUp:
		if (_anims.isBusy(kSlipSlot))
			return;
		_anims.setHold(kSlipSlot, false);
		showCharacter();
		CursorMan.showMouse(true);
		_observatoryStep = 0;
		debugC(1, kDebugRooms, "observatory: back on his feet");
		return;

	case kStepStairs:
		if (_observatoryPos < kClimbAt)
			return;
		if (_observatoryPos == kClimbAt) {
			playCharacterAnim(kClimbSlot, 1, kClimbFrames, kClimbRate, kClimbMode);
			_observatoryPos++;
			debugC(1, kDebugRooms, "observatory: up the stairs");
			return;
		}
		if (_anims.isBusy(kClimbSlot))
			return;
		_observatoryStep = 0;
		takeExit(kStairsSubmode);
		return;

	default:
		return;
	}
}

} // End of namespace Alien
