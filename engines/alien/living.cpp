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

// Room 11's television, and the arrow that comes out of it.
//
// The living room holds four things worth taking: the scissors, the radio, the
// videocassette that goes back out of the recorder, and -- on the satellite
// dish over the set -- an Indian arrow. The arrow is not there when the room
// opens. Its hotspot and its plate are both guarded on [0xa720] (hotspots.cpp,
// plates.cpp), and one routine in the whole game raises that flag:
// ovr_0b_0e77:0x0000, the tape machine.
//
//   anim_play_mode1(slot 6, frame 1, 0x42 frames, rate 5)
//   [0xa720] = 1   the arrow is on the dish
//   [0xa723] = 1   the set is left dead, and its plate with it
//   [0xa725] = 0   the static loop stops
//   [0xa49f] = 3   the room's machine takes over
//   [0xa948] = 0   with the cursor gone until it ends
//
// Sixty-six frames of the tape, and the set shakes the arrow down. Everything
// leading to it is the room's own click dispatch (entry 3, the [0xa956] ==
// 0x4e22 half -- an item used on an object) and none of it is a script row, so
// the lifted table has the cassette going into the recorder and nothing else.
// That is why the port could reach neither the arrow nor the diving suit it
// buys: playtest report "In the basement we should pick up the battery".
//
// The dispatch, 0x0061 to 0x0163, with the remote control (item 7) in hand:
//
//   [0xa6f1] != 1  the battery is not in it -> outcome 0x10, "There's no
//                  battery in this remote control", on the set or the recorder
//   [0xa723] == 1  the tape has already run -> outcome 0x0e, "It's no good.
//                  The TV's broken."
//   set off, no tape in the recorder -> six frames on slot 2, [0xa725] and
//                  [0xa722] set: the set comes on with nothing on it
//   set off, tape in the recorder ([0xa721]) -> the tape machine
//   set on         -> slot 2 is cut short and slot 3 holds one frame: off again
//
// And the other way round, at 0x0109: the cassette (item 25) going into the
// recorder while the set is already on starts the same machine, which is the
// half of that click the lifted row does not carry.
//
// The machine itself is in the room's tick (0x0873), keyed on slot 6's frames
// left -- [0xa4f0] is [0xa4ea] + 6 -- and it is three sounds and the cursor:
//
//   3     sound 2, at once
//   4     0x13 frames left: sound 3
//   8     0x11 frames left: sound 1
//   0xa   one frame left: [0xa49f] = 0 and the cursor comes back
static const int kLivingRoom = 11;

static const int kTelevision = 1;		///< the set, and the dish above it
static const int kRecorder = 2;			///< the VCR under it

static const byte kRemote = 7;			///< item 7, out of room 7
static const byte kCassette = 25;		///< item 25, out of the sitting room

static const uint16 kRemoteBattery = 0xa6f1;	///< the bar's combine put the battery in
static const uint16 kArrowShown = 0xa720;		///< the arrow is on the dish
static const uint16 kCassetteIn = 0xa721;		///< the tape is in the recorder
static const uint16 kSetOn = 0xa722;			///< the set is on
static const uint16 kSetDead = 0xa723;			///< the tape has run and broken it
static const uint16 kSetStatic = 0xa725;		///< the six-frame loop on slot 2

static const byte kLineBroken = 0x0e;		///< ROOM11.TAL: "The TV's broken."
static const byte kLineNoBattery = 0x10;	///< "There's no battery in this remote control."

/// The tape, 0x0000, and the two plays that switch the set itself.
static const uint kTapeSlot = 6;
static const int kTapeFrames = 0x42;
static const int kTapeRate = 5;

static const uint kStaticSlot = 2;
static const int kStaticFrames = 6;
static const int kStaticRate = 1;

static const uint kDeadSlot = 3;

/// [0xa49f]'s steps in this room, and the frames left each waits for.
static const byte kStepStart = 3;
static const byte kStepFirst = 4;
static const byte kStepSecond = 8;
static const byte kStepLast = 0xa;

static const int kFirstCue = 0x13;
static const int kSecondCue = 0x11;
static const int kLastCue = 1;

/// INPUT:sound_trigger, in the order the machine rings them.
static const uint kTapeSample = 2;
static const uint kShakeSample = 3;
static const uint kArrowSample = 1;

