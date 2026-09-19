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

#include "common/file.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// The 24h antique store, which is a scene rather than a room.
//
// Room 30 registers no rectangle of its own: its entry 1 (ovr_1e_0e9b:0x0074)
// calls HOTSPOT:sub_135e0 and sub_13605, the generic clear and the generic
// name test, and stops. It has no KIERRA files either, so there is no walk
// mask and no node ring. Everything the store does is the [0xa49f] machine in
// entry 2, which is the layer tools/roomlogic.py cannot see -- so the lift
// carried the plate, the eight banks and three opening effects, and the player
// arrived in a shop where nothing answered a click and nothing ever left
// (finding #100, and the report that asked for it).
//
// The conversation is the room. Three ways in, by the two flags the store keeps
// for itself:
//
//   [0xa76b]  he has been in the store before
//   [0xa76a]  the arrow has been traded for the diving suit
//
// A first visit (both clear) loads sales0.tal and runs state 1: the line he
// says walking in, the two steps in from the door, and then twelve lines of
// the salesman and Ben taking turns. A return visit with the trade done loads
// sales3.tal and runs state 0x1e, five more lines and out; a return visit
// without it loads SALES1.TAL -- the same file as sales1.tal, the names differ
// only in case -- and goes straight to the conversation menu. Each arm is
// guarded on [0xa602] as well, which the first arm sets: the click
// dispatcher's scratch byte doubles as the room's "one of these has already
// run" (0x0280, 0x02c7, 0x0310).
//
// Two things end the conversation, and they are the two halves of the report:
//
//   * option 2 of the menu, which is the goodbye ([0xa60c] == 2 at 0x067c);
//   * an item taken out of the bar, which is what "use" is in a room with no
//     rectangles: 10c9:sub_11001 sets [0xa825] = 2 when a click lands in a
//     filled slot, and the machine reads [0xa6bb], the held item, as what is
//     being offered (0x0692). The arrow, item 0x20, is the trade; anything
//     else gets one of two refusals picked at random.
//
// Both roads meet at state 9, which closes the menu, and state 0xff, which
// walks him to the door and arms submode 1 -- the room's only exit, and the
// link back to town (transitions.cpp).
//
// The salesman animates from the same call every line he speaks:
// MIDAS:sub_19a6b, a per-room routine in the resident segment, whose seven
// cases are seven mode-7 plays over frame lists in the data segment. Which one
// a line gets is picked inside DLGREQ:sub_0c275 for handler 0x1e (seg_0c25.asm
// 0xc2c3): the talking loop, or the two gestures that belong to the trade.
static const int kStoreRoom = 30;

static const uint16 kTraded = 0xa76a;		///< the arrow has been handed over
static const uint16 kVisited = 0xa76b;		///< he has been here before
static const uint16 kToldAgain = 0xa76d;	///< the return visit's line is spent
static const uint16 kBehindCounter = 0x33ba;	///< which bank the salesman talks from

// The three dialog files, by the way in. SALES1.TAL and sales1.tal are one
// file: the original's loader is case blind and the two names sit next to each
// other in the overlay's string block.
static const char *const kOpeningScript = "sales0.tal";
static const char *const kCounterScript = "sales1.tal";
static const char *const kTradedScript = "sales3.tal";

static const byte kArrow = 0x20;		///< what he trades
static const byte kDivingSuit = 0x1c;	///< and what he gets for it

/// The salesman's anchor and colour, the five immediates the room hands
/// DLGREQ:sub_0c250 as it opens (0x0262).
static const int kSalesX = 0x5b, kSalesY = 0x48;
static const byte kSalesInk[3] = { 0x3f, 0x37, 0x19 };

/// Ben answers from the door, in the white every other line is spoken in
/// (DIALOG:sub_0bebe, 0x0ae2:0x10b8).
static const int kBenX = 0x87, kBenY = 0x48;

