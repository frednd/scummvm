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

// Rooms 51, 53, 55 and 57, the four floors of the alien ship: the elevator
// between them, room 53's maintenance man, and room 57's terminal.
//
// The four are joined by an elevator, not by doorways. Every one of them
// carries the same four rows in the transition chain (transitions.cpp: 101->53,
// 102->57, 103->51, 104->55), and LOGIC:sub_1265c, which each room calls as its
// frame loop ends, picks among them from [0xa79e] once [0xa79f] says the panel
// has been used. What writes both is the resident CHARANIM:sub_150f4 (room 51:
// sub_151bc, the same body), called from every room's tick: a walk that ends
// ([0x9908] == 1) with the click that started it on object 2 ([0xa644] == 2)
// opens the panel, 15f3:sub_16895 (elevator.cpp), and raises [0xa79f] and
// [0xa881] as it returns. Object 2 is a registered rectangle with a walk row
// in all four rooms (check_hotspots.py / check_walkgeom.py --sweep), so the
// arrival test below is all the room side needs.
//
// Each room's prologue then answers [0xa7a1], which the panel raises on its
// way out: the lifted opening already plays the doors and puts Ben in front of
// them, but the prologue's flag writes are hand-owned by rule (gen_roominit.py,
// plays_of), and without them [0xa7a1] was never cleared and every later way
// in replayed the elevator. Room 55 also answers [0xa7a0], the no-card trip.
//
// **Room 53, the maintenance man** (ovr_35_0f9e entry 3). He stands at the
// door to the Boss' escape pod while [0xa7d6] holds -- the new game raises it
// -- registered as object 3, and talking to him opens the conversation menu
// on MAINT0.TAL's tree (entry 4, 0x013d): topic 0, or once he has handed over
// his badge, [0xa7d8], which alternates between 0x28 and 0x29. His answers are
// the tree's reply bytes, spoken at his own anchor and colour once Ben's line
// is down (CHARANIM:sub_151e8, DIALOG:sub_0bfb4 over what DLGREQ:sub_0c250 set
// up at the room's entry). Three of them hand the badge over -- topics 0x12,
// 0x16 and 0x19, "Here, take my badge..." -- by raising the room's [0xa49f] to
// 0x32: his hand-over pose, item 44 and [0xa7d7]. MIDAS:sub_19bf3 is his pose
// dispatcher, six frame lists on slot 2 ([0xa52c]).
//
// He leaves for good the next time any of rooms 51, 55 or 57 is entered with
// the badge held: each of their prologues clears [0xa7d6] (0x0270, 0x0258,
// 0x05f2). Room 53 then registers the pod door, object 6, and its card slot,
// object 5. The slot opens it for item 43 (a lifted row), which clears
// [0xa7d9] -- the prologue raises it on every way in but the one back from
// the pod -- and object 6's walk row takes submode 2 to room 59.
//
// **Room 57, the terminal** (entry 2). Card 42 in its slot while the force
// field is up ([0xa7b0]) speaks either "it's asking for a password" or, once
// the jail's LCD has been read ([0xa7ac], room 58), the password; the latter
// also raises [0xa49f] = 3 with the cursor gone, and the line coming down
// opens 15f3:sub_17373 (terminal.cpp) and leaves by submode 10, the room
// itself. Its entry with the field down plays the HAL_MONS alarm once
// (CUTSCENE:sub_0e458) before the room is loaded. Object 8 is the ship's
// loudspeaker, eight announcements in turn ([0xa7d5], 0x32..0x39).
static const int kLobbyRoom = 51;
static const int kHallwayRoomA = 53;
static const int kHallwayRoomB = 57;
static const int kCorridorRoom = 55;

/// Object 2 in all four rooms: the elevator door (sub_150f4 / sub_151bc).
static const byte kElevatorObj = 2;
/// [0xa644], the object the last left click was on (cemetery.cpp).
static const uint16 kClickedObj = 0xa644;
/// [0xa880], the room the last one was entered from.
static const uint16 kCameFrom = 0xa880;

/// [0xa7a1]: the panel has just been used, so this room opens at its elevator.
static const uint16 kElevatorArrival = 0xa7a1;
/// Room 51's pair of door flags and the other three rooms' pair, raised as the
/// doors are drawn open (ovr_33_0faa:0x03ad, ovr_35_0f9e:0x0857/0x0d7c,
/// ovr_37_0f9a:0x03c4) so the rooms' door ticks do not open them again. Room
/// 53's first pair is the pod door's.
static const uint16 kDoorA = 0xa7a2;
static const uint16 kDoorAWas = 0xa7a3;
static const uint16 kDoorB = 0xa7a4;
static const uint16 kDoorBWas = 0xa7a5;
/// [0xa7a0]: the panel was left with no card; room 55 speaks line 5
/// (ovr_37_0f9a:0x041d, answered at the top of entry 3 as [0xa956] == 5).
static const uint16 kNoCardLine = 0xa7a0;
static const byte kOutcomeNoCard = 5;

