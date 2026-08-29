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

#ifndef ALIEN_CUTSCENES_H
#define ALIEN_CUTSCENES_H

#include "alien/roomscripts.h"

namespace Alien {

/**
 * The cutscenes, as tools/gen_cutscenes.py lifts them.
 *
 * A room does not name a scene: it raises a *scene id* with CUTSCENE:sub_0de60,
 * whose arm for that id latches a byte so the scene plays once, may hold a
 * puzzle-state guard, may set other flags, and calls the loader with one or two
 * record numbers -- which are not the id. One arm plays two records in a row and
 * one has no record at all (the TV news studio, whose filenames the original
 * hardcodes in its code segment instead).
 *
 * A record is 218 bytes of data segment 271a: the background, the dialog, the
 * music slot, the animation banks by slot, the two speaker anchors and colours,
 * eight far pointers to the scene's own code, and three more pointers holding
 * the scene's *step stream* and a list of dialog ids for each speaker. Those
 * procedures are written in the same effect vocabulary the room scripts are, so
 * they are lifted into ScriptEffect and run by the same interpreter rather than
 * by one of their own.
 *
 * The stream is what makes a scene a scene: a cursor walks it, and each step
 * either holds for a number of animation ticks, puts one speaker's next line up
 * at that speaker's anchor in that speaker's colour, or is a wordless beat. The
 * cursor moves on when the line on screen has been read or the hold has run out,
 * and the scene ends when it reaches the end of the stream. Four of the eight
 * procedures hang off the stream (one per speaker as a line starts, one per
 * speaker as it ends), one runs after every step, one runs once as the scene
 * loads and one once as it finishes.
 */

/** One step of a scene's stream. */
struct CutsceneStep {
	byte op;			///< kStepPause, kStepSpeakA, kStepSpeakB or kStepBeat
	byte arg;			///< a pause's length in animation ticks, else a dialog id
};

enum {
	kStepPause = 0,		///< hold for `arg` animation ticks
	kStepSpeakA = 1,	///< the first speaker says dialog `arg`
	kStepSpeakB = 2,	///< the second speaker says dialog `arg`
	kStepBeat = 0x14	///< run the beat procedure and move straight on
};

/** One lifted scene procedure. */
struct CutsceneProc {
	uint16 addr;		///< where it sat in segment 0c55, for the debug channel
	uint16 first;		///< first effect
	uint16 count;
};

/** One 218-byte scene record. */
struct CutsceneRecord {
	const char *pcx;		///< background plate
	const char *tal;		///< dialog script, or null for a silent scene
	int16 music;			///< music slot, -1 for none
	const char *banks[10];	///< DL1 banks by animation slot, null where unused
	uint16 mainProc;		///< run once, after the banks are in
	/**
	 * 0, 1: as the first / second speaker's line goes up
	 * 2, 3: as that line is about to come down
	 * 4: on a wordless beat
	 * 5: once, as the scene ends
	 * 6: after every step
	 */
	uint16 subProcs[7];
	int16 points[4];		///< two (x, y) speaker anchors
	byte colors[6];			///< two RGB triples, components 0..63
	uint16 firstStep;		///< the scene's step stream
	uint16 stepCount;
};

/** One arm of the scene-id dispatch. */
struct CutsceneArm {
	byte id;				///< the id a room raises
	uint16 latch;			///< the one-shot latch, or 0 for the arm without one
	byte guardCount;
	ScriptCond guards[2];
	byte recordCount;
	byte records[2];		///< the record numbers, in the order they are played
	uint16 first;			///< the arm's own flag effects
	uint16 count;
	bool studio;			///< the TV news studio, which has no record
};

/** The arm for one scene id, or null when no arm handles it. */
const CutsceneArm *cutsceneArm(byte id);

/** The arms in dispatch order, for the debug channel. */
uint cutsceneArmCount();
const CutsceneArm *cutsceneArmAt(uint index);

/** One record by the loader's own numbering, which runs 1..15. */
const CutsceneRecord *cutsceneRecord(uint number);
uint cutsceneRecordCount();

/** A record's step stream. */
const CutsceneStep *cutsceneSteps(const CutsceneRecord &record, uint &count);

/** One scene procedure's effects. Procedure 0 is the empty one. */
const ScriptEffect *cutsceneProcEffects(uint proc, uint &count);
uint cutsceneProcAddr(uint proc);
uint cutsceneProcCount();

/** The flag effects an arm runs besides its latch. */
const ScriptEffect *cutsceneArmEffects(const CutsceneArm &arm, uint &count);

} // End of namespace Alien

#endif
