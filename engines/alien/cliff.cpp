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

#include "common/util.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// The cliff, which is two places drawn on one screen.
//
// Room 31 is the only way from the town up to the cemetery and the only way
// down from the chimney, and the screen holds both ends of that: the path at
// the foot of the rock, where the road from the Crossroads comes in, and the
// ledge at the top, where the chimney is to the left and the cemetery ahead.
// [0xa737] says which of the two he is standing on, and it is not puzzle
// progress -- room init works it out afresh on every arrival, clearing it when
// the mode left was 23 (the Crossroads) and setting it through [0xa73a] when
// the mode left was 14 (the chimney) or 32 (the cemetery), roominit.cpp rows
// 115-122. [0xa73a] is what the depth scale reads, so the top of the cliff is
// drawn at half size (scale.cpp), and the lighting dims it to 0x96 there too
// (lighting.cpp).
//
// Entry 0 is a branch on that flag and the two arms do not meet: the top arm
// returns at ovr_1f_0e87:0x0185 without running a line of the foot's geometry.
// So each ledge has its own idea of where a click sends him, and only the top
// arms the two doors -- obj 1 to the cemetery and obj 2 into the chimney are
// registered as hotspots wherever he stands, but their submodes are armed
// inside `if [0xa737] == 1` (0x008c). From the foot of the cliff, clicking the
// path does nothing at all, which is playtest report "in the room Cliff it is
// not possible to exit to the left".
//
// What answers that click is the climb, and it is this room's own [0xa49f]
// machine. Entry 0 ends by asking whether any of its rectangles took the click
// ([0xa807], which every zone and snap routine in 1021 sets and walk_to_object
// does not):
//
//   top  (0x0172)  a rectangle took it, and every rectangle up there is part of
//                  the way down -> [0xa738] = 1
//   foot (0x0241)  nothing took it, so the click was off the path he is on ->
//                  [0xa738] = 2, with the point the click had resolved to
//                  stashed in [0xa732]/[0xa734]/[0xa736] and the walk sent to
//                  the foot of the climb at 141,136 facing 1 instead
//
// The tick then waits for him to get there -- route run out, no turn owed,
// within three pixels of the point, facing the recorded way, which is the
// shared arrival test spelled out again at 0x06b9 -- and starts the machine:
//
//   3     down: after 0x19 of [0xa49c], play BEN_UP.DL1 on slot 0, 25 frames,
//         and clear [0xa737]
//   5     the slot has a frame left: the character is his own again at the
//         foot, placed at 133,70 facing front
//   0x14  up: play BEN_DOWN.DL1 on slot 1, 27 frames
//   0x15  the slot has a frame left
//   0x16  after another 0x1e of [0xa49c], set [0xa737] and [0xa73a], place him
//         on the ledge at 147,90 facing left, and send him on to the point the
//         click had asked for in the first place (OBJ:sub_07890 at 0x0838)
//
// The two banks are named for the rock face rather than for him: the way down
// plays BEN_UP.DL1 and the way up plays BEN_DOWN.DL1 (anims.cpp slots 0 and 1).
//
// One click does not make a door. Entry 0 ran while he was still at the foot,
// so the guarded submode was not armed and the climb ends with nothing waiting
// to fire; the second click, taken on the ledge, is the one that leaves. That
// is how the original plays too.
static const int kCliffRoom = 31;

static const uint16 kCliffTop = 0xa737;		///< he is on the ledge, not the path
static const uint16 kCliffLedge = 0xa73a;	///< the same state, as the scale reads it

static const uint kDownSlot = 0;	///< BEN_UP.DL1, the way off the ledge
static const uint kUpSlot = 1;		///< BEN_DOWN.DL1, the way onto it

/// [0xa738]: which climb the arrival is going to start.
static const byte kClimbDown = 1;
static const byte kClimbUp = 2;

static const byte kStepDown = 3;
static const byte kStepDownEnd = 5;
static const byte kStepUp = 0x14;
static const byte kStepUpPlaying = 0x15;
static const byte kStepUpEnd = 0x16;

