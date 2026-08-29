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
		const bool holds = (_script.flag(cond.addr) == cond.value) != cond.negate;
		if (!holds) {
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
		// off cutscene_pos rather than off a record's step stream.
		warning("scene %d: the TV news studio is not ported yet", id);
		return false;
	}

	for (uint r = 0; r < arm->recordCount; r++)
		playCutsceneRecord(arm->records[r]);

	return true;
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

		const uint32 now = g_system->getMillis();
		if (now - last >= kTickMillis) {
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
