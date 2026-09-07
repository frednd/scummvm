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

// Room 8's owl: the first conversation in the port.
//
// The owl is the game's simplest talker and so the one that shows the whole
// path. Three things have to line up before a word is said:
//
//   * the room's clock has to have woken it -- [0x33ac], set 0x5dc tick pairs
//     into a stay (roomtick.cpp), which is what puts the talk verb on its
//     rectangle in the first place;
//   * the click has to reach the room's own machine rather than a script body,
//     because the owl's other arm was lifted without the guards that keep it
//     off a talk (see armLibrary);
//   * and the tree at the end of ROOM8.TAL has to have a topic for the
//     conversation the owl has not had yet.
//
// The click arms [0xa49f] = 0x0f (ovr_08_0e67:0x0341) after queueing an outcome
// of its own, and the machine in the room's tick (0x0f34) runs three steps:
//
//   0x0f  the owl's line has been spoken: give the cursor back, open the
//         conversation menu on topic [0x33ad] (OBJ:sub_0967e), count the topic
//         on -- it stops at 3, which is what retires the talk arm -- and go on
//         to 0x14.
//   0x14  the menu has ended the conversation ([0xa60e]): take the cursor away
//         again and go on to 0x16.
//   0x16  what the player said has been said: play the owl's twelve frames on
//         slot 12 and speak outcome 0x4e through DLGREQ, which is where the
//         owl's own anchor and colour come from, then give the cursor back.
//
// The anchor and the colour are the five immediates room 8's enter routine
// hands DLGREQ:sub_0c250 (ovr_08_0e67:0x0d08): x 108, y 25 -- the owl on its
// shelf -- and a warm orange. Nothing in the port resets the speaker colour
// between lines the way DIALOG:sub_0b6a1 does, so the machine carries one step
// further than the original's does and puts white back when the owl has
// finished.
static const int kLibraryRoom = 8;

static const byte kOwl = 4;					///< the object under the talk verb
static const byte kVerbTalkTo = 6;

static const uint16 kOwlAwake = 0x33ac;		///< the room clock's latch
static const uint16 kOwlTopic = 0x33ad;		///< which conversation comes next, 0..3
static const byte kOwlTopicCount = 3;

/// What the owl says to open each of the three conversations.
static const byte kOwlOpening[kOwlTopicCount] = { 0x41, 0x45, 0x49 };

/// And what it answers with, whichever way the conversation went.
static const byte kOwlReply = 0x4e;

static const uint kOwlSlot = 12;			///< LIB_OWL.DL1, all twelve frames
static const uint kOwlFrames = 12;
static const uint kOwlRate = 3;

static const int kOwlAnchorX = 0x6c;
static const int kOwlAnchorY = 0x19;
static const byte kOwlInk[3] = { 0x3f, 0x28, 0x14 };

static const byte kStepOpen = 0x0f;
static const byte kStepChat = 0x14;
static const byte kStepReply = 0x16;
static const byte kStepColor = 0x17;		///< the port's own: hold the ink

/// The safe, and the item that cracks it: room 15's shelf gives up the
/// stethoscope, and this is the only thing in the game it is used on.
static const byte kSafe = 25;
static const byte kStethoscope = 11;

static const uint kStetSlot = 13;			///< LIB_STET.DL1, 55 frames of Ben
static const uint kSafeSlot = 5;			///< SAFEOPE1.DL1, the door swinging

static const byte kStepListen = 0x64;
static const byte kStepDialOnce = 0x6e;
static const byte kStepDialTwice = 0x78;
static const byte kStepStandUp = 0x82;
static const byte kStepCracked = 0x8c;
static const byte kStepSwing = 0x96;

/// [0xa49c] > this before each of the timed steps ends.
static const uint16 kListenWait = 0x28, kDialWait = 0x32, kDialAgainWait = 0x41,
					kStandWait = 0x5a, kSwingWait = 0x23;

/// What he says once it is open (queue_event(11), ovr_08_0e67:0x10a3).
static const byte kSafeLine = 11;

/// The two 10c9:sub_11e9d hands the delay queue as the door comes open.
static const uint kSafeSample1 = 4, kSafeSample2 = 3;
static const uint32 kSafeRate1 = 0x8ca0, kSafeRate2 = 0x2710;
static const byte kSafeVolume = 0x40;
static const int8 kSafePanning = 0x3e;
static const uint16 kSafeDelay2 = 6;

bool AlienEngine::armLibrary(int obj, byte verb, bool item, int anchorX, int anchorY) {
	if (_room != kLibraryRoom || item || obj != kOwl || verb != kVerbTalkTo)
		return false;

	// The talk arm only exists while the owl is awake, and the hotspot pass
	// retires it once all three conversations have been had ([0x33ad] != 3 is
	// the guard on the arm that carries the verb).
	if (_script.flag(kOwlAwake) != 1)
		return false;

	const byte topic = _script.flag(kOwlTopic);
	if (topic >= kOwlTopicCount)
		return false;

	queueOutcome(_tal, kOwlOpening[topic], anchorX, anchorY);
	CursorMan.showMouse(false);
	_libraryStep = kStepOpen;

	debugC(1, kDebugChat, "owl: conversation %u opens with outcome 0x%02x",
		   topic, kOwlOpening[topic]);
	return true;
}

