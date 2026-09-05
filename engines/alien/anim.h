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
class RoomScript;

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
 * | `0xa53a` | 1 = the room's tick keeps this slot looping         |
 * | `0xa55a` | 1 = return to the starting frame when it runs out   |
 * | `0xa5ba` | the frame it started on                             |
 * | `0xa5ca` | the frame count it started with                     |
 * | `0xa5da` | 1 = leave the finished animation behind             |
 *
 * There are **eight** play routines, not three: MIDAS holds the same routine
 * copied out eight times with different constants, and the number each writes
 * into `0xa4aa` is what names them here. All eight take the same first four
 * arguments, and the third of them is a frame *count*, not a last frame: the
 * door in room 6 opens with `(slot 2, first 1, count 6, rate 2)` over a
 * six-frame bank. Frames themselves are numbered from one.
 *
 * | mode | address | direction | on the last frame                    |
 * |------|---------|-----------|--------------------------------------|
 * | 1    | `0xb85` | forward   | left behind                          |
 * | 2    | `0xc2e` | forward   | back to the frame it started on      |
 * | 3    | `0xcd7` | backward  | left behind                          |
 * | 4    | `0xd80` | forward   | taken away                           |
 * | 5    | `0xe29` | backward  | taken away                           |
 * | 6    | `0xed2` | forward   | left behind                          |
 * | 7    | `0xf9b` | forward   | taken away                           |
 * | 8    | `0x1064`| forward   | back to the frame it started on      |
 *
 * Modes 6, 7 and 8 push two words more, a far pointer to a table of one sample
 * id per frame that MIDAS:sub_19431 reads as the slot advances. The port drops
 * the pointer: it has no per-frame sound.
 *
 * Advancing runs under the tick-pair gate (docs/timing.md). "Left behind" is
 * the original's `0xa5da`: on the tick a mode 1, 3 or 6 range ends, the final
 * frame goes into the *background* page rather than the front buffer, which is
 * why an opened door stays open with no slot still running. The port keeps the
 * frame in the slot and redraws it, which composites the same pixels without a
 * second page. The modes without that flag are simply not drawn once they have
 * run out (MIDAS:snd_func_1482 skips them), unless they are also the kind that
 * returns to their first frame -- which is how a glow that is only sometimes
 * lit goes out again.
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

	/**
	 * Drops every slot and loads the banks the room's overlay names.
	 *
	 * The state is wanted because one bank name is not a constant: room 7's
	 * closet is assembled out of two literals and a character the room picks
	 * from a puzzle flag (see AnimBank). The original picks it inline, on the
	 * instruction before the load, so this reads the same flag at the same
	 * point -- before the room's opening script runs, not after.
	 */
	void loadRoom(int room, const RoomScript &state);

	/**
	 * Drops every slot and loads banks named one per slot, as a cutscene record
	 * names them (see cutscenes.h). Unlike a room's manifest the slot is the
	 * position in the list, and a null entry leaves that slot empty.
	 */
	void loadBanks(const char *const *names, uint count);

	/// Clears the slots without touching the loaded banks.
	void reset();

	/**
	 * Starts a frame range on a slot, as MIDAS:0xb85 / 0xc2e / 0xcd7 do.
	 * @param first  the frame to start on, numbered from one
	 * @param count  how many frames to advance through
	 * @param rate   ticks per frame; zero advances on every tick
	 * @param mode   1..8, as the table above has them
	 */
	void play(uint slot, int first, int count, int rate, int mode);

	/** One animation tick: advances every slot with frames left. */
	void tick();

	/**
	 * MIDAS:snd_func_112d: re-issues a slot's own last play once it has one
	 * frame left, which is what makes an animation repeat.
	 *
	 * Nothing about it is automatic -- see AnimLoop in anims.h. It restarts one
	 * tick early on purpose, so the cycle runs on without the range's last
	 * frame ever reaching the screen.
	 */
	void relaunch(uint slot);

	/**
	 * One frame's worth of loop restarts, for the room this holds the banks of.
	 *
	 * The original spells this out in each room's tick, one call per looping
	 * slot; the calls are lifted into a table so the loop is data here rather
	 * than a switch on the room number.
	 */
	void stepLoops();

	/**
	 * MIDAS:sub_18ee5: one pass over all sixteen slots, relaunching every slot
	 * whose [0xa53a] byte is set.
	 *
	 * This is the *generic* loop stepper, and it is what a cutscene runs -- its
	 * player calls it every pass of the scene loop, and the scene's procedures
	 * turn the flag on beside the play they want repeated. A room does not use
	 * it: each room's tick spells its relaunches out one call at a time, which
	 * is what stepLoops() carries.
	 */
	void stepLoopFlags();

	/// The original's [0xa53a] byte for one slot: whether the room has its
	/// guarded loop turned on. Room code writes it as a plain flag.
	void setLoopFlag(uint slot, byte value);
	byte loopFlag(uint slot) const;

	/**
	 * Plays every loaded slot forward through its whole bank.
	 *
	 * A debug facility: it is what a room would look like if everything in it
	 * moved at once, and it exercises the slots without the click pipeline.
	 */
	void playAll(int rate);

	/** True while any slot still has frames to advance. */
	bool isBusy() const;

	/** True while this one slot still has frames to advance. */
	bool isBusy(uint slot) const { return _slots[slot].remaining > 0; }

	/// True while a slot the room does *not* keep relaunching still has frames
	/// to advance. A looping slot never runs out -- the room's tick re-issues it
	/// on its last frame -- so anything waiting for the room to go quiet has to
	/// leave those out or it waits for ever.
	bool isBusyOnce() const;

	/// Whether stepLoops() would relaunch this slot as things stand.
	bool isLooping(uint slot) const;

	/** Composites the current frame of every slot that has one, scroll-adjusted like Walker::draw. */
	void draw(Graphics::Surface &dest, int scrollX = 0,
			  int clipBottom = DL1Sprite::kNoClipBottom) const;

	/** Stamps the last frame of every finished persisting slot into the room
	 *  plate, which is what the original does with its second page: the slot
	 *  drawer writes that frame to the background buffer as well as to the
	 *  screen and then never draws the slot again (MIDAS:snd_func_1482 at
	 *  0x1566, guarded on `[0xa5da]` -- set by modes 1, 3 and 6 alone).
	 *  Without it a finished slot keeps drawing in slot order, so an older
	 *  play in a higher slot covers a newer one below it. */
	void bake(Graphics::Surface &background,
			  int clipBottom = DL1Sprite::kNoClipBottom);

	/** The bank a slot holds, for the debug console. */
	const Common::String &bankName(uint slot) const { return _slots[slot].name; }
	int frame(uint slot) const { return _slots[slot].frame; }

	/// Frames a slot still has to advance: the original's `0xa4ea` array, which
	/// room scripts guard on directly (see RoomScript::animSlotByte).
	int remaining(uint slot) const { return _slots[slot].remaining; }

	/// Cut a range short by zeroing that same array entry, which is how a room
	/// takes an animation off the screen without playing it out -- the sewer
	/// stops its rippling water the moment the drain starts.
	void stop(uint slot) {
		if (slot < kSlotCount)
			_slots[slot].remaining = 0;
	}

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
		bool restore;			///< modes 2 and 8: go back to the starting frame
		bool persist;			///< [0xa5da]: modes 1, 3 and 6 leave the last frame behind
		bool baked;				///< that last frame is in the room plate now
		byte loop;				///< [0xa53a]: the room's guard on this slot's loop
		byte mode;

		Slot() { clear(); }
		void clear();
	};

	/// The frame a slot should show, clamped to the range it was given.
	int visibleFrame(const Slot &slot) const;

	Slot _slots[kSlotCount];
	int _room;

	/// The room's own loop calls, from animLoopsForRoom.
	const AnimLoop *_loops;
	uint _loopCount;
};

} // End of namespace Alien

#endif
