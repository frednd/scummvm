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

// The sewer, which is the gate on the whole first act.
//
// The hall's front door (room 15, obj 7) arms submode 6 -- the link that leads
// to room 27 and the scene the mansion raises as it opens -- but only while
// [0xa6d6] is clear, and [0xa6d6] ships as 1 (seg_main.asm:0x07f9). The one
// writer that clears it in the whole game is the arm of scene 6
// (seg_cutscene.asm:0x19b4), and the only room that raises scene 6 is this one,
// on the way *back into* it: the trigger's guards are game_mode == 35 and
// game_submode == 20 (ovr_23_0e7b:0x049f). Nothing walks into submode 20. The
// room's own [0xa49f] machine arms it, and reaching that machine's hatch means
// first getting the water out of the room.
//
// The water is not a state at all, it is a clip line. [0x33be] ships as 1, and
// while it is set the room's tick (ovr_23_0e7b:0x0687) recomputes, every frame:
//
//     [0xa8e4] = [0xa8ee] + [0x4382] + ripple[[0x4380]]
//
// [0xa8ee] is the character's own sprite origin and [0xa8e4] is the bottom clip
// every DL1 blit reads (OBJ:dl1_load_and_blit, 0251:0c6f -- see
// DL1Sprite::drawFrame), so what the room is drawing is the surface of the
// water cutting the character off part-way down. [0x4380] walks the thirty-byte
// ripple table at [0x4362], which GAME.EXE ships as ten 0s, five 1s, ten 2s and
// five 1s -- a slow swell of two pixels. [0x4382] is how much of him the drain
// has given back, and starts at 0x32.
//
// The six states, in the order a playthrough meets them:
//
//   7      Arrival from the mansion (room 27). Room init plays the ladder
//          backwards on slot 1 and takes the cursor away (ovr_23_0e7b:0x0606);
//          the state gives it back with two frames still to run and turns him
//          to face front.
//   0x32   The valve, obj 10 under Use, the first time only ([0x33bf]).
//          Waits for slot 3 -- SEW_WATE, the surface itself -- to come round to
//          frame 2, then starts the drain: [0x4385] on, and slot 4's
//          twenty-nine frames of water going down. **This is the one gate the
//          port cannot yet satisfy**, and it is not a porting mistake: nothing
//          in room 35 ever starts slot 3. All nine of the overlay's play calls
//          go to slots 0, 1, 2, 4, 5 and 6; the tick's only word about slot 3
//          is MIDAS:snd_func_112d, which returns unless the slot has exactly
//          one frame left to run; and the scene the room raises on the way in
//          (scene 5) runs CUTSCENE's OBJ:sub_02510, which zeroes slots 1..15.
//          Room 19 uses the identical idiom against the same word and does
//          play the slot first, through MIDAS:sub_18810 -- so the idiom is
//          right and the room's own starter is what is still missing.
//   0x37   The drain has run: clear [0x33be], give the cursor back and put the
//          clip line back at the bottom of the playfield.
//   2      Not a state -- the hatch, obj 2 under Push, whose hotspot is guarded
//          on [0x33be] == 0 and so cannot be clicked until the room is drained.
//   0x64   Armed by that hatch. Once slot 6 has run out, raise loop_flag and
//          set game_submode to 20, which ends the room and re-enters it through
//          the chain: scene 6, and with it the front door of the house.
//   3, 4   The ladder out, obj 3 under Use: play it forwards on slot 1, take
//          the cursor away, and leave by submode 1 with one frame left to run.
//
// One arm is not here. The tick also starts state 3 from the walk system
// (ovr_23_0e7b:0x0805), when a blocked line of sight coincides with [0xa644]
// == 8 -- the same pair of runtime globals the kOpUnsupported burn-down left
// unmodelled -- so the ladder answers a click on it but not a walk into it.
static const int kSewerRoom = 35;
static const byte kSewerLadder = 3;
static const byte kSewerHatch = 2;
static const byte kSewerValve = 10;
static const byte kSewerUse = 10;
static const byte kSewerPush = 12;

