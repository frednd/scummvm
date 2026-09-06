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
		_libraryStep = 0;
		break;
	}
}

} // End of namespace Alien
