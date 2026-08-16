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

Walker::Walker() : _waypoint(0), _x(0), _y(0), _fx(0), _fy(0), _stepX(0), _stepY(0),
		_steps(0), _facing(3), _arrivalFacing(kFacingKeep), _phase(0),
		_frame(kIdleFrame[3]), _turnLeft(0) {
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
	updateFrame();
}

void Walker::stop() {
	_route.count = 0;
	_waypoint = 0;
	_steps = 0;
	updateFrame();
}

void Walker::follow(const WalkRoute &route, int targetX, int targetY,
					int arrivalFacing) {
	_route = route;
	_arrivalFacing = arrivalFacing;

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

	_frame = kIdleFrame[_facing];
}

void Walker::tick() {
	// A queued turn plays out before anything moves, the way the original
	// blocks the mover while its countdown is running.
	if (_turnLeft) {
		for (uint i = 1; i < _turnLeft; i++)
			_turn[i - 1] = _turn[i];
		_turnLeft--;
		updateFrame();
		return;
	}

	if (!isWalking()) {
		updateFrame();
		return;
	}

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

void Walker::draw(Graphics::Surface &dest) const {
	if (_anim.isLoaded())
		_anim.drawFrame(_frame, dest, _x, _y);
}

} // End of namespace Alien