static const int kPodRoom = 59;

static const uint16 kManThere = 0xa7d6;		///< ships 1: he is at the pod door
static const uint16 kBadge = 0xa7d7;		///< he has handed his badge over
static const uint16 kManTopic = 0xa7d8;		///< ships 0x28: the talk after that
static const uint16 kPodLocked = 0xa7d9;	///< the slot has not taken item 43
static const uint16 kSpeakerLine = 0xa7d5;	///< ships 0x32: the next announcement
static const uint16 kManGone = 0x33f8;		///< room 53 was entered without him

static const byte kMaintenanceMan = 3;
static const byte kPodDoor = 6;
static const byte kLoudspeaker = 8;
static const byte kTerminal = 4;
static const byte kVerbLookAt = 5;
static const byte kVerbTalkTo = 6;

static const byte kItemBadge = 44;
static const byte kItemRedCard = 42;

/// 0x0155: after the badge the talk opens on [0xa7d8], stepping it through
/// 0x28 and 0x29 in turn.
static const byte kManTopicFirst = 0x28, kManTopicLast = 0x29;

/// DLGREQ:sub_0c250(0x161, 0x2b, 0x28, 0x28, 0x3f) at the room's entry (0x0dc2):
/// where his answers are drawn and in what colour.
static const int kManX = 0x161, kManY = 0x2b;
static const byte kManInk[3] = { 0x28, 0x28, 0x3f };

/// DIALOG:sub_0bfb4: the topics whose answer is the badge.
static const uint kBadgeTopics[] = { 0x12, 0x16, 0x19 };

/// The room's [0xa49f] steps (0x0feb).
static const byte kStepIdle = 0;
static const byte kStepListening = 3;
static const byte kStepBadge = 0x32;
static const byte kStepTurnAway = 0x46;
static const byte kStepTurning = 0x47;

/// MIDAS:sub_19bf3's six poses, [0xa52c], on slot 2 (HAL2_MSP.DL1). Each is a
/// frame list from the data segment played in mode 8, the looping ones with
/// [0xa53c] raised for the room's tick to relaunch.
static const uint kManSlot = 2;
static const byte kPoseTurnIn = 1, kPoseListen = 2, kPoseTalk = 3, kPoseHandOver = 4,
				  kPoseTurnOut = 5, kPoseWork = 6;
static const byte kManTurnIn[] = { 1, 2, 3, 3, 4, 5 };					// 271a:0x6dfe
static const byte kManListen[] = {										// 271a:0x6e04
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 7, 7, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 7, 5, 7, 8
};
static const byte kManTalk[] = { 8, 9, 10, 8, 10, 9 };					// 271a:0x6e22
static const byte kManHandOver[] = { 5, 12, 13, 14, 14, 14, 14, 14, 14, 15, 16, 5 };	// 0x6e28
static const byte kManTurnOut[] = { 4, 3, 2, 1 };						// 271a:0x6e34
static const byte kManWork[] = {										// 271a:0x6dc8
	17, 18, 17, 18, 17, 18, 17, 19, 20, 21, 22, 23, 22, 21, 22, 23, 22, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 29, 29, 29, 30, 31, 32, 33, 32, 33, 32, 33, 32, 34, 35,
	36, 37, 38, 37, 36, 37, 38, 37, 36, 39, 40, 25, 26, 24, 1
};

struct ManPose {
	const byte *frames;
	uint count;
	int rate;
	bool loop;
};
static const ManPose kManPoses[] = {
	{ nullptr, 0, 0, false },
	{ kManTurnIn, ARRAYSIZE(kManTurnIn), 3, false },
	{ kManListen, ARRAYSIZE(kManListen), 5, true },
	{ kManTalk, ARRAYSIZE(kManTalk), 4, true },
	{ kManHandOver, ARRAYSIZE(kManHandOver), 3, false },
	{ kManTurnOut, ARRAYSIZE(kManTurnOut), 3, false },
	{ kManWork, ARRAYSIZE(kManWork), 3, true }
};
static const int kListMode = 8;

