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
#include "graphics/surface.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Room 58's guard, and the two things in the corridor that go with him: the
// force field over the cells and the red security card he leaves behind.
//
// The guard is the one character in the game besides Ben with a sprite set
// of his own and a mover of his own. CHARANIM keeps him in a block of globals
// next to Ben's: his position at [0xa8c6]/[0xa8ca], a 10.6 fixed-point mover
// ([0xa8ce]..[0xa8d8], stepped by sub_15607 on the tick pair), a frame stepper
// ([0xbee2]..[0xbf02], sub_156a5) over JAIL_GUA.DAT's 53 records, and a pose
// dispatcher, sub_15878, whose twelve cases are the plays below. The room
// loads him (sub_15f0d) and draws him (sub_1579e) only while [0xa7b4] is set,
// which the new game ships and only the guard's own machine clears.
//
// His machine is CHARANIM:sub_15c32, [0xbefc], called from room 58's tick
// (0x0c12). The room starts it (0x1095): the first time a visit to the
// corridor finds Ben's sprite left of 0x19a with the guard still there, it
// walks Ben to 0x126,0x89 facing left, holds the camera on the left edge
// ([0xa8e0]/[0xa8e2]) and sets [0xbefc] = 9:
//
//   9     the camera is home: 0xa  the guard turns (pose 6, then pose 8)
//   0xc   he walks up to 0x98,0x93, and 1 asks "Who are you?" (outcome 0xf),
//         or "What now?" (0x10) once he has sent Ben away before ([0xa7b7])
//   0xd   the conversation menu on topic 0 of ROOM58.TAL's tree
//   0x14  the menu has ended: the Boss wants to see him (topic 0 or 1,
//         option 2, or topic 3 option 1 after the permit) -> 0x19; the wrong
//         cell (topic 1 option 3) -> 0x1e
//   0x19  he walks off to 0x190,0x96, and on arriving 0x1a looks at
//         [0xa7e3], the ticket number room 54 calls (waiting.cpp):
//           not called  back to 0x98,0x93 (0x1b), turns (pose 3), and the
//                       menu on topic 0xa or 0xd -- "You lied!" -- then 0x1e
//           called      0x28: he stays gone. Ben says outcome 0x11, [0xa7b4]
//                       drops, and so do [0xa7da] and [0xa7db] -- room 54's
//                       door to Jack and the ring on its floor. The card he
//                       had goes into the slot (slot 6, [0xa7b9]).
//   0x1e  Ben walks out to 0x154,0x89; 0x46 tick pairs later [0xa7b7] and
//         [0xa7b8] are raised and submode 1 takes him to room 51.
//
// Every line the guard speaks is DLGREQ:sub_0c275 for handler 0x3a: anchored
// at his head (x - 0x32 + 0x46, y - 0x5d) in (0x2c, 0x2c, 0x12) and in his
// talking pose 0xb; the tick puts him in pose 0xc, standing, as it comes down
// (0x1051). A pick out of the menu whose tree record carries a reply has the
// reply spoken the same way (sub_1530a).
//
// The force field is drawn every tick while [0xa7b0] is up: sixteen
// three-pixel-wide bars in front of the two cells, each column of each bar
// pushed through one of JAIL_RED.TBL's eight colour maps, two sets of three
// that the tick flips between ([0xa7af]) so the field shimmers
// (CHARANIM:sub_14925/sub_1496c). It goes over Ben in the cell and under him
// in the corridor, which is the order the tick draws the two views in.
//
// The prisoners' talk is registered only when the guard is not standing in
// the corridor (entry 1, 0x058e), and the uncle's rectangle moves with his
// frame (0x05c5); both are code the hotspot lift cannot carry, so they are
// added here after the table has run.
static const int kJailRoom = 58;

static const uint16 kView = 0xa7b1;
static const uint16 kForceField = 0xa7b0;
static const uint16 kGuardHere = 0xa7b4;	///< the guard is in the corridor
static const uint16 kGuardMet = 0xa7b6;		///< he has come up this visit
static const uint16 kSentAway = 0xa7b7;		///< he has sent Ben out before
static const uint16 kSentAwayNow = 0xa7b8;
static const uint16 kCardInSlot = 0xa7b9;	///< the card he left, object 14
static const uint16 kGuardAnswer = 0xa7b3;	///< which answer he comes back with
static const uint16 kNumberCalled = 0xa7e3;	///< waiting.cpp
static const uint16 kJackDoor = 0xa7da;		///< room 54's door to Jack
static const uint16 kRing = 0xa7db;			///< and the ring on its floor
static const byte kCorridor = 2, kCell = 1;

