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

#include "common/events.h"
#include "common/system.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/cutscenes.h"
#include "alien/resources.h"

namespace Alien {

/// The pause step's argument is in animation ticks, the same divider the slots run on.
static const uint kAnimTickMask = 3;

/// How long before a line comes down the "line ending" procedure runs, in half ticks.
static const int kLineTailTicks = 0xF;

/// A line with no text in the file is stepped over rather than held on screen.
static const int kEmptyLineTicks = 1;

/**
 * The three elapsed-time scenes, as OBJ:sub_029ac counts them and
 * CUTSCENE:sub_0dfda decides them. Each counter is a long in the latch block,
 * with its enable byte two in front of it.
 */
static const struct {
	uint16 enable;
	uint16 counter;		///< low word; the high word follows it
	uint32 limit;		///< in animation ticks: about four, fourteen and sixteen minutes
	byte scene;
	bool notInRoom21;	///< the original tests handler_code against 0x15
} kTimers[] = {
	{ 0x33c2, 0x33c4, 0x1068, 1, true },
	{ 0x33c8, 0x33ca, 0x396c, 11, false },
	{ 0x33ce, 0x33d0, 0x41a0, 7, false }
};

/**
 * Raise a scene id, the way a room's own code does.
 *
 * The id is not a record number: the arm for it (see cutscenes.h) carries the
 * one-shot latch that keeps the scene from playing twice, whatever puzzle-state
 * guard the original put on it, flag stores of its own, and the one or two
 * records that actually play. Returns true when a scene played.
 */
bool AlienEngine::triggerCutscene(byte id) {
	const CutsceneArm *arm = cutsceneArm(id);
	if (!arm) {
		debugC(1, kDebugCutscene, "scene %d: no arm in the dispatch", id);
		return false;
	}

	for (uint g = 0; g < arm->guardCount; g++) {
		const ScriptCond &cond = arm->guards[g];
		if (!_script.condHolds(cond)) {
			debugC(1, kDebugCutscene, "scene %d: guard [0x%04x] == %d does not hold",
				   id, cond.addr, cond.value);
			return false;
		}
	}

	// The latch is the byte the arm tests against zero and then sets; the one arm
	// without one (the TV news studio) is meant to be watched again and again.
	if (arm->latch) {
		if (_script.flag(arm->latch)) {
			debugC(1, kDebugCutscene, "scene %d: latch 0x%04x already set", id, arm->latch);
			return false;
		}
		_script.setFlag(arm->latch, 1);
	}

	uint count = 0;
	const ScriptEffect *effects = cutsceneArmEffects(*arm, count);
	_script.runEffects(effects, count);

	if (arm->studio) {
		// CUTSCENE:sub_0c6dd, which names its own files in code and drives itself
		// off cutscene_pos rather than off a record's step stream: 709 lines of
		// hand-written timeline calling OBJ:obj_action_a..d, none of which the
		// port has lifted. It is a milestone of its own, not part of the tables.
		warning("scene %d: the TV news studio is not ported yet", id);
		return false;
	}

	for (uint r = 0; r < arm->recordCount; r++)
		playCutsceneRecord(arm->records[r]);

	return true;
}

/**
 * The scenes a room raises as it opens, in the order its own enter routine does.
 *
 * The original runs these from inside the routine that loads the room's banks,
 * before the room is finished; the port runs them once the room is up, because
 * a scene plays over the room -- own plate, own dialog, own banks -- and hands
 * it back at the end, so what the room looked like while the scene loaded is
 * not observable. The guards are the overlay's own, and are answered against
 * the same state a script guard is (see RoomScript::holds): a puzzle flag, the
 * transition globals, or an item being carried.
 */
void AlienEngine::roomCutscenes(int room) {
	// CUTSCENE:sub_0e042, which every enter routine calls first: the three
	// elapsed-time scenes get their chance before the room's own.
	stepCutsceneTimers();

	uint count = 0;
	const CutsceneTrigger *triggers = cutsceneTriggers(room, count);
	for (uint i = 0; i < count; i++) {
		const CutsceneTrigger &trigger = triggers[i];

		bool guarded = true;
		for (uint g = 0; g < trigger.guardCount; g++)
			guarded = guarded && _script.condHolds(trigger.guards[g]);

		debugC(1, kDebugCutscene, "raise room %2d scene %2d: %s",
			   room, trigger.scene, guarded ? "raised" : "guard does not hold");
		if (guarded)
			triggerCutscene(trigger.scene);
	}
}

/**
 * The three elapsed-time scenes, as CUTSCENE:sub_0dfda decides them.
 *
 * Each is a 32-bit counter in the latch block with an enable byte of its own,
 * and each fires its scene once the counter passes a threshold: 4200 animation
 * ticks (about four minutes), 14700 and 16800. The first is also suppressed
 * while the player is in room 21 -- the original compares handler_code against
 * 0x15 -- which is the room whose own overlay starts that timer running.
 *
 * The third timer has no writer anywhere in the game: nothing sets [0x33ce], so
 * its scene can never come up. That is the original's behaviour, not a gap here.
 */
void AlienEngine::stepCutsceneTimers() {
	for (uint i = 0; i < ARRAYSIZE(kTimers); i++) {
		if (kTimers[i].notInRoom21 && _room == 21)
			continue;
		if (cutsceneTimer(kTimers[i].counter) <= kTimers[i].limit)
			continue;
		debugC(1, kDebugCutscene, "timer 0x%04x past %u: scene %d",
			   kTimers[i].counter, kTimers[i].limit, kTimers[i].scene);
		triggerCutscene(kTimers[i].scene);
	}
}

/** One idle timer's count, the word pair the original compares as a long. */
uint32 AlienEngine::cutsceneTimer(uint16 addr) const {
	const uint32 low = _script.flag(addr) | ((uint32)_script.flag(addr + 1) << 8);
	const uint32 high = _script.flag(addr + 2) | ((uint32)_script.flag(addr + 3) << 8);
	return (high << 16) | low;
}

/**
 * The idle timers, advanced on the animation tick pair as OBJ:sub_029ac does.
 *
 * They live in the latch block, so a save carries them the way the original's
 * does, and each only runs while its own enable byte is set.
 */
void AlienEngine::tickCutsceneTimers() {
	for (uint i = 0; i < ARRAYSIZE(kTimers); i++) {
		if (_script.flag(kTimers[i].enable) != 1)
			continue;
		const uint32 next = cutsceneTimer(kTimers[i].counter) + 1;
		for (uint b = 0; b < 4; b++)
			_script.setFlag(kTimers[i].counter + b, (byte)(next >> (8 * b)));
	}
}

/**
 * One scene, as CUTSCENE:sub_0d962 plays it.
 *
 * The original freezes the room -- it turns the room's own tick off, drops the
 * scroll and hides the character -- loads the record's plate, dialog file and
 * animation banks over the room's, runs the record's opening procedure once, and
 * then walks the step stream until it runs out. This blocks, the way the video
 * player does, because everything the main loop would do belongs to the room the
 * scene interrupted.
 */
void AlienEngine::playCutsceneRecord(uint number) {
	const CutsceneRecord *rec = cutsceneRecord(number);
	if (!rec) {
		warning("cutscene: no record %u", number);
		return;
	}

	uint stepCount = 0;
	const CutsceneStep *steps = cutsceneSteps(*rec, stepCount);

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(rec->pcx), plate, palette)) {
		plate.free();
		warning("cutscene %u: could not load the plate %s", number, rec->pcx);
		return;
	}

	// Where the character was standing, so the room he is put back into does not
	// also move him: loadRoom() places him on its first walk node.
	const int room = _room;
	const int benX = _ben.walkX();
	const int benY = _ben.walkY();
	const int benFacing = _ben.facing();
	stopSpeech();

	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));

	if (rec->tal)
		_tal.load(Common::Path(rec->tal));
	else
		_tal.unload();
	_speechTal = &_tal;
	_dialogBand = false;

	_anims.loadBanks(rec->banks, ARRAYSIZE(rec->banks));

	debugC(1, kDebugCutscene, "cutscene %u: %s, %s, music %d, %u steps", number, rec->pcx,
		   rec->tal ? rec->tal : "no dialog", rec->music, stepCount);

	if (rec->music >= 0)
		playMusicSlot((uint)rec->music);

	// The opening procedure runs once the banks are in, which is where the
	// original calls it: from the tail of its bank-loading loop.
	runCutsceneProc(rec->mainProc);

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	static const uint32 kTickMillis = 1000 / 70;
	uint32 last = g_system->getMillis();
	uint32 tick = 0;

	uint cursor = 0;
	int hold = 0;			// animation ticks left on a pause
	bool advance = true;	// the first step is entered as the scene opens
	bool speaking = false;	// a speaker step is on screen, so its tail is still to run
	bool tailRun = false;	// and whether that tail has run
	int speaker = 0;		// which of the two the line on screen belongs to
	bool skipped = false;

	while (!shouldQuit() && !_quit && !skipped) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			// The original ends a scene early on both mouse buttons at once and
			// takes a click on a line as "read"; a keypress does both here
			// because a headless run has neither.
			if (event.type == Common::EVENT_KEYDOWN) {
				if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
					skipped = true;
				else
					advance = true;
			} else if (event.type == Common::EVENT_LBUTTONDOWN) {
				advance = true;
			} else if (event.type == Common::EVENT_RBUTTONDOWN) {
				skipped = true;
			}
		}

		// The sweep runs the same loop with the clock out of it: one tick per
		// pass, so holds and lines expire as fast as the steps can be walked.
		const uint32 now = g_system->getMillis();
		if (_cutsceneFast || now - last >= kTickMillis) {
			last = now;
			tick++;

			if ((tick & 1) == 0) {
				if (_anims.isBusy()) {
					_anims.tick();
					_dirty = true;
				}

				if (_speech && _speechTicks > 0) {
					// The line's own procedure runs as it is about to come down,
					// which is how a speaker stops moving on the last word
					// rather than when the next one starts.
					if (speaking && !tailRun && _speechTicks <= kLineTailTicks) {
						runCutsceneProc(rec->subProcs[speaker == 0 ? 2 : 3]);
						tailRun = true;
					}

					if (--_speechTicks == 0) {
						stopSpeech();
						speaking = false;
						advance = true;
					}
				}
			}

			if ((tick & kAnimTickMask) == 0 && hold > 0 && --hold == 0)
				advance = true;
		}

		bool entered = false;	// a step was entered this pass, so the screen moved
		while (advance && !skipped) {
			advance = false;
			entered = true;

			if (cursor >= stepCount) {
				skipped = true;
				break;
			}

			const CutsceneStep &step = steps[cursor++];
			switch (step.op) {
			case kStepPause:
				stopSpeech();
				speaking = false;
				hold = step.arg;
				debugC(2, kDebugCutscene, "cutscene %u: pause %d", number, step.arg);
				break;

			case kStepSpeakA:
			case kStepSpeakB:
				speaker = step.op == kStepSpeakA ? 0 : 1;
				runCutsceneProc(rec->subProcs[speaker]);
				setTextColor(rec->colors[speaker * 3 + 0], rec->colors[speaker * 3 + 1],
							 rec->colors[speaker * 3 + 2]);
				speakCutsceneLine(step.arg, rec->points[speaker * 2 + 0],
								  rec->points[speaker * 2 + 1]);
				speaking = true;
				tailRun = false;
				hold = 0;
				break;

			case kStepBeat:
				// A wordless beat: it runs its procedure and the stream moves on
				// with nothing to wait for.
				runCutsceneProc(rec->subProcs[4]);
				debugC(2, kDebugCutscene, "cutscene %u: beat", number);
				advance = true;
				break;

			default:
				warning("cutscene %u: step %u has opcode %d", number, cursor - 1, step.op);
				advance = true;
				break;
			}

			// And the per-step procedure, which the original calls whatever the
			// step was.
			runCutsceneProc(rec->subProcs[6]);
		}

		if (_dirty)
			redraw();

		// Level 3 writes one frame per step, which is the only way to look at a
		// scene from a headless run.
		if (entered && debugChannelSet(3, kDebugCutscene))
			dumpScreen(Common::String::format("cutscene-%u-step-%u.png", number,
											  cursor ? cursor - 1 : 0));

		g_system->updateScreen();
		if (!_cutsceneFast)
			g_system->delayMillis(10);
	}

	// The closing procedure, then the room the scene interrupted takes the
	// screen back. Reloading it is what puts its plate, banks, palette and
	// script state back in place; the scene left all four holding its own.
	runCutsceneProc(rec->subProcs[5]);

	stopSpeech();
	stopMusic();
	_cutscene = false;

	if (room > 0 && loadRoom(room))
		_ben.place(benX, benY, benFacing);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;
}

