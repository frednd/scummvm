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

// Gameson, the hippie chained to the tree at the crossroads, and the walkman
// that buys his hand-held game off him.
//
// The playtest report called him the biker; the overlay calls the room's first
// bank CROS_HIP and the walkthroughs call him Gameson, after the toy. He is
// room 23's object 1, and the rectangle that carries him is registered only
// while [0x33b8] says he is still there (hotspots.cpp) -- the cliff's opening
// clears that byte once the trade has been made (roominit.cpp).
//
// None of the conversation is a script row. tools/roomlogic.py recovered the
// three guards at the top of entry 3 and then lost every one of their bodies,
// because the body is `OBJ:sub_0967e(topic)` -- open the conversation menu --
// and a call is not an opcode the lift has. So the port had the rectangle, the
// verb and the hotspot label, and a talk on him did nothing at all.
//
// Four flags carry the whole thing, and all four are MAIN's to clear:
//
//   [0x33b8]  he is still chained to the tree
//   [0xa767]  the cassette player has been asked about (his topic 6)
//   [0xa768]  which of six dialog files the menu is being read out of
//   [0xa766]  the trade has been made
//   [0xa769]  ... and the gameson has changed hands, which is the pose he
//             keeps afterwards
//
// The six files are OBJ:sub_0461f, a switch on [0xa768] that loads one of them
// and rebuilds the menu from its tree (0251:210f, and the names are in the
// literal pool right in front of it). room23.tal is the conversation itself;
// the other five are one exchange each, and which one the room swaps in is the
// answer to "does he have a walkman on him" crossed with "do you":
//
//   0  room23.tal   everything up to the cassette player
//   1  musayes.tal  he does, and so do you -- the trade
//   2  musano.tal   he does not
//   3  musayes2.tal the walkman offered over rather than talked about
//   4  musano2.tal  you asked again without it
//   5  HIPPWALK.tal once it is done: "Oh, oh, oh, oh... Stayin' alive."
//
// Two hooks in OBJ turn a pick into one of those swaps, and both are handler
// 0x17's alone:
//
//   * OBJ:sub_046f8 (0251:21e8), off [0xa606] -- an option whose `next` is
//     0xfe, the byte that hands the conversation back to the room. Topic 6 is
//     the only one that carries it, so asking about the cassette player sets
//     [0xa767] and swaps in musayes.tal or musano.tal by whether the walkman
//     is in the bar.
//   * OBJ:sub_046a1 (0251:2191), off the pick itself -- option 1 of topic 1 of
//     musayes.tal or musayes2.tal, which is "Ok. Give me the Gameson and I'll
//     give you the walkman." That one sets [0xa766] and starts the room's
//     [0xa49f] machine at state 10.
//
// The machine is three states (ovr_17_0e9f:0x062b):
//
//   10  the line has come down: inv_add(30), inv_remove(33), [0xa769] = 1,
//       and the thirty frames of him swapping the two over
//   3   those frames have run out ([0xa4ea] == 1): the nine-frame loop he
//       keeps for the rest of the game
//   4   0x70 ticks later the cursor and the walker come back
//
// The fourth byte of a tree record is the reply, and unlike the store's banter
// it is a *dialog id* rather than an outcome code: DIALOG:sub_0bf08 hands it
// straight to sub_0bcba, which indexes zone 2's offset table with it
// (0ae2:0999). The room speaks it once the option's own line has come down,
// in green at his head, with the talking loop running unless the gameson has
// already changed hands (0x03f6..0x0464).
static const int kHippieRoom = 23;

static const byte kGameson = 1;			///< the object his rectangle registers as
static const byte kVerbTalkTo = 6;

static const byte kWalkman = 0x21;		///< what he takes
static const byte kGamesonToy = 0x1e;	///< and what he gives for it

static const uint16 kHippieHere = 0x33b8;	///< still chained to the tree
static const uint16 kTraded = 0xa766;
static const uint16 kAsked = 0xa767;		///< the cassette player has come up
static const uint16 kScriptFlag = 0xa768;	///< which file the menu is read from
static const uint16 kGiven = 0xa769;		///< the gameson has changed hands

static const byte kScriptCount = 6;
static const char *const kScripts[kScriptCount] = {
	"room23.tal", "musayes.tal", "musano.tal",
	"musayes2.tal", "musano2.tal", "HIPPWALK.tal"
};

