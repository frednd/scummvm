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

#include "alien/roomscripts.h"

namespace Alien {

class AnimSlots;

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

	/// [0xa602], set by a body that has consumed the click.
	static const uint16 kActionHandled = 0xa602;

	/// No outcome code queued. The original uses zero for "nothing to say".
	static const byte kNoEvent = 0;

	RoomScript();

	/// The slots an anim_play effect drives. Not owned.
	void setAnims(AnimSlots *anims) { _anims = anims; }

	/// Clears the whole state block, as starting a new game does.
	void reset();

	/// Binds the block table of a room. Rooms with no script bind nothing.
	void enterRoom(int room);

	/**
	 * Runs the room's response to a click that resolved to (obj, verb).
	 * Returns true when a body claimed the click, i.e. set action_handled.
	 */
	bool run(byte obj, byte verb);

	/// The outcome code the last run() queued, or kNoEvent.
	byte queuedEvent() const { return _queued; }

	byte flag(uint16 addr) const;
	void setFlag(uint16 addr, byte value);

private:
	bool holds(const ScriptCond &cond) const;
	bool matches(const ScriptBlock &block, byte obj, byte verb) const;
	void execute(const ScriptBlock &block);
	void playAnim(const ScriptEffect &effect);

	AnimSlots *_anims;
	byte _flags[kFlagCount];
	const ScriptBlock *_blocks;
	uint _blockCount;
	int _room;
	byte _queued;
};

} // End of namespace Alien

#endif
