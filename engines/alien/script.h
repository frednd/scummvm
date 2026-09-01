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

#ifndef ALIEN_SCRIPT_H
#define ALIEN_SCRIPT_H

#include "common/array.h"
#include "common/serializer.h"

#include "alien/hotspots.h"
#include "alien/roomscripts.h"
#include "alien/transitions.h"
#include "alien/walkgeom.h"

namespace Alien {

class AlienEngine;
class AnimSlots;
class Inventory;
class SoundFX;

/** Where a click sends the character, as the room's walk geometry decides. */
struct WalkTarget {
	int16 x;
	int16 y;
	byte facing;		///< 1..4, or kWalkFacingKeep for "arrive as you are"
	byte submode;		///< the game submode arriving there arms, 0 for none
};

/**
 * The room-script interpreter and the game state it reads and writes.
 *
 * The original keeps its puzzle state as a block of bytes in the data segment
 * -- 0xa600 to 0xa7ff, the range the save file carries -- and every room
 * overlay tests and sets those bytes directly. The port keeps the same block,
 * addressed the same way, so a table lifted out of the disassembly needs no
 * renumbering and a save game maps onto it one for one.
 *
 * run() walks the room's blocks in overlay order and executes the first one
 * whose object, verb and preconditions match, which is how the original behaves
 * once a body has set action_handled. Effects belonging to systems the port has
 * not reached yet are counted and logged rather than silently dropped.
 */
class RoomScript {
public:
	/// The state block: [0xa600, 0xa800). Includes action_handled at 0xa602.
	static const uint16 kFlagBase = 0xa600;
	static const uint kFlagCount = 0x200;

	/// The second, smaller block: the one-shot latches the resident units set
	/// the first time something happens ("Ben has already said this once").
	/// Room overlays guard a dozen of their hotspots on these.
	static const uint16 kLatchBase = 0x3380;
	static const uint kLatchCount = 0x80;

	/// [0xa602], set by a body that has consumed the click.
	static const uint16 kActionHandled = 0xa602;

	/// The two transition globals a guard may read. They sit outside the state
	/// block, and the engine rather than the block holds them.
	static const uint16 kGameMode = 0xa880;
	static const uint16 kGameSubmode = 0xa87e;

	/// The click the blocks match on, as LOGIC:sub_12198 and sub_12336 leave it:
	/// the item being used, or zero, and the object it was used on. Room overlays
	/// read these directly as well, so they live in the state block.
	static const uint16 kUsedItem = 0xa64b;
	static const uint16 kClickedObject = 0xa64c;

	/// Two of the sixteen-slot animation arrays (see anim.h) that room scripts
	/// read as if they were flags: the current frame, a word per slot, and the
	/// frames a slot has left to advance, a byte per slot. Nothing in the game
	/// writes them this way -- the play routines own them -- so the port answers
	/// reads out of AnimSlots and refuses writes.
	static const uint16 kAnimFrameBase = 0xa4ca;
	static const uint16 kAnimRemainingBase = 0xa4ea;

	/// No outcome code queued. The original uses zero for "nothing to say".
	static const byte kNoEvent = 0;

	RoomScript();

	/// The slots an anim_play effect drives. Not owned.
	void setAnims(AnimSlots *anims) { _anims = anims; }

	/// The list an inv_add / inv_remove / inv_has effect works on. Not owned.
	void setInventory(Inventory *inventory) { _inventory = inventory; }

	/// The bank and voices a sound / play_sample effect triggers. Not owned.
	void setSound(SoundFX *sound) { _sound = sound; }

	/// Where a music effect starts its slot: the module and the mixer stream
	/// belong to the engine, not to a subsystem of its own. Not owned.
	void setEngine(AlienEngine *vm) { _vm = vm; }

	/// Clears the whole state block, as starting a new game does.
	void reset();

	/**
	 * Binds the block table of a room and runs the room's own opening setup --
	 * the animation frame each of its slots starts on, under the puzzle-state
	 * guards the overlay puts it under. Rooms with no script bind nothing.
	 */
	void enterRoom(int room);

