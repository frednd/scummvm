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


#include "common/events.h"
#include "common/system.h"

#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/play.h"
#include "alien/resources.h"

namespace Alien {

// Room 26, the inside of Yodle's tree hut: the parrot, the note, the safe.
//
// docs/room_scripts.md's rows for object 6 are only ever `action_handled = 1`
// -- every reaction is a `call` or a step of the room's [0xa49f] machine
// (ovr_1a_0eaf entry 2, 0x0944-0x0cc5), none of which the lift can carry. The
// machine, whole:
//
//   2-5        any item but the bread: the parrot says 0x13, Ben says 0x15
//   0x14-0x15  a plain verb's line (event 0x12) has come down: the parrot
//              answers with 0x13 (entry 3 at 0x0017, [0xa956] == 0x4e2a)
//   0x1e-0x2a  the moldy bread on a hungry parrot: it eats (two mode 8 lists
//              on slot 0), talks, the bread leaves the bag (sprite_remove(3)),
//              it falls over (slot 0, 0x6a frames from 0x18), something gives
//              (slot 2), Ben steps back, and the safe opens (slot 1)
//   0x32-0x3a  the first visit ([0xa756]): "Yodle's not home", a walk over to
//              the parrot, "Where's Yodle gone?", the parrot, and Ben's guess
//
// Every line the parrot says is DIALOG:sub_0b63a at (0x95, 0x1a), which also
// points slot 0's frame list at the beak (ds:0x43a6); the state that waits for
// the line points it back at the idle list (ds:0x4390). Ben's are plain
// queue_events at his own anchor.
//
// The first port of this file ran the feed on slot 3 -- the squirrel -- with
// the fall's count and first frame swapped, never took the bread, and left the
// first visit out: manual playthrough #44 and #46.

static const int kForestRoom = 26;
static const byte kParrotObj = 6;
static const byte kNoteObj = 20;
static const byte kBread = 3;

static const byte kVerbLookAt = 5;
static const byte kSelfSubmode = 111;

static const uint16 kMood = 0xa752;			///< 1 hungry, 2 eating, 3 fed
static const uint16 kSafeShut = 0xa753;		///< cleared as the safe opens
static const uint16 kNoteRead = 0xa755;		///< the note's line has been said
static const uint16 kVisited = 0xa756;		///< the first visit has played
static const uint16 kScenePlayed = 0xa6d3;	///< CUTSCENE's teardown flag

/// The banks, in the overlay's load order.
static const uint kParrotSlot = 0;			///< parrot.dl1
static const uint kSafeSlot = 1;			///< yodlesaf.dl1
static const uint kLedSlot = 2;				///< yodleled.dl1

/// ds:0x4390, 0x43a6, 0x43bc and 0x43cc: idle, beak, and the two eating lists.
static const byte kParrotIdle[] = {
	1, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 1, 1, 1, 1, 5
};
static const byte kParrotBeak[] = {
	5, 6, 6, 7, 7, 8, 8, 7, 7, 8, 8, 7, 7, 8, 7, 6, 6, 5, 4, 4, 5, 5, 10
};
static const byte kParrotEat1[] = {
	10, 11, 12, 13, 14, 15, 16, 17, 17, 16, 15, 14, 13, 12, 11, 10
};
static const byte kParrotEat2[] = {
	19, 18, 19, 18, 19, 20, 19, 20, 19, 20, 21, 20, 21, 20, 18
};
static const int kIdleRate = 2;
static const int kListMode = 6;
static const int kReturnMode = 8;

/// DIALOG:sub_0b63a's anchor, the same for every line the parrot says.
static const int kParrotX = 0x95, kParrotY = 0x1a;

/// The lines, by outcome code.
static const byte kLineCroak = 0x13;		///< "Croak... Food.."
static const byte kLineNotInterested = 0x15;
static const byte kLineSmell = 0x16, kLineBread = 0x17, kLineLove = 0x18;
static const byte kLineWhat = 0x1f, kLineSafe = 0x20;
static const byte kLineNotHome = 0x05, kLineWhere = 0x07, kLineOut = 0x08;
static const byte kLineNote = 0x1e;
static const byte kEventTalk = 0x12;		///< what a plain verb on him raises

/// The states.
enum {
	kStepBrushOff = 2, kStepBrushOffSay = 3, kStepBrushOffBen = 4, kStepBrushOffDone = 5,
	kStepAnswer = 0x14, kStepAnswerDone = 0x15,
	kStepFeed = 0x1e, kStepEat = 0x1f, kStepSmell = 0x20, kStepBread = 0x21,
	kStepLove = 0x22, kStepSwallow = 0x23, kStepFall = 0x24, kStepLed = 0x25,
	kStepBack = 0x26, kStepWhat = 0x27, kStepSafe = 0x28, kStepSafeLine = 0x29,
	kStepFedDone = 0x2a,
	kStepVisit = 0x32, kStepVisitLine = 0x33, kStepVisitPause = 0x34,
	kStepVisitWalk = 0x35, kStepVisitWhere = 0x36, kStepVisitWhereDone = 0x37,
	kStepVisitCroak = 0x38, kStepVisitCroakDone = 0x39, kStepVisitOut = 0x3a
};

/// Where the first visit walks him, 1021:sub_1023c(0x8c, 0x78, 0x89, 0x72, 2),
/// and where he steps back to for the safe, OBJ:sub_07890(0xb3, 0x7a, 2).
static const int kVisitViaX = 0x8c, kVisitViaY = 0x78;
static const int kVisitX = 0x89, kVisitY = 0x72, kVisitFacing = 2;
static const int kWatchX = 0xb3, kWatchY = 0x7a, kWatchFacing = 2;

/// CUTSCENE:sub_0d384's picture.
static const char *const kNotePicture = "YOD_NOTE.PCX";

/// DIALOG:sub_0b63a: the parrot speaks, and its beak moves while it does.
void AlienEngine::parrotSays(byte line) {
	queueOutcome(_tal, line, kParrotX, kParrotY, false);
	_anims.play(kParrotSlot, 0, ARRAYSIZE(kParrotBeak), kIdleRate, kListMode, kParrotBeak);
	debugC(1, kDebugRooms, "forest: the parrot says outcome 0x%02x", line);
}

/// And the state after it: the idle list back under the loop.
void AlienEngine::parrotQuiet() {
	_anims.play(kParrotSlot, 0, ARRAYSIZE(kParrotIdle), kIdleRate, kListMode, kParrotIdle);
}

/// A plain queue_event: Ben's own line, at his own anchor.
void AlienEngine::forestBenSays(byte line) {
	int x, y;
	characterAnchor(x, y);
	queueOutcome(_tal, line, x, y);
}

void AlienEngine::forestArm(byte step) {
	_forestStep = step;
	_forestWait = 0;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "forest: step 0x%02x", step);
}

