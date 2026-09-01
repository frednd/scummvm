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

#include "common/debug.h"
#include "common/file.h"
#include "common/path.h"
#include "graphics/surface.h"

#include "alien/charanim.h"
#include "alien/detection.h"

namespace Alien {

// --------------------------------------------------------------------------
// <NAME>.DAT and its pixel strips
// --------------------------------------------------------------------------

CharAnim::CharAnim() {
	for (uint i = 0; i < kMaxStrips; i++) {
		_strip[i] = nullptr;
		_stripSize[i] = 0;
	}
}

CharAnim::~CharAnim() {
	unload();
}

void CharAnim::unload() {
	for (uint i = 0; i < kMaxStrips; i++) {
		delete[] _strip[i];
		_strip[i] = nullptr;
		_stripSize[i] = 0;
	}
	_frames.clear();
}

bool CharAnim::load(const Common::String &base) {
	unload();

	Common::File index;
	const Common::String indexName = base + ".DAT";
	if (!index.open(Common::Path(indexName))) {
		warning("Alien::CharAnim: cannot open %s", indexName.c_str());
		return false;
	}

	if (index.size() % kRecordSize) {
		warning("Alien::CharAnim: %s is %d bytes, not a whole number of records",
				indexName.c_str(), (int)index.size());
		return false;
	}

	const uint count = (uint)(index.size() / kRecordSize);
	_frames.resize(count);
	for (uint i = 0; i < count; i++) {
		Frame &f = _frames[i];
		f.strip = index.readByte();
		f.offset = index.readUint16LE();
		f.width = index.readUint16LE();
		f.height = index.readUint16LE();
		f.hotspotX = index.readByte();
		f.hotspotY = index.readByte();
	}

	if (index.err()) {
		unload();
		return false;
	}

	// Only the strips the records actually name are read; the sets differ in
	// how many they use, from one for ALI to eight for the jail guard.
	for (uint i = 0; i < count; i++) {
		const byte strip = _frames[i].strip;
		if (strip >= kMaxStrips) {
			warning("Alien::CharAnim: %s frame %u names strip %u", base.c_str(), i, strip);
			continue;
		}
		if (_strip[strip])
			continue;

		Common::File pixels;
		const Common::String name = Common::String::format("%s.%03u", base.c_str(), strip);
		if (!pixels.open(Common::Path(name))) {
			warning("Alien::CharAnim: cannot open %s", name.c_str());
			continue;
		}

		_stripSize[strip] = (uint32)pixels.size();
		_strip[strip] = new byte[_stripSize[strip]];
		pixels.read(_strip[strip], _stripSize[strip]);
	}

	debugC(1, kDebugResource, "%s: %u frames", base.c_str(), count);
	return true;
}

void CharAnim::drawFrame(uint index, Graphics::Surface &dest, int x, int y) const {
	if (index >= _frames.size())
		return;

	const Frame &f = _frames[index];
	if (f.strip >= kMaxStrips || !_strip[f.strip])
		return;

	const uint32 length = (uint32)f.width * f.height;
	if ((uint32)f.offset + length > _stripSize[f.strip])
		return;

	const byte *src = _strip[f.strip] + f.offset;
	const int left = x + f.hotspotX;
	const int top = y + f.hotspotY;

	for (int row = 0; row < (int)f.height; row++) {
		const int destY = top + row;
		if (destY < 0 || destY >= dest.h)
			continue;

		byte *out = (byte *)dest.getBasePtr(0, destY);
		const byte *in = src + (uint32)row * f.width;
		for (int col = 0; col < (int)f.width; col++) {
			const int destX = left + col;
			if (destX < 0 || destX >= dest.w)
				continue;
			if (in[col])
				out[destX] = in[col];
		}
	}
}

// --------------------------------------------------------------------------
// Walking the route
// --------------------------------------------------------------------------

// The standing frame per facing, from OBJ:sub_06466.
static const uint kIdleFrame[5] = { 0x40, 0x48, 0x4c, 0x40, 0x44 };

// The turn transitions registered by OBJ:sub_09810, which pairs each (from,
// to) facing with the frames to play between them: seven for a reversal, three
// for a quarter turn. The numbers are group relative and the player adds 0x3f
// to each, landing them in the turn range 0x40..0x6f.
struct TurnSequence {
	byte from;
	byte to;
	byte count;
	byte frames[Walker::kMaxTurnFrames];
};

static const TurnSequence kTurns[] = {
	{ 1, 3, 7, { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10 } },
	{ 1, 2, 3, { 0x0a, 0x0b, 0x0c, 0, 0, 0, 0 } },
	{ 1, 4, 3, { 0x08, 0x07, 0x06, 0, 0, 0, 0 } },
	{ 2, 4, 7, { 0x0e, 0x0f, 0x10, 0x01, 0x02, 0x03, 0x04 } },
	{ 2, 3, 3, { 0x0e, 0x0f, 0x10, 0, 0, 0, 0 } },
	{ 2, 1, 3, { 0x0c, 0x0b, 0x0a, 0, 0, 0, 0 } },
	{ 3, 1, 7, { 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 } },
	{ 3, 4, 3, { 0x02, 0x03, 0x04, 0, 0, 0, 0 } },
	{ 3, 2, 3, { 0x10, 0x0f, 0x0e, 0, 0, 0, 0 } },
	{ 4, 2, 7, { 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c } },
	{ 4, 1, 3, { 0x06, 0x07, 0x08, 0, 0, 0, 0 } },
	{ 4, 3, 3, { 0x04, 0x03, 0x02, 0, 0, 0, 0 } }
};

static const int kTurnFrameBase = 0x3f;

// Walking speed, in 1/64 pixel per tick. The mover keeps whichever axis leads
// at a fixed rate and scales the other by the slope: 1.5 pixels across, 0.625
// down, which is the perspective squash the room floors are drawn with.
static const int kSpeedX = 96;
static const int kSpeedY = 40;

// The idle machine's canned animations, from the frame lists at ds:0x283a,
// ds:0x2870 and ds:0x28b4. The original stores them one-based and subtracts one
// as it hands each to the blitter, so they are stored that way here too: the
// value that ends every list is the facing's own standing frame, which is how
// the character settles back rather than snapping.
static const byte kIdleShiftFront[] = {
	0x57,
	0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
	0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58,
	0x57, 0x41, 0x55,
	0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56,
	0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56,
	0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56, 0x56,
	0x55
};

static const byte kIdleShiftRight[] = {
	0x5b,
	0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
	0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
	0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
	0x5b, 0x4d, 0x59,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x5a, 0x5a, 0x5a, 0x5a, 0x5a,
	0x59
};

static const byte kIdleStretch[] = {
	0x51, 0x51, 0x52, 0x52, 0x53, 0x53,
	0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54,
	0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54,
	0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54,
	0x53, 0x53, 0x52, 0x52, 0x51, 0x51,
	0x41
};

// The talk cycle, from the four frame lists at ds:0x28dc, 0x28e2, 0x28ea and
// 0x28ee (OBJ:0x9be6 reads them, one list per facing). They are stored one
// based in the executable and the player subtracts one as it hands a frame to
// the blitter, so they are already zero based here. The frames sit at the top
// of BENANI, 0x64..0x6f, which is the range nothing else uses.
static const byte kTalkFrames[5][7] = {
	{ 0, 0, 0, 0, 0, 0, 0 },
	{ 0x6a, 0x6b, 0x6a, 0x6b, 0x6b, 0, 0 },					// 1, back
	{ 0x64, 0x65, 0x64, 0x66, 0x64, 0x65, 0x66 },			// 2, screen right
	{ 0x6c, 0x6d, 0x6e, 0x6f, 0, 0, 0 },					// 3, front
	{ 0x67, 0x68, 0x67, 0x69, 0x67, 0x68, 0x69 }			// 4, screen left
};

/// How much of each list is used, the counts OBJ:0x9a05 picks per facing.
static const uint kTalkCount[5] = { 0, 5, 7, 4, 7 };

/// [0xa808] has to be past this before the mouth opens, which is what keeps the
/// cycle out of the tick a walk ends on (OBJ:0x9995).
static const int kTalkSettle = 3;

/** One canned idle animation and the moment in the count it starts at. */
struct IdlePlay {
	int at;					///< the value of [0xa808] the original compares
	int facing;				///< only played from this standing frame
	const byte *frames;
	int count;
};

static const IdlePlay kIdlePlays[] = {
	{ 0x38, 3, kIdleShiftFront, ARRAYSIZE(kIdleShiftFront) },
	{ 0x38, 2, kIdleShiftRight, ARRAYSIZE(kIdleShiftRight) },
	{ 0x96, 3, kIdleStretch,    ARRAYSIZE(kIdleStretch) }
};

/// The count wraps here and carries into the cycle, as the original's 0xc8.
static const int kIdleWrap = 200;


Walker::Walker() : _waypoint(0), _x(0), _y(0), _fx(0), _fy(0), _stepX(0), _stepY(0),
		_steps(0), _facing(3), _arrivalFacing(kFacingKeep), _phase(0),
		_frame(kIdleFrame[3]), _turnLeft(0), _idleCount(0), _idleCycle(0),
		_idleStream(nullptr), _idleIndex(0), _idleLeft(0), _idleFrame(0),
		_talking(false), _talkReady(false), _talkPhase(0), _talkHalf(false) {
	memset(_turn, 0, sizeof(_turn));
}

void Walker::place(int walkX, int walkY, int facing) {
	_x = walkX - kWalkPointX;
	_y = walkY - kWalkPointY;
	_fx = _x * 64;
	_fy = _y * 64;
	_facing = facing;
	_arrivalFacing = kFacingKeep;
	_phase = 0;
	_steps = 0;
	_turnLeft = 0;
	_route.count = 0;
	_waypoint = 0;
	resetIdle();
	updateFrame();
}

void Walker::stop() {
	_route.count = 0;
	_waypoint = 0;
	_steps = 0;
	resetIdle();
	updateFrame();
}

void Walker::resetIdle() {
	_idleCount = 0;
	_idleCycle = 0;
	_idleIndex = 0;
	_idleLeft = 0;
	_idleStream = nullptr;
}

void Walker::follow(const WalkRoute &route, int targetX, int targetY,
					int arrivalFacing) {
	_route = route;
	_arrivalFacing = arrivalFacing;
	resetIdle();

	// Slot 0 is where the walk starts, and the nodes follow; the clicked point
	// is not in the route at all, so it goes on the end.
	if (_route.count < WalkRoute::kMaxPoints) {
		_route.points[_route.count].x = (int16)targetX;
		_route.points[_route.count].y = (int16)targetY;
		_route.count++;
	}

	_waypoint = _route.count > 1 ? 1 : 0;
	if (!_waypoint) {
		// Nothing to walk: he is already standing on the target, so the only
		// thing the route owed was the turn.
		stop();
		arrive();
		return;
	}

	startSegment();
	updateFrame();
}

int Walker::facingToward(int targetX, int targetY) const {
	// OBJ:sub_050c3 splits the plane at the character's feet and then compares
	// the two spans: the wider one decides whether this is a sideways walk or
	// one into or out of the screen. A tie is broken toward the horizontal,
	// which the original does by bumping the horizontal span by one.
	int dx = targetX - walkX();
	int dy = targetY - walkY();
	const bool left = dx < 0;
	const bool up = dy < 0;

	dx = ABS(dx);
	dy = ABS(dy);
	if (dx == dy)
		dx++;

	if (dx > dy)
		return left ? 4 : 2;
	return up ? 1 : 3;
}

void Walker::turnTo(int facing) {
	_turnLeft = 0;
	if (facing == _facing) {
		_facing = facing;
		return;
	}

	for (uint i = 0; i < ARRAYSIZE(kTurns); i++) {
		if (kTurns[i].from != _facing || kTurns[i].to != facing)
			continue;
		_turnLeft = kTurns[i].count;
		for (uint f = 0; f < _turnLeft; f++)
			_turn[f] = kTurns[i].frames[f];
		break;
	}

	_facing = facing;
}

void Walker::arrive() {
	// The room's geometry says which way to face what he walked to; anything
	// over four means it does not care, which is what the original's [0xa805]
	// default of ten encodes.
	const int facing = _arrivalFacing;
	_arrivalFacing = kFacingKeep;
	if (facing >= 1 && facing <= 4)
		turnTo(facing);
	updateFrame();
}

void Walker::startSegment() {
	// A route that starts where the character stands, or one whose last node is
	// the clicked point itself, carries waypoints with nothing to walk; those
	// are stepped over rather than costing a tick each.
	int dx = 0, dy = 0;
	while (isWalking()) {
		dx = _route.points[_waypoint].x - walkX();
		dy = _route.points[_waypoint].y - walkY();
		if (dx || dy)
			break;
		_waypoint++;
	}

	if (!isWalking()) {
		_steps = 0;
		_phase = 0;
		arrive();
		return;
	}

	const WalkRoute::Point &target = _route.points[_waypoint];
	turnTo(facingToward(target.x, target.y));

	// The axis that leads runs at its own speed and fixes the tick count; the
	// other is divided out over those ticks, which is how the original gets a
	// straight line without ever holding a slope.
	if (ABS(dx) * kSpeedY > ABS(dy) * kSpeedX) {
		_steps = ABS(dx) * 64 / kSpeedX;
		if (_steps < 1)
			_steps = 1;
		_stepX = dx > 0 ? kSpeedX : -kSpeedX;
		_stepY = dy * 64 / _steps;
	} else {
		_steps = ABS(dy) * 64 / kSpeedY;
		if (_steps < 1)
			_steps = 1;
		_stepY = dy > 0 ? kSpeedY : -kSpeedY;
		_stepX = dx * 64 / _steps;
	}
}

void Walker::updateFrame() {
	if (_turnLeft) {
		_frame = kTurnFrameBase + _turn[0];
		return;
	}

	if (isWalking()) {
		_frame = (uint)(_facing - 1) * kWalkFrames + _phase;
		return;
	}

	// Talking wins over the idle machine: the original reads the mouth frame
	// after the idle stream has already put one in [0xa8e6] (OBJ:0x9be6).
	if (_talking && _talkReady) {
		_frame = kTalkFrames[_facing][_talkPhase];
		return;
	}

	if (_idleLeft > 0) {
		_frame = _idleFrame;
		return;
	}

	_frame = kIdleFrame[_facing];
}

void Walker::setTalking(bool talking) {
	// [0x2938]. The dialog unit raises it as it dispatches a line and drops it
	// 25 half ticks before the line clears, so the mouth stops a moment before
	// the text does.
	_talking = talking;
	if (!talking)
		_talkHalf = false;
}

void Walker::stepTalk() {
	// [0x293a], which the standing path sets once the idle count is past three
	// and every branch that moves him clears.
	if (_idleCount > kTalkSettle)
		_talkReady = true;

	if (!_talking || !_talkReady)
		return;

	// A mouth frame replaces whatever the idle machine had started, which is
	// the original zeroing [0xa0ba] here rather than letting the two fight.
	_idleLeft = 0;

	// [0x2937] halves the rate: the list steps on every second animation tick.
	if (!_talkHalf)
		_talkPhase++;
	_talkHalf = !_talkHalf;

	if (_talkPhase >= kTalkCount[_facing])
		_talkPhase = 0;
}

void Walker::stepIdle(bool inventoryOpen) {
	if (++_idleCount >= kIdleWrap) {
		_idleCount = 0;
		_idleCycle++;
	}

	// A canned animation starts only from a standing frame, and only while the
	// inventory bar is down. Nothing stops the count while one plays, so the
	// two that share a starting point never collide: they want different
	// facings.
	if (!inventoryOpen) {
		for (uint i = 0; i < ARRAYSIZE(kIdlePlays); i++) {
			const IdlePlay &play = kIdlePlays[i];
			if (_idleCount != play.at || _facing != play.facing)
				continue;
			_idleStream = play.frames;
			_idleLeft = play.count;
			_idleIndex = 0;
			debugC(1, kDebugAnim, "idle: play %d frames facing %d at count %d",
				   play.count, _facing, _idleCount);
		}
	}

	// Left alone long enough, the character turns to face the player. The
	// original keeps the moment in [0x98fc] and [0x98fe], which it fills from
	// the facing: a quarter turn from the back or from screen left is a shorter
	// wait than the one from screen right.
	const bool quick = _facing == 1 || _facing == 4;
	if (_facing != 3 && _idleCount == (quick ? 5 : 0x28) &&
			_idleCycle == (quick ? 1 : 2)) {
		_idleIndex = 0;
		_idleLeft = 0;
		debugC(1, kDebugAnim, "idle: turn to face front after %d cycles",
			   _idleCycle);
		turnTo(3);
		return;
	}

	// The frame comes off the list between the countdown and the step, which is
	// what makes the last frame of a list the one the character is left holding.
	if (_idleLeft > 0) {
		_idleLeft--;
		_idleFrame = (uint)(_idleStream[_idleIndex] - 1);
		if (_idleLeft > 0)
			_idleIndex++;
	}
}

void Walker::tick(bool inventoryOpen) {
	// A queued turn plays out before anything moves, the way the original
	// blocks the mover while its countdown is running.
	if (_turnLeft) {
		for (uint i = 1; i < _turnLeft; i++)
			_turn[i - 1] = _turn[i];
		_turnLeft--;
		_talkReady = false;
		updateFrame();
		return;
	}

	if (!isWalking()) {
		stepIdle(inventoryOpen);
		stepTalk();
		updateFrame();
		return;
	}

	_talkReady = false;

	if (_steps > 0) {
		_fx += _stepX;
		_fy += _stepY;
		_x = _fx >> 6;
		_y = _fy >> 6;
		_steps--;

		// The walk cycle runs off its own counter rather than off the distance
		// covered, so it keeps time across a change of direction.
		_phase = (_phase + 1) % kWalkFrames;
	}

	if (_steps <= 0) {
		const WalkRoute::Point &target = _route.points[_waypoint];
		_x = target.x - kWalkPointX;
		_y = target.y - kWalkPointY;
		_fx = _x * 64;
		_fy = _y * 64;

		_waypoint++;
		if (isWalking()) {
			startSegment();
		} else {
			_phase = 0;
			arrive();
		}
	}

	updateFrame();
}

void Walker::draw(Graphics::Surface &dest, int scrollX) const {
	if (_anim.isLoaded())
		_anim.drawFrame(_frame, dest, _x - scrollX, _y);
}

bool Walker::bounds(Common::Rect &box) const {
	if (!_anim.isLoaded() || _frame >= _anim.frameCount())
		return false;

	// The same corner drawFrame() starts from: the character's position plus
	// the frame's own hotspot, which is an offset into his box rather than a
	// pivot.
	const CharAnim::Frame &f = _anim.frame(_frame);
	box = Common::Rect(_x + f.hotspotX, _y + f.hotspotY,
					   _x + f.hotspotX + f.width, _y + f.hotspotY + f.height);
	return true;
}

} // End of namespace Alien
