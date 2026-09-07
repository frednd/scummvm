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

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// How far away Ben is, and what the rooms do about it.
//
// There is one number: [0xa888], an 8.8 fixed-point divisor that the character
// blit reads on every draw (charanim.cpp has the geometry it puts a frame
// through). 0x100 is the frame at its stored size, larger is smaller, and the
// deepest thing in the game -- room 41's shore, seen from the water -- asks for
// 0x400, a quarter. MAIN boots it at 0x80 (seg_main.asm:0x0db1), double size,
// which only ever shows for the instant before a room writes it; OBJ:sub_08567,
// the reset every room entry runs, puts 0x100 back (0251:0x60af).
//
// Three families of writer feed it, and a room belongs to exactly one of them
// -- which one shows in the shape of the room's own tick:
//
//   * **A banded ladder in LOGIC**, called from the room's tick where the
//     rooms that do not have one call LOGIC:sub_11f79. A reset (sub_12bca:
//     band [0xa885] = 0, scale 0x100) and then a chain of near-identical subs
//     (sub_12bdc..sub_12cfc, seg_logic.asm:1205-1338), each of them "if the
//     character's y [0xa8ee] is above this line, band = n and scale =
//     0x100 + 4n". The thresholds descend, so the last one that matches wins.
//     Four chains cover seven rooms: sub_12d0e for room 6, sub_12d5c for rooms
//     10, 15, 21 and 25, sub_12dc8 for room 17, and sub_12dc2 -- the reset on
//     its own, so 1:1 everywhere -- for room 3.
//
//   * **The interpolator OBJ:sub_0abaf**, which LOGIC:sub_11f79 calls on every
//     animation frame and which a few rooms also call from their own tick. It
//     is a straight line through two points the room's init sets:
//     ([0xa88a], [0xa88e]) and ([0xa88c], [0xa890]), y against scale, clamped
//     to the endpoint scales outside the band. Most rooms set both scales to
//     0x100 and get a flat 1:1 out of it, which is the point -- the four
//     parameters are globals, and a room that did not write them would be
//     interpolating on the last room's line.
//
//   * **A constant the room writes itself**: room 14's chimney shaft holds
//     0x200 for as long as he is in it, and room 31's cliff swaps between
//     0x100 and 0x200 as he goes over the edge, tracking the same [0xa73a]
//     the room's own machine sets.
//
// The report this closes is the pair at either end: "smaller in the hallway
// than in the lab". Room 15's hall runs the thirteen-band chain and its top
// band is 0x130, a fifth off; room 3's lab runs the chain that is only a reset.

/// A rung of a LOGIC ladder: above this row, that scale.
struct ScaleBand {
	int16 above;
	uint16 scale;
};

// sub_12d0e, room 6: nine rungs, 0x104 to 0x124.
static const ScaleBand kBandsLanding[] = {
	{ 0x52, 0x104 }, { 0x4d, 0x108 }, { 0x48, 0x10c }, { 0x43, 0x110 },
	{ 0x3e, 0x114 }, { 0x39, 0x118 }, { 0x34, 0x11c }, { 0x2f, 0x120 },
	{ 0x2a, 0x124 }, { 0, 0 }
};

// sub_12d5c, rooms 10, 15, 21 and 25: twelve rungs on an even four-row pitch.
static const ScaleBand kBandsHall[] = {
	{ 0x52, 0x104 }, { 0x4e, 0x108 }, { 0x4a, 0x10c }, { 0x46, 0x110 },
	{ 0x42, 0x114 }, { 0x3e, 0x118 }, { 0x3a, 0x11c }, { 0x36, 0x120 },
	{ 0x32, 0x124 }, { 0x2e, 0x128 }, { 0x2a, 0x12c }, { 0x26, 0x130 },
	{ 0, 0 }
};

// sub_12dc8, room 17: it starts at band 8 rather than band 1, so the first rung
// is already a step down, and the rungs above it are one and two rows apart.
static const ScaleBand kBandsObservatory[] = {
	{ 0x64, 0x120 }, { 0x3e, 0x124 }, { 0x3d, 0x128 }, { 0x3b, 0x12c },
	{ 0x39, 0x130 }, { 0x37, 0x134 }, { 0x35, 0x138 }, { 0x32, 0x13c },
	{ 0x2f, 0x140 }, { 0x2c, 0x144 }, { 0, 0 }
};

