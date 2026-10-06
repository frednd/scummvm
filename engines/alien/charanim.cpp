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

// The depth scale, and the shape the blit puts a frame through to honour it.
//
// [0xa888] is one 8.8 fixed-point *divisor* and OBJ:dl1_load_and_blit -- which,
// despite the name the disassembly gives it, is the character's blit and
// nothing else's; every other sprite in the game goes down the DL1 strip path
// -- reads it into a local before it does anything else (0251:0c82). Four
// things come out of it:
//
//   * the top edge moves down by `0x40 - 0x4000/scale` (0251:0cdf), 0x40 being
//     the walk point's offset down the frame. That is what keeps the *feet*
//     where the walk system put them while the rest of him shrinks upward.
//   * the record's own hotspot is divided by it too (0251:0d03), so the frame
//     stays in the same place inside a box that is itself smaller.
//   * the drawn width and height are counted rather than divided (0251:0d59,
//     0251:0d79): one output pixel per step of the source that is still inside
//     the frame.
//   * and the source is walked with the divisor as its step -- integer part in
//     AX, fraction accumulated in DH, `adc si, ax` carrying it (0251:0e5e).
//
// The column accumulator starts one step in and the row accumulator starts at
// zero, so the two axes are half a step out of phase with each other. That is
// the original's, not a slip here: the row offset is recomputed from a running
// total after each row (0251:0e81) while the column fraction is primed once
// before the run (0251:0e65).
//
// Which rooms ask for which scale is scale.cpp.

/// The extent one axis occupies at `scale`, counted the original's way.
static int scaledExtent(int size, uint16 scale) {
	int n = 0;
	while ((((n + 1) * (int)scale) >> 8) <= size)
		n++;
	return n;
}

bool CharAnim::geometry(uint index, int x, int y, uint16 scale, Geometry &out) const {
	if (index >= _frames.size())
		return false;
	if (!scale)
		scale = kUnitScale;

	const Frame &f = _frames[index];

	// The feet stay put: whatever the shrink takes off the height is given
	// back to the top edge.
	y += kFootOffset - 0x4000 / (int)scale;

	out.left = x + (((int)f.hotspotX << 8) / (int)scale);
	out.top = y + (((int)f.hotspotY << 8) / (int)scale);
	out.width = scaledExtent((int)f.width, scale);
	out.height = scaledExtent((int)f.height, scale);
	return true;
}