static const byte kFileRoom = 0;		///< [0xa768]'s six values, by the file
static const byte kFileYes = 1, kFileNo = 2;
static const byte kFileYes2 = 3, kFileNo2 = 4;
static const byte kFileWalk = 5;

static const uint kOpeningTopic = 7;	///< where a first talk starts the tree
static const uint kOfferTopic = 6;		///< and the topic that asks for a player
/// Where "Ok. Give me the Gameson and I'll give you the walkman." sits in each
/// of the two files that carry it: topic 1 of musayes.tal, which is reached by
/// talking, and topic 3 of musayes2.tal, which is reached by holding the
/// walkman out. Both are option 1 (OBJ:sub_046a1's two arms, 0251:219b and
/// 0251:21c4).
static const uint kTradeTopicYes = 1, kTradeTopicYes2 = 3;
static const uint kTradeOption = 1;

/// His anchor and colour, the five immediates DIALOG:sub_0bf77 carries for
/// handler 0x17 (0ae2:1183).
static const int kHippieX = 0x91, kHippieY = 0x3c;
static const byte kHippieInk[3] = { 0x28, 0x3f, 0x28 };

/// CROS_HIP.DL1, which is all of him: MIDAS:sub_197ee plays every pose on it.
static const uint kHippieSlot = 0;

// The pose dispatcher's twelve cases, by the number [0xa52a] is set to. Only
// four of them are reachable from this room's own code; the rest are the
// gestures of the conversation the overlay never asks for. Every one is a play
// over a list in the data segment, slot 0 at rate 4, mode 7 but for the
// handover, which is mode 8 (MIDAS:sub_18a2b and sub_18af4).
static const byte kPoseIdle = 1;		///< 0x1e46, and the tick keeps it going
static const byte kPoseTalk = 9;		///< 0x1e70, the same while he answers
static const byte kPoseHandOver = 0x0a;	///< 0x1e7c, the trade itself
static const byte kPoseTraded = 0x0c;	///< 0x1e9a, what he does ever after

static const byte kIdleFrames[] = { 1, 2, 2 };
static const byte kTalkFrames[] = { 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 0 };
static const byte kHandFrames[] = {
	1, 26, 27, 28, 29, 30, 30, 30, 30, 30, 30, 54, 55, 56, 57, 58, 59, 60, 61,
	62, 63, 64, 65, 66, 67, 67, 68, 69, 70, 71
};
static const byte kTradedFrames[] = { 34, 35, 36, 37, 38, 39, 40, 41, 42 };

static const int kPoseRate = 4;
static const int kLoopMode = 7, kHandMode = 8;

static const byte kStepTrade = 0x0a;	///< the item swap and its thirty frames
static const byte kStepHand = 3;		///< those frames, running
static const byte kStepSettle = 4;		///< and the wait that gives the cursor back

/// [0xa49c] > this before the room hands the player back (0x0650).
static const uint16 kSettleWait = 0x70;

/**
 * MIDAS:sub_197ee, room 23's own pose dispatcher.
 *
 * [0xa52a] is the byte it writes, and the room's tick relaunches slot 0 only
 * while that byte is one of the three looping poses -- the guard the port's
 * loop table now carries (anims.cpp, tools/gen_anims.py). The handover is the
 * play that must not be picked up, and it is the one pose that is not in the
 * set.
 */
void AlienEngine::hippiePose(byte pose) {
	const bool loops = pose == kPoseIdle || pose == kPoseTalk || pose == kPoseTraded;
	_anims.setLoopFlag(kHippieSlot, loops ? 1 : 0);

	switch (pose) {
	case kPoseIdle:
		_anims.play(kHippieSlot, 0, ARRAYSIZE(kIdleFrames), kPoseRate, kLoopMode,
					kIdleFrames);
		break;
	case kPoseTalk:
		_anims.play(kHippieSlot, 0, ARRAYSIZE(kTalkFrames), kPoseRate, kLoopMode,
					kTalkFrames);
		break;
	case kPoseHandOver:
		_anims.play(kHippieSlot, 0, ARRAYSIZE(kHandFrames), kPoseRate, kHandMode,
					kHandFrames);
		break;
	case kPoseTraded:
		_anims.play(kHippieSlot, 0, ARRAYSIZE(kTradedFrames), kPoseRate, kLoopMode,
					kTradedFrames);
		break;
	default:
		return;
	}

	debugC(3, kDebugRooms, "hippie: pose %u", pose);
}