void AlienEngine::stepLibrary() {
	if (_room != kLibraryRoom || !_libraryStep)
		return;

	switch (_libraryStep) {
	case kStepOpen:
		if (!speechDone())
			return;

		CursorMan.showMouse(true);
		openChat(_script.flag(kOwlTopic));

		// The counter stops at three, and the third conversation is the last
		// one the owl's rectangle offers.
		if (_script.flag(kOwlTopic) < kOwlTopicCount)
			_script.setFlag(kOwlTopic, _script.flag(kOwlTopic) + 1);

		_libraryStep = kStepChat;
		break;

	case kStepChat:
		if (!_chat.isFinished())
			return;

		CursorMan.showMouse(false);
		_libraryStep = kStepReply;
		break;

	case kStepReply:
		if (!speechDone())
			return;

		_anims.play(kOwlSlot, 1, kOwlFrames, kOwlRate, 1);
		setTextColor(kOwlInk[0], kOwlInk[1], kOwlInk[2]);
		uploadTextColor();
		queueOutcome(_tal, kOwlReply, kOwlAnchorX, kOwlAnchorY);
		CursorMan.showMouse(true);
		_libraryStep = kStepColor;

		debugC(1, kDebugChat, "owl: answers with outcome 0x%02x", kOwlReply);
		break;

	case kStepColor:
		// White is what the original resets to before every line; the port has
		// no such reset, so the room takes its colour back itself.
		if (!speechDone())
			return;

		setTextColor(0x3f, 0x3f, 0x3f);
		uploadTextColor();
		_libraryStep = 0;
		break;

	default:
		// The safe's states run on the tick pair, in stepLibrarySafe: they are
		// the same [0xa49f] the owl's are, so they come through here too.
		break;
	}
}

/**
 * A click body is about to run: start the safe if it is the stethoscope's.
 *
 * The room's other [0xa49f] machine, and the longest of the ones the port runs.
 * The arm is ovr_08_0e67:0x0049, under the item-use half of the room's click
 * dispatch ([0xa956] == 0x4e22) with item 11 in hand and object 25 clicked: it
 * takes the cursor and the walker away ([0xa948] and [0xa94d]), starts the
 * first ten frames of LIB_STET on slot 13 -- which the lifted body plays too,
 * with the same arguments -- and sets [0xa49f] to 100 with [0xa49c] at zero.
 *
 * From there the room's tick (ovr_08_0e67:0x0fd8 onwards) runs five more steps,
 * each waiting on that counter and playing the next stretch of the same bank:
 *
 *   0x64  40 ticks after the arm: frames 10..25, the ear against the door.
 *   0x6e  50 later: frames 26..35, the dial.
 *   0x78  65 later: frames 26..35 again, the dial a second time.
 *   0x82  90 later: frames 36..55, which is him standing back up.
 *   0x8c  waits for that play to have two frames left rather than for the
 *         clock, and gives the walker back ([0xa94d] = 1).
 *   0x96  35 later: the safe swings open on slot 5, the two sounds of
 *         10c9:sub_11e9d go into the delay queue, outcome 11 is spoken and the
 *         cursor comes back.
 *
 * Without the machine the port played the arm's ten frames and stopped there,
 * which is both halves of playtest report 11: the safe never opened, and the
 * ten-frame play -- mode 2, which holds the frame before the last one it
 * advanced to -- left him crouched at the door for good. The whole of
 * LIB_STET's last stretch is what takes that picture away again: frame 54 is a
 * four-by-two speck, so the slot ends holding nothing anyone can see.
 */
void AlienEngine::armLibrarySafe(int obj, byte item) {
	if (_room != kLibraryRoom || obj != kSafe || item != kStethoscope)
		return;

	_libraryStep = kStepListen;
	_libraryPos = 0;
	_drawCharacter = false;
	CursorMan.showMouse(false);

	debugC(1, kDebugRooms, "library: the stethoscope goes on the safe, step 0x%02x",
		   kStepListen);
}

void AlienEngine::stepLibrarySafe() {
	if (_room != kLibraryRoom)
		return;

	// [0xa49c], which LOGIC:sub_11f79 advances on every tick pair whether or
	// not a machine is running (11f3:0069).
	_libraryPos++;

	switch (_libraryStep) {
	case kStepListen:
		if (_libraryPos <= kListenWait)
			break;
		_anims.play(kStetSlot, 10, 16, 3, 2);
		_libraryStep = kStepDialOnce;
		_libraryPos = 0;
		break;

	case kStepDialOnce:
		if (_libraryPos <= kDialWait)
			break;
		_anims.play(kStetSlot, 26, 10, 5, 2);
		_libraryStep = kStepDialTwice;
		_libraryPos = 0;
		break;

	case kStepDialTwice:
		if (_libraryPos <= kDialAgainWait)
			break;
		_anims.play(kStetSlot, 26, 10, 5, 2);
		_libraryStep = kStepStandUp;
		_libraryPos = 0;
		break;

	case kStepStandUp:
		if (_libraryPos <= kStandWait)
			break;
		_anims.play(kStetSlot, 36, 20, 4, 2);
		_libraryStep = kStepCracked;
		_libraryPos = 0;
		break;

	case kStepCracked:
		// This one waits on the play, not on the clock: he is his own again two
		// frames before it ends, which is where the walker takes the pose back.
		if (_anims.remaining(kStetSlot) != 2)
			break;
		_drawCharacter = true;
		_libraryStep = kStepSwing;
		_libraryPos = 0;
		break;

	case kStepSwing: {
		if (_libraryPos <= kSwingWait)
			break;
		_anims.play(kSafeSlot, 1, 7, 1, 1);
		_sound.queue(kSafeSample1, kSafeRate1, kSafeVolume, kSafePanning, 0);
		_sound.queue(kSafeSample2, kSafeRate2, kSafeVolume, kSafePanning, kSafeDelay2);

		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kSafeLine, anchorX, anchorY);
		CursorMan.showMouse(true);
		_libraryStep = 0;

		debugC(1, kDebugRooms, "library: the safe is open, outcome %d", kSafeLine);
		break;
	}

	default:
		break;
	}
}

} // End of namespace Alien