/**
 * Play every record end to end, for the mirror.
 *
 * No scripted run raises a scene id yet -- the per-room triggers are the half of
 * the cutscene work still open -- so the channel plays the records itself, in
 * their own numbering. `_cutsceneFast` only takes the waits out: the steps, the
 * procedures and the lines are the ones a triggered scene would run, and they
 * print the same lines tools/check_cutscenes.py --play prints from the files.
 */
void AlienEngine::sweepCutscenes() {
	_cutsceneFast = true;
	for (uint n = 1; n <= cutsceneRecordCount() && !shouldQuit() && !_quit; n++)
		playCutsceneRecord(n);
	_cutsceneFast = false;
}

/** One of a record's eight procedures, or nothing for the empty slot 0. */
void AlienEngine::runCutsceneProc(uint proc) {
	uint count = 0;
	const ScriptEffect *effects = cutsceneProcEffects(proc, count);
	if (!count)
		return;

	debugC(2, kDebugCutscene, "cutscene: proc %u (0c55:%04x), %u effects", proc,
		   cutsceneProcAddr(proc), count);
	_script.runEffects(effects, count);
}

/** Puts one dialog id up at a speaker's own anchor, for as long as it reads. */
void AlienEngine::speakCutsceneLine(uint id, int anchorX, int anchorY) {
	const TalFile::Entry &entry = _tal.entry(id);

	uint length = 0;
	for (uint i = 0; i < entry.lines.size(); i++)
		length += entry.lines[i].size();

	_dialogId = id;
	_speechTal = &_tal;
	// An id whose slot holds no text still takes a step: it is held for a tick
	// and stepped over, rather than stopping the scene on an empty line.
	_speech = true;
	_speechTicks = entry.lines.empty() ? kEmptyLineTicks
									   : MAX<int>((int)length * kTicksPerCharacter,
												  kMinSpeechTicks);
	_speechX = anchorX;
	_speechY = anchorY;
	_dirty = true;

	debugC(2, kDebugCutscene, "cutscene: line %u at %d,%d, %u lines, %d ticks", id,
		   anchorX, anchorY, entry.lines.size(), _speechTicks);
}

} // End of namespace Alien
