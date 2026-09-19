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

// The mailbox full of dynamite, and the way it puts him in town.
//
// Room 25 is the park outside Sluggs' house, and the mailbox beside the gate is
// packed with the old man's Halloween explosives. One rectangle carries both
// halves of it: 100,72..124,122 registers as object 1 while [0xa775] is clear
// and as object 2 once it is set, which is a "he has looked inside" flag the
// room's own click path writes (hotspots.cpp, ovr_19_0e8f:0x00d7).
//
// The matches on either of them is a script body the lift already has -- the
// pair of blocks that queue outcome 13, "I bet this'll wake Sluggs up.. / I
// better get some distance here..." -- and what the lift could not follow is
// everything after the line. Playtest report: "it is not possible to use
// matches with the mailbox/dynamite". The use was possible; nothing answered
// it.
//
// What answers it is entry 3's own top, before any of the click dispatch
// (ovr_19_0e8f:0x0017): while [0xa956] holds 0x4e2a -- the dialog unit's "a
// line has just come down" word -- and [0xacf6], the id the last queue_event
// was raised with, is still 13, the room lights the fuse. PAL_MAIL.DL1 goes on
// slot 0 for 0x52 frames and [0xa49f] takes over:
//
//   2      walk him clear of the blast, OBJ:sub_07890(50, 112, 2)
//   3      0x46 of [0xa49c] later, outcome 16: "This should be a blast..."
//   4      once the fuse has burnt out -- slot 0's remaining count at zero --
//          set [0xa773], play PAL_BLOW.DL1 on slot 1 and ring sample 4 twice
//   0xa    two frames into the blast the character himself goes ([0xa94d])
//   0xf    0x46 later, [0x33bc] = 1 and submode 0x14, which the chain routes
//          room 25 -> room 33 (transitions.cpp)
//
// [0xa773] is not bookkeeping: both links back into the park (room 14 submode 3
// and room 33 submode 2) are guarded on it being clear, so the park closes
// behind the bang.
//
// [0x33bc] is the hand-off, and room 33 is the other end of it. Its enter
// routine (ovr_21_0e97:0x03ce) clears the latch, takes the cursor and the
// character away, puts him down at 179,79 facing left and plays TOW_GETU.DL1 --
// "town get up", 0x63 frames of him picking himself off the road -- on slot 2,
// then waits in its own [0xa49f] state 0xa for that slot to come down to two
// frames left before handing both back. Nothing else in the game writes
// [0x33bc].
static const int kParkRoom = 25;
static const int kTownRoom = 33;

static const byte kMailbox = 1;		///< the rectangle while [0xa775] is clear
static const byte kDynamite = 2;	///< and once he has seen what is inside it
static const byte kMatches = 1;		///< item 1

/// The two outcomes: the one the body queues, and the one the machine speaks.
static const byte kLineMatches = 0x0d;
static const byte kLineBlast = 0x10;

static const uint16 kParkGone = 0xa773;		///< the way back into the park is shut
static const uint16 kTownEntry = 0x33bc;	///< [0x33bc], the hand-off to room 33

static const uint kFuseSlot = 0;	///< PAL_MAIL.DL1
static const uint kBlowSlot = 1;	///< PAL_BLOW.DL1
static const uint kGetUpSlot = 2;	///< TOW_GETU.DL1, in room 33

static const int kFuseFrames = 0x52;
static const int kFuseRate = 2;
static const int kBlowFrames = 7;
static const int kBlowRate = 2;
static const int kGetUpFrames = 0x63;
static const int kGetUpRate = 4;
static const int kPlayMode = 1;		///< all three are anim_play_mode1

/// Where he backs off to, OBJ:sub_07890 at 0x06ee.
static const int kClearX = 0x32, kClearY = 0x70, kClearFacing = 2;

/// [0xa49c] > this before states 3 and 0xf run, at 0x070a and 0x078f.
static const uint16 kBlastWait = 0x46;

/// The blast's own cue: slot 1 past its second frame, [0xa4cc] at 0x0772.
static const int kBlowCue = 1;

/// Sample 4 twice over, sfx_play_delayed(4, 0, 0x36b0, 0x40, 0, 0) at 0x0742
/// and 0x0754 -- one bang laid on top of itself.
static const uint kBlastSample = 4;
static const uint32 kBlastRate = 0x36b0;
static const byte kBlastVolume = 0x40;
static const int8 kBlastPanning = 0;
static const uint16 kBlastDelay = 0;

static const byte kStepClear = 2;
static const byte kStepSpeak = 3;
static const byte kStepBlow = 4;
static const byte kStepBlown = 0x0a;
static const byte kStepLeave = 0x0f;

static const byte kSubmodeTown = 0x14;

/// Room 33's half of it: where it puts him, and the state it waits in.
static const int kTownX = 179, kTownY = 79, kTownFacing = 4;
static const byte kStepGetUp = 0x0a;
static const int kGetUpCue = 2;

/**
 * The matches have just been used: take the cursor away for the line.
 *
 * The rest of that branch (ovr_19_0e8f:0x0073) is the script body the lift has
 * -- outcome 13 and action_handled -- and this is the [0xa948] beside it, which
 * is also what keeps the player from clicking into the machine that follows.
 */