/// Room 57: what card 42 in the terminal's slot speaks with the password known
/// (outcome 3, ending "Yes! That was it.") and the room's step waiting on it.
static const uint16 kFieldUp = 0xa7b0;
static const uint16 kPasswordKnown = 0xa7ac;
static const byte kStepCardLine = 3;
static const byte kTerminalSubmode = 10;	///< transitions.cpp: room 57, submode 10 -> room 57

/// The loudspeaker's lines (0x0195): DIALOG:sub_0b63a(line, 0xad, 0x48) in
/// green, eight of them in turn.
static const int kSpeakerX = 0xad, kSpeakerY = 0x48;
static const byte kSpeakerInk[3] = { 0x2f, 0x3f, 0x2f };
static const byte kSpeakerFirst = 0x32, kSpeakerLast = 0x39;

/// Room 57's alarm (0x05c6): one-shot, once the field is down.
static const uint16 kAlarmLatch = 0x33f7;
static const uint16 kAlarmPlayed = 0x33f6;
static const uint16 kWaitingFlags[] = { 0xa7dc, 0xa7dd, 0xa7de };
static const uint kAlarmMusic = 11;

/// [0xa7d4]: the terminal has just been left; room 57 puts Ben back at it.
static const uint16 kTerminalReturn = 0xa7d4;

/// sub_15120: the pod door, walked up to while it is locked.
static const byte kOutcomePodLocked = 5;

static bool isElevatorRoom(int room) {
	return room == kLobbyRoom || room == kHallwayRoomA || room == kHallwayRoomB ||
		   room == kCorridorRoom;
}

/// The prologues' flag writes, which the lift leaves to hand code (see this
/// file's header), and room 53's man.
void AlienEngine::startCorridor() {
	_hallMan = HallMan();
	_terminalStep = 0;
	if (!isElevatorRoom(_room))
		return;

	// 0x0270 / 0x0258 / 0x05f2: with the badge handed over, the man is gone
	// the next time any other floor is entered.
	if (_room != kHallwayRoomA && _script.flag(kBadge) == 1 && _script.flag(kManThere) != 0) {
		_script.setFlag(kManThere, 0);
		debugC(1, kDebugRooms, "corridor: the maintenance man has left room 53");
	}

	// 0x0be3: room 53 locks the pod door on every way in, and drops both its
	// door pairs; the way back from the pod opens it again (0x0d3d).
	if (_room == kHallwayRoomA) {
		_script.setFlag(kPodLocked, 1);
		_script.setFlag(kDoorA, 0);
		_script.setFlag(kDoorAWas, 0);
		_script.setFlag(kDoorB, 0);
		_script.setFlag(kDoorBWas, 0);
		if (_script.flag(kCameFrom) == kPodRoom) {
			_script.setFlag(kDoorA, 1);
			_script.setFlag(kDoorAWas, 1);
			_script.setFlag(kPodLocked, 0);
		}
	}

	if (_script.flag(kElevatorArrival) == 1) {
		_script.setFlag(kElevatorArrival, 0);
		if (_room == kLobbyRoom) {
			_script.setFlag(kDoorA, 1);
			_script.setFlag(kDoorAWas, 1);
		} else {
			_script.setFlag(kDoorB, 1);
			_script.setFlag(kDoorBWas, 1);
		}
		debugC(1, kDebugRooms, "corridor: out of the elevator into room %d", _room);
	}

	// 0x087d: back from the terminal, whose placement is a lifted row.
	if (_room == kHallwayRoomB)
		_script.setFlag(kTerminalReturn, 0);

	if (_room == kHallwayRoomA) {
		if (_script.flag(kManThere) == 1) {
			// 0x0dbb: DLGREQ:sub_0c250 is only the anchor and colour his
			// answers are drawn in (kManX, kManInk), and he is at work.
			hallManPose(kPoseWork);
		} else {
			_script.setFlag(kManGone, 1);
		}
	}

	if (_room == kCorridorRoom && _script.flag(kNoCardLine) == 1) {
		_script.setFlag(kNoCardLine, 0);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeNoCard, anchorX, anchorY);
		debugC(1, kDebugRooms, "corridor: back without a card");
	}
}

/**
 * Room 57's alarm, CUTSCENE:sub_0e458, which the room's entry plays before
 * anything of its own the first time it is entered with the field down
 * (0x05c6): HAL_MONS.DL2 over HAL_MONS.PCX, music slot 11.
 */
