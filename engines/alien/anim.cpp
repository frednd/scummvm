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
#include "common/path.h"

#include "alien/anim.h"
#include "alien/detection.h"
#include "alien/script.h"

namespace Alien {

void AnimSlots::Slot::clear() {
	started = false;
	frame = 0;
	first = 0;
	count = 0;
	remaining = 0;
	rate = 0;
	tick = 0;
	forward = true;
	restore = false;
	persist = true;
	baked = false;
	loop = 0;
	mode = 0;
	frames = nullptr;
	hold = false;
	heldFrame = 0;
}

AnimSlots::AnimSlots() : _room(0), _loops(nullptr), _loopCount(0), _state(nullptr) {
}

void AnimSlots::loadBanks(const char *const *names, uint count) {
	for (uint i = 0; i < kSlotCount; i++) {
		_slots[i].clear();
		_slots[i].bank.unload();
		_slots[i].name.clear();
	}

	_room = -1;
	_loops = nullptr;
	_loopCount = 0;
	_state = nullptr;

	for (uint i = 0; i < count && i < kSlotCount; i++) {
		if (!names[i] || !*names[i])
			continue;

		Slot &slot = _slots[i];
		const Common::String name(names[i]);
		if (!slot.bank.load(Common::Path(name))) {
			debugC(1, kDebugResource, "cutscene slot %u: could not load %s", i,
				   name.c_str());
			continue;
		}

		slot.name = name;
		debugC(2, kDebugResource, "cutscene slot %u: %s, %u frames", i, name.c_str(),
			   slot.bank.frameCount());
	}
}

void AnimSlots::loadBank(uint slot, const char *name) {
	if (slot >= kSlotCount)
		return;

	Slot &s = _slots[slot];
	s.clear();
	s.bank.unload();
	s.name.clear();

	if (!name || !*name)
		return;

	if (!s.bank.load(Common::Path(Common::String(name)))) {
		debugC(1, kDebugResource, "slot %u: could not load %s", slot, name);
		return;
	}

	s.name = name;
	debugC(2, kDebugResource, "slot %u: %s, %u frames", slot, name,
		   s.bank.frameCount());
}

void AnimSlots::reset() {
	for (uint i = 0; i < kSlotCount; i++) {
		const bool loaded = _slots[i].started;
		_slots[i].clear();
		if (loaded)
			debugC(2, kDebugGraphics, "anim: slot %u cleared", i);
	}
}

void AnimSlots::loadRoom(int room, const RoomScript &state) {
	for (uint i = 0; i < kSlotCount; i++) {
		_slots[i].clear();
		_slots[i].bank.unload();
		_slots[i].name.clear();
	}

	_room = room;
	_state = &state;
	_loops = animLoopsForRoom(room, _loopCount);
	if (_loopCount)
		debugC(2, kDebugGraphics, "room %d loops %u slots", room, _loopCount);

	uint count = 0;
	const AnimBank *banks = animBanksForRoom(room, count);
	if (!banks)
		return;

	for (uint i = 0; i < count; i++) {
		if (banks[i].slot >= kSlotCount) {
			warning("room %d loads a bank into slot %u, past the sixteen the "
					"original has", room, banks[i].slot);
			continue;
		}

		Slot &slot = _slots[banks[i].slot];

		// The one name the table does not carry whole: room 7 writes the
		// character between its two literals from a flag, so which of the two
		// closet banks it means is only known now. ovr_07_0e63:0xb26.
		Common::String name(banks[i].name);
		if (banks[i].flag && state.flag(banks[i].flag) == banks[i].value)
			name = banks[i].alt;

		if (!slot.bank.load(Common::Path(name))) {
			debugC(1, kDebugResource, "room %d slot %u: could not load %s", room,
				   banks[i].slot, name.c_str());
			continue;
		}

		slot.name = name;
		debugC(2, kDebugResource, "room %d slot %u: %s, %u frames", room,
			   banks[i].slot, name.c_str(), slot.bank.frameCount());
	}
}

void AnimSlots::play(uint slot, int first, int count, int rate, int mode,
					 const byte *frames) {
	if (slot >= kSlotCount) {
		debugC(1, kDebugGraphics, "anim: slot %u is past the end", slot);
		return;
	}

	Slot &s = _slots[slot];
	s.started = true;
	s.mode = (byte)mode;
	s.frames = frames;
	s.frame = first;
	s.first = first;
	s.count = count;
	s.remaining = count;
	s.rate = rate;
	s.tick = 0;

	// The three flags the eight routines differ in, exactly as they write them
	// (see the table in anim.h): only 3 and 5 run backward, only 2 and 8 come
	// back to where they started, and only 1, 3 and 6 leave the last frame
	// behind when the range runs out.
	s.forward = mode != 3 && mode != 5;
	s.restore = mode == 2 || mode == 8;
	s.persist = mode == 1 || mode == 3 || mode == 6;
	s.baked = false;

	debugC(2, kDebugGraphics, "anim: slot %u plays %s frames %d..%d rate %d "
		   "(mode %d)", slot, s.name.empty() ? "<no bank>" : s.name.c_str(),
		   first, s.forward ? first + count - 1 : first - count + 1, rate, mode);
}

void AnimSlots::relaunch(uint slot) {
	if (slot >= kSlotCount)
		return;

	// MIDAS:snd_func_112d. The test is on one frame left, not none: the room's
	// tick makes the call after the stepper has run, so the range is restarted
	// before the frame it would have ended on is ever drawn, and the cycle runs
	// without a stutter at the seam.
	Slot &s = _slots[slot];
	if (!s.started || s.remaining != 1)
		return;

	play(slot, s.first, s.count, s.rate, s.mode, s.frames);
}

void AnimSlots::takeDown(uint slot) {
	if (slot >= kSlotCount)
		return;

	Slot &s = _slots[slot];
	s.started = false;
	s.remaining = 0;
	s.persist = false;
	s.restore = false;
	s.baked = true;			// nothing of it goes into the plate either
	s.loop = 0;
	s.hold = false;
	s.heldFrame = 0;
	debugC(2, kDebugGraphics, "anim: slot %u taken down", slot);
}

/**
 * Whether one lifted row's relaunch is switched on as things stand.
 *
 * A row with no flag is a call the room makes every frame. A row with one is
 * made only while that byte is set, and the byte is of two kinds: the slot's
 * own loop byte (or the pose byte a few rooms use in its place), which the
 * slot carries, and an ordinary puzzle flag, which has to be read out of the
 * state block. Seven rooms guard a relaunch on the second kind, and room 45 is
 * the room that showed it up: its standing loop is switched on by [0xa784],
 * two states into the crystal entry's machine, and running it before that ate
 * the `remaining == 1` edge the state after it waits for, which stopped the
 * scene dead with Awlox half out of the crystal (finding #113).
 *
 * Only stepLoops() asks this. isLooping() deliberately does not -- see its own
 * comment: a parked loop slot is no more a one-shot in flight than a running
 * one, so it answers on membership alone.
 */
bool AnimSlots::loopArmed(const AnimLoop &row) const {
	if (!row.flag)
		return true;

	if (row.flag >= RoomScript::kFlagBase
			&& row.flag < RoomScript::kFlagBase + RoomScript::kFlagCount)
		return _state && _state->flag(row.flag) == 1;

	return _slots[row.slot].loop != 0;
}

void AnimSlots::stepLoops() {
	for (uint i = 0; i < _loopCount; i++) {
		const AnimLoop &row = _loops[i];
		if (row.slot >= kSlotCount || !loopArmed(row))
			continue;

		relaunch(row.slot);
	}
}

bool AnimSlots::stamp(uint slot, int frame, Graphics::Surface &background, int clipBottom,
					  int pageX) {
	if (slot >= kSlotCount)
		return false;

	Slot &s = _slots[slot];
	const int count = (int)s.bank.frameCount();
	if (frame < 1 || frame > count)
		return false;

	if (pageX != kNoPage)
		s.bank.drawFramePage((uint)(frame - 1), background, pageX, clipBottom);
	else
		s.bank.drawFrame((uint)(frame - 1), background, 0, clipBottom);
	debugC(2, kDebugGraphics, "anim: slot %u stamped frame %d of %s into the plate",
		   slot, frame, s.name.c_str());
	return true;
}

void AnimSlots::stepLoopFlags() {
	for (uint i = 0; i < kSlotCount; i++) {
		if (_slots[i].loop == 1)
			relaunch(i);
	}
}

void AnimSlots::setLoopFlag(uint slot, byte value) {
	if (slot < kSlotCount)
		_slots[slot].loop = value;
}

byte AnimSlots::loopFlag(uint slot) const {
	return slot < kSlotCount ? _slots[slot].loop : 0;
}

void AnimSlots::setHold(uint slot, bool value) {
	if (slot >= kSlotCount)
		return;
	_slots[slot].hold = value;
	if (!value)
		_slots[slot].heldFrame = 0;
}

bool AnimSlots::holdFlag(uint slot) const {
	return slot < kSlotCount ? _slots[slot].hold : false;
}

void AnimSlots::tick() {
	// MIDAS:0x1a28, one pass over every slot. A slot with no frames left is
	// stepped over, which is what leaves its last frame standing.
	for (uint i = 0; i < kSlotCount; i++) {
		Slot &slot = _slots[i];
		if (slot.remaining <= 0)
			continue;

		// The original compares the counter with the rate after incrementing it,
		// so a rate of zero advances on every tick.
		slot.tick++;
		if (slot.tick < slot.rate)
			continue;
		slot.tick = 0;

		const int before = slot.frame;
		slot.frame += slot.forward ? 1 : -1;
		slot.remaining--;

		// Mode 2 plays its range and then shows the frame it started on again.
		if (slot.restore && slot.remaining == 0)
			slot.frame = before;
	}
}

void AnimSlots::playAll(int rate) {
	for (uint i = 0; i < kSlotCount; i++) {
		const uint frames = _slots[i].bank.frameCount();
		if (frames)
			play(i, 1, (int)frames, rate, 1);
	}
}

bool AnimSlots::isLooping(uint slot) const {
	// Membership, not armed state: a slot the room ever loops is one whose
	// `remaining` never means "still playing". Armed, it is relaunched for as
	// long as the room stands and would keep isBusyOnce() true forever;
	// unarmed, it is parked on the last frame of a persisting play with
	// `remaining == 1`, which also never falls to zero. Neither is a one-shot
	// the harness should wait out, so both answer the same.
	//
	// stepLoops() is the one that wants loopArmed(). Threading the guard in
	// here as well would make a parked slot -- one whose guard is clear, so
	// nothing relaunches it -- count as busy for as long as the room stands,
	// and isBusyOnce() is what the scripted harness's `settle` waits on. No
	// leg has been seen to hang on it, because the rooms that guard a loop
	// park the slot with nothing left rather than on a held frame, but the
	// asymmetry is not worth carrying for that reason alone.
	for (uint i = 0; i < _loopCount; i++) {
		if (_loops[i].slot == slot)
			return true;
	}
	return false;
}

bool AnimSlots::isBusyOnce() const {
	for (uint i = 0; i < kSlotCount; i++) {
		if (_slots[i].remaining > 0 && !isLooping(i))
			return true;
	}
	return false;
}

bool AnimSlots::isBusy() const {
	for (uint i = 0; i < kSlotCount; i++) {
		if (_slots[i].remaining > 0)
			return true;
	}
	return false;
}

int AnimSlots::visibleFrame(const Slot &slot) const {
	// A slot playing a frame list steps a cursor, not a frame: what is drawn is
	// the list's entry for the cursor, and a zero there is the blank below the
	// bank's first frame -- the erase the list ends on where a scene takes
	// something away. The cursor is clamped to the list the way a frame is
	// clamped to its range.
	if (slot.frames) {
		const int last = slot.count > 0 ? slot.count - 1 : 0;
		return slot.frames[CLIP(slot.frame, 0, last)];
	}

	// The last advance leaves the frame one past the range, because the original
	// bakes the final frame into the background page on the tick before that and
	// never draws from the slot again. Clamping to the range shows the same
	// pixels out of the slot itself.
	const int lo = slot.forward ? slot.first : slot.first - slot.count + 1;
	const int hi = slot.forward ? slot.first + slot.count - 1 : slot.first;
	int frame = CLIP(slot.frame, lo, hi);

	// A range longer than the bank is a loop: the propeller in room 46 plays 33
	// frames of a bank of 10, which is three passes of the eleven the bank plus
	// its terminator make up, and the corridor's shutter in room 57 plays 53 the
	// same way. The frame wraps in that cycle rather than walking past the bank
	// into whatever was loaded after it.
	//
	// Frame 0 is a blank at the low end, the mirror of the terminator at the
	// high one, so the wrap is a plain modulo over 0..frameCount() with the sign
	// forced positive. Only mode 1 to 5 reach here: the modes that open on a
	// cursor of zero read their frame list above.
	//
	// A range that only just overruns its bank is not that: it is a single pass
	// over a file that ships fewer frames than it declares, and wrapping it puts
	// the animation's *first* frame back on screen at the end. Room 33 is where
	// that showed: TOW_GETU declares 99 frames, ships 97, and is played as 99 --
	// so the two frames past the terminator wrapped round to frame 1, leaving
	// Ben lying in the road under the Ben who had just stood up. Every single
	// pass in the game ends within a frame or two of the bank, and every real
	// loop (room 46's propeller, the axe in the maze) is three passes or more --
	// tools/check_anims.py sorts every range in the game by that same test -- so
	// the cycle is only entered when the range covers two of them or more. Below that the overrun runs off the end and draws
	// nothing, which is what the terminator does anyway.
	const int cycle = (int)slot.bank.frameCount() + 1;
	if (cycle > 1 && slot.count >= 2 * cycle && (frame > cycle || frame < 1))
		frame = ((frame % cycle) + cycle) % cycle;

	return frame;
}

void AnimSlots::draw(Graphics::Surface &dest, int scrollX, int clipBottom) const {
	for (uint i = 0; i < kSlotCount; i++) {
		const Slot &slot = _slots[i];
		if (!slot.started || !slot.bank.frameCount())
			continue;

		// A held slot never goes blank: whenever the ordinary rule below would
		// draw nothing for it, the last real frame it showed stands in instead
		// (see setHold()). Calling this does not move any state -- it only
		// decides what pixels this pass puts on screen.
		const auto drawHeld = [&]() {
			if (slot.hold && slot.heldFrame > 0)
				slot.bank.drawFrame((uint)(slot.heldFrame - 1), dest, scrollX, clipBottom);
		};

		// MIDAS:snd_func_1482 draws a slot while it still has frames to run,
		// and after that only if it is one of the modes that come back to their
		// first frame. Everything else is either already in the background page
		// (the modes that leave their last frame behind, which the port stands
		// in for by carrying on drawing it) or gone.
		if (slot.remaining <= 0 && !slot.restore && !slot.persist) {
			drawHeld();
			continue;
		}

		// A frame already stamped into the plate is drawn by the plate.
		if (slot.baked)
			continue;

		// Frames are numbered from one: room 10 shows the emptied matchbox with
		// frame 2 of a two-frame bank.
		const int frame = visibleFrame(slot) - 1;
		const int count = (int)slot.bank.frameCount();

		// One frame past the last is the terminator entry the DL1 header counts
		// but ships no strips for, and playing into it is how the game *removes*
		// something: the original restores the background under the frame it drew
		// last and then draws nothing. Seven ranges end there, and room 3 takes
		// the rope off the wall by showing nothing but the terminator.
		// The same holds one frame below the first: a range that opens on frame
		// 0 opens on a blank, which is how several of the per-frame-sound plays
		// start -- and, unheld, how a mode 8 talk cycle that ends its frame list
		// on 0 goes invisible between cycles (findings #67, #102).
		if (frame == count || frame == -1) {
			drawHeld();
			continue;
		}

		if (frame < 0 || frame > count) {
			debugC(1, kDebugGraphics, "anim: slot %u wants frame %d of %s, which "
				   "has %d", i, frame + 1, slot.name.c_str(), count);
			drawHeld();
			continue;
		}

		debugC(4, kDebugGraphics, "anim: slot %u draws frame %d of %s: %u strips, first %u",
			   i, frame + 1, slot.name.c_str(), slot.bank.stripCount((uint)frame),
			   slot.bank.firstStripAddr((uint)frame));
		slot.bank.drawFrame((uint)frame, dest, scrollX, clipBottom);
		if (slot.hold)
			slot.heldFrame = frame + 1;
	}
}

void AnimSlots::bake(Graphics::Surface &background, int clipBottom) {
	for (uint i = 0; i < kSlotCount; i++) {
		Slot &slot = _slots[i];
		if (!slot.started || slot.baked || !slot.persist || slot.remaining > 0)
			continue;
		if (!slot.bank.frameCount())
			continue;

		// A held slot (setHold()) is a character kept alive by draw()'s
		// fallback, not a piece of set dressing to freeze into the plate --
		// baking it would make later plays on the same slot invisible, since
		// a baked slot is never drawn from again. None of the port's current
		// callers combine hold with a persisting mode (1/3/6), but a future
		// one would silently lose the character here without this guard.
		if (slot.hold)
			continue;

		const int frame = visibleFrame(slot) - 1;
		const int count = (int)slot.bank.frameCount();

		// The terminator and the blank below the first frame are the two ends a
		// range can stop on with nothing to stamp; the erase they stand for has
		// already been done by the play that ran into them.
		if (frame >= 0 && frame < count)
			slot.bank.drawFrame((uint)frame, background, 0, clipBottom);

		slot.baked = true;
		debugC(2, kDebugGraphics, "anim: slot %u baked frame %d of %s into the "
			   "plate", i, frame + 1, slot.name.c_str());
	}
}

} // End of namespace Alien