void CharAnim::drawFrame(uint index, Graphics::Surface &dest, int x, int y, int clipBottom,
						 uint16 scale) const {
	if (index >= _frames.size())
		return;

	const Frame &f = _frames[index];
	if (f.strip >= kMaxStrips || !_strip[f.strip])
		return;

	const uint32 length = (uint32)f.width * f.height;
	if ((uint32)f.offset + length > _stripSize[f.strip])
		return;

	if (!scale)
		scale = kUnitScale;

	Geometry g;
	if (!geometry(index, x, y, scale, g))
		return;

	// The two clips that also move the source along, and then the two that only
	// shorten the run. The second pair is measured from the *unclipped* corner:
	// the original still has that in its argument slots, having moved only its
	// own copy of it (0251:0dfb and 0251:0e1b against 0251:0db9).
	const int leftUnclipped = g.left;
	const int topUnclipped = g.top;
	int srcRow = 0, srcCol = 0;

	if (g.top < kFieldTop) {
		const int cut = kFieldTop - g.top;
		g.height -= cut;
		if (g.height < 1)
			return;
		srcRow = (cut * (int)scale) >> 8;
		g.top += cut;
	}

	if (g.left < 0) {
		const int cut = -g.left;
		g.width -= cut;
		if (g.width < 1)
			return;
		srcCol = (cut * (int)scale) >> 8;
		g.left += cut;
	}

	if (leftUnclipped + g.width >= (int)dest.w) {
		g.width = (int)dest.w - leftUnclipped;
		if (g.width < 1)
			return;
	}

	if (topUnclipped + g.height >= clipBottom) {
		g.height = clipBottom - topUnclipped;
		if (g.height < 1)
			return;
	}

	const byte *base = _strip[f.strip] + f.offset;
	const int step = scale >> 8;
	const int frac = scale & 0xff;

	int acc = 0;
	for (int row = 0; row < g.height; row++) {
		const int source = srcRow + (acc >> 8);
		acc += (int)scale;
		if (source >= (int)f.height)
			break;

		const int destY = g.top + row;
		if (destY < 0 || destY >= dest.h || destY >= clipBottom)
			continue;

		const byte *in = base + (uint32)source * f.width;
		byte *out = (byte *)dest.getBasePtr(0, destY);

		int col = srcCol;
		int carry = frac;			///< primed one step in, as DH is
		for (int i = 0; i < g.width; i++) {
			if (col >= (int)f.width)
				break;

			const int destX = g.left + i;
			if (destX >= 0 && destX < dest.w && in[col])
				out[destX] = in[col];

			col += step;
			carry += frac;
			if (carry >= 0x100) {
				carry -= 0x100;
				col++;
			}
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

// Walking speed, in 1/64 pixel per tick pair. The mover keeps whichever axis leads
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

// The pumpkin mask's talk cycle, OBJ:sub_09d22's lists at ds:0x28f6, 0x2902,
// 0x2914 and 0x2928, with the counts it picks per facing (0x9e5b..). They are
// longer than BENANI's -- the mask's mouth is a whole head bobbing -- and sit
// at PUMPWALK's top, 0x56..0x69, already zero based here.
static const byte kPumpkinTalk1[] = { 0x56, 0x57, 0x56, 0x56, 0x57, 0x58, 0x59, 0x58, 0x57, 0x56, 0x58, 0x59 };
static const byte kPumpkinTalk2[] = { 0x5a, 0x5b, 0x5c, 0x5b, 0x5c, 0x5b, 0x5a, 0x5b, 0x5c, 0x5b, 0x5c, 0x5b,
									  0x5a, 0x5b, 0x5c, 0x5b, 0x5d, 0x5d };
static const byte kPumpkinTalk3[] = { 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x5e, 0x5f,
									  0x60, 0x61, 0x62, 0x62, 0x63, 0x64, 0x65, 0x5e };
static const byte kPumpkinTalk4[] = { 0x66, 0x67, 0x68, 0x67, 0x68, 0x67, 0x66, 0x67, 0x68, 0x67, 0x68, 0x67,
									  0x69, 0x69 };
static const byte *const kPumpkinTalk[5] = { nullptr, kPumpkinTalk1, kPumpkinTalk2, kPumpkinTalk3, kPumpkinTalk4 };
static const uint kPumpkinTalkCount[5] = { 0, ARRAYSIZE(kPumpkinTalk1), ARRAYSIZE(kPumpkinTalk2),
										   ARRAYSIZE(kPumpkinTalk3), ARRAYSIZE(kPumpkinTalk4) };

/// sub_09d22's turn to the front: one moment for every facing, the one
/// sub_098d1 keeps for screen right, and none of its gates on [0xa94d] and
/// [0xa4a1].
static const int kPumpkinTurnAt = 0x28, kPumpkinTurnCycle = 2;

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

// The swim, segment 0x0ec7 and OBJ:sub_0585d (setSwimming). DIVEANI has no
// back or front walk: facing 1 is the stroke to screen right, frames
// 0x00..0x10, and 4 the stroke to screen left, 0x12..0x22. 3 is upright, the
// way he comes down through the hole, and nothing swims facing 2.
static const uint kSwimRight = 0x00;
static const uint kSwimLeft = 0x12;
static const uint kSwimUpright = 0x24;
static const uint kSwimFrames = 0x11;		///< [0xa9a1] wraps here (0ec7:0x933)

/// The point that swims to the click, (0x28, 0x1e) into the sprite: the middle
/// of a frame lying flat, where the walker uses his feet (OBJ:0x335d).
static const int kSwimAnchorX = 0x28;
static const int kSwimAnchorY = 0x1e;

/// In 1/64 pixel per pass: a pixel across when the leg is mostly sideways,
/// three quarters of one down or up when it is not (OBJ:0x33fb, 0x34bc).
static const int kSwimSpeedX = 0x40;
static const int kSwimSpeedY = 0x30;

/// [0xa959] at or above this holds him still, so the last frames of a turn
/// already move (0ec7:0xb08).
static const uint kSwimTurnMoves = 3;

// The treading loops at ds:0x4418 (facing 1) and ds:0x4428 (facing 4), one
// based like every frame list OBJ:sub_09773 is handed.
static const byte kTreadRight[] = { 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 2, 2, 2 };
static const byte kTreadLeft[] = { 19, 19, 19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 21, 20, 20, 20 };

/// Trunc of the original's six-byte reals: toward zero.
static int swimTrunc(double v) {
	return (int)v;
}

/// And Round (0x2567:0x13bf): half away from zero.
static int swimRound(double v) {
	return v < 0 ? -(int)(-v + 0.5) : (int)(v + 0.5);
}


Walker::Walker() : _waypoint(0), _x(0), _y(0), _fx(0), _fy(0), _stepX(0), _stepY(0),
		_steps(0), _scale(CharAnim::kUnitScale), _facing(3),
		_arrivalFacing(kFacingKeep), _phase(0),
		_frame(kIdleFrame[3]), _turnLeft(0), _idleCount(0), _idleCycle(0),
		_idleStream(nullptr), _idleIndex(0), _idleLeft(0), _idleFrame(0),
		_idleAllowed(true), _turnBlocked(false),
		_talking(false), _talkReady(false), _talkPhase(0), _talkHalf(false),
		_placements(0) {
	memset(_turn, 0, sizeof(_turn));
	_swimming = false;
	_pumpkin = false;
	_swimFacing = 3;
	memset(_swimTurn, 0, sizeof(_swimTurn));
}

void Walker::place(int walkX, int walkY, int facing) {
	_x = walkX - kWalkPointX;
	_y = walkY - kWalkPointY;
	_fx = _x * 64;
	_fy = _y * 64;
	_facing = facing;
	// CHARANIM:sub_13bce's arguments: the sprite origin and the facing.
	traceEvent("place %d %d %d", _x, _y, facing);
	_arrivalFacing = kFacingKeep;
	_phase = 0;
	_steps = 0;
	_turnLeft = 0;
	_route.count = 0;
	_waypoint = 0;
	_placements++;
	resetIdle();
	if (_swimming) {
		// CHARANIM:sub_13bce sets [0xa945] with [0xa944], so a placement owes
		// no turn; the frame is the facing's first stroke until the treading
		// loop takes over on the next pass.
		_swimFacing = facing;
		_frame = facing == 4 ? kSwimLeft : facing == 3 ? kSwimUpright : kSwimRight;
		return;
	}
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
	// is not in the route at all, so it goes on the end -- unless the last node
	// is the point itself, which the original walks once. With the mover no
	// longer snapping, a second leg to it from where the first one stopped
	// short took one step more (dosbox state parity, finding #158).
	const bool endsThere = _route.count > 1 &&
		_route.points[_route.count - 1].x == targetX && _route.points[_route.count - 1].y == targetY;
	if (!endsThere && _route.count < WalkRoute::kMaxPoints) {
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

/**
 * OBJ:sub_050c3, the setup of one straight segment, in the original's Real48
 * arithmetic. It splits the plane at the feet, bumps the horizontal span by one
 * on a tie, and then has two branches that run in turn: a sideways walk while
 * the horizontal span is the wider -- 1.5 px a step, Trunc(span / 1.5) steps,
 * the other axis Round(span / steps * 64) sixty-fourths a step -- and, whenever
 * the vertical span is more than half the horizontal one, a walk into or out of
 * the screen over the first: 0.625 px a step the same way round. The branch
 * that runs last sets the facing. Nothing is snapped: the position the mover
 * keeps is in 1/64 px from the last placement on, and the next segment starts
 * from wherever this one ended (dosbox state parity, finding #158).
 */
void Walker::planSegment(int targetX, int targetY, Segment &seg) const {
	// The Real48 constants at [bp-6] and [bp-0xc], and the steps they go with:
	// 1.5 and 0.625 px, 0x60 and 0x28 sixty-fourths -- or, on room 31's ledge
	// ([0xa73a]), 1.0 and 0.33 (Real48 7f 5c8f c2f5 28 -> 0.32999999999992724),
	// 0x40 and 0x15.
	const double kDivX = _ledgePace ? 1.0 : 1.5;
	const double kDivY = _ledgePace ? 0.32999999999992724 : 0.625;
	const int speedX = _ledgePace ? 0x40 : kSpeedX;
	const int speedY = _ledgePace ? 0x15 : kSpeedY;
	const int fx = walkX(), fy = walkY();
	const int signX = targetX < fx ? -1 : 1;
	const int signY = targetY < fy ? -1 : 1;
	int dx = ABS(targetX - fx);
	const int dy = ABS(targetY - fy);
	if (dx == dy)
		dx++;

	// TP's Round: to nearest, halves away from zero.
	struct R { static int round(double v) { return v < 0 ? -(int)(-v + 0.5) : (int)(v + 0.5); } };

	seg.facing = 0;
	seg.steps = 0;
	seg.stepX = seg.stepY = 0;
	if (dx > dy) {
		seg.facing = signX < 0 ? 4 : 2;
		seg.steps = (int)(dx / kDivX);
		seg.stepX = signX * speedX;
		if (seg.steps > 0)
			seg.stepY = R::round((double)dy / seg.steps * 64.0 * signY);
	}
	if (dy > dx / 2.0) {
		seg.facing = signY < 0 ? 1 : 3;
		seg.steps = (int)(dy / kDivY);
		seg.stepY = signY * speedY;
		if (seg.steps > 0)
			seg.stepX = R::round((double)dx / seg.steps * 64.0 * signX);
	}
}

int Walker::facingToward(int targetX, int targetY) const {
	Segment seg;
	planSegment(targetX, targetY, seg);
	return seg.facing;
}

void Walker::turnTo(int facing) {
	// Under water the facing is written straight into [0xa944]: the next pass
	// sees it differ from [0xa945] and plays the turn itself (stepSwim).
	if (_swimming) {
		_facing = facing;
		return;
	}

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
	// the clicked point itself, carries waypoints with nothing to walk; so does
	// one too short for a single step. Those are stepped over rather than
	// costing a tick each. (The original sets each of them up on a tick pair
	// of its own, standing; the port's arrival path is not ready for that.)
	Segment seg;
	while (isWalking()) {
		const WalkRoute::Point &target = _route.points[_waypoint];
		if (target.x != walkX() || target.y != walkY()) {
			planSegment(target.x, target.y, seg);
			// OBJ:sub_050c3's arguments and the position it starts from.
			traceEvent("seg %d %d from %d %d at %d %d", target.x, target.y, walkX(), walkY(),
					   (int)_fx, (int)_fy);
			if (seg.steps > 0)
				break;
		}
		_waypoint++;
	}

	if (!isWalking()) {
		_steps = 0;
		_phase = 0;
		arrive();
		return;
	}

	turnTo(seg.facing);
	_steps = seg.steps;
	_stepX = seg.stepX;
	_stepY = seg.stepY;
}

void Walker::updateFrame() {
	// The swim picks its own frames, pass by pass (stepSwim).
	if (_swimming)
		return;

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
		_frame = _pumpkin ? kPumpkinTalk[_facing][_talkPhase] : kTalkFrames[_facing][_talkPhase];
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

	if (_talkPhase >= (_pumpkin ? kPumpkinTalkCount[_facing] : kTalkCount[_facing]))
		_talkPhase = 0;
}

void Walker::stepIdle(bool inventoryOpen) {
	// OBJ:0x7455: the bar being up is one of the two things that keep him
	// from turning round (setTurnBlocked).
	if (inventoryOpen)
		_turnBlocked = true;

	if (++_idleCount >= kIdleWrap) {
		_idleCount = 0;
		_idleCycle++;
	}

	// A line being spoken drops whatever was playing: the mouth cycle takes the
	// frame over, and the original zeroes [0xa0ba] as it does.
	if (_talking && _talkReady)
		_idleLeft = 0;

	// Nothing new starts past OBJ:sub_098d1's three gates at 0x9a4d --
	// [0xa94d] == 1, [0xa4a1] == 0 and the talk flag [0x2938] == 0. The counter
	// above them runs either way, so a scene that ends leaves him as far into
	// the count as he really has been standing, and so does the stream below:
	// a list already playing is stepped to its end rather than frozen.
	if ((_idleAllowed || _pumpkin) && !_talking) {
		// A canned animation starts only from a standing frame, and only while
		// the inventory bar is down. Nothing stops the count while one plays,
		// so the two that share a starting point never collide: they want
		// different facings.
		if (!inventoryOpen && !_pumpkin) {
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
		// original keeps the moment in [0x98fc] and [0x98fe], which it fills
		// from the facing: a quarter turn from the back or from screen left is
		// a shorter wait than the one from screen right.
		const bool quick = !_pumpkin && (_facing == 1 || _facing == 4);
		const int turnAt = _pumpkin ? kPumpkinTurnAt : quick ? 5 : 0x28;
		const int turnCycle = _pumpkin ? kPumpkinTurnCycle : quick ? 1 : 2;
		if (_facing != 3 && !_turnBlocked && _idleCount == turnAt && _idleCycle == turnCycle) {
			_idleIndex = 0;
			_idleLeft = 0;
			debugC(1, kDebugAnim, "idle: turn to face front after %d cycles",
				   _idleCycle);
			turnTo(3);
			return;
		}
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
	if (_swimming) {
		stepSwim(true);
		return;
	}

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

	// The walk cycle runs off its own counter rather than off the distance
	// covered, so it keeps time across a change of direction. It steps on the
	// animation gate only; the position steps twice as often (stepMove).
	if (_steps > 0)
		_phase = (_phase + 1) % kWalkFrames;

	advance();
	updateFrame();
}

bool Walker::stepMove() {
	if (_swimming) {
		stepSwim(false);
		return true;
	}

	if (_turnLeft || !isWalking())
		return false;

	_talkReady = false;
	advance();
	updateFrame();
	return true;
}

void Walker::advance() {
	// OBJ:0x7780: one step of [0xa926]/[0xa928] while [0xa8f4] is left, then
	// sub_09719 takes the next waypoint -- neither behind [0xa5f9].
	if (_steps > 0) {
		_fx += _stepX;
		_fy += _stepY;
		_x = _fx >> 6;
		_y = _fy >> 6;
		_steps--;
	}

	if (_steps <= 0) {
		// No snap to the waypoint: the original goes on from where the steps
		// ran out (finding #158).
		_waypoint++;
		if (isWalking()) {
			startSegment();
		} else {
			_phase = 0;
			arrive();
		}
	}
}

void Walker::draw(Graphics::Surface &dest, int scrollX, int clipBottom) const {
	if (_anim.isLoaded())
		_anim.drawFrame(_frame, dest, drawX() - scrollX, drawY(),
						clipBottom, _scale);
}

bool Walker::bounds(Common::Rect &box) const {
	if (!_anim.isLoaded() || _frame >= _anim.frameCount())
		return false;

	// The same corner and the same extents drawFrame() works with, which is
	// where the original's [0xa97a]..[0xa980] come from as well: the character's
	// position plus the frame's own hotspot -- an offset into his box rather
	// than a pivot -- both of them through the scale.
	CharAnim::Geometry g;
	if (!_anim.geometry(_frame, drawX(), drawY(), _scale, g))
		return false;

	box = Common::Rect(g.left, g.top, g.left + g.width, g.top + g.height);
	return true;
}

void Walker::setSwimming(bool swimming) {
	if (swimming == _swimming)
		return;

	_swimming = swimming;
	_turnLeft = 0;
	_steps = 0;
	_route.count = 0;
	_waypoint = 0;
	resetIdle();

	const Common::String set = currentSet();
	if (!set.empty() && !_anim.load(set))
		warning("Alien::Walker: cannot load %s", set.c_str());
}

Common::String Walker::currentSet() const {
	if (_swimming)
		return "DIVEANI";
	if (_pumpkin)
		return "PUMPWALK";
	return _set;
}

void Walker::setPumpkin(bool on) {
	if (on == _pumpkin)
		return;

	_pumpkin = on;
	_talkPhase = 0;
	resetIdle();

	const Common::String set = currentSet();
	if (!set.empty() && !_anim.load(set))
		warning("Alien::Walker: cannot load %s", set.c_str());
	debugC(1, kDebugAnim, "walker: %s", set.c_str());
}

void Walker::swimTo(int walkX, int walkY, int arrivalFacing) {
	// OBJ:0x3350: the treading loop and whatever leg was running are dropped.
	_idleLeft = 0;
	_idleIndex = 0;
	_steps = 0;
	_stepX = 0;
	_stepY = 0;
	resetIdle();

	// The leg is plotted from the middle of the swimmer, and in four
	// quadrants: the target left or right of it picks the facing outright,
	// and within each the wider span leads. A tie goes to the horizontal, the
	// way the walker's own splitter breaks one. A leg that is under twice as
	// wide as it is high is then plotted again as a vertical one -- the second
	// test runs whatever the first decided (0x348e, 0x3641, ...).
	const int ax = _x + kSwimAnchorX;
	const int ay = _y + kSwimAnchorY;
	const bool left = walkX < ax;
	const bool up = walkY < ay;

	int adx = ABS(walkX - ax);
	const int ady = ABS(walkY - ay);
	if (adx == ady)
		adx++;

	_facing = left ? 4 : 1;
	const int signX = left ? -1 : 1;
	const int signY = up ? -1 : 1;

	if (adx > ady) {
		_steps = adx;
		_stepX = signX * kSwimSpeedX;
		if (_steps > 0)
			_stepY = swimRound((double)ady / _steps * 64.0 * signY);
	}

	if ((double)ady > (double)adx / 2.0) {
		_steps = swimTrunc(ady / 0.75);
		_stepY = signY * kSwimSpeedY;
		if (_steps > 0)
			_stepX = swimRound((double)adx / _steps * 64.0 * signX);
	}

	// OBJ:sub_07890 queues the room's facing for the end of the leg, in
	// [0xa803]/[0xa804], when it names one.
	_arrivalFacing = arrivalFacing >= 1 && arrivalFacing <= 4 ? arrivalFacing : kFacingKeep;
}

void Walker::startSwimTurn(int from, int to) {
	// 0ec7:sub_0f35b. The lists are played from the top down, so they are
	// filled here back to front: a quarter turn is eleven frames through the
	// upright ones, a reversal is two of those back to back. Anything with a
	// facing of 2 in it has no list and turns at once.
	uint count = 0;
	byte first = 0;
	int step = 0;

	if (from == 4 && to == 1) {
		// 0x39 down to 0x2f, then 0x24 up to 0x2e.
		for (uint i = 1; i <= 11; i++)
			_swimTurn[i] = (byte)(0x2e - (i - 1));
		for (uint i = 12; i <= 22; i++)
			_swimTurn[i] = (byte)(0x2f + (i - 12));
		_turnLeft = 22;
		return;
	}

	if (from == 1 && to == 4) {
		for (uint i = 1; i <= 11; i++)
			_swimTurn[i] = (byte)(0x39 - (i - 1));
		for (uint i = 12; i <= 22; i++)
			_swimTurn[i] = (byte)(0x24 + (i - 12));
		_turnLeft = 22;
		return;
	}

	if (from == 3 && to == 4) {
		first = 0x39; step = -1; count = 11;
	} else if (from == 3 && to == 1) {
		first = 0x2e; step = -1; count = 11;
	} else if (from == 1 && to == 3) {
		first = 0x24; step = 1; count = 11;
	} else if (from == 4 && to == 3) {
		first = 0x2f; step = 1; count = 11;
	}

	for (uint i = 1; i <= count; i++)
		_swimTurn[i] = (byte)(first + step * (int)(i - 1));
	_turnLeft = count;
}

void Walker::stepSwim(bool gate) {
	// 0ec7:sub_0f560, once a tick pair; `gate` is [0xa5f9] == 1, the half of
	// the pairs the animation runs on.
	const bool moving = _steps > 0;

	// The stroke cycle, and its frame, only while nothing is turning him.
	if (moving && !_turnLeft) {
		if (gate)
			_phase = (_phase + 1) % kSwimFrames;
		if (_facing == 4)
			_frame = kSwimLeft + _phase;
		else if (_facing == 1)
			_frame = kSwimRight + _phase;
	}

	// Standing: the idle count, and the treading loop, restarted the moment it
	// runs out unless a turn is still owed. It steps on every pass, not only
	// on the gate.
	bool streamed = false;
	if (!moving && !_turnLeft) {
		if (gate && ++_idleCount > kIdleWrap) {
			_idleCount = 0;
			_idleCycle++;
		}

		if (!_idleLeft && _arrivalFacing == kFacingKeep) {
			if (_facing == 1) {
				_idleStream = kTreadRight;
				_idleLeft = ARRAYSIZE(kTreadRight);
				_idleIndex = 0;
			} else if (_facing == 4) {
				_idleStream = kTreadLeft;
				_idleLeft = ARRAYSIZE(kTreadLeft);
				_idleIndex = 0;
			}
		}

		if (_idleLeft > 0) {
			_idleLeft--;
			streamed = true;
		}
	}

	// The turn the leg owed, once it is over.
	if (_arrivalFacing != kFacingKeep && !moving && !_turnLeft) {
		_facing = _arrivalFacing;
		_arrivalFacing = kFacingKeep;
	}

	if (_facing != _swimFacing)
		startSwimTurn(_swimFacing, _facing);

	if (!_turnLeft) {
		if (!moving && streamed) {
			_frame = (uint)(_idleStream[_idleIndex] - 1);
			if (_idleLeft > 0)
				_idleIndex++;
		}
	} else {
		_frame = _swimTurn[_turnLeft];
		if (gate)
			_turnLeft--;
	}

	_swimFacing = _facing;

	// The step, which the last frames of a turn do not hold up. The position
	// is the fixed point one divided down (GFX:gfx_div32, toward zero), and the
	// leg simply runs out where it ends: nothing snaps him onto the target.
	if (_steps > 0 && _turnLeft < kSwimTurnMoves) {
		_steps--;
		_fx += _stepX;
		_fy += _stepY;
		_x = _fx / 64;
		_y = _fy / 64;
	}
}

} // End of namespace Alien