void AlienEngine::hallwayScene(int room) {
	if (room != kHallwayRoomB || _script.flag(kAlarmLatch) != 0 || _script.flag(kFieldUp) != 0)
		return;

	_script.setFlag(kAlarmLatch, 1);
	debugC(1, kDebugRooms, "corridor: the alarm, with the force field down");
	playDl2Clip("HAL_MONS.DL2", "HAL_MONS.PCX", kAlarmMusic, "alarm");
	for (uint i = 0; i < ARRAYSIZE(kWaitingFlags); i++)
		_script.setFlag(kWaitingFlags[i], 0);
	_script.setFlag(kAlarmPlayed, 1);
}

/// CHARANIM:sub_150f4 / sub_151bc: a walk that ended at the elevator door; and
/// in room 53 sub_15120, one that ended at the pod door while it is locked.
void AlienEngine::corridorArrival() {
	if (!isElevatorRoom(_room))
		return;
	if (_ben.isWalking() || _ben.isTurning() || !speechDone())
		return;

	if (_room == kHallwayRoomA && _script.flag(kClickedObj) == kPodDoor &&
		_script.flag(kPodLocked) == 1) {
		_script.setFlag(kClickedObj, 0);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomePodLocked, anchorX, anchorY);
		return;
	}

	if (_script.flag(kClickedObj) != kElevatorObj)
		return;

	// 0x158a: the latch is spent as the panel opens.
	_script.setFlag(kClickedObj, 0);
	playElevatorPanel();
}

/// MIDAS:sub_19bf3: the man's pose byte and the list it plays.
void AlienEngine::hallManPose(byte pose) {
	if (pose < kPoseTurnIn || pose > kPoseWork)
		return;

	_hallMan.pose = pose;
	const ManPose &p = kManPoses[pose];

	// A port addition, as jail.cpp has for the uncle: the tick pair between
	// two poses never shows him blank.
	_anims.setHold(kManSlot, true);
	_anims.play(kManSlot, 0, p.count, p.rate, kListMode, p.frames);
	_anims.setLoopFlag(kManSlot, p.loop ? 1 : 0);
}

/**
 * The clicks the two rooms' lifted rows get wrong, taken before the table: the
 * talk to the man, whose topic counter the lift writes flat and whose body --
 * the menu -- is a call it cannot carry, and the loudspeaker, the same twice
 * over (entry 4, 0x013d and 0x0187).
 */
bool AlienEngine::armHallway(int obj, byte verb, int item) {
	if (item)
		return false;

	if (_room == kHallwayRoomA && obj == kMaintenanceMan && verb == kVerbTalkTo &&
		_script.flag(kManThere) == 1) {
		uint topic = 0;
		if (_script.flag(kBadge) == 1) {
			topic = _script.flag(kManTopic);
			byte next = (byte)(topic + 1);
			if (next > kManTopicLast)
				next = kManTopicFirst;
			_script.setFlag(kManTopic, next);
		}

		openChat(topic);
		_hallMan.step = kStepListening;
		hallManPose(kPoseTurnIn);
		debugC(1, kDebugChat, "corridor: the maintenance man, topic %u", topic);
		return true;
	}

	if (_room == kHallwayRoomB && obj == kLoudspeaker && verb == kVerbLookAt) {
		const byte line = (byte)_script.flag(kSpeakerLine);
		const TalFile::Entry &entry = _tal.entry(line);
		if (entry.present) {
			setTextColor(kSpeakerInk[0], kSpeakerInk[1], kSpeakerInk[2]);
			uploadTextColor();
			speakEntry(entry, kSpeakerX, kSpeakerY, speechTicksFor(_tal, line));
		}

		byte next = (byte)(line + 1);
		if (next > kSpeakerLast)
			next = kSpeakerFirst;
		_script.setFlag(kSpeakerLine, next);
		debugC(1, kDebugRooms, "corridor: the loudspeaker, dialog %u", line);
		return true;
	}

	return false;
}

/**
 * Card 42 in room 57's terminal: the table speaks the line, and with the
 * password known (the lifted row's outcome 3) the room takes the cursor and
 * waits for it to come down (0x0072).
 */
void AlienEngine::armHallwayCard(int obj, int item) {
	if (_room != kHallwayRoomB || obj != kTerminal || item != kItemRedCard)
		return;
	if (_script.flag(kFieldUp) != 1 || _script.flag(kPasswordKnown) != 1)
		return;

	_terminalStep = kStepCardLine;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "corridor: the card goes into the terminal");
}