	/**
	 * Runs the room's response to a click that resolved to (obj, verb), with
	 * `item` the inventory item being used on the object, or zero for a plain
	 * verb click. Returns true when a body claimed the click, i.e. set
	 * action_handled.
	 */
	bool run(byte obj, byte verb, byte item = 0);

	/// The outcome code the last run() queued, or kNoEvent.
	byte queuedEvent() const { return _queued; }

	/// Clears it, for a caller that has spoken what was standing there.
	void clearQueuedEvent() { _queued = kNoEvent; }

	/**
	 * Where the room being entered stands the character, if it says at all.
	 *
	 * A room's opening effects can carry a CHARANIM:0x4e call -- often several,
	 * one per way in, under guards on the room that was left. The last one whose
	 * guards hold wins, as it does in the original, and a room with none leaves
	 * the caller to place him however it likes.
	 */
	bool placeRequest(int &x, int &y, int &facing) const;

	/// No submode. The original leaves a room only on a non-zero one.
	static const byte kNoSubmode = 0;

	/**
	 * The submode the last run() entered, or kNoSubmode.
	 *
	 * A body that ends the scene sets game_submode itself rather than arming it
	 * on an approach point, which is how a cutscene or a close-up is entered
	 * from a click that never moved the character.
	 */
	byte submodeRequest() const { return _submode; }

	/**
	 * Where leaving `room` by `submode` leads, or 0 when the chain has no link
	 * for that pair.
	 *
	 * This is the main loop's chain of check_event() calls, from the table in
	 * transitions.h, scanned in the original's order: the first link whose
	 * guards hold is the one that dispatches.
	 */
	int nextRoom(byte room, byte submode) const;

	/**
	 * Resolve a click into the point the character walks to.
	 *
	 * This is entry 0 of the room's overlay, from the table in walkgeom.h: the
	 * target starts as the click, the room's rectangles snap or redirect it, and
	 * an object's own approach point wins over all of them. `obj` is the object
	 * the click landed on, or zero for a click on the floor.
	 *
	 * False when the room has no geometry of its own, in which case the click
	 * itself is the target.
	 */
	bool walkTarget(int clickX, int clickY, byte obj, WalkTarget &out) const;

	/**
	 * Runs a room's hotspot program -- entry 1 of its overlay -- into `out`.
	 *
	 * The original re-registers every rectangle on every frame, so which ones
	 * exist follows the puzzle state for free. The port runs the program again
	 * whenever the state may have moved, which comes to the same thing without
	 * the per-frame cost.
	 */
	void buildHotspots(int room, Common::Array<Hotspot> &out) const;

	/**
	 * Runs a run of effects that came from somewhere other than a room's own
	 * script -- a cutscene procedure (see cutscenes.h), which is written in the
	 * same vocabulary and plays against the same slots and state.
	 */
	void runEffects(const ScriptEffect *effects, uint count);

	byte flag(uint16 addr) const;
	void setFlag(uint16 addr, byte value);

	/// Whether one lifted guard holds right now. Public because the cutscene
	/// tables carry guards of their own and answer them against this state.
	bool condHolds(const ScriptCond &cond) const { return holds(cond); }

	/// The whole state block and the latches, which is all a save carries.
	void syncGame(Common::Serializer &s);

private:
	bool holds(const ScriptCond &cond) const;
	bool matches(const ScriptBlock &block, byte obj, byte verb, byte item) const;
	void execute(const ScriptBlock &block);
	void runEffect(const ScriptEffect &effect);
	void playAnim(const ScriptEffect &effect);
	void inventoryEffect(const ScriptEffect &effect);
	void soundEffect(const ScriptEffect &effect);

	byte *flagSlot(uint16 addr);
	const byte *flagSlot(uint16 addr) const;
	bool animSlotByte(uint16 addr, byte &out) const;

	AlienEngine *_vm;
	AnimSlots *_anims;
	Inventory *_inventory;
	SoundFX *_sound;
	byte _flags[kFlagCount];
	byte _latches[kLatchCount];
	const ScriptBlock *_blocks;
	uint _blockCount;
	int _room;
	byte _queued;
	byte _submode;

	/// The CHARANIM:0x4e call the room's opening effects last made, if any.
	bool _placed;
	int _placeX;
	int _placeY;
	int _placeFacing;
};

} // End of namespace Alien

#endif