static const byte kSecurityCard = 42;		///< OBJ:sprite_add(0x2a), 0x0ea2
static const byte kCardObj = 14;
static const byte kVerbPickUp = 1;
static const uint kCardSlot = 6;			///< JAI_CARD.DL1

static const byte kYodleObj = 5, kUncleObj = 6;
static const byte kYodleLabel = 9, kUncleLabel = 10;
static const byte kVerbTalkTo = 6;
static const uint kUncleSlot = 1;

/// sub_15bd9: where he starts, and the corner his frames hang from.
static const int kGuardHomeX = 0x3a, kGuardHomeY = 0x8f;
static const int kGuardOffsetX = -0x32, kGuardOffsetY = -0x5d;
static const int kGuardClipBottom = 0x9b;	///< OBJ:sub_0676f's bottom clip

/// sub_159a1: horizontal and vertical speeds, 10.6.
static const int kGuardSpeedX = 0x78, kGuardSpeedY = 0x20;

/// DLGREQ:sub_0c275 for handler 0x3a: his anchor and ink.
static const int kGuardTalkX = 0x46;
static const byte kGuardInk[3] = { 0x2c, 0x2c, 0x12 };
static const byte kWhite[3] = { 0x3f, 0x3f, 0x3f };

/// The steps of [0xbefc].
static const byte kStepIdle = 0;
static const byte kStepAsk = 1;
static const byte kStepCamera = 9;
static const byte kStepTurn = 0xa;
static const byte kStepTurned = 0xb;
static const byte kStepApproach = 0xc;
static const byte kStepMenu = 0xd;
static const byte kStepMenuUp = 0xe;
static const byte kStepAnswered = 0x14;
static const byte kStepCheck = 0x19;
static const byte kStepChecking = 0x1a;
static const byte kStepBack = 0x1b;
static const byte kStepTurnBack = 0x1c;
static const byte kStepLied = 0x1d;
static const byte kStepOut = 0x1e;
static const byte kStepLeaving = 0x1f;
static const byte kStepCalled = 0x28;
static const byte kStepGone = 0x29;

static const byte kLineWho = 0x0f, kLineAgain = 0x10;
static const byte kLineGone = 0x11;			///< "I have a feeling he won't be back..."

/// The menu's topics and the picks 0x14 reads ([0xa635], [0xa60c]).
static const uint kTopicAsk = 0, kTopicLied = 0xa, kTopicNoRelease = 0xd;

/// The points the machine walks the two of them to.
static const int kBenMeetX = 0x126, kBenMeetY = 0x89, kBenMeetFacing = 4;
static const int kGuardMeetX = 0x98, kGuardMeetY = 0x93;
static const int kGuardAwayX = 0x190, kGuardAwayY = 0x96;
static const int kBenOutX = 0x154, kBenOutY = 0x89, kBenOutFacing = 2;
static const int kTriggerX = 0x19a;			///< [0xa8ec] left of this, 0x10aa
static const uint16 kOutWait = 0x46;
static const byte kLobbySubmode = 1;

/// [0xa8e2] as the cell view holds it (0x06e9).
static const int kCellHold = 0x15c;

/// The three frame lists sub_15811 steps, from the data segment.
static const byte kGuardIdle[] = {			// 271a:0x640c, pose 5
	1, 2, 3, 2, 3, 1, 3, 2, 1, 2, 1, 2, 3, 1, 3, 2
};
static const byte kGuardLook[] = {			// 271a:0x641c, pose 6
	1, 4, 5, 6, 7, 8, 8, 8, 7, 7, 8, 7, 6, 5, 4, 1, 9, 10, 11, 12, 13
};
static const byte kGuardTalk[] = {			// 271a:0x6432, pose 0xb
	1, 2, 3, 4, 5, 6, 5, 4, 5, 6, 7, 8, 1, 2, 1, 8, 7, 8
};

