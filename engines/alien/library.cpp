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

// What the room's enter routine draws from the puzzle state (enterLibrary).
static const uint16 kShelfPushed = 0xa708;	///< the bookshelf is off the safe
static const uint16 kSafeShut = 0xa709;		///< 0 once the safe has been cracked
static const uint16 kPadlockOff = 0xa70b;	///< 0 once the key has taken it off
static const uint16 kSafeItem1 = 0xa70c;	///< what is still inside, the pair
static const uint16 kSafeItem2 = 0xa70d;	///< the two banks below pick between

static const uint kShelfPageA = 0;			///< SHELF_A.DL1, columns 266..319
static const uint kShelfPageB = 1;			///< SHELF_B.DL1, page B's 0..55
static const uint kPadlockSlot = 10;		///< LIB_LODR.DL1, page B's 13..32

// The four banks of the open safe, by what is left on its shelf, and the frame
// of each that is the door standing open.
static const uint kSafeEmptyBank = 2;		///< SAFEOPEN.DL1
static const uint kSafeBothBank = 5;		///< SAFEOPE1.DL1
static const uint kSafeSecondBank = 6;		///< SAFEOPE2.DL1
static const uint kSafeFirstBank = 7;		///< SAFEOPE3.DL1
static const int kSafeOpenFrame = 7;	///< the door standing open
static const int kSafeFrames = 7;		///< and the whole swing, either way

/// Which of the four the puzzle state names ([0x9908]/[0xa5ef]).
static uint safeBank(bool first, bool second) {
	if (first && second)
		return kSafeBothBank;
	if (first)
		return kSafeFirstBank;
	if (second)
		return kSafeSecondBank;
	return kSafeEmptyBank;
}

// The objects the safe answers under: its door open and shut, and the two
// things on its shelf. The door's rectangle is one box that registers under
// whichever of them the state allows (hotspots.cpp, room 8).
static const byte kSafeDoorOpen = 8;
static const byte kSafeDoorShut = 10;
static const byte kSafeShelfPair = 12;
static const byte kSafeShelfSingle = 13;

/// What the shelf gives up: OBJ:sprite_add(0x14)/(0xe) and (0x13).
static const byte kShelfItem1 = 20;
static const byte kShelfItem2 = 14;
static const byte kShelfItem3 = 19;

/// And the sample the door makes on the way shut (play_sample(3, 0, 15000, ...)).
static const uint kShutSample = 3;
static const uint32 kShutRate = 15000;
static const uint16 kShutDelay = 6;

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
	// The four plays below all land on this one slot, so naming it once here
	// hands the whole sequence to showCharacter() at the end of it.
	hideCharacter(kStetSlot);
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
		// Mode 2 comes back to the frame it started on when it runs out, so
		// the slot would go on drawing him crouched at the safe with the walker
		// already standing: it goes down with him.
		showCharacter();
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

/**
 * Room 8's own plate patch-up, the one that does not live in the 10c9 unit.
 *
 * Finding #71 lifted every room's `10c9` routine, and room 8's (`10c9:0a0c`) is
 * three steps about the bookshelf alone. The rest of what the library has to
 * draw from its puzzle state is written by the room's *enter routine*, straight
 * into the two background pages before the 10c9 call ever runs
 * (`ovr_08_0e67:0x0b79..0x0c2c`) -- the only overlay in the game that does this,
 * room 21's single call aside:
 *
 * ```
 * 0b79  mov byte [0xa5ef], 2        ; which safe bank holds what is still inside
 * 0b8c    [0xa70c]==1 && [0xa70d]==1 -> 5   SAFEOPE1, both
 * 0b9f    [0xa70c]==1 && [0xa70d]==0 -> 7   SAFEOPE3
 * 0bb2    [0xa70c]==0 && [0xa70d]==1 -> 6   SAFEOPE2
 * 0bb7  [0xa708]==1: SHELF_A frame 1 -> page A, SHELF_B frame 1 -> page B
 * 0be0  [0xa709]==0: that bank's frame 7 -> both pages, the safe standing open
 * 0c0d  [0xa708]==0 && [0xa70b]==0: LIB_LODR frame 1 -> page B: the shelf
 *       with its padlock off, for the stretch between unlocking it and pushing it
 * ```
 *
 * Without it the room is drawn from its plate alone, so a safe that has been
 * cracked comes back shut and the padlock comes back with it the moment the
 * room is entered again -- or restored, which is how the second batch's report
 * found it: the state block had the flags right (the key was gone, the hotspots
 * inside the safe answered), and only the picture had forgotten.
 *
 * **Two of the three banks belong to the second page.** Room 8 is 380 pixels
 * wide, so its B plate is the room's last 60 columns, and a bank written into
 * that page addresses it as the flat 320-wide buffer it is: every one of
 * SHELF_B's strips starts at column 319 and runs 48 pixels, which is one pixel
 * at the end of a row and forty-seven at the start of the next. Unwound, the
 * two cover room 320..376 and 333..352 -- and that second rectangle is the
 * padlock's own, 338,95..354,113. The port keeps a wide room as one stitched
 * plate, so both go down through DL1Sprite::drawFramePage with the page's
 * origin. The safe's second stamp is dropped: page A already holds the columns
 * it draws, and the original writes it twice only because it keeps two pages.
 */
