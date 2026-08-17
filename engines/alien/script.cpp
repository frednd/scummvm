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
	memset(_latches, 0, sizeof(_latches));
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

bool RoomScript::walkTarget(int clickX, int clickY, byte obj, WalkTarget &out) const {
	// 1021:0x3ea, which every entry 0 calls first: the target is the click, held
	// off the top of the room and out of the strip just above the toolbar, and no
	// turn is owed. A click on the toolbar itself -- y at 0x9f or below it -- is
	// left alone, because it is not a walk at all.
	out.x = (int16)clickX;
	out.y = (int16)MAX(clickY, 0x0d);
	if (out.y > 0x9a && out.y < 0x9f)
		out.y = 0x9a;
	out.facing = kWalkFacingKeep;
	out.submode = 0;

	uint count = 0;
	const WalkGeom *geom = walkGeomForRoom(_room, count);
	if (!geom)
		return false;

	// Every test reads the click, never the target the previous row produced, so
	// the rows do not chain; the last one that matches is the one that decides.
	for (uint i = 0; i < count; i++) {
		const WalkGeom &row = geom[i];

		bool guarded = true;
		for (uint g = 0; g < row.guardCount && guarded; g++)
			guarded = holds(row.guards[g]);
		if (!guarded)
			continue;

		const bool inside = clickX >= row.a && clickY >= row.b &&
							clickX <= row.c && clickY <= row.d;

		switch (row.kind) {
		case kWalkSnapDown:
			if (inside)
				out.y = row.d;
			break;
		case kWalkSnapUp:
			if (inside)
				out.y = row.b;
			break;
		case kWalkSnapRight:
			if (inside)
				out.x = row.c;
			break;
		case kWalkSnapLeft:
			if (inside)
				out.x = row.a;
			break;
		case kWalkZone:
			if (inside) {
				out.x = row.x;
				out.y = row.y;
				out.facing = row.facing;
			}
			break;
		case kWalkObject:
			if (obj && (row.a == obj || row.b == obj || row.c == obj || row.d == obj)) {
				out.x = row.x;
				out.y = row.y;
				out.facing = row.facing;
			}
			break;
		case kWalkMaxY:
			if (clickY > row.a)
				out.y = row.y;
			break;
		case kWalkMinY:
			if (clickY < row.a)
				out.y = row.y;
			break;
		case kWalkSubmode:
			// The click state the original also tests here -- left button down,
			// right button up, no action already pending -- is what a click in
			// this port is, so only the object has to match.
			if (obj && row.a == obj)
				out.submode = (byte)row.x;
			break;
		default:
			break;
		}
	}

	return true;
}

void RoomScript::buildHotspots(int room, Common::Array<Hotspot> &out) const {
	out.clear();

	uint count = 0;
	const HotspotOp *ops = hotspotProgramForRoom(room, count);
	if (!ops)
		return;

	// The scratch globals the program computes fields in. They are compiler
	// temporaries in the original and carry nothing between runs, so each one
	// starts at zero -- except where the overlay names a real flag instead, and
	// then it starts at the flag's value and no kHotspotSet ever touches it.
	byte vars[16];
	const uint varCount = MIN<uint>(hotspotVarCount(), ARRAYSIZE(vars));
	for (uint i = 0; i < varCount; i++)
		vars[i] = flag(hotspotVarAddr(i));

	for (uint i = 0; i < count; i++) {
		const HotspotOp &op = ops[i];

		bool guarded = true;
		for (uint g = 0; g < op.guardCount && guarded; g++)
			guarded = holds(op.guards[g]);
		if (!guarded)
			continue;

		if (op.kind == kHotspotSet) {
			if (op.var < varCount)
				vars[op.var] = op.value;
			continue;
		}

		Hotspot spot;
		spot.x1 = op.x1;
		spot.y1 = op.y1;
		spot.x2 = op.x2;
		spot.y2 = op.y2;
		spot.label = op.label;
		spot.obj = op.obj;
		spot.verb = op.verb;
		spot.outcomeCount = op.outcomeCount;
		for (uint o = 0; o < ARRAYSIZE(spot.outcomes); o++)
			spot.outcomes[o] = op.outcomes[o];

		// varOf holds a slot plus one per field, in this order.
		byte *fields[7] = { &spot.label, &spot.obj, &spot.verb,
							&spot.outcomes[0], &spot.outcomes[1],
							&spot.outcomes[2], &spot.outcomes[3] };
		for (uint f = 0; f < ARRAYSIZE(fields); f++) {
			const byte slot = op.varOf[f];
			if (slot && slot - 1 < varCount)
				*fields[f] = vars[slot - 1];
		}

		out.push_back(spot);
	}
}

byte *RoomScript::flagSlot(uint16 addr) {
	if (addr >= kFlagBase && addr < kFlagBase + kFlagCount)
		return &_flags[addr - kFlagBase];
	if (addr >= kLatchBase && addr < kLatchBase + kLatchCount)
		return &_latches[addr - kLatchBase];
	return nullptr;
}

const byte *RoomScript::flagSlot(uint16 addr) const {
	return const_cast<RoomScript *>(this)->flagSlot(addr);
}

byte RoomScript::flag(uint16 addr) const {
	const byte *slot = flagSlot(addr);
	return slot ? *slot : 0;
}

void RoomScript::setFlag(uint16 addr, byte value) {
	byte *slot = flagSlot(addr);
	if (!slot) {
		debugC(1, kDebugGraphics, "script: flag 0x%04x is outside the state block", addr);
		return;
	}
	*slot = value;
}

bool RoomScript::holds(const ScriptCond &cond) const {
	// A guard on an address outside the two state blocks is one this port cannot
	// answer -- the handful that exist read the inventory and the game mode --
	// so the arm it protects is treated as not taken rather than guessed at.
	const byte *slot = flagSlot(cond.addr);
	if (!slot)
		return false;

	const bool equal = *slot == cond.value;
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