/// sub_15878's plays: first frame (or list index), count, rate, loop,
/// backwards, the list and the base added to what it reads.
struct GuardPlay {
	byte pose;
	byte first;
	byte count;
	byte rate;
	bool loop;
	bool backward;
	const byte *list;
	int base;
	bool stop;				///< cases 2 and 3 also stop the mover
};

static const GuardPlay kGuardPlays[] = {
	{ 0x1,  0x01, 0x08, 2, true,  false, nullptr,    -1,   false },	// walking right
	{ 0x2,  0x09, 0x10, 2, false, false, nullptr,    -1,   true  },
	{ 0x3,  0x18, 0x10, 2, false, true,  nullptr,    -1,   true  },	// turning back
	{ 0x4,  0x19, 0x08, 2, true,  false, nullptr,    -1,   false },	// walking left
	{ 0x5,  0x00, 0x10, 5, true,  false, kGuardIdle, 0x1f, false },	// standing
	{ 0x6,  0x00, 0x15, 4, false, false, kGuardLook, 0x1f, false },	// looking up
	{ 0x7,  0x09, 0x08, 3, false, false, nullptr,    -1,   false },
	{ 0x8,  0x10, 0x08, 3, false, true,  nullptr,    -1,   false },	// turning
	{ 0x9,  0x11, 0x08, 3, false, false, nullptr,    -1,   false },
	{ 0xa,  0x18, 0x08, 2, false, true,  nullptr,    -1,   false },
	{ 0xb,  0x00, 0x12, 4, true,  false, kGuardTalk, 0x2c, false },	// talking
	{ 0xc,  0x01, 0x01, 0, false, false, nullptr,    -1,   false }	// still
};

static const byte kPoseWalkRight = 1, kPoseTurnBack = 3, kPoseWalkLeft = 4,
				  kPoseIdle = 5, kPoseLook = 6, kPoseTurn = 8, kPoseTalk = 0xb,
				  kPoseStill = 0xc;

/// The sixteen bars, 0x7e at their foot: sub_14925 for the left cell,
/// sub_1496c for the right one.
struct FieldBar {
	int x;
	int height;
};

static const FieldBar kFieldBars[] = {
	{ 0x0e4, 0x64 }, { 0x0f2, 0x64 }, { 0x100, 0x64 }, { 0x10e, 0x64 },
	{ 0x11c, 0x64 }, { 0x12a, 0x64 }, { 0x0d6, 0x5e }, { 0x138, 0x5c },
	{ 0x189, 0x64 }, { 0x197, 0x64 }, { 0x1a5, 0x64 }, { 0x1b3, 0x64 },
	{ 0x1c1, 0x64 }, { 0x1cf, 0x64 }, { 0x17b, 0x58 }, { 0x1dd, 0x5e }
};
static const int kFieldFoot = 0x7e;
static const int kFieldView = 0x13d;		///< sub_146d5's own visibility test

/// CHARANIM:sub_146d5: which of the eight maps each of a bar's three
/// columns goes through, by the phase.
static const byte kFieldMaps[2][3] = { { 7, 5, 2 }, { 6, 4, 1 } };

/// Prologue: the guard (sub_15f0d) when he is there, the field's colour maps
/// (sub_14aa2), and the idle pose every visit puts him in (0x0946).
void AlienEngine::startJailGuard() {
	_guard.step = kStepIdle;
	_guard.clock = 0;
	_guard.talking = false;
	_guard.reply = 0;
	_guard.anim.unload();

	if (_room != kJailRoom)
		return;

	// 0x06d3 and 0x06d8.
	_script.setFlag(kGuardMet, 0);
	_script.setFlag(kSentAwayNow, 0);
	_jailFieldPhase = 0;

	if (!_jailRedLoaded) {
		Common::File table;
		if (table.open(Common::Path("JAIL_RED.TBL")) &&
				table.read(_jailRed, sizeof(_jailRed)) == sizeof(_jailRed))
			_jailRedLoaded = true;
		else
			warning("room 58: could not read JAIL_RED.TBL");
	}

	// [0xa8e0] = 1 with [0xa8e2] = 0x15c: the cell holds the camera on itself.
	if (_script.flag(kView) == kCell)
		_scrollHold = kCellHold;

	if (_script.flag(kGuardHere) == 1) {
		// sub_15bd9.
		_guard.x = kGuardHomeX;
		_guard.y = kGuardHomeY;
		_guard.fx = kGuardHomeX << 6;
		_guard.fy = kGuardHomeY << 6;
		_guard.vx = _guard.vy = 0;
		_guard.steps = 0;
		_guard.heading = 1;
		_guard.arrived = false;
		_guard.frame = 0;
		_guard.count = 0;

		// sub_15525.
		if (!_guard.anim.load("JAIL_GUA"))
			warning("room 58: could not load the guard's JAIL_GUA set");
	}

	jailGuardPose(kPoseIdle);
}