static const uint kLadderSlot = 1;
static const uint kRippleSlot = 3;
static const uint kDrainSlot = 4;
static const uint kValveSlot = 5;
static const uint kHatchSlot = 6;

static const uint16 kFlooded = 0x33be;		///< the room still has its water in
static const uint16 kValveUsed = 0x33bf;	///< the valve has been turned once
static const uint16 kValveOpen = 0x33c0;	///< which way the valve turns next
static const uint16 kHatchOpen = 0xa777;
static const uint16 kSceneLatch = 0x339a;

static const byte kStepArriving = 7;
static const byte kStepClimbing = 3;
static const byte kStepLeaving = 4;
static const byte kStepValve = 0x32;
static const byte kStepDraining = 0x37;
static const byte kStepHatch = 0x64;

static const byte kSubmodeLadder = 1;
static const byte kSubmodeHatch = 20;

static const byte kEnterFromMansion = 27;

/// [0xa803] = 3: the way the room turns him once he is off the ladder.
static const int kFacingFront = 3;

/// The valve's click, sfx_play_delayed(2, 0, 0x3e80, 0x40, 0, 1).
static const uint kValveSample = 2;
static const uint32 kValveRate = 0x3e80;
static const byte kValveVolume = 0x40;
static const int8 kValvePanning = 0;
static const uint16 kValveDelay = 1;

/// [0x4362]: the swell the water rides, one entry per animation frame.
static const byte kRipple[] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 1
};

/// The divider [0x4384] counts to before the drain takes another row.
static const byte kDrainDivider = 3;

/// The frame of the water the valve waits for before the drain starts.
static const int kDrainCue = 2;

/**
 * ovr_23_0e7b_sub_0000: the valve turning, which happens on every use of it.
 *
 * It is a toggle rather than an animation: [0x33c0] picks between playing slot
 * 5's six frames forwards and playing them back again, and flips afterwards.
 * The room's script table has none of this -- the body reaches it through a
 * near call, which tools/roomlogic.py does not follow -- so only its
 * `[0x33bf] = 1` was ported.
 */
void AlienEngine::sewerValve() {
	_sound.queue(kValveSample, kValveRate, kValveVolume, kValvePanning, kValveDelay);

	if (_script.flag(kValveOpen) == 0)
		_anims.play(kValveSlot, 1, 6, 5, 1);
	else if (_script.flag(kValveOpen) == 1)
		_anims.play(kValveSlot, 6, 6, 5, 3);

	_script.setFlag(kValveOpen, _script.flag(kValveOpen) == 0 ? 1 : 0);
}

/// The room has just opened: the ladder down, when he came in over it.
void AlienEngine::enterSewer() {
	if (_room != kSewerRoom)
		return;

	// Room init's own effects are lifted already, the placement and the
	// backwards ladder play among them (roominit.cpp); what is left is the pair
	// of writes around them. [0xa94d] goes with the cursor everywhere it is
	// touched in this overlay, so the port folds the two together.
	if (_mode != kEnterFromMansion)
		return;

	CursorMan.showMouse(false);
	_sewerStep = kStepArriving;
	debugC(1, kDebugRooms, "sewer: down the ladder from room %d, step %d",
		   _mode, kStepArriving);
}