/// Whether the man still has the answer to the last pick to give (chat.cpp).
bool AlienEngine::hallwayOwesReply() const {
	return _room == kHallwayRoomA && _hallMan.answer;
}

/**
 * Room 53's tick: CHARANIM:sub_151e8 and DIALOG:sub_0bfb4, the answer to a
 * pick, and the room's [0xa49f] machine around them (0x0feb).
 */
void AlienEngine::stepHallMan() {
	// [0xad1a]: a line has just come down, or a pick has just been made
	// (OBJ:0x5fc7 and 0x23a0).
	const bool speaking = !speechDone();
	bool settled = _hallMan.speaking && !speaking;
	_hallMan.speaking = speaking;
	if (settled) {
		setTextColor(0x3f, 0x3f, 0x3f);
		uploadTextColor();
	}

	if (_chatPickNew) {
		_chatPickNew = false;
		settled = true;

		// [0xa60f]: the option picked ends the conversation.
		_hallMan.ended = _chatPickNext == TalFile::kChatEnd;

		if (_chatPickReply) {
			const TalFile::Entry &entry = _tal.entry(_chatPickReply);
			if (entry.present) {
				_hallMan.replyEntry = entry;
				_hallMan.replyTicks = speechTicksFor(_tal, _chatPickReply);
				_hallMan.reply = _chatPickReply;
				_hallMan.replyTopic = _chatPickTopic;
				_hallMan.answer = true;
			}
		}
	}

	// sub_151e8: the pick's own line is down, so he answers it; sub_0bfb4
	// speaks the reply byte and, on three topics, hands the badge over.
	if (_hallMan.answer && speechDone()) {
		_hallMan.answer = false;
		setTextColor(kManInk[0], kManInk[1], kManInk[2]);
		uploadTextColor();
		speakEntry(_hallMan.replyEntry, kManX, kManY, _hallMan.replyTicks);
		_hallMan.speaking = true;
		if (_hallMan.pose != kPoseWork)
			hallManPose(kPoseTalk);
		debugC(1, kDebugChat, "corridor: the man answers with dialog %u", _hallMan.reply);

		for (uint i = 0; i < ARRAYSIZE(kBadgeTopics); i++)
			if (_hallMan.replyTopic == kBadgeTopics[i])
				_hallMan.step = kStepBadge;
	}

	switch (_hallMan.step) {
	case kStepListening:
		if (_hallMan.ended)
			_hallMan.step = kStepTurnAway;
		if (settled)
			hallManPose(kPoseListen);
		break;

	case kStepBadge:
		hallManPose(kPoseHandOver);
		_inventory.add(kItemBadge);
		_script.setFlag(kBadge, 1);
		_hallMan.step = kStepListening;
		debugC(1, kDebugItems, "corridor: item %u (%s), the maintenance man's badge",
			   kItemBadge, _inventory.name(kItemBadge).c_str());
		break;

	case kStepTurnAway:
		hallManPose(kPoseTurnOut);
		_hallMan.step = kStepTurning;
		break;

	case kStepTurning:
		if (_anims.remaining(kManSlot) == 0) {
			hallManPose(kPoseWork);
			_hallMan.step = kStepIdle;
		}
		break;

	default:
		break;
	}

	_hallMan.ended = false;
}

/**
 * Rooms 53 and 57 have no walk mask and no node ring: ovr_35_0f9e's two
 * prologues name only R57, NOFADE, the TAL and the sprite banks, and never
 * call the KIERRA loaders rooms 51, 54 and 55 do. Entry 0's clamps and snaps
 * are what keep Ben on the floor there, so the click it resolved is walked to
 * in a straight line, the open-floor reading finding #57 gives an absent mask
 * page (docs/walk_system.md). Without this, plotRoute refused every click and
 * Ben could not move in either hallway.
 */
bool AlienEngine::hallwayWalkTo(int x, int y, int arrivalFacing) {
	if (_room != kHallwayRoomA && _room != kHallwayRoomB)
		return false;

	straightWalkTo(x, y, arrivalFacing);
	return true;
}

/**
 * The rooms' ticks: the elevator door and the pod door, room 53's man, and
 * room 57's card line opening the terminal (0x0b29).
 */
void AlienEngine::stepCorridor() {
	corridorArrival();

	if (_room == kHallwayRoomA)
		stepHallMan();

	if (_room != kHallwayRoomB || _terminalStep != kStepCardLine || !speechDone())
		return;

	_terminalStep = 0;
	CursorMan.showMouse(true);
	playNetTerminal();
	takeExit(kTerminalSubmode);
}

} // End of namespace Alien