static const byte kArrivalLine = 0x14;	///< queue_event(0x14), walking in
static const byte kFarewell = 0x0f;		///< what option 2 is answered with
static const byte kReturnLine = 0x1e;	///< and what a return visit opens with

static const byte kOpeningLast = 0x0c;	///< the opening ends on this line
static const byte kTradeLast = 0x09;		///< and the trade's own banter here
static const byte kReturnLast = 0x0d;
static const byte kRefuseFirst[2] = { 0x14, 0x16 };	///< the two refusals,
static const byte kRefuseLast[2] = { 0x15, 0x17 };	///< each two lines long

static const byte kLeaveOption = 2;		///< [0xa60c], the option that ends it

// The walk in and the walk out, as 1021:sub_1023c builds them: a route of two
// points written straight into the walk arrays, which is how a room with no
// mask moves him at all.
static const int kDoorX = 0x8b, kDoorY = 0x88;
static const int kCounterX = 0x7d, kCounterY = 0x88, kCounterFacing = 4;
static const int kLeaveX = 0xa9, kLeaveY = 0x68, kLeaveFacing = 2;

static const uint16 kArrivalWait = 0x46;	///< [0xa49c] before the first line
static const uint16 kTurnWait = 0x8c;		///< and before he turns to the counter
static const uint16 kGreetWait = 0xc8;		///< and before the salesman starts
static const uint16 kReturnWait = 2;		///< the return visit's own two waits
static const uint16 kAgainWait = 5;

static const byte kStepOpening = 1;		///< the twelve lines of the first visit
static const byte kStepBanter = 2;
static const byte kStepMenu = 3;			///< the conversation menu, topic 0
static const byte kStepWait = 4;			///< listening: a pick, or an item
static const byte kStepOffer = 5;			///< something has been offered
static const byte kStepTrade = 6;			///< it was the arrow
static const byte kStepRefuse = 7;			///< it was not
static const byte kStepGoodbye = 8;		///< option 2 was picked
static const byte kStepClose = 9;			///< the menu comes down
static const byte kStepAgain = 0x0a;		///< and the menu opens on topic 1
static const byte kStepReturn = 0x1e;		///< the traded return visit
static const byte kStepReturnLines = 0x1f;
static const byte kStepReturnDone = 0x20;
static const byte kStepUntraded = 0x28;	///< the untraded one
static const byte kStepUntradedLine = 0x29;
static const byte kStepLeaving = 0xff;

static const byte kSubmodeTown = 1;		///< room 30, submode 1 -> room 33