void AlienEngine::armMailbox(int obj, byte item) {
	if (_room != kParkRoom || item != kMatches)
		return;
	if (obj != kMailbox && obj != kDynamite)
		return;

	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "mailbox: the matches are struck, outcome 0x%02x", kLineMatches);
}

/**
 * The park's machine: the fuse, the bang and the road into town.
 *
 * The arming half is entry 3's test of [0xa956] / [0xacf6], turned inside out
 * the way the hallway's and the lab's are: the port has no [0xa956], so this
 * asks whether the queue has just run dry with outcome 13 still standing as
 * the id it was raised with.
 */
void AlienEngine::stepMailbox() {
	if (_room != kParkRoom)
		return;

	if (!_mailboxStep) {
		if (_lastEvent != kLineMatches || !speechDone())
			return;

		// Any other line would overwrite [0xacf6]; clearing it here is what
		// keeps the fuse from being lit a second time by the same id.
		_lastEvent = 0;
		_anims.play(kFuseSlot, 1, kFuseFrames, kFuseRate, kPlayMode);
		_mailboxStep = kStepClear;
		_mailboxPos = 0;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "mailbox: the fuse is lit, step %d", kStepClear);
		return;
	}

	// [0xa49c], which LOGIC advances whether or not a machine is reading it.
	_mailboxPos++;

	switch (_mailboxStep) {
	case kStepClear:
		// 0x06ee: he gets the distance the line said he wanted.
		walkTo(kClearX, kClearY, kClearFacing);
		_mailboxStep = kStepSpeak;
		_mailboxPos = 0;
		break;

	case kStepSpeak: {
		// 0x070a. The wait is on the counter alone, not on the walk: the
		// original speaks over it if the route is still running.
		if (_mailboxPos <= kBlastWait)
			break;
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kLineBlast, anchorX, anchorY);
		_mailboxStep = kStepBlow;
		_mailboxPos = 0;
		break;
	}

	case kStepBlow:
		// 0x0729: the fuse has burnt all the way down.
		if (_anims.remaining(kFuseSlot) != 0)
			break;
		_script.setFlag(kParkGone, 1);
		// PAL_BLOW's own first frame is Ben standing where the walk put him --
		// the bank draws him and then blows him up -- so the walker goes away
		// for the whole play rather than at the original's cue below, which
		// only works while the two are pixel for pixel the same.
		playCharacterAnim(kBlowSlot, 1, kBlowFrames, kBlowRate, kPlayMode);
		_sound.queue(kBlastSample, kBlastRate, kBlastVolume, kBlastPanning, kBlastDelay);
		_sound.queue(kBlastSample, kBlastRate, kBlastVolume, kBlastPanning, kBlastDelay);
		_mailboxStep = kStepBlown;
		debugC(1, kDebugRooms, "mailbox: the mailbox goes up, step 0x%02x", kStepBlown);
		break;

	case kStepBlown:
		// 0x0772: two frames in, the blast is over him. [0xa94d] is already
		// down (the play above took it), so this is only the cue the rest of
		// the machine times itself off.
		if (_anims.frame(kBlowSlot) <= kBlowCue)
			break;
		_mailboxStep = kStepLeave;
		_mailboxPos = 0;
		break;

	case kStepLeave:
		// 0x078f: and then the park is done with him.
		if (_mailboxPos <= kBlastWait)
			break;
		_mailboxStep = 0;
		_script.setFlag(kTownEntry, 1);
		debugC(1, kDebugRooms, "mailbox: submode 0x%02x, blown into town", kSubmodeTown);
		takeExit(kSubmodeTown);
		break;

	default:
		break;
	}
}

/// Room 33 has just opened: the landing, when the mailbox is what sent him.
void AlienEngine::enterTown(int room) {
	// The room being opened is passed in, the way the sewer's open takes it:
	// _room is still the room being left until loadRoom is through.
	if (room != kTownRoom || _script.flag(kTownEntry) != 1)
		return;

	// 0x03d5: the latch is a hand-off and is spent on arrival.
	_script.setFlag(kTownEntry, 0);
	CursorMan.showMouse(false);
	_ben.place(kTownX, kTownY, kTownFacing);
	playCharacterAnim(kGetUpSlot, 1, kGetUpFrames, kGetUpRate, kPlayMode);
	_townStep = kStepGetUp;
	debugC(1, kDebugRooms, "mailbox: down in the road, step 0x%02x", kStepGetUp);
}

/// And the one state that ends it (ovr_21_0e97:0x04aa).
void AlienEngine::stepTown() {
	if (_room != kTownRoom || _townStep != kStepGetUp)
		return;
	if (_anims.remaining(kGetUpSlot) != kGetUpCue)
		return;

	_townStep = 0;
	CursorMan.showMouse(true);
	// TOW_GETU is played two frames longer than it ships, so the range ends
	// past its terminator rather than on it: taking the slot down with the
	// walker is what keeps the frame it stopped on -- Ben down in the road --
	// from being left behind under the Ben who has just stood up.
	showCharacter();
	debugC(1, kDebugRooms, "mailbox: up off the road, the town is his");
}

} // End of namespace Alien
