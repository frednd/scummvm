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

// The basement, which is how the first act gets out of the house.
//
// Room 13 is the one link between the bedroom and the sewer, and the sewer is
// what unlocks the front door (sewer.cpp). Its two ways out are the ladder back
// up to room 7 and the hole down to room 35, and neither of them is an exit in
// the sense the rest of the game means it.
//
// **Room 13 never calls OBJ:sub_078dd.** That routine is the shared arrival
// test -- route run out, standing 5 ticks, within three pixels of the approach
// point, facing the recorded way -- and it ends by copying walk_submode into
// game_submode, which is what takes every other room out of itself. It has 42
// xrefs; the two overlays that do not carry one are this room and room 46. So
// an armed submode does nothing here on its own: the room answers the arrival
// with an animation of the character climbing, and writes game_submode out of
// how far that animation has got.
//
// Both arms sit at the end of the tick (ovr_0d_0e6f:0x0bb5 and 0x0c21) and have
// the same shape as the shared test with the position and facing clauses
// dropped and the standing count raised:
//
//   walk_submode 1, standing 7, [0xa6d1] clear
//       anim_play_mode1(10, 1, 0x10, 2), state 0x0a, hide the character
//   walk_submode 2, standing 0x19, [0xa6d0] clear
//       anim_play_mode1(9, 1, 0x21, 3), state 0x1e, hide the character
//
// -- so he pauses a good deal longer at the hole than at the ladder. Both
// one-shot flags are reset by the room's own open (0x0816), so leaving and
// coming back arms them again. The exits then come off the slots the arms
// started, not off the arrival:
//
//   [0xa4f4] == 1        slot 10 has one frame left: submode 1, room 7
//   state 0x1e, [0xa4f3] == 0   slot 9 has run out: submode 10, room 35
//
// The way in is the same idea from the other side. Arriving from the bedroom
// (game_mode == 7) the open plays slot 0 forwards -- eleven frames of him
// coming down -- takes the cursor and the character away and starts state 3;
// arriving from anywhere else it stamps slot 0's last frame and leaves it
// there. State 3 waits for the climb to be seven frames from the end and rings
// it, state 4 answers with a second sample, and the frame before the animation
// finishes the character is given back and sent walking away from the ladder.
//
// The hole is not reachable until the rope is on it: obj 1 arms submode 2 only
// while [0xa6ee] is set (walkgeom.cpp, ovr_0d_0e6f:0x03b8), and [0xa6ee] is
// what using item 8 on obj 2 sets. Item 8 is the rope, and room 3's obj 4 is
// where the game hands it out.
static const int kBasementRoom = 13;

/// The other overlay with no shared arrival test, for the suppression rule.
static const int kDivingRoom = 46;

static const uint kClimbSlot = 0;		///< the way in from the bedroom
static const uint kHoleSlot = 9;		///< down to the sewer
static const uint kLadderSlot = 10;		///< back up to the bedroom

static const uint16 kHoleArmed = 0xa6d0;	///< one-shot, cleared by the room open
static const uint16 kLadderArmed = 0xa6d1;
static const uint16 kCabinetVariant = 0xa700;

static const byte kStepArriving = 3;
static const byte kStepArrived = 4;
static const byte kStepLadder = 0x0a;
static const byte kStepLadderSound = 0x0b;
static const byte kStepHole = 0x1e;

// The two numbers an exit here carries are not the same number. What the walk
// geometry arms is a *walk* submode -- obj 5 arms 1 and obj 1 arms 2
// (ovr_0d_0e6f:0x03af and 0x03b8) -- and what the room finally writes into
// game_submode is what the transition chain is keyed on. For the ladder the two
// happen to coincide; for the hole they do not, and the chain link is
// "room 13, submode 10 -> room 35".
static const byte kArmedLadder = 1;
static const byte kArmedHole = 2;

static const byte kSubmodeLadder = 1;
static const byte kSubmodeHole = 10;

static const byte kEnterFromBedroom = 7;

/// [0xa808]: how long he stands before each arm answers.
static const int kLadderWait = 7;
static const int kHoleWait = 0x19;

/// The two samples the climb down rings, INPUT:sound_trigger.
static const uint kArrivalSample = 3;
static const uint kLadderSample = 4;

/// Where the room sends him once he is off the ladder, OBJ:sub_07890(203, 126, 4).
static const int kOffLadderX = 203;
static const int kOffLadderY = 126;
static const int kOffLadderFacing = 4;

/// The frames of slot 0 the arrival machine keys on.
static const int kArrivalCue = 7;
static const int kArrivalRelease = 1;

/// And the frame of slot 10 the ladder leaves on.
static const int kLadderCue = 9;
static const int kLadderLeave = 1;

/**
 * Whether this room answers an arrival itself rather than through sub_078dd.
 *
 * Called by checkExit, which is the port's model of that routine. Room 46 is
 * listed for the same reason room 13 is -- its overlay has no call either --
 * although nothing in the port drives it yet.
 */
bool AlienEngine::roomHasSharedExit(int room) const {
	return room != kBasementRoom && room != kDivingRoom;
}