/// [0xa49c] > this before the step ends, at 0x076a and 0x080c.
static const uint16 kDownWait = 0x19;
static const uint16 kUpWait = 0x1e;

/// The plays at 0x0771 and 0x07d6, both mode 4 -- forward, last frame not left.
static const int kDownFrames = 0x19;
static const int kUpFrames = 0x1b;
static const int kClimbRate = 3;
static const int kClimbMode = 4;

/// The frames left that end a climb, [0xa4ea] and [0xa4eb] at 0x0799 and 0x07f4.
static const int kClimbCue = 1;

/// Where the foot of the climb is, and the walk that reaches it (0x025f).
static const int kClimbX = 0x8d;
static const int kClimbY = 0x88;
static const int kClimbFacing = 1;

/// Where each climb puts him down, the char_place calls at 0x07b5 and 0x0828.
static const int kFootX = 133, kFootY = 70, kFootFacing = 3;
static const int kLedgeX = 147, kLedgeY = 90, kLedgeFacing = 4;

/// [0xa888] on each ledge: whole size at the foot, half size up top.
static const uint16 kFootScale = 0x100;
static const uint16 kLedgeScale = 0x200;

struct CliffRect {
	int16 x1, y1, x2, y2;
};

/// The way down: every rectangle the top arm registers, 0x0112 to 0x016d. They
/// are the four guarded zones of walkgeom.cpp, and all four send him to 157,154
/// at the head of the climb.
static const CliffRect kDownRects[] = {
	{ 120, 138, 319, 159 },
	{ 135, 129, 319, 137 },
	{ 151, 117, 319, 128 },
	{ 169,   0, 319, 116 }
};

/// The path at the foot, which is every rectangle the bottom arm registers --
/// 0x019e to 0x01ff, a snap, a zone, a floor rectangle and two more snaps --
/// and then the ledge rectangle the arm tests for itself at 0x021c. A click
/// inside any of them is a walk along the path; a click outside every one of
/// them is the climb.
static const CliffRect kFootRects[] = {
	{ 214, 137, 319, 159 },		// 0x019e
	{ 201,   0, 319, 136 },		// 0x01b6
	{ 169,   0, 200, 135 },		// 0x01c6
	{ 120, 138, 151, 159 },		// 0x01d6
	{ 135, 129, 151, 137 },		// 0x01e7
	{ 151, 117, 169, 128 }		// 0x01ff
};

/// 0x021c, which is an open test rather than the closed one the rows use.
static const CliffRect kLedgeRect = { 151, 128, 214, 159 };

static bool cliffInside(const CliffRect &r, int x, int y) {
	return x >= r.x1 && y >= r.y1 && x <= r.x2 && y <= r.y2;
}

/**
 * Entry 0's tail: whether this click is a climb, and where it walks to first.
 *
 * Called from the click path with the target the room's geometry has already
 * produced, which is what the original has in walk_pos by the time it asks.
 */
void AlienEngine::armCliff(int clickX, int clickY, WalkTarget &target) {
	if (_room != kCliffRoom || _cliffStep)
		return;

	// Whichever way this click goes, it replaces the one before it: entry 0
	// clears [0xa738] at the top of both arms.
	_cliffClimb = 0;

	if (_script.flag(kCliffTop) == 1) {
		bool over = false;
		for (uint i = 0; i < ARRAYSIZE(kDownRects) && !over; i++)
			over = cliffInside(kDownRects[i], clickX, clickY);
		if (!over)
			return;

		// The zone has already put the walk at the head of the climb.
		_cliffClimb = kClimbDown;
		_cliffX = target.x;
		_cliffY = target.y;
		_cliffFacing = target.facing;
		debugC(1, kDebugRooms, "cliff: over the edge, walking to %d,%d facing %u",
			   target.x, target.y, target.facing);
		return;
	}

	bool onPath = cliffInside(kLedgeRect, clickX, clickY);
	for (uint i = 0; i < ARRAYSIZE(kFootRects) && !onPath; i++)
		onPath = cliffInside(kFootRects[i], clickX, clickY);
	if (onPath)
		return;

	// Off the path: the click is kept for after the climb and the walk is sent
	// to the rock instead.
	_cliffClimb = kClimbUp;
	_cliffResumeX = target.x;
	_cliffResumeY = target.y;
	_cliffResumeFacing = target.facing;
	target.x = kClimbX;
	target.y = kClimbY;
	target.facing = kClimbFacing;
	_cliffX = kClimbX;
	_cliffY = kClimbY;
	_cliffFacing = kClimbFacing;
	debugC(1, kDebugRooms, "cliff: up the rock, then on to %d,%d facing %u",
		   _cliffResumeX, _cliffResumeY, _cliffResumeFacing);
}

