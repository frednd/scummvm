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

#ifndef ALIEN_PLATES_H
#define ALIEN_PLATES_H

#include "alien/roomscripts.h"

namespace Alien {

/**
 * How a room brings its background page up to date with the puzzle state.
 *
 * A room does not rebuild the plate from the flags it remembers, and its
 * overlay does not do it either: the last thing a room's enter routine does is
 * call, by address, a *per-room routine in the resident 10c9 unit*. Those
 * routines are one family of the same shape -- a chain of flag guards, and
 * under each one either a single frame of a slot written straight into the
 * background page (10c9:sub_1132a) or a play that leaves the slot running.
 *
 * The sewer's, `sub_117f5`, was the first one found (finding #69, the drain's
 * missing starter); this is the whole family, lifted by tools/gen_plates.py.
 * Without it a room re-entered draws the door it remembers as open shut, the
 * items it remembers taken back on their shelves, and the shelf it remembers
 * pushed aside still in the way.
 *
 * Two scratch words in the original, `[0x9926]` and `[0x98fe]`, serve as one
 * select: a run of guards writes a slot or a frame number into it and the call
 * that follows reads it back. That is `kPlateSel`, and `kPlateSelect` is what
 * an argument that reads it holds.
 */
enum PlateOp {
	kPlateStamp = 0,	///< args: slot, frame -- one frame into the plate
	kPlatePlay,			///< args: slot, first, count, rate, mode
	kPlateSel,			///< args: the value the select takes
	kPlateFlag,			///< args: state address, value (the slot loop flags)
	kPlateMusic			///< args: the music slot INPUT:music_play_slot starts
};

/// What an argument holds where the original reads the select back.
static const uint16 kPlateSelect = 0xff;

/** One step of a room's plate routine, with the guards it sits under. */
struct PlateStep {
	byte room;
	byte op;			///< a PlateOp
	uint16 args[5];

	uint16 firstGuard;	///< into the table's puzzle-state guards
	byte guardCount;
};

/// The steps of one room's plate routine, in the order the original runs them.
const PlateStep *plateSteps(int room, uint &count);

/// The guards one step sits under; all of them have to hold for it to run.
const ScriptCond *plateGuards(const PlateStep &step, uint &count);

uint plateStepCount();

} // End of namespace Alien

#endif
