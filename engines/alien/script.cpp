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
#include "common/textconsole.h"

#include "alien/anim.h"
#include "alien/detection.h"
#include "alien/roominit.h"
#include "alien/script.h"

namespace Alien {

RoomScript::RoomScript() : _anims(nullptr), _blocks(nullptr), _blockCount(0),
		_room(0), _queued(kNoEvent) {
	reset();
}

void RoomScript::reset() {
	memset(_flags, 0, sizeof(_flags));
	_queued = kNoEvent;

	// The state block is uninitialised data in the original, so a new game is
	// the run of writes MAIN makes before the first room -- the doors that
	// start shut, the objects still on their shelves.
	uint count = 0;
	const ScriptFlagInit *initial = initialFlags(count);
	for (uint i = 0; i < count; i++)
		setFlag(initial[i].addr, initial[i].value);

	debugC(1, kDebugGraphics, "script: %u state bytes set for a new game", count);
}

void RoomScript::enterRoom(int room) {
	_room = room;
	_blocks = scriptForRoom(room, _blockCount);
	_queued = kNoEvent;

	// The room's opening setup, out of the same overlay routine that loads its
	// banks. Without it a slot stays blank until something plays it, so a door
	// left open would open again from scratch.
	uint count = 0;
	const ScriptEffect *init = roomInitEffects(room, count);
	if (!init)
		return;

	debugC(1, kDebugGraphics, "script: room %d opens with %u slot plays", room, count);
	for (uint i = 0; i < count; i++)
		runEffect(init[i]);
}

byte RoomScript::flag(uint16 addr) const {
	if (addr < kFlagBase || addr >= kFlagBase + kFlagCount)
		return 0;
	return _flags[addr - kFlagBase];
}

void RoomScript::setFlag(uint16 addr, byte value) {
	if (addr < kFlagBase || addr >= kFlagBase + kFlagCount) {
		debugC(1, kDebugGraphics, "script: flag 0x%04x is outside the state block", addr);
		return;
	}
	_flags[addr - kFlagBase] = value;
}

bool RoomScript::holds(const ScriptCond &cond) const {
	// A guard on an address outside the state block is one this port cannot
	// answer -- the handful that exist read the inventory and the game mode --
	// so the arm it protects is treated as not taken rather than guessed at.
	if (cond.addr < kFlagBase || cond.addr >= kFlagBase + kFlagCount)
		return false;

	const bool equal = _flags[cond.addr - kFlagBase] == cond.value;
	return cond.negate ? !equal : equal;
}

bool RoomScript::matches(const ScriptBlock &block, byte obj, byte verb) const {
	if (block.obj >= 0 && block.obj != obj)
		return false;
	if (block.verb >= 0 && block.verb != verb)
		return false;

	for (uint i = 0; i < block.condCount; i++) {
		if (!holds(block.conds[i]))
			return false;
	}

	return true;
}

void RoomScript::execute(const ScriptBlock &block) {
	for (uint i = 0; i < block.count; i++)
		runEffect(*scriptEffect(block.first + i));
}

void RoomScript::runEffect(const ScriptEffect &effect) {
	// The arms inside a body: the refusal and the success path of the same click
	// sit side by side, each under its own guard. A guard the port cannot answer
	// -- an address outside the state block -- fails, so its arm is not taken.
	for (uint g = 0; g < effect.guardCount; g++) {
		if (!holds(effect.guards[g]))
			return;
	}

	switch (effect.op) {
	case kOpSetFlag:
		setFlag((uint16)effect.args[0], (byte)effect.args[1]);
		break;

	case kOpActionHandled:
		setFlag(kActionHandled, (byte)effect.args[0]);
		break;

	case kOpQueueEvent:
		// The last one wins: the original writes them into one queue, and each
		// call replaces what is standing there.
		_queued = (byte)effect.args[0];
		break;

	case kOpAnimPlay1:
	case kOpAnimPlay2:
	case kOpAnimPlay3:
		playAnim(effect);
		break;

	// Everything below belongs to a system the port has not reached. The
	// arguments are in the table, so each of these becomes a call once the
	// system behind it lands.
	case kOpSubmode:
		debugC(1, kDebugGraphics, "script: room %d enters submode %d",
			   _room, effect.args[0]);
		break;

	case kOpInvAdd:
	case kOpInvRemove:
	case kOpInvHas:
		debugC(2, kDebugGraphics, "script: item %d %s", effect.args[0],
			   effect.op == kOpInvAdd ? "picked up"
									  : (effect.op == kOpInvRemove ? "given up"
																   : "tested for"));
		break;

	case kOpSound:
	case kOpPlaySample:
		debugC(2, kDebugGraphics, "script: sound %d", effect.args[0]);
		break;

	default:
		debugC(1, kDebugGraphics, "script: room %d has an effect this table "
			   "could not recover", _room);
		break;
	}
}

void RoomScript::playAnim(const ScriptEffect &effect) {
	// All three play routines take the same four arguments. A body whose call
	// lost one of them to a register cannot be run: the frame count and the rate
	// decide how long the animation is on screen, and guessing either would
	// leave a door half open.
	if (!_anims || effect.argCount < 4) {
		debugC(1, kDebugGraphics, "script: room %d plays an animation with %u of "
			   "4 arguments", _room, effect.argCount);
		return;
	}

	const int mode = effect.op == kOpAnimPlay1 ? 1 : (effect.op == kOpAnimPlay2 ? 2 : 3);
	_anims->play(effect.args[0], (int)effect.args[1], (int)effect.args[2],
				 (int)effect.args[3], mode);
}

bool RoomScript::run(byte obj, byte verb) {
	_queued = kNoEvent;
	setFlag(kActionHandled, 0);

	for (uint i = 0; i < _blockCount; i++) {
		if (!matches(_blocks[i], obj, verb))
			continue;

		debugC(1, kDebugGraphics, "script: room %d block %u runs for object %u verb %u",
			   _room, i, obj, verb);
		execute(_blocks[i]);

		// The overlay guards the blocks that follow on action_handled, so a
		// body that claims the click ends the chain; one that only sets a flag
		// lets the next matching body run, which is how a room reacts to both
		// the object and the verb alone.
		if (flag(kActionHandled))
			return true;
	}

	return false;
}

} // End of namespace Alien