/**
 * Entry 3, as far as the parrot goes (0x0049-0x00b9 for items, 0x00e4 for the
 * plain verbs). The already-fed row, queue_event(28), is the lifted table's.
 */
void AlienEngine::armForestParrot(int obj, byte verb, int item) {
	if (_room != kForestRoom || obj != kParrotObj)
		return;
	if (_forestStep)
		return;

	if (item == Inventory::kNoItem) {
		// 0x00e4: the cursor goes for whatever the verb says, and the
		// answer is armed when event 0x12 comes down (0x0017).
		if (_script.flag(kMood) == 1) {
			_forestTalk = true;
			CursorMan.showMouse(false);
		}
		return;
	}

	if (_script.flag(kMood) == 3)
		return;

	if (item == kBread && _script.flag(kMood) == 1)
		forestArm(kStepFeed);
	else
		forestArm(kStepBrushOff);
}

void AlienEngine::stepForestParrot() {
	if (_room != kForestRoom)
		return;

	// Entry 3's arrival half: the plain verb's line is down.
	if (_forestTalk && speechDone()) {
		_forestTalk = false;
		if (_lastEvent == kEventTalk && !_forestStep)
			forestArm(kStepAnswer);
		else
			CursorMan.showMouse(true);
	}

	if (!_forestStep)
		return;

	// [0xa49c], one a tick pair.
	_forestWait++;

	switch (_forestStep) {
	// --- The brush-off, and the answer to a plain verb ----------------------
	case kStepBrushOff:
		if (_forestWait <= 0x14)
			break;
		_forestStep = kStepBrushOffSay;
		break;

	case kStepBrushOffSay:
		parrotSays(kLineCroak);
		_forestStep = kStepBrushOffBen;
		break;

	case kStepBrushOffBen:
		if (!speechDone())
			break;
		parrotQuiet();
		forestBenSays(kLineNotInterested);
		_forestStep = kStepBrushOffDone;
		break;

	case kStepAnswer:
		parrotSays(kLineCroak);
		_forestStep = kStepAnswerDone;
		break;

	case kStepBrushOffDone:
	case kStepAnswerDone:
		if (!speechDone())
			break;
		if (_forestStep == kStepAnswerDone)
			parrotQuiet();
		_forestStep = 0;
		CursorMan.showMouse(true);
		break;

	// --- The feed ----------------------------------------------------------
	case kStepFeed:
		if (_forestWait <= 0x46)
			break;
		_forestStep = kStepEat;
		break;

	case kStepEat:
		_script.setFlag(kMood, 2);
		_anims.play(kParrotSlot, 0, ARRAYSIZE(kParrotEat1), 2, kReturnMode, kParrotEat1);
		_forestStep = kStepSmell;
		_forestWait = 0;
		break;

	case kStepSmell:
		if (_forestWait <= 0x1e)
			break;
		_anims.play(kParrotSlot, 0, ARRAYSIZE(kParrotEat2), 3, kReturnMode, kParrotEat2);
		queueOutcome(_tal, kLineSmell, kParrotX, kParrotY, false);
		_forestStep = kStepBread;
		break;

	case kStepBread:
		if (!speechDone())
			break;
		forestBenSays(kLineBread);
		_forestStep = kStepLove;
		break;

	case kStepLove:
		if (!speechDone())
			break;
		_anims.play(kParrotSlot, 0, ARRAYSIZE(kParrotEat2), 3, kReturnMode, kParrotEat2);
		queueOutcome(_tal, kLineLove, kParrotX, kParrotY, false);
		_forestStep = kStepSwallow;
		_forestWait = 0;
		break;

	case kStepSwallow:
		// 0x0ad2: the bread is gone, and so is the parrot.
		if (_forestWait <= 0x96)
			break;
		_inventory.remove(kBread);
		_script.setFlag(kMood, 3);
		_anims.play(kParrotSlot, 0x18, 0x6a, 3, 1);
		_forestStep = kStepFall;
		_forestWait = 0;
		debugC(1, kDebugItems, "forest: the parrot eats item %u (%s)", kBread,
			   _inventory.name(kBread).c_str());
		break;

	case kStepFall:
		if (_anims.remaining(kParrotSlot) != 0)
			break;
		_anims.play(kLedSlot, 1, 0xa, 4, 1);
		_forestStep = kStepLed;
		break;

	case kStepLed:
		if (_anims.remaining(kLedSlot) != 0)
			break;
		_forestStep = kStepBack;
		break;

	case kStepBack:
		walkTo(kWatchX, kWatchY, kWatchFacing);
		_forestStep = kStepWhat;
		_forestWait = 0;
		break;

	case kStepWhat:
		if (_forestWait != 0x3c)
			break;
		forestBenSays(kLineWhat);
		_forestStep = kStepSafe;
		break;

	case kStepSafe:
		if (!speechDone())
			break;
		_script.setFlag(kSafeShut, 0);
		_anims.play(kSafeSlot, 1, 0xf, 3, 1);
		_forestStep = kStepSafeLine;
		_forestWait = 0;
		break;

	case kStepSafeLine:
		if (_anims.remaining(kSafeSlot) != 0 || _forestWait <= 0x14)
			break;
		forestBenSays(kLineSafe);
		_forestStep = kStepFedDone;
		break;

	case kStepFedDone:
		if (!speechDone())
			break;
		CursorMan.showMouse(true);
		_forestStep = 0;
		rebuildHotspots();
		debugC(1, kDebugRooms, "forest: the parrot is fed, the safe is open");
		break;

	// --- The first visit ---------------------------------------------------
	case kStepVisit:
		if (_forestWait <= 0xf)
			break;
		forestBenSays(kLineNotHome);
		_forestStep = kStepVisitLine;
		break;

	case kStepVisitLine:
		if (!speechDone())
			break;
		_forestStep = kStepVisitPause;
		_forestWait = 0;
		break;

	case kStepVisitPause:
		if (_forestWait <= 5)
			break;
		_forestStep = kStepVisitWalk;
		break;

	case kStepVisitWalk:
		walkHandRoute(kVisitViaX, kVisitViaY, kVisitX, kVisitY, kVisitFacing);
		_forestStep = kStepVisitWhere;
		_forestWait = 0;
		break;

	case kStepVisitWhere:
		if (_forestWait <= 0x50)
			break;
		forestBenSays(kLineWhere);
		_forestStep = kStepVisitWhereDone;
		break;

	case kStepVisitWhereDone:
		if (!speechDone())
			break;
		_forestStep = kStepVisitCroak;
		break;

	case kStepVisitCroak:
		parrotSays(kLineCroak);
		_forestStep = kStepVisitCroakDone;
		break;

	case kStepVisitCroakDone:
		if (!speechDone())
			break;
		parrotQuiet();
		_forestStep = kStepVisitOut;
		_forestWait = 0;
		break;

	case kStepVisitOut:
		if (_forestWait <= 0x23)
			break;
		forestBenSays(kLineOut);
		CursorMan.showMouse(true);
		_forestStep = 0;
		break;

	default:
		_forestStep = 0;
		break;
	}
}