/// sub_15878: one of the twelve plays, through sub_157bc or sub_15811.
void AlienEngine::jailGuardPose(byte pose) {
	_guard.pose = pose;

	for (uint i = 0; i < ARRAYSIZE(kGuardPlays); i++) {
		const GuardPlay &p = kGuardPlays[i];
		if (p.pose != pose)
			continue;

		if (p.stop)
			_guard.steps = 0;
		_guard.list = p.list;
		_guard.base = p.base;
		_guard.index = p.first;
		_guard.start = p.first;
		_guard.count = p.count;
		_guard.countInit = p.count;
		_guard.rate = p.rate;
		_guard.rateCount = 0;
		_guard.loop = p.loop;
		_guard.backward = p.backward;
		_guard.frame = (p.list ? p.list[p.first] : p.first) + p.base;
		break;
	}
}

/// sub_156a5, on the tick pair.
void AlienEngine::jailGuardAnimate() {
	_guard.done = false;
	if (!_guard.count)
		return;
	if (++_guard.rateCount < _guard.rate)
		return;
	_guard.rateCount = 0;

	if (_guard.count > 1) {
		_guard.index += _guard.backward ? -1 : 1;
		_guard.frame = (_guard.list ? _guard.list[_guard.index] : _guard.index) + _guard.base;
	}

	_guard.count--;
	if (!_guard.count && !_guard.loop)
		_guard.done = true;
	if (!_guard.count && _guard.loop) {
		_guard.index = _guard.start;
		_guard.frame = (_guard.list ? _guard.list[_guard.index] : _guard.index) + _guard.base;
		_guard.count = _guard.countInit;
	}
}

/// sub_15607, on the tick pair: a step along the line sub_159a1 laid, and
/// the stop (sub_155f8) the tick pair the steps run out.
void AlienEngine::jailGuardMove() {
	_guard.arrived = false;
	const uint16 before = _guard.steps;

	if (_guard.steps > 0) {
		_guard.steps--;
		_guard.fx += _guard.vx;
		_guard.fy += _guard.vy;
		_guard.x = _guard.fx / 64;
		_guard.y = _guard.fy / 64;
		_dirty = true;
	}

	if (!_guard.steps && before != _guard.steps) {
		_guard.arrived = true;
		_guard.count = 0;
		_guard.loop = false;
	}
}

/**
 * sub_15b9e over sub_159a1: the straight line to a point, and the walking
 * pose that goes with its heading.
 *
 * The line is the mostly-horizontal one when he has further to go across than
 * down, at 0x78/64 of a pixel a tick pair, and the vertical one (half a
 * pixel) whenever the rise is at least the run over 1.3 -- the original's
 * Real test, which overrides the first. His y is only ever carried along
 * the horizontal line as (dy << 6) / |dx| a step, which is how it reads.
 */
void AlienEngine::jailGuardWalk(int x, int y) {
	const int dx = ABS(x - _guard.x);
	const int dy = ABS(y - _guard.y);

	if (dx > dy) {
		_guard.heading = x > _guard.x ? 2 : 4;
		_guard.steps = (uint16)((dx << 6) / kGuardSpeedX);
		_guard.vx = x > _guard.x ? kGuardSpeedX : -kGuardSpeedX;
		_guard.vy = (int16)(((y - _guard.y) << 6) / dx);
	}

	if (dy && dy * 13 >= dx * 10) {
		_guard.heading = y > _guard.y ? 3 : 1;
		_guard.steps = (uint16)(dy * 2);
		_guard.vy = y > _guard.y ? kGuardSpeedY : -kGuardSpeedY;
		_guard.vx = (int16)(((x - _guard.x) << 6) / (dy * 2));
	}

	if (_guard.steps > 0) {
		if (_guard.heading == 2)
			jailGuardPose(kPoseWalkRight);
		else if (_guard.heading == 4)
			jailGuardPose(kPoseWalkLeft);
		else
			_guard.pose = 0;		// sub_15878(0): no case, the frames keep going
	}

	debugC(1, kDebugRooms, "jail: the guard walks %d,%d -> %d,%d, %u steps", _guard.x,
		   _guard.y, x, y, _guard.steps);
}

