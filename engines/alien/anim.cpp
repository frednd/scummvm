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
	mode = 0;
}

AnimSlots::AnimSlots() : _room(0) {
}

void AnimSlots::reset() {
	for (uint i = 0; i < kSlotCount; i++) {
		const bool loaded = _slots[i].started;
		_slots[i].clear();
		if (loaded)
			debugC(2, kDebugGraphics, "anim: slot %u cleared", i);
	}
}

void AnimSlots::loadRoom(int room) {
	for (uint i = 0; i < kSlotCount; i++) {
		_slots[i].clear();
		_slots[i].bank.unload();
		_slots[i].name.clear();
	}

	_room = room;

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

		// Room 7 stores a stem plus a bare ".DL1" because it appends a digit at
		// runtime, and two rooms load a second bank over the first from inside a
		// branch the table does not carry. Neither can be resolved here.
		Common::String name(banks[i].name);
		if (name.size() < 5 || name[0] == '.') {
			debugC(1, kDebugResource, "room %d slot %u: bank name %s is built at "
				   "runtime", room, banks[i].slot, name.c_str());
			continue;
		}

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

void AnimSlots::play(uint slot, int first, int count, int rate, int mode) {
	if (slot >= kSlotCount) {
		debugC(1, kDebugGraphics, "anim: slot %u is past the end", slot);
		return;
	}

	Slot &s = _slots[slot];
	s.started = true;
	s.mode = (byte)mode;
	s.frame = first;
	s.first = first;
	s.count = count;
	s.remaining = count;
	s.rate = rate;
	s.tick = 0;
	s.forward = mode != 3;
	s.restore = mode == 2;

	debugC(2, kDebugGraphics, "anim: slot %u plays %s frames %d..%d rate %d "
		   "(mode %d)", slot, s.name.empty() ? "<no bank>" : s.name.c_str(),
		   first, s.forward ? first + count - 1 : first - count + 1, rate, mode);
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

bool AnimSlots::isBusy() const {
	for (uint i = 0; i < kSlotCount; i++) {
		if (_slots[i].remaining > 0)
			return true;
	}
	return false;
}

int AnimSlots::visibleFrame(const Slot &slot) const {
	// The last advance leaves the frame one past the range, because the original
	// bakes the final frame into the background page on the tick before that and
	// never draws from the slot again. Clamping to the range shows the same
	// pixels out of the slot itself.
	const int lo = slot.forward ? slot.first : slot.first - slot.count + 1;
	const int hi = slot.forward ? slot.first + slot.count - 1 : slot.first;
	int frame = CLIP(slot.frame, lo, hi);

	// A range longer than the bank is a loop: the propeller in room 46 plays 33
	// frames of a bank of 10, which is three passes of the eleven the bank plus
	// its terminator make up, and the fire in room 11 and the candle in room 26
	// are the same shape. The frame wraps in that cycle rather than walking past
	// the bank into whatever was loaded after it.
	const int cycle = (int)slot.bank.frameCount() + 1;
	if (cycle > 1 && frame > cycle)
		frame = (frame - 1) % cycle + 1;

	return frame;
}

void AnimSlots::draw(Graphics::Surface &dest) const {
	for (uint i = 0; i < kSlotCount; i++) {
		const Slot &slot = _slots[i];
		if (!slot.started || !slot.bank.frameCount())
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
		if (frame == count)
			continue;

		if (frame < 0 || frame > count) {
			debugC(1, kDebugGraphics, "anim: slot %u wants frame %d of %s, which "
				   "has %d", i, frame + 1, slot.name.c_str(), count);
			continue;
		}

		slot.bank.drawFrame((uint)frame, dest);
	}
}

} // End of namespace Alien
