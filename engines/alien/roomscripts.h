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

#ifndef ALIEN_ROOMSCRIPTS_H
#define ALIEN_ROOMSCRIPTS_H

#include "common/scummsys.h"
#include "common/util.h"

namespace Alien {

/**
 * What a room does with a click on its own account.
 *
 * A click resolves into an (object, verb) pair and an outcome code. The code
 * drives the dialog, and is handled by the generic path shared by every room
 * (see TalFile and AlienEngine::queueOutcome). Everything else a click does --
 * opening a door, taking an object off the wall, arming a puzzle flag -- lives
 * in the room's own overlay, as a chain of guarded bodies that the overlay
 * walks after the generic path has run.
 *
 * The original stores those bodies as code, so the port lifts them into this
 * table: tools/roomlogic.py decodes the overlay disassembly and
 * tools/gen_roomscripts.py writes roomscripts.cpp from its dump. The blocks of
 * a room are in the overlay's own order, and the interpreter runs the first one
 * whose object, verb and preconditions all match, which is how the original
 * behaves once a body sets action_handled.
 *
 * Effects belonging to systems the port has not reached yet (animation slots,
 * the sprite-object table, sound) are still in the table with their arguments,
 * so wiring one up is an arm in the interpreter's switch. Bodies the
 * disassembler could not fully recover carry kOpUnsupported.
 */
enum ScriptOpcode {
	kOpUnsupported = 0,	///< recovered as code, not as arguments; skipped
	kOpSetFlag,			///< args: state address (0xa6xx/0xa7xx), value
	kOpActionHandled,	///< args: value of [0xa602]
	kOpSubmode,			///< args: value of game_submode [0xa87e]
	kOpQueueEvent,		///< args: outcome code -> the TAL dialog chain
	kOpAnimPlay1,		///< args: slot, first, last, rate
	kOpAnimPlay3,		///< args: slot, first, last, rate (reverse form)
	kOpSound,			///< args: sound slot
	kOpPlaySample,		///< args: sample, ?, rate, volume, ?, delay
	kOpSpriteAdd,		///< args: sprite id
	kOpSpriteRemove,	///< args: sprite id
	kOpSpritePresent	///< args: sprite id (a test in the original)
};

/** One byte of the state block as a new game leaves it. */
struct ScriptFlagInit {
	uint16 addr;
	byte value;
};

/** One `[address] == value` guard, as the overlay's `cmp`/`jne` pair. */
struct ScriptCond {
	uint16 addr;
	byte value;
	bool negate;
};

/** One effect, with the branch arms it sits under inside its body. */
struct ScriptEffect {
	byte op;
	byte argCount;
	uint16 args[6];		///< flag addresses reach 0xa7ff, so these are unsigned
	byte guardCount;
	ScriptCond guards[3];
};

/** One guarded body: an (object, verb) pair in a given puzzle state. */
struct ScriptBlock {
	int16 obj;			///< -1 = any object
	int16 verb;			///< -1 = any verb
	byte condCount;
	ScriptCond conds[4];
	uint16 first;		///< first effect
	uint16 count;
};

/**
 * A room's script blocks in overlay order, or null with a count of zero for a
 * room whose overlay has no (object, verb) code at all -- seven rooms leave
 * every click to the generic outcome path.
 */
const ScriptBlock *scriptForRoom(int room, uint &count);

/** One effect out of the shared pool ScriptBlock::first indexes. */
const ScriptEffect *scriptEffect(uint index);

/**
 * The state a new game starts in, in the order MAIN writes it: which doors are
 * shut, which objects are still in place. The state block is uninitialised data
 * in the original, so without this every flag would start at zero.
 */
const ScriptFlagInit *initialFlags(uint &count);

} // End of namespace Alien

#endif