/// The anchor DLGREQ:sub_0c250 is given for his lines.
void AlienEngine::jailGuardAnchor(int &x, int &y) const {
	x = _guard.x + kGuardOffsetX + kGuardTalkX;
	y = _guard.y + kGuardOffsetY;
}

/// DLGREQ:sub_0c275 for handler 0x3a.
void AlienEngine::jailGuardSpeak(byte code) {
	int x, y;
	jailGuardAnchor(x, y);
	setTextColor(kGuardInk[0], kGuardInk[1], kGuardInk[2]);
	uploadTextColor();
	queueOutcome(_tal, code, x, y, false);
	jailGuardPose(kPoseTalk);
	_guard.talking = true;
	debugC(1, kDebugRooms, "jail: the guard says outcome 0x%02x", code);
}

/// 0x1095: the guard comes up to Ben the first time he walks left of 0x19a.
void AlienEngine::jailGuardTrigger() {
	if (_script.flag(kGuardHere) != 1 || _script.flag(kGuardMet) != 0 ||
		_script.flag(kView) != kCorridor || _ben.spriteX() >= kTriggerX)
		return;

	_script.setFlag(kGuardMet, 1);
	_guard.step = kStepCamera;
	walkTo(kBenMeetX, kBenMeetY, kBenMeetFacing);
	_scrollHold = 0;
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "jail: the guard has seen him");
}

/// CHARANIM:sub_15c32.
void AlienEngine::jailGuardMachine() {
	const bool quiet = speechDone() && !_guard.reply && !_guard.talking;

	switch (_guard.step) {
	case kStepAsk:
		if (!_guard.arrived)
			break;
		jailGuardSpeak(_script.flag(kSentAway) == 1 ? kLineAgain : kLineWho);
		_guard.step = kStepMenu;
		break;

	case kStepCamera:
		if (_scrollX == 0)
			_guard.step = kStepTurn;
		break;

	case kStepTurn:
		jailGuardPose(kPoseLook);
		_guard.step = kStepTurned;
		break;

	case kStepTurned:
		if (!_guard.done)
			break;
		jailGuardPose(kPoseTurn);
		_guard.step = kStepApproach;
		break;

	case kStepApproach:
		if (!_guard.done)
			break;
		jailGuardWalk(kGuardMeetX, kGuardMeetY);
		_guard.step = kStepAsk;
		break;

	case kStepMenu:
		if (!quiet)
			break;
		openChat(kTopicAsk);
		_guard.step = kStepMenuUp;
		break;

	case kStepMenuUp:
		if (_chat.isActive() || !_chat.isFinished())
			break;
		_guard.step = kStepAnswered;
		break;

	case kStepAnswered: {
		const uint topic = _chatPickTopic, choice = _chatPickChoice;
		if ((topic == 0 && choice == 2) || (topic == 1 && choice == 1)) {
			_guard.step = kStepCheck;
			_script.setFlag(kGuardAnswer, 1);
		} else if (topic == 3 && choice == 1) {
			_guard.step = kStepCheck;
			_script.setFlag(kGuardAnswer, 2);
		} else if (topic == 1 && choice == 3) {
			_guard.step = kStepOut;
		}
		debugC(1, kDebugRooms, "jail: the menu ended on topic %u option %u, step 0x%02x",
			   topic, choice, _guard.step);
		break;
	}

	case kStepCheck:
		if (!quiet)
			break;
		jailGuardWalk(kGuardAwayX, kGuardAwayY);
		_guard.step = kStepChecking;
		break;

	case kStepChecking:
		if (!_guard.arrived)
			break;
		if (_script.flag(kNumberCalled) == 0) {
			jailGuardWalk(kGuardMeetX, kGuardMeetY);
			_guard.step = kStepBack;
		} else {
			_script.setFlag(kCardInSlot, 1);
			_anims.play(kCardSlot, 1, 1, 0, 2);
			_script.setFlag(kGuardAnswer, 0xa);
			_guard.step = kStepCalled;
		}
		break;

	case kStepBack:
		if (!_guard.arrived)
			break;
		jailGuardPose(kPoseTurnBack);
		_guard.step = kStepTurnBack;
		break;

	case kStepTurnBack:
		if (!_guard.done)
			break;
		if (_script.flag(kGuardAnswer) == 1)
			openChat(kTopicLied);
		else if (_script.flag(kGuardAnswer) == 2)
			openChat(kTopicNoRelease);
		_guard.step = kStepLied;
		break;

	case kStepLied:
		if (_chat.isActive() || !_chat.isFinished())
			break;
		_guard.step = kStepOut;
		break;

	case kStepOut:
		if (!quiet)
			break;
		CursorMan.showMouse(false);
		walkTo(kBenOutX, kBenOutY, kBenOutFacing);
		_guard.step = kStepLeaving;
		_guard.clock = 0;
		break;

	case kStepLeaving:
		if (_guard.clock <= kOutWait)
			break;
		_script.setFlag(kSentAway, 1);
		_script.setFlag(kSentAwayNow, 1);
		_guard.step = kStepIdle;
		debugC(1, kDebugRooms, "jail: the guard sends him back to the lobby");
		takeExit(kLobbySubmode);
		break;

	case kStepCalled: {
		int x, y;
		characterAnchor(x, y);
		setTextColor(kWhite[0], kWhite[1], kWhite[2]);
		uploadTextColor();
		queueOutcome(_tal, kLineGone, x, y);
		_script.setFlag(kGuardHere, 0);
		_script.setFlag(kJackDoor, 1);
		_script.setFlag(kRing, 1);
		_guard.step = kStepGone;
		_dirty = true;
		debugC(1, kDebugRooms, "jail: the Boss wants to see the guard; the ring and Jack's door are out");
		break;
	}

	case kStepGone:
		if (!speechDone())
			break;
		_scrollHold = -1;
		CursorMan.showMouse(true);
		_guard.step = kStepIdle;
		rebuildHotspots();
		break;

	default:
		break;
	}
}