/**
 * OBJ:sub_0461f: the file the menu is read out of, and the tree with it.
 *
 * The original's loader is case blind and the room's own file is named
 * room23.tal in the pool and ROOM23.TAL on the disc, which is why the table
 * above spells them the way the overlay does.
 */
void AlienEngine::loadHippieScript(byte which) {
	_script.setFlag(kScriptFlag, which);
	if (which >= kScriptCount)
		return;

	const Common::Path path(kScripts[which]);
	if (!Common::File::exists(path)) {
		debugC(1, kDebugRooms, "hippie: %s is missing", kScripts[which]);
		return;
	}

	_tal.load(path, &_pack);
	debugC(1, kDebugChat, "hippie: speaking out of %s", kScripts[which]);
}

/**
 * The room has opened: the pose he is standing in.
 *
 * 0x03a7, the tail of the opening. The static frame the other arm plays is a
 * lifted row already (roominit.cpp); these two are not, because they are calls
 * into the handler's own dispatcher rather than plays.
 */
void AlienEngine::startHippie() {
	if (_room != kHippieRoom)
		return;

	_hippieStep = 0;
	_hippiePos = 0;
	_hippieReply = 0;
	_hippieAnswer = false;
	_hippieTalking = false;

	if (_script.flag(kHippieHere) != 1)
		return;

	hippiePose(kPoseIdle);
	if (_script.flag(kGiven) == 1)
		hippiePose(kPoseTraded);
}

/**
 * The talk verb on Gameson, and the walkman offered to him.
 *
 * Entry 3's two halves, both of which end in a conversation the lift could not
 * carry. Returns true when the room has taken the click, the way the owl and
 * Sluggs do.
 */
bool AlienEngine::armHippie(int obj, byte verb, int item) {
	if (_room != kHippieRoom || obj != kGameson)
		return false;

	const bool held = _inventory.has(kWalkman);
	const bool traded = _script.flag(kTraded) == 1;
	const byte file = (byte)_script.flag(kScriptFlag);

	// 0x0038, the item-use half: the walkman itself, once the cassette player
	// has come up and before the trade has been made.
	if (item != Inventory::kNoItem) {
		if (item != kWalkman || _script.flag(kAsked) != 1 || traded)
			return false;

		loadHippieScript(kFileYes2);
		openChat(0);
		holdItem(Inventory::kNoItem);
		debugC(1, kDebugItems, "hippie: the walkman is offered over");
		return true;
	}

	if (verb != kVerbTalkTo)
		return false;

	// 0x008f, the plain-verb half, in the order the original tests it.
	if (file == kFileRoom && _script.flag(kAsked) == 0) {
		loadHippieScript(kFileRoom);
		openChat(kOpeningTopic);
		debugC(1, kDebugChat, "hippie: the conversation opens on topic %u", kOpeningTopic);
		return true;
	}

	if (_script.flag(kAsked) == 1 && !traded) {
		loadHippieScript(held ? kFileYes2 : kFileNo2);
		openChat(0);
		debugC(1, kDebugChat, "hippie: asked again, %s the walkman",
			   held ? "with" : "without");
		return true;
	}

	if (traded) {
		loadHippieScript(kFileWalk);
		openChat(0);
		debugC(1, kDebugChat, "hippie: nothing left but the song");
		return true;
	}

	return false;
}

/**
 * The answer to a pick, the two hooks that swap the file under it, and the
 * [0xa49f] machine behind the trade.
 */