/// The click body has just run: start the machine if the click was one of its.
void AlienEngine::armSewer(int obj, byte verb) {
	if (_room != kSewerRoom)
		return;

	if (obj == kSewerHatch && verb == kSewerPush) {
		// [0xa777] is what the body sets as the hatch swings open; pushing it
		// shut again clears the flag and arms nothing.
		if (_script.flag(kHatchOpen) != 1 || _script.flag(kSceneLatch) != 0)
			return;

		_sewerStep = kStepHatch;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "sewer: the hatch is open, step 0x%02x", kStepHatch);
		return;
	}

	if (obj == kSewerLadder && verb == kSewerUse) {
		// The body has already played the climb on slot 1.
		_sewerStep = kStepClimbing;
		debugC(1, kDebugRooms, "sewer: up the ladder, step %d", kStepClimbing);
		return;
	}

	if (obj == kSewerValve && verb == kSewerUse) {
		sewerValve();

		// The original reads [0x33bf] before the body sets it; here the body has
		// run, so the same question is asked of the water instead. The two agree:
		// the valve has never been turned exactly while the room is still
		// flooded and the drain has not started.
		if (_script.flag(kFlooded) != 1 || _sewerDraining)
			return;

		CursorMan.showMouse(false);
		_sewerStep = kStepValve;
		debugC(1, kDebugRooms, "sewer: the valve turns, step 0x%02x", kStepValve);
		return;
	}
}

void AlienEngine::stepSewer() {
	if (_room != kSewerRoom)
		return;

	if (_script.flag(kFlooded) == 1) {
		// The water line, recomputed off wherever the character is standing.
		_clipBottom = _ben.spriteY() + (int)_sewerDepth + (int)kRipple[_sewerPhase];
		_dirty = true;

		if (++_sewerPhase >= ARRAYSIZE(kRipple))
			_sewerPhase = 0;

		if (_sewerDraining) {
			if (_sewerDivider == 0)
				_sewerDepth++;
			if (++_sewerDivider > kDrainDivider)
				_sewerDivider = 0;
		}

		// And the surface keeps moving: the tick re-issues slot 3 every frame
		// the room is flooded, the same way a room's lifted loop calls do.
		_anims.relaunch(kRippleSlot);
	}

	switch (_sewerStep) {
	case kStepArriving:
		if (_anims.remaining(kLadderSlot) != 2)
			break;
		CursorMan.showMouse(true);
		_sewerStep = 0;
		_ben.faceTo(kFacingFront);
		debugC(1, kDebugRooms, "sewer: at the foot of the ladder");
		break;

	case kStepClimbing:
		CursorMan.showMouse(false);
		_sewerStep = kStepLeaving;
		break;

	case kStepLeaving:
		if (_anims.remaining(kLadderSlot) != 1)
			break;
		_sewerStep = 0;
		debugC(1, kDebugRooms, "sewer: submode %d, back up the ladder", kSubmodeLadder);
		takeExit(kSubmodeLadder);
		break;

	case kStepValve:
		// Slot 3 is the room's own loop, so this waits for the water to come
		// round to the same point of its cycle every time.
		if (_anims.frame(kRippleSlot) != kDrainCue) {
			debugC(3, kDebugRooms, "sewer: the valve waits for slot %u frame %d, "
				   "which is at %d", kRippleSlot, kDrainCue,
				   _anims.frame(kRippleSlot));
			break;
		}
		_sewerDraining = 1;
		_anims.play(kDrainSlot, 1, 29, 3, 1);
		_anims.stop(kRippleSlot);
		_sewerStep = kStepDraining;
		debugC(1, kDebugRooms, "sewer: draining, step 0x%02x", kStepDraining);
		break;

	case kStepDraining:
		if (_anims.remaining(kDrainSlot) != 0)
			break;
		_script.setFlag(kFlooded, 0);
		CursorMan.showMouse(true);
		_sewerStep = 0;
		_clipBottom = kPlayfieldBottom;
		_dirty = true;
		debugC(1, kDebugRooms, "sewer: drained, the hatch can be reached");

		// The hatch's rectangle is guarded on [0x33be], so the room's hotspots
		// have to be built again now that it is clear.
		_script.buildHotspots(_room, _spots);
		break;

	case kStepHatch:
		if (_anims.remaining(kHatchSlot) != 0)
			break;
		_sewerStep = 0;
		debugC(1, kDebugRooms, "sewer: submode %d, which raises scene 6", kSubmodeHatch);
		takeExit(kSubmodeHatch);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