/// The guard's machine is mid-scene: walking, turning or talking, with the
/// cursor still up. Only a menu waiting on a pick hands the turn back.
bool AlienEngine::jailGuardBusy() const {
	if (_room != kJailRoom || _guard.step == kStepIdle)
		return false;
	if ((_guard.step == kStepMenuUp || _guard.step == kStepLied) && _chat.isActive())
		return false;
	return true;
}

/// The room's tick as far as the guard and the field go.
void AlienEngine::stepJailGuard() {
	if (_room != kJailRoom)
		return;

	// 0x09da: [0xa7af] flips every tick the field is up.
	if (_script.flag(kForceField) == 1) {
		_jailFieldPhase ^= 1;
		_dirty = true;
	}

	// 0x09d6.
	_guard.clock++;

	if (_script.flag(kGuardHere) == 1) {
		jailGuardMove();
		jailGuardAnimate();
		_dirty = true;
	}

	// 0x1051: a line of his coming down stands him still again.
	if (_guard.talking && speechDone()) {
		_guard.talking = false;
		if (_guard.pose == kPoseTalk)
			jailGuardPose(kPoseStill);
		setTextColor(kWhite[0], kWhite[1], kWhite[2]);
		uploadTextColor();
	}

	// sub_1530a: a pick with a reply has him answer once Ben's own line of it
	// is down.
	if (_chatPickNew && _guard.step != kStepIdle) {
		_chatPickNew = false;
		if (_chatPickReply)
			_guard.reply = _chatPickReply;
	}
	if (_guard.reply && speechDone()) {
		const TalFile::Entry &entry = _tal.entry(_guard.reply);
		if (entry.present) {
			int x, y;
			jailGuardAnchor(x, y);
			setTextColor(kGuardInk[0], kGuardInk[1], kGuardInk[2]);
			uploadTextColor();
			speakEntry(entry, x, y, speechTicksFor(_tal, _guard.reply));
			jailGuardPose(kPoseTalk);
			_guard.talking = true;
			debugC(1, kDebugRooms, "jail: the guard answers with dialog %u", _guard.reply);
		}
		_guard.reply = 0;
	}

	jailGuardMachine();
	jailGuardTrigger();
}