/**
 * ovr_0b_0e77:0x0000 -- the tape plays, and the arrow comes down with it.
 */
void AlienEngine::startTape() {
	_anims.play(kTapeSlot, 1, kTapeFrames, kTapeRate, 1);
	_script.setFlag(kArrowShown, 1);
	_script.setFlag(kSetDead, 1);
	_script.setFlag(kSetStatic, 0);
	_livingStep = kStepStart;
	CursorMan.showMouse(false);
	_dirty = true;

	debugC(1, kDebugRooms, "living: the tape runs, step 0x%02x", kStepStart);
}

/**
 * The item-use half of room 11's click dispatch: the remote on the set.
 *
 * @return whether the room answered the click, which is the [0xa602] the
 *         original sets beside each of these branches -- without it the shared
 *         refusal speaks over the room's own line.
 */
bool AlienEngine::armLiving(int obj, byte item, int anchorX, int anchorY) {
	if (_room != kLivingRoom || item != kRemote)
		return false;
	if (obj != kTelevision && obj != kRecorder)
		return false;

	// 0x0147: the remote is useless on either box until the battery is in it.
	if (_script.flag(kRemoteBattery) != 1) {
		queueOutcome(_tal, kLineNoBattery, anchorX, anchorY);
		return true;
	}

	// Past that the branch is the set's alone; the recorder falls through to
	// the generic refusal, as it does in the original.
	if (obj != kTelevision)
		return false;

	// 0x0084: once the tape has run there is nothing left to switch on.
	if (_script.flag(kSetDead) == 1) {
		queueOutcome(_tal, kLineBroken, anchorX, anchorY);
		return true;
	}

	if (_script.flag(kSetOn) == 0) {
		// 0x00a5: with the tape in, the press starts it; without, the set just
		// comes on and shows the six frames of nothing.
		if (_script.flag(kCassetteIn) == 1) {
			startTape();
			return true;
		}
		_anims.play(kStaticSlot, 1, kStaticFrames, kStaticRate, 1);
		_script.setFlag(kSetStatic, 1);
		_script.setFlag(kSetOn, 1);
		_dirty = true;
		debugC(1, kDebugRooms, "living: the set comes on");
		return true;
	}

	// 0x00e8: and off again, the loop cut short and one frame of the dead
	// screen left standing.
	_anims.stop(kStaticSlot);
	_anims.play(kDeadSlot, 1, 1, 0, 1);
	_script.setFlag(kSetStatic, 0);
	_script.setFlag(kSetOn, 0);
	_dirty = true;
	debugC(1, kDebugRooms, "living: the set goes off");
	return true;
}

/**
 * 0x0130, the tail of the cassette's own branch: putting the tape in while the
 * set is already on runs it there and then. The row the lift recovered stops at
 * the recorder's seven frames.
 *
 * Called after the script body, because the flag it reads is the one that body
 * sets.
 */
void AlienEngine::livingCassette(int obj, byte item) {
	if (_room != kLivingRoom || obj != kRecorder || item != kCassette)
		return;
	if (_script.flag(kSetOn) != 1 || _script.flag(kSetDead) != 0)
		return;

	startTape();
}

void AlienEngine::stepLiving() {
	if (_room != kLivingRoom || !_livingStep)
		return;

	switch (_livingStep) {
	case kStepStart:
		// 0x0873: the tape's own sound, on the tick the machine starts.
		_sound.play(kTapeSample, SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		_livingStep = kStepFirst;
		break;

	case kStepFirst:
		// 0x0888
		if (_anims.remaining(kTapeSlot) != kFirstCue)
			break;
		_sound.play(kShakeSample, SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		_livingStep = kStepSecond;
		break;

	case kStepSecond:
		// 0x08a1
		if (_anims.remaining(kTapeSlot) != kSecondCue)
			break;
		_sound.play(kArrowSample, SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		_livingStep = kStepLast;
		break;

	case kStepLast:
		// 0x08ba: the arrow is on the dish and the room is the player's again.
		if (_anims.remaining(kTapeSlot) != kLastCue)
			break;
		_livingStep = 0;
		CursorMan.showMouse(true);
		_script.buildHotspots(_room, _spots);
		_dirty = true;
		debugC(1, kDebugRooms, "living: the tape has run, the arrow is on the dish");
		break;

	default:
		break;
	}
}

} // End of namespace Alien
