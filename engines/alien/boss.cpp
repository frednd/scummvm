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

// Room 60, Jack's room -- the boss.
//
// The one room in the game with no overlay: MAIN's tick chain sends handler
// 0x3c to CUTSCENE:sub_0e071 (seg_main.asm:0x046f), which is the room's whole
// frame loop, and its entry 0 is the resident 1021:sub_105fa. There is no
// fight in it. BOSSGAM1-3.DL1 are named by nothing in GAME.EXE; what the room
// runs is a conversation.
//
// sub_0e071's prologue takes the ticket number back (OBJ:sprite_remove(0x2f),
// item 47, the one room 54's desk stamped) and runs LOGIC:sub_125ec, the
// room's init: room60.tal, a flat scale line, Ben's sprite placed at 0x44,0x25 facing
// the front, and [0xa49f] = 3. It loads BOSSBALL.DL1 and BOSSANIM.DL1 into
// slots 0 and 1 (sub_0cf5f), BOS_LIGH.DL1 and BOS_DOOR.DL1 into 2 and 3, and
// starts all four; its loop relaunches slots 0 and 2 on every pass and slot 1
// while [0x293c] says the boss is talking. Then the machine:
//
//   3    after 0xa: walk to 0xaa,0x6e facing right; the door shuts behind him
//        (slot 3 backwards from frame 9) with INPUT:sub_01d1c(1)
//   4    after 0x32: DLGREQ:sub_0c4d1 -- nine outcomes from 1, the boss first
//   5    DLGREQ:sub_0c432 until [0xad3f]: the conversation's own pass
//   6    walk to 0x50,0x65 facing left
//   8    after 0x1e: the door opens (slot 3, nine frames) with
//        INPUT:sub_01d00(1)
//   0xa  after 0x2d: inv_add(0x2b) -- item 43, his escape pod card -- and
//        submode 1, back out to room 54
//
// DLGREQ:sub_0c384 answers handler 0x3c on every line the boss takes
// (0c25:0x0176): slot 1's eight talking frames at rate 4, and [0x293c] = 1
// so the loop above keeps them going until the line comes down.
static const int kBossRoom = 60;

static const byte kTicket = 47;			///< OBJ:sprite_remove(0x2f)
static const byte kCard = 43;			///< OBJ:sub_08ee5(0x2b), the escape pod card

/// CHARANIM:sub_13bce(0, 0x44, 0, 0x25, 3), from LOGIC:sub_125ec: the sprite's
/// corner, as every char_place is, not his feet.
static const int kStartX = 0x44, kStartY = 0x25, kStartFacing = 3;

/// The four slots and what sub_0e071 starts in them, as anim_play_mode1.
static const uint kBallSlot = 0;		///< BOSSBALL.DL1, 0x21 frames at rate 2
static const uint kBossSlot = 1;		///< BOSSANIM.DL1, held on frame 1
static const uint kLightSlot = 2;		///< BOS_LIGH.DL1, three frames at rate 0
static const uint kDoorSlot = 3;		///< BOS_DOOR.DL1, held on frame 9, open
static const char *const kBanks[] = {
	"BOSSBALL.DL1", "BOSSANIM.DL1", "BOS_LIGH.DL1", "BOS_DOOR.DL1"
};
static const int kBallFrames = 0x21, kBallRate = 2;
static const int kLightFrames = 3;
static const int kDoorFrames = 9, kDoorRate = 1;

/// 0c25:0x017d, handler 0x3c: anim_play_mode1(1, 1, 8, 4).
static const int kTalkFrames = 8, kTalkRate = 4;

/// The steps of [0xa49f], and the waits on [0xa49c] each one takes.
static const byte kStepIdle = 0;
static const byte kStepEnter = 3;
static const byte kStepTalk = 4;
static const byte kStepTalking = 5;
static const byte kStepLeave = 6;
static const byte kStepDoor = 8;
static const byte kStepExit = 0xa;
static const uint16 kEnterWait = 0xa;
static const uint16 kTalkWait = 0x32;
static const uint16 kDoorWait = 0x1e;
static const uint16 kExitWait = 0x2d;

/// OBJ:sub_07890's two walks: into the room, and back to the door.
static const int kInX = 0xaa, kInY = 0x6e, kInFacing = 2;
static const int kOutX = 0x50, kOutY = 0x65, kOutFacing = 4;

/// INPUT:sub_01d1c(1) and INPUT:sub_01d00(1): sfx_play_delayed of sample 1 at
/// 0x2af8, volume 0x37, panned -0x32, two ticks late and one.
static const uint kDoorSample = 1;
static const uint32 kDoorSampleRate = 0x2af8;
static const byte kDoorVolume = 0x37;
static const int8 kDoorPanning = -0x32;

/// DLGREQ:sub_0c4d1(0, 1, 9, 0xf5, 0x42, 0x28, 0x3f, 0x28, 0xa9, 0x2c, 0x3f,
/// 0x3f, 0x3f): the boss first, nine outcomes from 1, the boss in green at
/// 0xf5,0x42 and Ben in white at 0xa9,0x2c.
static const byte kFirstLine = 1, kLineCount = 9;
static const int kBossX = 0xf5, kBossY = 0x42;
static const byte kBossInk[3] = { 0x28, 0x3f, 0x28 };
static const int kBenX = 0xa9, kBenY = 0x2c;
static const byte kBenInk[3] = { 0x3f, 0x3f, 0x3f };

