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

#include "alien/alien.h"
#include "alien/anim.h"
#include "alien/detection.h"
#include "alien/inventory.h"
#include "alien/roominit.h"
#include "alien/cutscenes.h"
#include "alien/script.h"
#include "alien/sfx.h"

namespace Alien {

RoomScript::RoomScript() : _vm(nullptr), _anims(nullptr), _inventory(nullptr), _sound(nullptr), _blocks(nullptr), _blockCount(0),
		_room(0), _queued(kNoEvent), _submode(kNoSubmode),
		_placed(false), _placeX(0), _placeY(0), _placeFacing(0), _frameList(cutsceneFrameList) {
	reset();
}

void RoomScript::resetScene() {
	memset(_scene, 0, sizeof(_scene));
	_cutscenePos = 0;
	_scenePos = 0;
}

void RoomScript::reset() {
	memset(_flags, 0, sizeof(_flags));
	memset(_latches, 0, sizeof(_latches));
	resetScene();
	_queued = kNoEvent;
	_submode = kNoSubmode;

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
	_submode = kNoSubmode;
	_placed = false;

	// The room's opening setup, out of the same overlay routine that loads its
	// banks. Without it a slot stays blank until something plays it, so a door
	// left open would open again from scratch.
	uint count = 0;
	const ScriptEffect *init = roomInitEffects(room, count);
	if (!init)
		return;

	debugC(1, kDebugGraphics, "script: room %d opens with %u slot plays", room, count);

	// The openings' frame lists are their own table, not the pack's.
	_frameList = roomInitFrameList;
	runEffects(init, count);
	_frameList = cutsceneFrameList;
}

bool RoomScript::placeRequest(int &x, int &y, int &facing) const {
	if (!_placed)
		return false;
	x = _placeX;
	y = _placeY;
	facing = _placeFacing;
	return true;
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
		case kWalkMaxX:
			if (clickX > row.a)
				out.x = row.x;
			break;
		case kWalkMinX:
			if (clickX < row.a)
				out.x = row.x;
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
			if (slot && (uint)(slot - 1) < varCount)
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
	// The scene words, which live beside the latches in the original's data
	// segment and are read and written a byte at a time here: neither ever
	// holds more than a handful, so the high byte of each is dead.
	if (addr >= kSceneBase && addr < kSceneBase + kSceneCount)
		return &_scene[addr - kSceneBase];
	return nullptr;
}

const byte *RoomScript::flagSlot(uint16 addr) const {
	return const_cast<RoomScript *>(this)->flagSlot(addr);
}

/**
 * Answers a read of one of the animation-slot arrays.
 *
 * The original keeps its sixteen slots as parallel arrays in the data segment
 * (anim.h lists them), and a room's guards read two of those arrays as plain
 * memory: `0xa4ca`, the current frame as a word per slot, and `0xa4ea`, the
 * frames left to advance as a byte per slot. That is how a room says "only
 * start this if the slot is idle" ([0xa4eb] == 0 gates a play on slot 1) or
 * "start this two frames before the other one runs out" ([0xa4ef] == 2 gates a
 * play on slot 5). The port keeps that state in AnimSlots rather than in a
 * memory image, so the reads are forwarded there.
 */
bool RoomScript::animSlotByte(uint16 addr, byte &out) const {
	if (!_anims)
		return false;

	if (addr >= kAnimFrameBase && addr < kAnimFrameBase + 2 * AnimSlots::kSlotCount) {
		const uint16 offset = addr - kAnimFrameBase;
		const uint16 frame = (uint16)_anims->frame(offset / 2);
		out = (offset & 1) ? (byte)(frame >> 8) : (byte)frame;
		return true;
	}

	if (addr >= kAnimRemainingBase && addr < kAnimRemainingBase + AnimSlots::kSlotCount) {
		out = (byte)_anims->remaining(addr - kAnimRemainingBase);
		return true;
	}

	if (addr >= kAnimLoopBase && addr < kAnimLoopBase + AnimSlots::kSlotCount) {
		out = _anims->loopFlag(addr - kAnimLoopBase);
		return true;
	}

	return false;
}

byte RoomScript::flag(uint16 addr) const {
	byte value;
	if (animSlotByte(addr, value))
		return value;

	// The scene clock is a word the engine steps, so its two bytes are answered
	// out of it rather than out of a block.
	if (addr == kCutscenePos)
		return (byte)_cutscenePos;
	if (addr == kCutscenePos + 1)
		return (byte)(_cutscenePos >> 8);

	// The stream cursor is the player's own, for the same reason.
	if (addr == kScenePos)
		return (byte)_scenePos;
	if (addr == kScenePos + 1)
		return (byte)(_scenePos >> 8);

	const byte *slot = flagSlot(addr);
	return slot ? *slot : 0;
}

void RoomScript::setFlag(uint16 addr, byte value) {
	// Every write to the clock in the game is a word store of zero -- a scene
	// resetting it -- so writing the low byte writes the whole word.
	if (addr == kCutscenePos) {
		_cutscenePos = value;
		return;
	}

	if (addr == kScenePos) {
		_scenePos = value;
		return;
	}

	// The one animation-slot array a room does write: [0xa53a] is where it
	// turns a loop of its own on and off, and it does so with a plain store
	// beside the play that starts the animation.
	if (_anims && addr >= kAnimLoopBase && addr < kAnimLoopBase + AnimSlots::kSlotCount) {
		_anims->setLoopFlag(addr - kAnimLoopBase, value);
		return;
	}

	byte animValue;
	if (animSlotByte(addr, animValue)) {
		// The play routines own these; nothing lifted out of the game writes
		// them, and letting a write through here would desynchronise the slot.
		debugC(1, kDebugGraphics, "script: 0x%04x is animation slot state, not a flag", addr);
		return;
	}

	byte *slot = flagSlot(addr);
	if (!slot) {
		debugC(1, kDebugGraphics, "script: flag 0x%04x is outside the state block", addr);
		return;
	}
	*slot = value;
}

bool RoomScript::holds(const ScriptCond &cond) const {
	bool equal = false;

	if (cond.kind == kCondAbove) {
		// The ordering guard reads its address as a word, which is what the
		// scene clock needs: it runs past 255 inside a single scene.
		const uint16 word = flag(cond.addr) | ((uint16)flag(cond.addr + 1) << 8);
		const bool above = word > cond.value;
		return cond.negate ? !above : above;
	}

	if (cond.kind == kCondItem) {
		// OBJ:0x6add returns 1 when the item is carried, and every guard lifted
		// this way compares that answer against an immediate.
		const byte carried = (_inventory && _inventory->has((byte)cond.addr)) ? 1 : 0;
		equal = carried == cond.value;
	} else if (cond.addr == kGameMode || cond.addr == kGameSubmode) {
		// The two transition globals live in the engine rather than in the state
		// block: they are how the room being left and the exit it was left by
		// reach the room being entered.
		const byte value = !_vm ? 0
			: (cond.addr == kGameMode ? _vm->gameMode() : _vm->gameSubmode());
		equal = value == cond.value;
	} else {
		// A guard on an address outside the two state blocks is one this port
		// cannot answer, so the arm it protects is treated as not taken rather
		// than guessed at.
		byte value;
		if (animSlotByte(cond.addr, value)) {
			equal = value == cond.value;
			debugC(2, kDebugGraphics, "script: anim state 0x%04x is %d, guard wants %d",
				   cond.addr, value, cond.value);
		} else {
			const byte *slot = flagSlot(cond.addr);
			if (!slot)
				return false;
			equal = *slot == cond.value;
		}
	}

	return cond.negate ? !equal : equal;
}

bool RoomScript::matches(const ScriptBlock &block, byte obj, byte verb, byte item) const {
	if (block.obj >= 0 && block.obj != obj)
		return false;
	if (block.verb >= 0 && block.verb != verb)
		return false;

	// A block that names an item is one item-use combination, and a click with
	// nothing held matches none of them; one that names no item takes whatever
	// is in hand, as the original's guard chain does.
	if (block.item >= 0 && block.item != item)
		return false;

	for (uint i = 0; i < block.condCount; i++) {
		if (!holds(block.conds[i]))
			return false;
	}

	return true;
}

void RoomScript::execute(const ScriptBlock &block) {
	// Through runEffects, so a body's arms are tested once each rather than once
	// per effect: room 35's hatch opens by setting the very flag its arm is
	// guarded on ([0xa777]), and re-answering that guard for the next effect
	// dropped the rest of the arm -- including the action_handled that ends the
	// chain -- and then let the "swing it shut again" arm undo it.
	runEffects(scriptEffect(block.first), block.count);
}

/**
 * A run of effects lifted out of one body or one cutscene procedure.
 *
 * Effects carry the guards of the arm they were lifted from, one copy each, and
 * consecutive effects with the same guards *are* one arm. The original tests
 * such an arm once and then runs its body, so the test has to be made once here
 * too -- an arm that writes the flag it is guarded on is otherwise cut off
 * after its first effect. Both places that happens are exactly that shape: room
 * 35's hatch sets [0xa777] as it swings open, and one of the boss scenes stores
 * into the word its timeline waits on.
 */
void RoomScript::runEffects(const ScriptEffect *effects, uint count) {
	uint i = 0;
	while (i < count) {
		uint end = i + 1;
		while (end < count && sameGuards(effects[i], effects[end]))
			end++;

		bool taken = true;
		for (uint g = 0; g < effects[i].guardCount && taken; g++)
			taken = holds(effects[i].guards[g]);

		if (taken) {
			for (uint e = i; e < end; e++)
				runEffect(effects[e]);
		}
		i = end;
	}
}

/** Whether two effects were lifted under the same arm. */
bool RoomScript::sameGuards(const ScriptEffect &a, const ScriptEffect &b) {
	if (a.guardCount != b.guardCount)
		return false;
	for (uint g = 0; g < a.guardCount; g++) {
		if (a.guards[g].addr != b.guards[g].addr || a.guards[g].value != b.guards[g].value ||
			a.guards[g].negate != b.guards[g].negate || a.guards[g].kind != b.guards[g].kind)
			return false;
	}
	return true;
}

/**
 * One effect, with its arm already answered by runEffects.
 *
 * The arms inside a body -- the refusal and the success path of the same click,
 * side by side -- are what the guards on an effect are, and they are tested a
 * whole arm at a time above rather than here, so this runs unconditionally.
 */
void RoomScript::runEffect(const ScriptEffect &original) {
	// A handful of arguments the original loaded into a register instead of
	// pushing as an immediate; roomlogic.py could still resolve them exactly
	// where the register held a byte this port already tracks as state (see
	// roomscripts.h), and marks them in dynArgs rather than as literals. Read
	// them now, once, so the switch below can treat every arg the same way.
	ScriptEffect effect = original;
	for (uint a = 0; a < effect.argCount; a++) {
		if (effect.dynArgs & (1 << a))
			effect.args[a] = (uint16)(flag((uint16)effect.args[a]) + effect.bias);
	}

	switch (effect.op) {
	case kOpSetFlag:
		setFlag((uint16)effect.args[0], (byte)effect.args[1]);
		break;

	case kOpAddFlag:
		setFlag((uint16)effect.args[0],
				(byte)(flag((uint16)effect.args[0]) + effect.args[1]));
		break;

	case kOpCharPlace:
		// The coordinates are longs in the original and their high words are
		// always zero, which is why only the low ones are read here.
		_placed = true;
		_placeX = effect.args[1];
		_placeY = effect.args[3];
		_placeFacing = effect.args[4];
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
	case kOpAnimPlay4:
	case kOpAnimPlay5:
	case kOpAnimPlay6:
	case kOpAnimPlay7:
	case kOpAnimPlay8:
		playAnim(effect);
		break;

	case kOpSubmode:
		// The room ends itself: the caller reads this back and takes the exit
		// the chain has for it, the same as an arrival would.
		_submode = (byte)effect.args[0];
		break;

	case kOpInvAdd:
	case kOpInvRemove:
	case kOpInvReplace:
	case kOpInvHas:
		inventoryEffect(effect);
		break;

	case kOpSound:
	case kOpPlaySample:
		soundEffect(effect);
		break;

	case kOpMusic:
		// The original starts a track from a room's own code, so a room that
		// changes the music does it here rather than from a table of its own.
		if (_vm)
			_vm->playMusicSlot(effect.args[0]);
		break;

	default:
		debugC(1, kDebugGraphics, "script: room %d has an effect this table "
			   "could not recover", _room);
		break;
	}
}

void RoomScript::inventoryEffect(const ScriptEffect &effect) {
	const byte item = (byte)effect.args[0];
	if (!_inventory)
		return;

	if (effect.op == kOpInvAdd)
		_inventory->add(item);
	else if (effect.op == kOpInvRemove)
		_inventory->remove(item);
	else if (effect.op == kOpInvReplace)
		// The original pushes the item to look for first, so it arrives as
		// (old, new); the slot it is found in is the slot the new item takes.
		_inventory->replace(item, (byte)effect.args[1]);
	else
		// A test, and its answer goes into a register the decoder could not
		// follow, so the arm it guards is already flattened into the body. The
		// answer is logged rather than acted on.
		debugC(2, kDebugItems, "script: room %d asks for item %u: %s", _room, item,
			   _inventory->has(item) ? "carried" : "not carried");
}

void RoomScript::soundEffect(const ScriptEffect &effect) {
	if (!_sound)
		return;

	// sound(n): INPUT:0x194 fills the rest in -- centred, full volume, 11000 Hz.
	if (effect.op == kOpSound) {
		_sound->play(effect.args[0], SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);
		return;
	}

	// play_sample(sample, rate high, rate low, volume, panning, delay): the rate
	// reaches INPUT:0x55E as two words, and the panning is signed.
	if (effect.argCount < 6) {
		debugC(1, kDebugSound, "script: room %d queues a sample whose arguments "
			   "the table does not have", _room);
		return;
	}

	const uint32 rate = ((uint32)effect.args[1] << 16) | effect.args[2];
	_sound->queue(effect.args[0], rate, (byte)effect.args[3],
				  (int8)(int16)effect.args[4], effect.args[5]);
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

	int mode = 1;
	switch (effect.op) {
	case kOpAnimPlay2: mode = 2; break;
	case kOpAnimPlay3: mode = 3; break;
	case kOpAnimPlay4: mode = 4; break;
	case kOpAnimPlay5: mode = 5; break;
	case kOpAnimPlay6: mode = 6; break;
	case kOpAnimPlay7: mode = 7; break;
	case kOpAnimPlay8: mode = 8; break;
	default: break;
	}

	// Modes 6, 7 and 8 read a frame list, and the effect's fifth argument is
	// where that list starts in whichever pool this run is reading -- the
	// cutscenes' or the rooms' openings'. Without it the cursor those modes
	// step would be drawn as a frame.
	const byte *frames = nullptr;
	if (mode >= 6 && effect.argCount >= 5 && _frameList)
		frames = _frameList(effect.args[4], effect.args[2]);

	_anims->play(effect.args[0], (int)effect.args[1], (int)effect.args[2],
				 (int)effect.args[3], mode, frames);
}

bool RoomScript::run(byte obj, byte verb, byte item) {
	_queued = kNoEvent;
	_submode = kNoSubmode;
	setFlag(kActionHandled, 0);

	// The click, where a room's own code reads it: an overlay tests these two
	// directly as well as through the block guards.
	setFlag(kUsedItem, item);
	setFlag(kClickedObject, obj);

	for (uint i = 0; i < _blockCount; i++) {
		if (!matches(_blocks[i], obj, verb, item))
			continue;

		debugC(1, kDebugGraphics, "script: room %d block %u runs for object %u verb %u item %u",
			   _room, i, obj, verb, item);
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

int RoomScript::nextRoom(byte room, byte submode) const {
	if (submode == kNoSubmode)
		return 0;

	uint count = 0;
	const Transition *table = transitionTable(count);

	for (uint i = 0; i < count; i++) {
		const Transition &link = table[i];
		if (link.mode != room || link.submode != submode)
			continue;

		bool ok = true;
		for (uint g = 0; g < link.guardCount && ok; g++)
			ok = holds(link.guards[g]);
		if (!ok)
			continue;

		return link.room;
	}

	return 0;
}

void RoomScript::syncGame(Common::Serializer &s) {
	s.syncBytes(_flags, kFlagCount);
	s.syncBytes(_latches, kLatchCount);
	s.syncAsByte(_queued);
	s.syncAsByte(_submode);
}

} // End of namespace Alien