void AlienEngine::stepCliff() {
	if (_room != kCliffRoom)
		return;

	// [0xa49c], which LOGIC advances on every tick pair whether or not a machine
	// is running; both waits below are measured in it.
	_cliffPos++;

	if (_cliffClimb && !_cliffStep) {
		// 0x06b9: the arrival, which is the shared test written out again.
		if (_ben.isWalking() || _ben.isTurning())
			return;
		if (ABS(_ben.walkX() - _cliffX) > 3 || ABS(_ben.walkY() - _cliffY) > 3)
			return;
		if (_ben.facing() != _cliffFacing)
			return;

		_cliffStep = _cliffClimb == kClimbDown ? kStepDown : kStepUp;
		_cliffClimb = 0;
		_cliffPos = 0;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "cliff: at the rock, step 0x%02x", _cliffStep);
		return;
	}

	switch (_cliffStep) {
	case kStepDown:
		// 0x0763: the pause before he goes over, and then the climb itself.
		if (_cliffPos <= kDownWait)
			break;
		playCharacterAnim(kDownSlot, 1, kDownFrames, kClimbRate, kClimbMode);
		_script.setFlag(kCliffTop, 0);
		_cliffStep = kStepDownEnd;
		_cliffPos = 0;
		break;

	case kStepDownEnd:
		// 0x0792: down, and the room is the near, bright, full-size one again.
		if (_anims.remaining(kDownSlot) != kClimbCue)
			break;
		CursorMan.showMouse(true);
		showCharacter();
		_ben.setScale(kFootScale);
		_script.setFlag(kCliffLedge, 0);
		_ben.place(kFootX, kFootY, kFootFacing);
		_cliffStep = 0;
		_dirty = true;
		debugC(1, kDebugRooms, "cliff: down at the foot of the path");
		break;

	case kStepUp:
		// 0x07ca
		playCharacterAnim(kUpSlot, 1, kUpFrames, kClimbRate, kClimbMode);
		_cliffStep = kStepUpPlaying;
		_cliffPos = 0;
		break;

	case kStepUpPlaying:
		// 0x07ed
		if (_anims.remaining(kUpSlot) != kClimbCue)
			break;
		_cliffStep = kStepUpEnd;
		_cliffPos = 0;
		break;

	case kStepUpEnd:
		// 0x0805: on the ledge, at half size and in the ledge's own light, and
		// then on to wherever the click that started the climb was going.
		if (_cliffPos <= kUpWait)
			break;
		CursorMan.showMouse(true);
		showCharacter();
		_ben.setScale(kLedgeScale);
		_script.setFlag(kCliffTop, 1);
		_script.setFlag(kCliffLedge, 1);
		_ben.place(kLedgeX, kLedgeY, kLedgeFacing);
		_cliffStep = 0;
		_dirty = true;

		// The room's hotspots and its geometry both read [0xa737], so the ledge
		// has to be built again now that he is standing on it.
		_script.buildHotspots(_room, _spots);
		walkTo(_cliffResumeX, _cliffResumeY, _cliffResumeFacing);
		debugC(1, kDebugRooms, "cliff: up on the ledge, walking on to %d,%d",
			   _cliffResumeX, _cliffResumeY);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
