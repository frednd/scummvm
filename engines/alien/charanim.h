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
#include "common/rect.h"
#include "common/str.h"

#include "alien/dl1.h"
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
		kMaxStrips = 8,			///< JAIL_GUA, the only set that needs them all

		/// [0xa888] at one to one: the divisor is 8.8, so 0x200 is half size
		/// and 0x80 -- the value MAIN boots with -- is double.
		kUnitScale = 0x100,

		/// The walk point's offset down the frame, the 0x40 the blit takes the
		/// shrink out of so that the feet do not move (0251:0cdf).
		kFootOffset = 0x40,

		/// The first row of the playfield, which is where the blit's own top
		/// clip is measured from (0251:0d99).
		kFieldTop = 13
	};

	/// Where a frame lands, and how big it lands, once the scale has been
	/// applied: the original's ([bp-0x20], [bp-0x22]) corner and the row and
	/// column counts it works out at 0251:0d59 and 0251:0d79.
	struct Geometry {
		int left;
		int top;
		int width;
		int height;
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
	/// `clipBottom` is the first row that must not be drawn, the original's
	/// [0xa8e4]: the character goes through the same OBJ:dl1_load_and_blit the
	/// room's slots do, so the same global shortens him (see DL1Sprite).
	///
	/// `scale` is [0xa888], the depth divisor the same blit reads (scale.cpp).
	void drawFrame(uint index, Graphics::Surface &dest, int x, int y,
				   int clipBottom = DL1Sprite::kNoClipBottom,
				   uint16 scale = kUnitScale) const;

	/// Where the frame would land at that scale, before any clipping.
	bool geometry(uint index, int x, int y, uint16 scale, Geometry &out) const;

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
		/// The character is not blitted at his own position: OBJ:sub_06466
		/// passes [0xa8ec] - 10 and [0xa8ee] + 5 to the blitter, so the frame
		/// sits ten pixels left of the origin and five below it. Two rooms
		/// shift it further (climbing, [0xa73a]; the teleport costume,
		/// [0xa79b]), which is not modelled.
		kDrawOffsetX = -10,
		kDrawOffsetY = 5,
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

	/// The same, given the sprite origin instead of the walk point -- which is
	/// what a room's own CHARANIM:0x4e call passes (kOpCharPlace).
	void placeSprite(int spriteX, int spriteY, int facing = 3) {
		place(spriteX + kWalkPointX, spriteY + kWalkPointY, facing);
	}

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

	/// Queue the turn to a facing, the original's [0xa803] plus [0xa804]. Room
	/// code writes that pair directly to turn him where its own machine wants
	/// him (sewer.cpp); 1 = back, 2 = right, 3 = front, 4 = left.
	void faceTo(int facing) { turnTo(facing); }

	bool isWalking() const { return _waypoint < _route.count; }
	bool isTurning() const { return _turnLeft != 0; }

	/**
	 * Restart the idle machine, which every click reaching LOGIC's walk
	 * dispatch does: it zeroes [0xa808] and [0xa80a] whether or not the click
	 * ended up plotting a route.
	 */
	void resetIdle();

	/// Whether an idle animation is playing right now -- the original's
	/// [0xa0ba], the frames left in the stream, being non-zero.
	bool isIdlePlaying() const { return _idleLeft > 0; }

	/**
	 * Whether the character is his own again, the original's [0xa94d] and
	 * [0xa4a1] together: a room that drives him from an [0xa49f] machine
	 * clears the first as it hides the cursor and sets it back at the step
	 * that ends the machine, and the dialog unit raises the second for as long
	 * as a scene it requested is running. While either says no, the counter
	 * still runs but nothing fidgets and he does not turn to face the player.
	 */
	void setIdleAllowed(bool allowed) { _idleAllowed = allowed; }

	/**
	 * Whether a line is being spoken, the original's [0x2938].
	 *
	 * BENANI's top twelve frames are a mouth cycle, four lists of them, one per
	 * facing. The dialog unit raises this flag as it dispatches a line and the
	 * tick drops it 25 half ticks before the line clears; while it is up and the
	 * character has been standing still for a moment, the cycle replaces his
	 * standing frame.
	 */
	void setTalking(bool talking);
	bool isTalking() const { return _talking; }

	/**
	 * One animation tick: the original runs these at the vsync rate over 4.
	 *
	 * `inventoryOpen` is the original's [0xa605], which suppresses the idle
	 * machine: nothing fidgets while the bar is up.
	 */
	void tick(bool inventoryOpen = false);

	void draw(Graphics::Surface &dest, int scrollX = 0,
			  int clipBottom = DL1Sprite::kNoClipBottom) const;

	/**
	 * The box the current frame occupies in room space, which is what the
	 * original leaves in [0xa97a]..[0xa980] as it blits the character and what
	 * the foreground rectangles are tested against. False when nothing is
	 * loaded, and so nothing was drawn.
	 *
	 * It is the *scaled* box: the original writes it from the corner and the
	 * row and column counts the blit has just drawn with, so a Ben who is half
	 * size is half a Ben to the foreground rectangles too.
	 */
	bool bounds(Common::Rect &box) const;

	/**
	 * The depth scale to draw at, [0xa888].
	 *
	 * A global in the original and kept as one here: the room decides it and
	 * every draw of the character reads it (scale.cpp).
	 */
	void setScale(uint16 scale) { _scale = scale ? scale : CharAnim::kUnitScale; }
	uint16 scale() const { return _scale; }

	/// The sprite's own origin, the original's [0xa8ec] and [0xa8ee].
	int spriteX() const { return _x; }
	int spriteY() const { return _y; }

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
	void stepIdle(bool inventoryOpen);
	void stepTalk();

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

	uint16 _scale;					///< [0xa888], the depth divisor

	int _facing;
	int _arrivalFacing;				///< the turn owed at the end of the route
	uint _phase;					///< walk cycle position, 0..15
	uint _frame;

	byte _turn[kMaxTurnFrames];
	uint _turnLeft;

	/// The idle machine, OBJ:sub_098d1. It counts the ticks the character has
	/// spent standing -- [0xa808] up to 200, then [0xa80a] one cycle on -- and
	/// at fixed points in that count starts a canned animation or turns him
	/// round to face the player.
	int _idleCount;
	int _idleCycle;
	const byte *_idleStream;	///< the frame list being played, [0xa0bc]:[0xa0be]
	int _idleIndex;				///< how far into it, [0xa0b8]
	int _idleLeft;				///< frames still to play, [0xa0ba]
	uint _idleFrame;			///< the one this tick chose

	/// The talk cycle: whether a line is up [0x2938], whether he has stood still
	/// long enough for the mouth to open [0x293a], how far into the facing's
	/// frame list it is [0x2939], and the toggle that halves its rate [0x2937].
	bool _idleAllowed;			///< [0xa94d] == 1 and [0xa4a1] == 0

	bool _talking;
	bool _talkReady;
	uint _talkPhase;
	bool _talkHalf;
};

} // End of namespace Alien

#endif