static const byte kWaitingExitSubmode = 1;	///< transitions.cpp: room 60, submode 1 -> room 54

/// 1021:sub_105fa, the room's entry 0: a click walks straight to where it
/// was made, its row clamped to 0xd..0x9a. There is no walk mask to route
/// through.
static const int kTopRow = 0xd, kBottomRow = 0x9a, kBarRow = 0x9f;

/// sub_0e071's prologue and LOGIC:sub_125ec.
void AlienEngine::startBoss() {
	if (_room != kBossRoom)
		return;

	_inventory.remove(kTicket);
	_ben.placeSprite(kStartX, kStartY, kStartFacing);

	for (uint i = 0; i < ARRAYSIZE(kBanks); i++)
		_anims.loadBank(i, kBanks[i]);
	_anims.markLooping(kBallSlot);
	_anims.markLooping(kLightSlot);
	_anims.markLooping(kBossSlot);
	_anims.play(kBallSlot, 1, kBallFrames, kBallRate, 1);
	_anims.play(kLightSlot, 1, kLightFrames, 0, 1);
	_anims.play(kBossSlot, 1, 1, 0, 1);
	_anims.play(kDoorSlot, kDoorFrames, 1, 0, 1);

	_bossStep = kStepEnter;
	_bossPos = 0;
	_bossLeft = 0;
	_bossSpeaking = false;
	_bossTalking = false;
	debugC(1, kDebugRooms, "boss: in, item %u taken back", kTicket);
}

/// 1021:sub_105fa: straight there, whatever is in the way.
bool AlienEngine::bossWalkTo(int x, int y, int arrivalFacing) {
	if (_room != kBossRoom)
		return false;

	if (y < kTopRow)
		y = kTopRow;
	else if (y > kBottomRow && y < kBarRow)
		y = kBottomRow;

	straightWalkTo(x, y, arrivalFacing);
	return true;
}

/// DLGREQ:sub_0c432, one pass: the line standing in [0xa4a2], by whoever's
/// turn it is (sluggs.cpp has the same runner for room 34).
void AlienEngine::bossSpeak() {
	if (!_bossLeft) {
		_bossSpeaking = false;
		return;
	}

	const bool boss = _bossSpeaker == 0;
	const byte *ink = boss ? kBossInk : kBenInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, _bossLine, boss ? kBossX : kBenX, boss ? kBossY : kBenY);

	if (boss) {
		_anims.play(kBossSlot, 1, kTalkFrames, kTalkRate, 1);
		_bossTalking = true;
	}

	debugC(1, kDebugRooms, "boss: %s says outcome 0x%02x", boss ? "the boss" : "Ben", _bossLine);

	_bossSpeaker = _bossSpeaker ? 0 : 1;
	_bossLine++;
	_bossLeft--;
}

/// sub_0e071's loop: the relaunches, the conversation's pass, and [0xa49f].
void AlienEngine::stepBoss() {
	if (_room != kBossRoom)
		return;

	// MIDAS:snd_func_112d(0) and (2) on every pass, and (1) while [0x293c]
	// stands -- which OBJ drops as the line comes down (0251:0x23aa).
	if (_bossTalking && speechDone())
		_bossTalking = false;
	_anims.relaunch(kBallSlot);
	_anims.relaunch(kLightSlot);
	if (_bossTalking)
		_anims.relaunch(kBossSlot);

	if (_bossSpeaking && speechDone())
		bossSpeak();

	if (_bossStep == kStepIdle)
		return;

	// [0xa49c], which LOGIC:sub_11f79 advances on every tick pair.
	_bossPos++;

	switch (_bossStep) {
	case kStepEnter:
		if (_bossPos <= kEnterWait)
			break;
		walkTo(kInX, kInY, kInFacing);
		_anims.play(kDoorSlot, kDoorFrames, kDoorFrames, kDoorRate, 3);
		_sound.queue(kDoorSample, kDoorSampleRate, kDoorVolume, kDoorPanning, 2);
		_bossStep = kStepTalk;
		_bossPos = 0;
		break;

	case kStepTalk:
		if (_bossPos <= kTalkWait)
			break;
		// DLGREQ:sub_0c4d1 takes the cursor away as it accepts the run.
		CursorMan.showMouse(false);
		_bossSpeaker = 0;
		_bossLine = kFirstLine;
		_bossLeft = kLineCount;
		_bossSpeaking = true;
		bossSpeak();
		_bossStep = kStepTalking;
		break;

	case kStepTalking:
		// [0xad3f], the run over and its last line down.
		if (_bossSpeaking || !speechDone())
			break;
		CursorMan.showMouse(true);
		_bossStep = kStepLeave;
		break;

	case kStepLeave:
		walkTo(kOutX, kOutY, kOutFacing);
		_bossPos = 0;
		_bossStep = kStepDoor;
		break;

	case kStepDoor:
		if (_bossPos <= kDoorWait)
			break;
		_anims.play(kDoorSlot, 1, kDoorFrames, kDoorRate, 1);
		_sound.queue(kDoorSample, kDoorSampleRate, kDoorVolume, kDoorPanning, 1);
		_bossStep = kStepExit;
		break;

	case kStepExit:
		if (_bossPos <= kExitWait)
			break;
		_inventory.add(kCard);
		debugC(1, kDebugItems, "boss: hands over item %u (%s)", kCard,
			   _inventory.name(kCard).c_str());
		_bossStep = kStepIdle;
		takeExit(kWaitingExitSubmode);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