// MIDAS:sub_19a6b's cases, and the frame lists they step. The lists are in the
// data segment rather than in any room's table, so they are here rather than in
// roominit.cpp: every one of these plays is mode 7, which reads its list one
// byte per tick and takes the last frame away when it runs out.
static const byte kPoseGreet[] = {		// case 6, slot 0, rate 3
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 1, 1
};
static const byte kPoseTurn[] = {		// case 1, slot 0, rate 3
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const byte kPoseHandOver[] = {	// case 2, slot 3, rate 4
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13, 13, 13, 14
};
static const byte kPoseTalk[] = {		// cases 3 and 7, slots 4 and 7, rate 4
	1, 2, 3, 4, 2, 5, 6, 5, 7, 8, 7, 5, 6, 7, 6, 7, 8, 7, 6, 7, 8, 7, 6, 7, 8, 7,
	6, 7, 6, 5, 8, 7, 6, 8, 8, 8, 8, 8, 8, 8, 7, 6, 8, 6, 7, 8, 7, 6, 8
};
static const byte kPoseFetch[] = {		// case 4, slot 5, rate 4
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 13, 12, 11, 10, 9, 8, 9, 10,
	11, 12, 13, 14, 13, 12, 11, 10, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 16, 18,
	17, 16, 17, 18, 19, 20, 21, 22, 22, 22, 23, 23, 24, 25, 26, 27, 28, 29, 30, 30,
	31, 32, 33, 34, 35, 34, 33, 32, 31, 30, 31, 32, 33, 34, 35, 34, 35, 34, 33, 32,
	31, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 38, 39, 38, 39, 40, 1
};

static const int kPoseMode = 7;
static const uint kTalkSlotCounter = 4;	///< ANT_SAC3, while [0x33ba] is set
static const uint kTalkSlotFloor = 7;	///< ANT_SAC6, once it has been cleared

/// The poses, by the case number MIDAS:sub_19a6b answers to.
static const byte kPoseGreetCase = 6, kPoseTurnCase = 1, kPoseTalkCase = 3,
				  kPoseFetchCase = 4, kPoseHandCase = 2, kPoseFloorCase = 7,
				  kPoseStopCase = 0;

/// The two lines of the trade the gestures belong to (seg_0c25.asm 0xc32a).
static const byte kFetchLine = 2, kHandLine = 6;

/**
 * MIDAS:sub_19a6b, the store's own animation dispatcher.
 *
 * Every case is a mode-7 play over one of the lists above, and case 0 is the
 * stop: it takes the talking loop's slot away and clears the loop flags, which
 * is what the room does the frame a line comes down (0x08f1).
 */
void AlienEngine::storePose(byte pose) {
	// [0xa53e] = 0 at the top of every call: the talking loop only runs while
	// the case that started it says so.
	_anims.setLoopFlag(kTalkSlotCounter, 0);

	switch (pose) {
	case kPoseStopCase: {
		const uint slot = _script.flag(kBehindCounter) == 0 ? kTalkSlotFloor
															: kTalkSlotCounter;
		_anims.stop(kTalkSlotCounter);
		_anims.stop(kTalkSlotFloor);
		_anims.stop(5);
		_anims.stop(6);
		_anims.setLoopFlag(kTalkSlotFloor, 0);
		_anims.play(slot, 1, 1, 0, 4);
		break;
	}
	case kPoseTurnCase:
		_anims.play(0, 0, ARRAYSIZE(kPoseTurn), 3, kPoseMode, kPoseTurn);
		break;
	case kPoseHandCase:
		_anims.play(3, 0, ARRAYSIZE(kPoseHandOver), 4, kPoseMode, kPoseHandOver);
		break;
	case kPoseTalkCase:
		_anims.play(kTalkSlotCounter, 0, ARRAYSIZE(kPoseTalk), 4, kPoseMode, kPoseTalk);
		_anims.setLoopFlag(kTalkSlotCounter, 1);
		break;
	case kPoseFetchCase:
		_anims.play(5, 0, ARRAYSIZE(kPoseFetch), 4, kPoseMode, kPoseFetch);
		break;
	case kPoseGreetCase:
		_anims.play(0, 0, ARRAYSIZE(kPoseGreet), 3, kPoseMode, kPoseGreet);
		break;
	case kPoseFloorCase:
		_anims.play(kTalkSlotFloor, 0, ARRAYSIZE(kPoseTalk), 4, kPoseMode, kPoseTalk);
		_anims.setLoopFlag(kTalkSlotFloor, 1);
		break;
	default:
		break;
	}

	debugC(3, kDebugRooms, "store: pose %u", pose);
}

/**
 * DLGREQ:sub_0c275 as room 30 reaches it: the salesman's line, in his colour,
 * at his anchor, with the pose that goes with it.
 */
void AlienEngine::storeSalesmanLine(byte code) {
	byte pose = kPoseTalkCase;
	if (_script.flag(kBehindCounter) == 0)
		pose = kPoseFloorCase;
	if (_storeStep == kStepTrade && _storeLine == kFetchLine)
		pose = kPoseFetchCase;
	if (_storeStep == kStepTrade && _storeLine == kHandLine) {
		pose = kPoseHandCase;
		// He comes out from behind the counter with the suit and stays there:
		// every line after this one talks from the other bank.
		_script.setFlag(kBehindCounter, 0);
	}

	storePose(pose);
	setTextColor(kSalesInk[0], kSalesInk[1], kSalesInk[2]);
	uploadTextColor();
	queueOutcome(_tal, code, kSalesX, kSalesY);
	_storeTalking = true;

	debugC(1, kDebugRooms, "store: the salesman says outcome 0x%02x", code);
}

/**
 * DIALOG:sub_0bebe: the two of them taking turns.
 *
 * [0xa4a3] is the speaker and it flips on every call, so the same line number
 * is spoken by whoever's turn it is: the salesman through DLGREQ, Ben in white
 * from the door.
 */
void AlienEngine::storeSpeak(byte code) {
	if (_storeSpeaker == 0) {
		storeSalesmanLine(code);
	} else {
		setTextColor(0x3f, 0x3f, 0x3f);
		uploadTextColor();
		queueOutcome(_tal, code, kBenX, kBenY);
		debugC(1, kDebugRooms, "store: Ben says outcome 0x%02x", code);
	}

	_storeSpeaker = _storeSpeaker ? 0 : 1;
}

/**
 * 1021:sub_1023c, the hand-built route.
 *
 * The room has no walk mask, so nothing can be routed through it: the original
 * writes two points into the walk arrays itself and lets the walker run them.
 * The port's walker takes a route the same way, with the character's own
 * position as the first point and the target appended by `follow`.
 */
void AlienEngine::storeWalk(int viaX, int viaY, int x, int y, int facing) {
	WalkRoute route;
	route.points[0].x = (int16)_ben.walkX();
	route.points[0].y = (int16)_ben.walkY();
	route.points[1].x = (int16)viaX;
	route.points[1].y = (int16)viaY;
	route.count = 2;
	_ben.follow(route, x, y, facing);
	_dirty = true;

	debugC(1, kDebugRooms, "store: walks %d,%d -> %d,%d -> %d,%d facing %d",
		   _ben.walkX(), _ben.walkY(), viaX, viaY, x, y, facing);
}

/// The dialog file the machine swaps under itself mid-scene.
void AlienEngine::loadStoreScript(const char *name) {
	const Common::Path path(name);
	if (!Common::File::exists(path)) {
		debugC(1, kDebugRooms, "store: %s is missing", name);
		return;
	}

	_tal.load(path, &_pack);
	debugC(1, kDebugRooms, "store: speaking out of %s", name);
}

/**
 * The room has opened: pick the arm, as the top of entry 2 does.
 *
 * Called from the tail of loadRoom rather than from its arrival hooks, because
 * the arms load a dialog file of their own and the room's own file is not read
 * until further down.
 */
void AlienEngine::startStore() {
	if (_room != kStoreRoom)
		return;

	_storeStep = 0;
	_storePos = 0;
	_storeWalked = false;
	_storeTalking = false;
	_storeOffer = 0;

	// 0x0271: the cursor goes away for the whole of the way in, and the machine
	// gives it back when the menu opens.
	CursorMan.showMouse(false);

	if (_script.flag(kVisited) == 0 && _script.flag(kTraded) == 0) {
		// 0x0280, the first visit.
		_script.setFlag(kVisited, 1);
		loadStoreScript(kOpeningScript);
		_storeStep = kStepOpening;
		_storeLine = 1;
		_storeSpeaker = 0;
		storePose(kPoseGreetCase);
		debugC(1, kDebugRooms, "store: the first visit, step %d", kStepOpening);
		return;
	}

	if (_script.flag(kTraded) == 1) {
		// 0x02c7: he has the suit, so the store has five more lines and no menu.
		// [0xa4a2] is not reset here: it is left where the trade's own banter
		// stopped, which is nine, and sales3.tal is numbered to follow on.
		loadStoreScript(kTradedScript);
		_storeStep = kStepReturn;
		_storeSpeaker = 0;
		storePose(kPoseStopCase);
		debugC(1, kDebugRooms, "store: back with the suit, step 0x%02x", kStepReturn);
		return;
	}

	// 0x0310: he has been here and has not traded, so it is the menu again.
	loadStoreScript(kCounterScript);
	_storeStep = kStepUntraded;
	_storeSpeaker = 0;
	storePose(kPoseStopCase);
	debugC(1, kDebugRooms, "store: back without the suit, step 0x%02x", kStepUntraded);
}

void AlienEngine::stepStore() {
	if (_room != kStoreRoom || !_storeStep)
		return;

	// [0xad1b]: the frame a line comes down the room stops the talking loop
	// (0x08f1). The port has no such pulse, so the flag the line was started
	// with stands in for it.
	if (_storeTalking && speechDone()) {
		_storeTalking = false;
		storePose(kPoseStopCase);
	}

	_storePos++;

	switch (_storeStep) {
	case kStepOpening:
		// 0x056a. Three waits on the room's own counter, and the walk in, which
		// the original hangs on the arrival line coming down rather than on the
		// clock.
		if (_storePos == kArrivalWait) {
			int anchorX, anchorY;
			characterAnchor(anchorX, anchorY);
			queueOutcome(_tal, kArrivalLine, anchorX, anchorY);
			debugC(1, kDebugRooms, "store: walks in on outcome 0x%02x", kArrivalLine);
		}

		if (!_storeWalked && _storePos > kArrivalWait && speechDone()) {
			_storeWalked = true;
			storeWalk(kDoorX, kDoorY, kCounterX, kCounterY, kCounterFacing);
		}

		if (_storePos == kTurnWait)
			storePose(kPoseTurnCase);

		if (_storePos > kGreetWait) {
			_storeStep = kStepBanter;
			_storePos = 0;
			_storeSpeaker = 1;
			_storeLine = 1;
			storeSalesmanLine(_storeLine);
			// [0xa4a3] = 1 after the greeting: Ben has the next line.
		}
		break;

	case kStepBanter:
		// 0x05ca: one line per line that came down, until the twelfth.
		if (!speechDone())
			break;

		_storeLine++;
		storeSpeak(_storeLine);
		if (_storeLine == kOpeningLast) {
			_storeStep = kStepMenu;
			_storePos = 0;
			loadStoreScript(kCounterScript);
		}
		break;

	case kStepMenu:
		// 0x0647: the cursor comes back and the menu opens on topic 0.
		CursorMan.showMouse(true);
		if (_chat.isActive())
			break;

		openChat(0);
		_storeStep = kStepWait;
		_storePos = 0;
		debugC(1, kDebugChat, "store: the menu opens, step %d", kStepWait);
		break;

	case kStepWait:
		// 0x0670, the wait the whole room hangs on. Two things end it. The
		// state's own [0xa892] = 1, which it writes every frame it stands, is
		// left out: nothing in the port reads that byte.
		if (_chat.choice() == kLeaveOption) {
			_storeStep = kStepGoodbye;
			_storePos = 0;
			CursorMan.showMouse(false);
			debugC(1, kDebugChat, "store: option %u, the goodbye", kLeaveOption);
			break;
		}

		if (_heldItem) {
			// [0xa825] == 2 with [0xa6bb] set: an item out of the bar is what
			// an offer is in a room with nothing to click on. OBJ:sub_09700
			// empties the hand either way.
			_storeOffer = _heldItem;
			holdItem(Inventory::kNoItem);
			_storeStep = kStepOffer;
			_storePos = 0;
			CursorMan.showMouse(false);
			debugC(1, kDebugItems, "store: offered item %u (%s)", _storeOffer,
				   _inventory.name(_storeOffer).c_str());
		}
		break;

	case kStepOffer:
		// 0x06b3: the arrow is the trade and everything else is a refusal.
		_storeSpeaker = 1;
		if (_storeOffer == kArrow) {
			_script.setFlag(kTraded, 1);
			_inventory.add(kDivingSuit);
			_inventory.remove(kArrow);
			_storeStep = kStepTrade;
			_storePos = 0;
			_storeLine = 1;
			storeSpeak(_storeLine);
			debugC(1, kDebugItems, "store: the arrow buys item %u, step %d",
				   kDivingSuit, kStepTrade);
			break;
		}

		{
			// GFX:sub_26ebc(2), the resident random: two refusals, two lines
			// each.
			const uint which = _rnd.getRandomNumber(1);
			_storeStep = kStepRefuse;
			_storePos = 0;
			_storeLine = kRefuseFirst[which];
			storeSpeak(_storeLine);
			debugC(1, kDebugItems, "store: refusal %u, outcome 0x%02x", which,
				   _storeLine);
		}
		break;

	case kStepTrade:
		// 0x073c
		if (!speechDone())
			break;

		_storeLine++;
		storeSpeak(_storeLine);
		if (_storeLine == kTradeLast) {
			_storeStep = kStepClose;
			_storePos = 0;
		}
		break;

	case kStepRefuse:
		// 0x0764
		if (!speechDone())
			break;

		_storeLine++;
		storeSpeak(_storeLine);
		if (_storeLine == kRefuseLast[0] || _storeLine == kRefuseLast[1]) {
			_storeStep = kStepAgain;
			_storePos = 0;
		}
		break;

	case kStepAgain:
		// 0x07f0: the menu comes back, on the topic after the one he used.
		CursorMan.showMouse(true);
		if (_chat.isActive())
			break;

		openChat(1);
		_storeStep = kStepWait;
		_storePos = 0;
		break;

	case kStepGoodbye:
		// 0x0793: the option's own line has to come down before the answer.
		CursorMan.showMouse(false);
		if (!speechDone())
			break;

		storeSalesmanLine(kFarewell);
		_storeStep = kStepClose;
		_storePos = 0;
		break;

	case kStepClose:
		// 0x07b7: the menu goes, and with it the cursor.
		if (_chat.isActive()) {
			_chat.close();
			CursorMan.showMouse(false);
			_dirty = true;
		}

		_storeStep = kStepLeaving;
		_storePos = 0;
		storeWalk(kDoorX, kDoorY, kLeaveX, kLeaveY, kLeaveFacing);
		break;

	case kStepReturn:
		// 0x0814, the visit with the suit already bought.
		if (_storePos == kReturnWait)
			storeWalk(kDoorX, kDoorY, kCounterX, kCounterY, kCounterFacing);

		if (_storePos > kArrivalWait) {
			_storeStep = kStepReturnLines;
			_storePos = 0;
			storeSpeak(_storeLine);
		}
		break;

	case kStepReturnLines:
		// 0x0845
		if (!speechDone())
			break;

		_storeLine++;
		storeSpeak(_storeLine);
		if (_storeLine == kReturnLast) {
			_storeStep = kStepReturnDone;
			_storePos = 0;
		}
		break;

	case kStepReturnDone:
		// 0x086d
		if (!speechDone())
			break;

		_script.setFlag(kToldAgain, 1);
		_storeStep = kStepClose;
		_storePos = 0;
		break;

	case kStepUntraded:
		// 0x0885, the visit without it: one line and then the menu.
		if (_storePos <= kAgainWait)
			break;

		_storeStep = kStepUntradedLine;
		_storePos = 0;
		storeSalesmanLine(kReturnLine);
		storeWalk(kDoorX, kDoorY, kCounterX, kCounterY, kCounterFacing);
		break;

	case kStepUntradedLine:
		// 0x08b1
		if (!speechDone())
			break;

		_storeStep = kStepMenu;
		_storePos = 0;
		break;

	case kStepLeaving:
		// 0x08c4: the route has to be run out, and nothing may still be being
		// said. The original tests two runtime globals more -- [0xa8f4] and
		// [0xa808] -- which are the walk system's own and have no counterpart
		// here; the walker being idle is what they come to.
		if (_ben.isWalking() || _ben.isTurning() || !speechDone())
			break;

		_storeStep = 0;
		debugC(1, kDebugRooms, "store: submode %d, back out into town", kSubmodeTown);
		takeExit(kSubmodeTown);
		return;

	default:
		break;
	}
}

} // End of namespace Alien