enum ScaleModel {
	kScaleFixed,		///< one constant, whatever the room's init left
	kScaleBands,		///< a LOGIC ladder
	kScaleInterp,		///< OBJ:sub_0abaf between two points
	kScaleLedge			///< room 31: the constant depends on [0xa73a]
};

struct RoomScale {
	int room;
	ScaleModel model;
	const ScaleBand *bands;		///< kScaleBands
	uint16 y0, y1;				///< kScaleInterp: the ends of the band
	uint16 s0, s1;				///< kScaleInterp: and the scale at each
	uint16 fixed;				///< kScaleFixed, and kScaleLedge's near value
};

// Every room that says anything about the scale at all. What is not here has
// nothing to say and draws at 0x100, which is what the entry reset leaves.
//
// The identity interpolators are kept as the constants they work out to: the
// room writes 0x100 at both ends of its line, so the line is flat, and the only
// thing the four parameters are doing is stopping the previous room's line from
// carrying over.
static const RoomScale kRoomScales[] = {
	{  3, kScaleBands,  nullptr,          0, 0, 0, 0, 0x100 },	// sub_12dc2, reset only
	{  6, kScaleBands,  kBandsLanding,    0, 0, 0, 0, 0x100 },
	{  8, kScaleInterp, nullptr,       0x28, 0x52, 0x127, 0x100, 0 },
	{ 10, kScaleBands,  kBandsHall,       0, 0, 0, 0, 0x100 },
	{ 14, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x200 },
	{ 15, kScaleBands,  kBandsHall,       0, 0, 0, 0, 0x100 },
	{ 17, kScaleBands,  kBandsObservatory, 0, 0, 0, 0, 0x100 },
	{ 21, kScaleBands,  kBandsHall,       0, 0, 0, 0, 0x100 },
	{ 22, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	// Room 23 has no model of its own; it is here because it carries an inlined
	// copy of the entry reset, which is a writer of [0xa888] like any other.
	{ 23, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 25, kScaleBands,  kBandsHall,       0, 0, 0, 0, 0x100 },
	{ 31, kScaleLedge,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 32, kScaleInterp, nullptr,       0x11, 0x34, 0x190, 0x100, 0 },
	{ 33, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 34, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 35, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 40, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 41, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x400 },
	{ 43, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 44, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 45, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 46, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 48, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 49, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 50, kScaleInterp, nullptr,       0x26, 0x4e, 0x154, 0x100, 0 },
	{ 51, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 52, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 53, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 54, kScaleInterp, nullptr,       0x27, 0x3c, 0x136, 0x100, 0 },
	{ 55, kScaleInterp, nullptr,       0x27, 0x3c, 0x136, 0x100, 0 },
	{ 56, kScaleInterp, nullptr,       0x27, 0x3c, 0x136, 0x100, 0 },
	{ 57, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 58, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 },
	{ 59, kScaleFixed,  nullptr,          0, 0, 0, 0, 0x100 }
};

/// [0xa73a], room 31's "he is over the edge" flag: the room's init reads it
/// back on the way in and the room's own machine sets it as he climbs down.
static const uint16 kLedgeFlag = 0xa73a;
static const uint16 kLedgeScale = 0x200;

static const RoomScale *roomScale(int room) {
	for (uint i = 0; i < ARRAYSIZE(kRoomScales); i++) {
		if (kRoomScales[i].room == room)
			return &kRoomScales[i];
	}
	return nullptr;
}

/**
 * OBJ:sub_0abaf, the straight line between the room's two points.
 *
 * The original works the slope out in 1/64ths -- `((s1 - s0) << 6) / (y1 - y0)`
 * through the 32-bit divide, then `s0 + slope * (y - y0) / 0x40` -- so the
 * rounding is the rounding of that, not of a wider fixed point. Outside the
 * band the answer is replaced outright by the endpoint's scale rather than
 * being let run on, which is the clamp at 0251:0x86fc.
 */
static uint16 interpolate(const RoomScale &rs, int y) {
	const int span = (int)rs.y1 - (int)rs.y0;
	int scale = (int)rs.s0;

	if (span != 0) {
		const int slope = (((int)rs.s1 - (int)rs.s0) << 6) / span;
		scale = (int)rs.s0 + (slope * (y - (int)rs.y0)) / 0x40;
	}

	if (y <= (int)rs.y0)
		scale = (int)rs.s0;
	if (y >= (int)rs.y1)
		scale = (int)rs.s1;

	return (uint16)scale;
}

uint16 AlienEngine::charScale(int room, int y, bool ledge) const {
	const RoomScale *rs = roomScale(room);
	if (!rs)
		return CharAnim::kUnitScale;

	switch (rs->model) {
	case kScaleBands: {
		// The reset first, and then the ladder in order: every rung the
		// character is above overwrites the one below it.
		uint16 scale = CharAnim::kUnitScale;
		for (const ScaleBand *b = rs->bands; b && b->above; b++) {
			if (y < (int)b->above)
				scale = b->scale;
		}
		return scale;
	}

	case kScaleInterp:
		return interpolate(*rs, y);

	case kScaleLedge:
		return ledge ? kLedgeScale : CharAnim::kUnitScale;

	default:
		return rs->fixed;
	}
}

/**
 * The scale for where the character is standing now, once a tick.
 *
 * The original does this from the room's tick -- the ladder rooms call their
 * chain there outright, the rest reach OBJ:sub_0abaf through LOGIC:sub_11f79 --
 * and always before the frame is drawn.
 */
void AlienEngine::stepCharScale() {
	const bool ledge = _script.flag(kLedgeFlag) == 1;
	const uint16 scale = charScale(_room, _ben.spriteY(), ledge);
	if (scale == _ben.scale())
		return;

	_ben.setScale(scale);
	_dirty = true;

	Common::Rect box;
	if (_ben.bounds(box))
		debugC(2, kDebugScale, "scale: room %d at y %d -> %d, box %dx%d", _room,
			   _ben.spriteY(), scale, box.width(), box.height());
}

void AlienEngine::dumpScale() {
	debug("scale: %d rooms", (int)ARRAYSIZE(kRoomScales));

	for (uint i = 0; i < ARRAYSIZE(kRoomScales); i++) {
		const RoomScale &rs = kRoomScales[i];
		switch (rs.model) {
		case kScaleBands: {
			uint count = 0;
			for (const ScaleBand *b = rs.bands; b && b->above; b++)
				count++;
			debug("model room %2d bands %u", rs.room, count);
			for (const ScaleBand *b = rs.bands; b && b->above; b++)
				debug("band room %2d above %3d scale %3d", rs.room, b->above, b->scale);
			break;
		}

		case kScaleInterp:
			debug("model room %2d interp y %3d %3d scale %3d %3d", rs.room, rs.y0, rs.y1,
				  rs.s0, rs.s1);
			break;

		case kScaleLedge:
			debug("model room %2d ledge scale %3d %3d", rs.room, CharAnim::kUnitScale,
				  kLedgeScale);
			break;

		default:
			debug("model room %2d fixed scale %3d", rs.room, rs.fixed);
			break;
		}
	}
}

/**
 * Every model over the rows the character can stand on, so the mirror checks
 * the arithmetic and not only the table it reads.
 *
 * Room 31 is swept off the ledge as well as on it; nothing else has a second
 * state to sweep.
 */
void AlienEngine::sweepScale() {
	for (uint i = 0; i < ARRAYSIZE(kRoomScales); i++) {
		const RoomScale &rs = kRoomScales[i];
		const int states = rs.model == kScaleLedge ? 2 : 1;

		for (int state = 0; state < states; state++) {
			for (int y = 0; y <= 0x9b; y += 4)
				debug("sample room %2d state %d at y %3d scale %3d", rs.room, state, y,
					  charScale(rs.room, y, state != 0));
		}
	}
}

} // End of namespace Alien
