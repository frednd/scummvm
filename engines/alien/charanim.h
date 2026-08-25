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

#ifndef ALIEN_CHARANIM_H
#define ALIEN_CHARANIM_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "alien/walk.h"

namespace Graphics {
struct Surface;
}

namespace Alien {

/**
 * A character animation set: <NAME>.DAT plus its <NAME>.000.. pixel strips.
 *
 * Six sets ship with the game, BENANI (the player character) being the one the
 * walk system drives. The .DAT is a flat array of nine byte records with no
 * header, and each record points at a run of uncompressed 8bpp pixels inside
 * one of the strip files:
 *
 *   u8  strip      which pixel file: 0 = .000, 1 = .001, ...
 *   u16 offset     byte offset within that strip
 *   u16 width
 *   u16 height
 *   u8  hotspotX   drawn at the character position plus this
 *   u8  hotspotY
 *
 * Colour 0 is transparent, rows are not padded, and the frames tile their
 * strip exactly. The format is documented in docs/file_formats.md 12 of the
 * reverse-engineering repository, tools/charani.py being the reference reader.
 */
class CharAnim {
public:
	enum {
		kRecordSize = 9,
		kMaxStrips = 8			///< JAIL_GUA, the only set that needs them all
	};

	struct Frame {
		byte strip;
		uint16 offset;
		uint16 width;
		uint16 height;
		byte hotspotX;
		byte hotspotY;
	};

	CharAnim();
	~CharAnim();

	/** Read <base>.DAT and every strip it references. */
	bool load(const Common::String &base);
	void unload();

	bool isLoaded() const { return !_frames.empty(); }
	uint frameCount() const { return _frames.size(); }
	const Frame &frame(uint index) const { return _frames[index]; }

	/**
	 * Draw a frame with the character standing at (x, y). The original adds
	 * the record's hotspot to that position rather than subtracting it, so
	 * the hotspot is where the frame sits inside the character's box, not a
	 * pivot to centre on.
	 */
	void drawFrame(uint index, Graphics::Surface &dest, int x, int y) const;

private:
	Common::Array<Frame> _frames;
	byte *_strip[kMaxStrips];
	uint32 _stripSize[kMaxStrips];
};

/**
 * The player character walking a plotted route.
 *
 * Facing is 1..4 as the original's [0xa944]: 1 back, 2 screen right, 3 front,
 * 4 screen left. That picks the animation directly -- the sixteen walk frames
 * of a facing are (facing - 1) * 16 + phase, the standing frame is one of
 * 0x40/0x44/0x48/0x4c, and a change of facing plays a queued turn first.
 *
 * The character position is the sprite's own origin; the point the walk system
 * routes is that position plus (10, 64), which is where the feet are.
 */
class Walker {
public:
	enum {
		kWalkPointX = 10,
		kWalkPointY = 64,
		kWalkFrames = 16,		///< per facing, phases 0..15
		kMaxTurnFrames = 7,
		kFacingKeep = 10		///< arrive without turning, the original's default
	};

	Walker();

	bool load(const Common::String &base) { return _anim.load(base); }
	void unload() { _anim.unload(); }
	bool isLoaded() const { return _anim.isLoaded(); }

	/** Put the character down with its feet at a walk point, facing forward. */
	void place(int walkX, int walkY, int facing = 3);

	/**
	 * Take a route from Walk::plotRoute and start walking it. The router only
	 * ever ends its route on a walk node -- the last leg, from that node to the
	 * point that was actually clicked, is the mover's own -- so the target is
	 * passed in and appended as the final waypoint.
	 *
	 * `arrivalFacing` is the way to turn once the route runs out, 1..4, or
	 * kFacingKeep to arrive facing however the last leg left him. The room's
	 * walk geometry supplies it per object (see walkgeom.h), and the original
	 * hands it to the mover the same way: walk_plot_route copies [0xa805] into
	 * the pending turn when it is under five.
	 */
	void follow(const WalkRoute &route, int targetX, int targetY,
				int arrivalFacing = kFacingKeep);

	/** Drop the route and fall back to standing. */
	void stop();

	bool isWalking() const { return _waypoint < _route.count; }
	bool isTurning() const { return _turnLeft != 0; }

	/** One animation tick: the original runs these at the vsync rate over 4. */
	void tick();

	void draw(Graphics::Surface &dest, int scrollX = 0) const;

	int walkX() const { return _x + kWalkPointX; }
	int walkY() const { return _y + kWalkPointY; }
	int facing() const { return _facing; }
	uint frame() const { return _frame; }

private:
	void startSegment();
	int facingToward(int targetX, int targetY) const;
	void turnTo(int facing);
	void arrive();
	void updateFrame();

	CharAnim _anim;

	WalkRoute _route;
	uint _waypoint;					///< the point being walked to, 0 when idle

	int _x;							///< character origin, whole pixels
	int _y;
	int _fx;						///< and the same in 1/64 pixel, as the original
	int _fy;
	int _stepX;						///< 1/64 pixel per tick
	int _stepY;
	int _steps;						///< ticks left in this segment

	int _facing;
	int _arrivalFacing;				///< the turn owed at the end of the route
	uint _phase;					///< walk cycle position, 0..15
	uint _frame;

	byte _turn[kMaxTurnFrames];
	uint _turnLeft;
};

} // End of namespace Alien

#endif
