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
 * Modes 6, 7 and 8 push two words more, a far pointer to a table of one byte per
 * tick of the play. That table is **not** a sample list, which is what it was
 * taken for: MIDAS:snd_func_1482 reads `table[cursor]` for such a slot and hands
 * the byte to the blitter as the frame (0x15dc, then the draw at 0x1674), so
 * what those three routines step is a cursor into the list and the list holds
 * the frames. It is why every mode 8 play in the game starts at zero -- zero is
 * an index, not a frame -- and reading the cursor as the frame put a blank
 * frame, the entry below the bank's first, into every talk cycle a scene has.
 * The same byte is the sample id where a slot has per-frame sound, which the
 * port still has none of.
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

	/**
	 * Loads one bank into one slot, leaving every other slot exactly as it
	 * stands -- unlike loadRoom() and loadBanks(), which both drop everything
	 * first.
	 *
	 * 15f3:sub_16450 loads O_SCANV1.DL1 or O_SCANV2.DL1 into slot 4 mid-run,
	 * with the observatory computer's menu, cursor and result banks all still
	 * playing in their own slots (telescope.cpp); loadBanks() would silently
	 * wipe the screen it is meant to add to.
	 */
	void loadBank(uint slot, const char *name);

	/// Clears the slots without touching the loaded banks.
	void reset();

	/**
	 * Starts a frame range on a slot, as MIDAS:0xb85 / 0xc2e / 0xcd7 do.
	 * @param first  the frame to start on, numbered from one -- or, with a frame
	 *               list, the cursor into that list, which is numbered from zero
	 * @param count  how many frames to advance through
	 * @param rate   ticks per frame; zero advances on every tick
	 * @param mode   1..8, as the table above has them
	 * @param frames the frame list modes 6, 7 and 8 read, `count` bytes long, or
	 *               null. Not copied: the pool it points into outlives the slot.
	 */
	void play(uint slot, int first, int count, int rate, int mode,
			  const byte *frames = nullptr);

	/**
	 * Takes a slot off the screen: no frames left, nothing left behind.
	 *
	 * The original has no such call -- a play there always runs to its own end
	 * -- but it does have banks that draw the character himself, and those are
	 * bracketed by [0xa94d], the "draw the character" byte. What ends such an
	 * animation in the original is running into its terminator on the same tick
	 * the byte comes back; a bank that ships fewer frames than the room plays,
	 * or a machine that hands the character back early, would otherwise leave
	 * the drawn-on version of him standing under the real one. This is the port
	 * clearing the slot at that point instead of relying on the frame count
	 * landing exactly right (AlienEngine::showCharacter).
	 *
	 * Unlike stop(), which is the original's own way of cutting a range short
	 * by zeroing its frame counter, this also clears the flags that would keep
	 * the last frame on screen or stamp it into the plate.
	 */
	void takeDown(uint slot);

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
	 * Marks a slot as a character that must never go blank -- a port addition,
	 * not anything the original does (MIDAS:snd_func_1482 really does skip a
	 * finished mode 4/5/7/8 slot, see anim.h's table). Once held, whenever
	 * draw() would otherwise show nothing for this slot -- no frames left in a
	 * mode that does not persist or restore, or the visible frame is the blank
	 * below the first / the terminator above the last -- it draws the last real
	 * frame the slot showed instead. A gap of exactly one tick pair between a
	 * finished play and the next one issued by a room's step hook (the ordinary
	 * case: see the tick-order note in anim.cpp) never reaches the screen.
	 *
	 * Not a substitute for relaunching a loop correctly -- a slot that never
	 * plays again just keeps showing its last frame forever, silently, which is
	 * why plays on a held slot should still be logged (kDebugGraphics level 2)
	 * if the room's own loop drops out for good. Opt-in per slot: applying it
	 * everywhere would keep dead animations (an opened door, a finished cutscene
	 * flourish) glued to the screen, which is exactly what finding #35 warned
	 * against.
	 */
	void setHold(uint slot, bool value);
	bool holdFlag(uint slot) const;

	/**
	 * Names a slot as one the room's tick keeps looping, for a room whose tick
	 * is hand-ported rather than lifted into the loop table -- room 60, whose
	 * frame loop is resident code (boss.cpp). It only changes isLooping(): the
	 * caller still does its own relaunching. Cleared with the room's banks.
	 */
	void markLooping(uint slot);

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

	/// Whether the room loops this slot at all -- membership in the loop
	/// table, not whether its guard is set. isBusyOnce() is the caller, and a
	/// loop slot is never a one-shot in flight either way.
	bool isLooping(uint slot) const;

	/// Whether one lifted loop row's relaunch is switched on as things stand.
	/// stepLoops() only; isLooping() must not use it (finding #113).
	bool loopArmed(const AnimLoop &row) const;

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
	void bake(Graphics::Surface &background, int scrollX,
			  int clipBottom = DL1Sprite::kNoClipBottom);

	/** Whether bake() has a slot to stamp this tick. */
	bool bakePending() const;

	/**
	 * Stamps one frame of a slot's bank into the background page.
	 *
	 * 10c9:sub_1132a, which several rooms' enter routines call a few times to
	 * bring the plate up to date with the puzzle state: it writes the frame
	 * straight to the background buffer (MIDAS:sub_18006) and leaves the slot
	 * itself alone, so nothing is animating afterwards. Returns false when the
	 * slot holds no bank or the frame is outside it.
	 *
	 * `pageX` stamps a bank authored against the room's *second* page instead:
	 * it is where that page's column zero sits in the plate, and the frame is
	 * placed through DL1Sprite::drawFramePage, which unwinds the flat page the
	 * bank was written for (library.cpp). kNoPage is the ordinary draw.
	 */
	static const int kNoPage = -1;
	bool stamp(uint slot, int frame, Graphics::Surface &background,
			   int clipBottom = DL1Sprite::kNoClipBottom, int pageX = kNoPage);

	/**
	 * Draws one frame of a slot's bank onto a surface, leaving the slot alone.
	 *
	 * MIDAS:sub_180d4 called by a room itself rather than by the slot drawer:
	 * room 21 steps its puddle through bank 0 by hand, one frame per two tick
	 * pairs, and never starts a play on that slot (yodle.cpp). `frame` is
	 * numbered from one; anything outside the bank draws nothing.
	 */
	void drawBankFrame(uint slot, int frame, Graphics::Surface &dest, int scrollX = 0,
					   int clipBottom = DL1Sprite::kNoClipBottom) const;

	/// The bank itself, for a scene that draws and wipes its frames by hand
	/// and needs their stored boxes to do it (studio.cpp).
	const DL1Sprite &bank(uint slot) const { return _slots[slot].bank; }

	/** The bank a slot holds, for the debug console. */
	const Common::String &bankName(uint slot) const { return _slots[slot].name; }
	int frame(uint slot) const { return _slots[slot].frame; }

	/// The frame a slot last put on screen: the original's `0xa54a` byte array,
	/// which MIDAS:snd_func_1482 fills from the frame (or, for a list, the
	/// list's entry) as it draws, and which room 58 waits on.
	int shownFrame(uint slot) const { return visibleFrame(_slots[slot]); }

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
		const byte *frames;		///< the frame list, for modes 6, 7 and 8

		bool hold;				///< a port addition: never show this slot blank
		mutable int heldFrame;	///< 1-based, the last real frame drawn while held

		Slot() { clear(); }
		void clear();
	};

	/// The frame a slot should show, clamped to the range it was given.
	int visibleFrame(const Slot &slot) const;

	Slot _slots[kSlotCount];
	int _room;

	/// The room's own loop calls, from animLoopsForRoom.
	/// The room's script, for the loop rows guarded on a puzzle flag rather
	/// than on a loop or pose byte (loopArmed()). Set by loadRoom(); null for
	/// the cutscene player's banks, which have no loop rows at all.
	const RoomScript *_state;

	const AnimLoop *_loops;
	uint _loopCount;

	/// Slots a hand-ported tick relaunches itself (markLooping()), one bit each.
	uint16 _handLoops;
};

} // End of namespace Alien

#endif
