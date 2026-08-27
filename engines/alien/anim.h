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

#ifndef ALIEN_ANIM_H
#define ALIEN_ANIM_H

#include "common/str.h"

#include "alien/anims.h"
#include "alien/dl1.h"

namespace Graphics {
struct Surface;
}

namespace Alien {

struct RoomAssets;

/**
 * The animation slots: what makes a room react on screen.
 *
 * A slot owns one DL1 bank, loaded when the room opens (see anims.h), and the
 * room's script plays a frame range of it. The original keeps sixteen slots as
 * parallel arrays in its data segment, all indexed by slot number:
 *
 * | Array    | Field                                              |
 * |----------|----------------------------------------------------|
 * | `0xa4aa` | which of the play routines wrote the slot, 1-8      |
 * | `0xa4ba` | the slot number again, as the blitter's argument    |
 * | `0xa4ca` | current frame, a word                               |
 * | `0xa4ea` | frames left to advance                              |
 * | `0xa4fa` | ticks per frame                                     |
 * | `0xa50a` | ticks since the last advance                        |
 * | `0xa51a` | 1 = forward, 0 = backward                           |
 * | `0xa55a` | 1 = return to the starting frame when it runs out   |
 * | `0xa5ba` | the frame it started on                             |
 * | `0xa5ca` | the frame count it started with                     |
 *
 * The three play routines the room scripts reach are MIDAS:0xb85 (mode 1,
 * forward), 0xc2e (mode 2, forward and then back to the frame it started on)
 * and 0xcd7 (mode 3, backward). All three take the same arguments, and the
 * third of them is a frame *count*, not a last frame: the door in room 6 opens
 * with `(slot 2, first 1, count 6, rate 2)` over a six-frame bank. Frames
 * themselves are numbered from one.
 *
 * Advancing runs under the tick-pair gate (docs/timing.md), and a slot whose
 * count has run out keeps its last frame on screen -- the original gets that by
 * blitting the final frame into the background page rather than the front
 * buffer, which is why an opened door stays open with no slot still running.
 * The port keeps the frame in the slot and redraws it, which composites the same
 * pixels without a second page.
 *
 * A range may end one frame past the last frame its bank ships. That frame is the
 * terminator entry the DL1 header counts but stores no strips for, and playing
 * into it is how the game takes something away: nothing is drawn, and the
 * background the original restores under the previous frame stays. Room 3 takes
 * the rope off the wall with a range that is nothing but the terminator.
 */
class AnimSlots {
public:
	/// The original loops slots 0 through 15 in both its stepper and its init.
	static const uint kSlotCount = 16;

	AnimSlots();

	/// Drops every slot and loads the banks the room's overlay names.
	void loadRoom(int room);

	/// Clears the slots without touching the loaded banks.
	void reset();

	/**
	 * Starts a frame range on a slot, as MIDAS:0xb85 / 0xc2e / 0xcd7 do.
	 * @param first  the frame to start on, numbered from one
	 * @param count  how many frames to advance through
	 * @param rate   ticks per frame; zero advances on every tick
	 * @param mode   1 forward, 2 forward and back, 3 backward
	 */
	void play(uint slot, int first, int count, int rate, int mode);

	/** One animation tick: advances every slot with frames left. */
	void tick();

	/**
	 * Plays every loaded slot forward through its whole bank.
	 *
	 * A debug facility: it is what a room would look like if everything in it
	 * moved at once, and it exercises the slots without the click pipeline.
	 */
	void playAll(int rate);

	/** True while any slot still has frames to advance. */
	bool isBusy() const;

	/** Composites the current frame of every slot that has one, scroll-adjusted like Walker::draw. */
	void draw(Graphics::Surface &dest, int scrollX = 0) const;

	/** The bank a slot holds, for the debug console. */
	const Common::String &bankName(uint slot) const { return _slots[slot].name; }
	int frame(uint slot) const { return _slots[slot].frame; }

private:
	struct Slot {
		DL1Sprite bank;
		Common::String name;

		bool started;			///< a play routine has written this slot
		int frame;				///< current frame, numbered from one
		int first;				///< the frame play() started on
		int count;				///< the frame count play() started with
		int remaining;
		int rate;
		int tick;
		bool forward;
		bool restore;			///< mode 2: go back to the starting frame
		byte mode;

		Slot() { clear(); }
		void clear();
	};

	/// The frame a slot should show, clamped to the range it was given.
	int visibleFrame(const Slot &slot) const;

	Slot _slots[kSlotCount];
	int _room;
};

} // End of namespace Alien

#endif