void AlienEngine::enterLibrary(int room) {
	if (room != kLibraryRoom)
		return;

	// [0xa5ef]: which of the four safe banks is the one whose open door also
	// shows what is still on the shelf inside.
	const uint bank = safeBank(_script.flag(kSafeItem1) == 1,
							   _script.flag(kSafeItem2) == 1);

	if (_script.flag(kShelfPushed) == 1) {
		_anims.stamp(kShelfPageA, 1, _background);
		_anims.stamp(kShelfPageB, 1, _background, DL1Sprite::kNoClipBottom,
					 kScreenWidth);
	}

	if (_script.flag(kSafeShut) == 0)
		_anims.stamp(bank, kSafeOpenFrame, _background);

	if (_script.flag(kShelfPushed) == 0 && _script.flag(kPadlockOff) == 0)
		_anims.stamp(kPadlockSlot, 1, _background, DL1Sprite::kNoClipBottom,
					 kScreenWidth);

	debugC(1, kDebugPlate, "library: shelf %d, safe %d on bank %u, padlock off %d",
		   _script.flag(kShelfPushed), _script.flag(kSafeShut), bank,
		   _script.flag(kPadlockOff));
}

/**
 * The four bodies of room 8's safe, which the lifted table cannot carry.
 *
 * Everything the safe's door and its shelf do picks an animation slot from
 * `[0x9908]` -- the walk unit's scratch word, reused -- which a run of guards
 * above the dispatch writes from what is still inside (`ovr_08_0e67:0x018a`,
 * the same four cases the enter routine writes `[0xa5ef]` from):
 *
 *   both there -> SAFEOPE1 (5)   only the first -> SAFEOPE3 (7)
 *   only the second -> SAFEOPE2 (6)   nothing left -> SAFEOPEN (2)
 *
 * tools/roomlogic.py follows the immediates and not the register, so obj 8's
 * body came out of the lift as `anim_play_mode1(1, 7, 1)` -- three arguments
 * where four were wanted, and slot 1 is SHELF_B -- and obj 10's as
 * `anim_play_mode3(7, 7, 1)`. The two that take what is on the shelf (0x0236
 * and 0x0279) were dropped whole, which is why the safe could be cracked and
 * then not emptied: the rectangles for them register (the hotspot pass has
 * them under `[0xa709] == 0`), the click resolves, and nothing answered it.
 *
 * So the room runs its own four, before the table is asked:
 *
 *   obj  8  the door swings open: that bank's frames 1..7, the two samples of
 *           10c9:sub_11e9d, [0xa709] = 0
 *   obj 10  and shut again: the same seven backward, one sample, [0xa709] = 1
 *   obj 12  the shelf's pair comes off it: the bank *without* them, frame 7,
 *           items 20 and 14, [0xa70c] = 0
 *   obj 13  and the other one: items 19, [0xa70d] = 0
 */
bool AlienEngine::runLibraryBody(int obj, bool item) {
	// The whole block sits under the plain-verb half of the dispatch
	// ([0xa956] == 0x4e25), so an item in hand never reaches it.
	if (_room != kLibraryRoom || item)
		return false;

	const bool first = _script.flag(kSafeItem1) == 1;
	const bool second = _script.flag(kSafeItem2) == 1;

	switch (obj) {
	case kSafeDoorOpen:
		_anims.play(safeBank(first, second), 1, kSafeFrames, 1, 1);
		_sound.queue(kSafeSample1, kSafeRate1, kSafeVolume, kSafePanning, 0);
		_sound.queue(kSafeSample2, kSafeRate2, kSafeVolume, kSafePanning, kSafeDelay2);
		_script.setFlag(kSafeShut, 0);
		break;

	case kSafeDoorShut:
		// Mode 3 runs the range backward from the frame it is given, so this is
		// the same seven frames the other way about.
		_anims.play(safeBank(first, second), kSafeOpenFrame, kSafeFrames, 1, 3);
		_sound.queue(kShutSample, kShutRate, kSafeVolume, kSafePanning, kShutDelay);
		_script.setFlag(kSafeShut, 1);
		break;

	case kSafeShelfPair:
		// The frame that is left is the one the *other* bank holds: taking this
		// pair off the shelf leaves either the single item or an empty safe.
		_anims.play(safeBank(false, second), kSafeOpenFrame, 1, 0, 1);
		_inventory.add(kShelfItem1);
		_inventory.add(kShelfItem2);
		_script.setFlag(kSafeItem1, 0);
		break;

	case kSafeShelfSingle:
		_anims.play(safeBank(first, false), kSafeOpenFrame, 1, 0, 1);
		_inventory.add(kShelfItem3);
		_script.setFlag(kSafeItem2, 0);
		break;

	default:
		return false;
	}

	debugC(1, kDebugRooms, "library: object %d answered by the room, safe %d, "
		   "shelf %d/%d", obj, _script.flag(kSafeShut), _script.flag(kSafeItem1),
		   _script.flag(kSafeItem2));
	return true;
}

} // End of namespace Alien