/**
 * Entry 2's open, the parts the lift does not carry: the note's line on the way
 * back from reading it (0x069f, once, [0xa755]) and the first visit (0x070a).
 */
void AlienEngine::startForest() {
	_forestStep = 0;
	_forestWait = 0;
	_forestTalk = false;
	if (_room != kForestRoom)
		return;

	if (_script.flag(kScenePlayed) == 1 && _script.flag(kNoteRead) == 0) {
		_script.setFlag(kNoteRead, 1);
		forestBenSays(kLineNote);
	}

	if (_script.flag(kVisited) == 0) {
		_script.setFlag(kVisited, 1);
		forestArm(kStepVisit);
	}
}

/**
 * Look at the note: object 20 ends in the table as nothing but submode 111, the
 * room reloading itself, and the body the lift dropped is CUTSCENE:sub_0d384 --
 * yod_note.pcx over the whole screen, faded in, until a click (manual
 * playthrough #45). The teardown's [0xa6d3] is what the reload places him by
 * and what startForest() says the note's line on.
 */
bool AlienEngine::runYodleNote(int obj, byte verb, byte submode) {
	if (_room != kForestRoom || submode != kSelfSubmode || obj != kNoteObj ||
		verb != kVerbLookAt)
		return false;

	const int room = _room;
	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kNotePicture), plate, palette)) {
		plate.free();
		warning("forest: could not load %s", kNotePicture);
		return false;
	}

	// CUTSCENE:sub_0c597, the shared setup: the room goes dark first.
	stopSpeech();
	fadeOut();
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	const int clip = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	for (uint i = 0; i < AnimSlots::kSlotCount; i++)
		_anims.takeDown(i);
	CursorMan.showMouse(true);
	redraw();
	fadeIn();

	debugC(1, kDebugRooms, "forest: the note");

	// 0x0e85: [0xa94a], any click.
	bool quit = false;
	uint scripted = 0;
	while (!shouldQuit() && !_quit && !quit) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_LBUTTONDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN ||
				(event.type == Common::EVENT_KEYDOWN &&
				 event.kbd.keycode == Common::KEYCODE_ESCAPE))
				quit = true;
		}

		if (_playActive && !quit) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					_playIndex++;
					continue;
				}
				if (cmd.type == PlayCommand::kClick || cmd.type == PlayCommand::kRightClick) {
					_playIndex++;
					quit = true;
				}
				break;
			}
			if (++scripted > 3000)
				quit = true;
		}

		present();
		sleep(10);
	}

	// CUTSCENE:sub_0c5c9, the teardown every scene shares.
	fadeOut();
	_cutscene = false;
	_clipBottom = clip;
	_script.setFlag(kScenePlayed, 1);
	if (loadRoom(room))
		_pendingCutscenes = false;
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
	return true;
}

} // End of namespace Alien