void AlienEngine::stepHippie() {
	if (_room != kHippieRoom)
		return;

	// 0x0710: the frame a line comes down puts him back in the idle loop,
	// unless the gameson has gone, in which case he keeps the other one.
	if (_hippieTalking && speechDone()) {
		_hippieTalking = false;
		hippiePose(_script.flag(kGiven) == 1 ? kPoseTraded : kPoseIdle);
	}

	// A pick the menu has taken. Both hooks are answered here and now, the way
	// OBJ answers them: the menu moves on by itself while a reply is still on
	// the screen, so a hook left until the reply came down would be reading the
	// topic and option of whatever the tree had reached by then.
	if (_chatPickNew) {
		_chatPickNew = false;
		hippiePick();
	}

	// And the reply itself, once the option's own line is off the screen.
	if (_hippieAnswer && speechDone()) {
		_hippieAnswer = false;
		hippieAnswer();
	}

	// [0xa49c], which the room advances on the tick pair whether or not a state
	// is reading it.
	_hippiePos++;

	switch (_hippieStep) {
	case kStepTrade:
		// 0x0676: the walkman for the gameson, and the thirty frames of it.
		if (!speechDone())
			break;

		_inventory.add(kGamesonToy);
		_inventory.remove(kWalkman);
		_script.setFlag(kGiven, 1);
		hippiePose(kPoseHandOver);
		_hippieStep = kStepHand;
		_hippiePos = 0;
		debugC(1, kDebugItems, "hippie: item %u (%s) for item %u (%s)",
			   kGamesonToy, _inventory.name(kGamesonToy).c_str(),
			   kWalkman, _inventory.name(kWalkman).c_str());
		break;

	case kStepHand:
		// 0x0632: [0xa4ea] == 1, the last frame of the handover.
		if (_anims.remaining(kHippieSlot) > 1)
			break;

		hippiePose(kPoseTraded);
		_hippieStep = kStepSettle;
		_hippiePos = 0;
		break;

	case kStepSettle:
		// 0x0650: and the room is the player's again.
		if (_hippiePos <= kSettleWait)
			break;

		_hippieStep = 0;
		CursorMan.showMouse(true);
		debugC(1, kDebugRooms, "hippie: the trade is done");
		break;

	default:
		break;
	}
}

/**
 * A pick, as OBJ sees it: the reply the room owes, and the two hooks.
 *
 * The reply is taken as a copy of the entry rather than as its id, because the
 * second hook swaps the file the id was an index into. The room speaks it once
 * the option's own line has come down (0x03f6); the hooks run now, which is
 * where sub_046a1 and sub_046f8 run.
 */
void AlienEngine::hippiePick() {
	const byte file = (byte)_script.flag(kScriptFlag);
	const uint topic = _chatPickTopic;
	const uint choice = _chatPickChoice;

	if (_chatPickReply) {
		const TalFile::Entry &entry = _tal.entry(_chatPickReply);
		if (entry.present) {
			_hippieReplyEntry = entry;
			_hippieReplyTicks = speechTicksFor(_tal, _chatPickReply);
			_hippieReply = _chatPickReply;
			_hippieAnswer = true;
		}
	}

	// OBJ:sub_046a1: the offer, wherever the file being read puts it.
	const bool offer = (file == kFileYes && topic == kTradeTopicYes)
					   || (file == kFileYes2 && topic == kTradeTopicYes2);
	if (offer && choice == kTradeOption) {
		_script.setFlag(kTraded, 1);
		_hippieStep = kStepTrade;
		_hippiePos = 0;
		CursorMan.showMouse(false);
		debugC(1, kDebugChat, "hippie: the trade is struck, step 0x%02x", kStepTrade);
		return;
	}

	// OBJ:sub_046f8: topic 6 is the only one whose options hand the
	// conversation back to the room, and what the room does with it is swap in
	// the file that answers it.
	if (_chatPickNext == TalFile::kChatRoom && topic == kOfferTopic
			&& file == kFileRoom) {
		_script.setFlag(kAsked, 1);
		const bool held = _inventory.has(kWalkman);
		loadHippieScript(held ? kFileYes : kFileNo);
		openChat(0);
		debugC(1, kDebugChat, "hippie: the cassette player, %s a walkman",
			   held ? "with" : "without");
	}
}

/// The reply line, in green at his head, with the loop he talks in.
void AlienEngine::hippieAnswer() {
	setTextColor(kHippieInk[0], kHippieInk[1], kHippieInk[2]);
	uploadTextColor();
	speakEntry(_hippieReplyEntry, kHippieX, kHippieY, _hippieReplyTicks);

	// DLGREQ's half of it: he talks while the line stands, unless the gameson
	// has gone and he has the other loop to keep.
	if (_script.flag(kGiven) == 0) {
		hippiePose(kPoseTalk);
		_hippieTalking = true;
	}

	debugC(1, kDebugChat, "hippie: answers with dialog %u", _hippieReply);
}

} // End of namespace Alien