/// Entry 1's two prisoners, 0x058e..0x05f6.
void AlienEngine::buildJailHotspots(int room) {
	if (room != kJailRoom)
		return;

	for (uint i = 0; i < _spots.size(); i++) {
		if (_spots[i].obj != kYodleObj || _spots[i].label != kYodleLabel)
			continue;

		// [0x98f6]: neither is there to talk to while the guard stands in
		// the corridor they are seen from.
		if (_script.flag(kGuardHere) == 1 && _script.flag(kView) == kCorridor) {
			_spots.remove_at(i);
			return;
		}

		// [0x98e4]: the uncle leans over while slot 1 is on frames 4..0xb.
		const int frame = _anims.shownFrame(kUncleSlot);
		const int x = (frame >= 4 && frame <= 0xb) ? 0xc8 : 0xeb;

		Hotspot uncle;
		uncle.x1 = (int16)x;
		uncle.y1 = 0x3d;
		uncle.x2 = (int16)(x + 0x19);
		uncle.y2 = 0x7b;
		uncle.label = kUncleLabel;
		uncle.obj = kUncleObj;
		uncle.verb = kVerbTalkTo;
		uncle.outcomeCount = 1;
		memset(uncle.outcomes, 0, sizeof(uncle.outcomes));
		_spots.insert_at(i + 1, uncle);
		return;
	}
}

/**
 * Entry 3, 0x0245: the red card out of the slot. The lifted row for it
 * (object 14, verb 1) already clears [0xa7b9] and plays the empty slot; what
 * it cannot carry is [0xa49f] = 0x64, the state that hands the card over on
 * the next tick (0x0ea2, OBJ:sprite_add(0x2a)).
 */
bool AlienEngine::armJailCard(int obj, byte verb) {
	if (_room != kJailRoom || obj != kCardObj || verb != kVerbPickUp ||
		_inventory.has(kSecurityCard))
		return false;

	_inventory.add(kSecurityCard);
	debugC(1, kDebugItems, "jail: item %u (%s) out of the slot", kSecurityCard,
		   _inventory.name(kSecurityCard).c_str());
	return true;
}

/// Whether the menu has to wait for an answer of his before it moves on.
bool AlienEngine::jailGuardHoldsChat() const {
	if (_room != kJailRoom || _guard.step == kStepIdle)
		return false;
	return _guard.reply || _guard.talking || (_chatPickNew && _chatPickReply);
}

/// The cell sees the field in front of Ben; the corridor behind him.
bool AlienEngine::jailFieldOverBen() const {
	return _script.flag(kView) != kCorridor;
}

/// CHARANIM:sub_14925 and sub_1496c over what is already on the page.
void AlienEngine::drawJailField(Graphics::Surface &dest) const {
	if (_room != kJailRoom || _script.flag(kForceField) != 1 || !_jailRedLoaded)
		return;

	const byte *maps = kFieldMaps[_jailFieldPhase & 1];
	for (uint i = 0; i < ARRAYSIZE(kFieldBars); i++) {
		const FieldBar &bar = kFieldBars[i];
		if (bar.x < _scrollX || bar.x > _scrollX + kFieldView)
			continue;

		for (int row = kFieldFoot - bar.height + 1; row <= kFieldFoot; row++) {
			if (row < 0 || row >= dest.h)
				continue;
			for (int c = 0; c < 3; c++) {
				const int x = bar.x + c - _scrollX;
				if (x < 0 || x >= dest.w)
					continue;
				byte *px = (byte *)dest.getBasePtr(x, row);
				*px = _jailRed[maps[c] * 256 + *px];
			}
		}
	}
}

/// sub_1579e: OBJ:sub_0676f at his corner, unscaled.
void AlienEngine::drawJailGuard(Graphics::Surface &dest) const {
	if (_room != kJailRoom || _script.flag(kGuardHere) != 1 || !_guard.anim.isLoaded())
		return;
	if (_guard.frame < 0 || (uint)_guard.frame >= _guard.anim.frameCount())
		return;

	_guard.anim.drawFrame((uint)_guard.frame, dest, _guard.x + kGuardOffsetX - _scrollX,
						  _guard.y + kGuardOffsetY, kGuardClipBottom);
}

} // End of namespace Alien