/// The room has just opened: the climb down, when he came in over it.
void AlienEngine::enterBasement(int room) {
	// The room being opened is passed in: loadRoom does not publish it as _room
	// until after this runs, so _room here is still the room being left.
	if (room != kBasementRoom)
		return;

	// 0x0816: both arms are armed afresh on every entry.
	_script.setFlag(kHoleArmed, 0);
	_script.setFlag(kLadderArmed, 0);

	if (_mode != kEnterFromBedroom) {
		// 0x0855: no climb, just slot 0 left standing at its last frame.
		_anims.play(kClimbSlot, 11, 1, 0, 1);
		return;
	}

	// 0x0837: eleven frames of him coming down, with the character and the
	// cursor out of the way until the animation gives them back.
	playCharacterAnim(kClimbSlot, 1, 11, 2, 1);
	_basementStep = kStepArriving;
	_basementClimbing = true;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "basement: down from room %d, step %d",
		   _mode, kStepArriving);
}

void AlienEngine::stepBasement() {
	if (_room != kBasementRoom)
		return;

	// 0x0a40: the character comes back a frame before the climb ends, and the
	// room walks him off the foot of the ladder rather than placing him.
	//
	// The original tests slot 0's frame count alone, and can do so because the
	// only thing that ever leaves that slot part-way through is the climb. The
	// port cannot: its other entry stamps the slot's last frame with a count of
	// one and a rate of zero, which stands at one frame left for as long as the
	// room is open, so the test is tied to the climb explicitly. It outlives the
	// state machine, which ends two frames earlier.
	if (_basementClimbing && _anims.remaining(kClimbSlot) == kArrivalRelease) {
		_basementClimbing = false;
		// The slot goes down with him: the climb has one frame left here, and
		// leaving it to finish would put the drawn-on Ben back on the ladder
		// for that frame with the walker already at the foot of it.
		showCharacter();
		walkTo(kOffLadderX, kOffLadderY, kOffLadderFacing);
		debugC(1, kDebugRooms, "basement: off the ladder, walking to %d,%d",
			   kOffLadderX, kOffLadderY);
	}

	switch (_basementStep) {
	case kStepArriving:
		// 0x0b0c
		if (_anims.remaining(kClimbSlot) != kArrivalCue)
			break;
		_basementStep = kStepArrived;
		break;

	case kStepArrived:
		// 0x0b1e
		_sound.play(kArrivalSample, SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		_basementStep = 0;
		CursorMan.showMouse(true);
		break;

	case kStepLadder:
		// 0x0b30
		if (_anims.remaining(kLadderSlot) != kLadderCue)
			break;
		_basementStep = kStepLadderSound;
		break;

	case kStepLadderSound:
		// 0x0b42
		_sound.play(kLadderSample, SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		_basementStep = 0;
		break;

	case kStepHole:
		// 0x0b54: the climb down has run, and the room ends on submode 10.
		if (_anims.remaining(kHoleSlot) != 0)
			break;
		_basementStep = 0;
		debugC(1, kDebugRooms, "basement: submode %d, down to the sewer",
			   kSubmodeHole);
		takeExit(kSubmodeHole);
		return;

	default:
		break;
	}

	// 0x0c3d: and the ladder's own exit, one frame before its animation ends.
	// The original tests the slot alone; the arm's own one-shot is added here
	// because it is the only thing in the room that ever plays slot 10, so the
	// two are equivalent and this cannot answer a frame count left by anything
	// else.
	if (_script.flag(kLadderArmed) == 1 &&
		_anims.remaining(kLadderSlot) == kLadderLeave) {
		debugC(1, kDebugRooms, "basement: submode %d, back up to the bedroom",
			   kSubmodeLadder);
		takeExit(kSubmodeLadder);
		return;
	}

	// The two arrival arms. Both want the route run out, which is the branch of
	// the tick this is called from, plus a standing count of their own.
	if (_ben.isWalking() || _ben.isTurning())
		return;

	debugC(3, kDebugRooms, "basement: armed %u, standing %d, hole %d, ladder %d",
		   _armed, _ben.idleCount(), _script.flag(kHoleArmed),
		   _script.flag(kLadderArmed));

	if (_armed == kArmedLadder && _ben.idleCount() >= kLadderWait &&
		_script.flag(kLadderArmed) == 0) {
		// 0x0bb5
		playCharacterAnim(kLadderSlot, 1, 0x10, 2, 1);
		_basementStep = kStepLadder;
		_script.setFlag(kLadderArmed, 1);
		_script.setFlag(kCabinetVariant, 0);
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "basement: up the ladder, step 0x%02x", kStepLadder);
		return;
	}

	if (_armed == kArmedHole && _ben.idleCount() >= kHoleWait &&
		_script.flag(kHoleArmed) == 0) {
		// 0x0c21
		playCharacterAnim(kHoleSlot, 1, 0x21, 3, 1);
		_basementStep = kStepHole;
		_script.setFlag(kHoleArmed, 1);
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "basement: into the hole, step 0x%02x", kStepHole);
		return;
	}
}

} // End of namespace Alien
