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
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/cutscenes.h"
#include "alien/resources.h"

namespace Alien {

/// The pause step's argument is in animation ticks, the same divider the slots run on.
static const uint kAnimTickMask = 3;

/// [0xa6d3]: a scene has just played, which every teardown in CUTSCENE raises
/// and room 26 is the only reader of.
static const uint16 kScenePlayed = 0xa6d3;

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

	// The scene's own state, cleared before anything is loaded exactly as the
	// original clears it: the beat counter, the state word one of the boss
	// scenes switches on, and the clock the procedures wait against.
	_script.resetScene();

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

	// A scene owns the screen the way a clip does, and the original clears
	// cursor_visible [0xa948] for the length of one.
	CursorMan.showMouse(false);
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));

	if (rec->tal)
		_tal.load(Common::Path(rec->tal), &_pack);
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
	uint16 streamPos = 0;	// [0xa498]: the cursor in bytes, not in steps
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

				// MIDAS:sub_18ee5, which the scene loop calls right behind the
				// stepper: a scene marks the slot it wants repeated with the
				// loop flag rather than spelling the relaunch out the way a
				// room's tick does, and without this every talk cycle played
				// once and then left its speaker on the frame the range came
				// back to -- which for the mode 8 cycles is frame zero, so the
				// character was not merely still, it was gone.
				_anims.stepLoopFlags();

				// Level 4: what every loaded slot is showing on this tick, which
				// is the only way to see a cycle blink from a headless run.
				// Level 5 writes one frame per animation tick -- thousands of
				// files for a sweep, and the only way to watch a scene move
				// from a headless run. Level 4 below is the cheap version: what
				// every loaded slot holds on this tick.
				if (debugChannelSet(5, kDebugCutscene)) {
					redraw();
					dumpScreen(Common::String::format("tick-%u-%04u.png", number,
													  (uint)tick));
				}

				if (debugChannelSet(4, kDebugCutscene)) {
					Common::String line;
					for (uint s = 0; s < AnimSlots::kSlotCount; s++) {
						if (_anims.bankName(s).empty())
							continue;
						line += Common::String::format(" %u:%d/%d", s, _anims.frame(s),
													   _anims.remaining(s));
					}
					debugC(4, kDebugCutscene, "cutscene %u: tick %u%s", number,
						   (uint)tick, line.c_str());
				}

				// The clock the procedures are measured against, stepped on the
				// same tick the original steps it on (0c55:16d7, under
				// anim_frame_due).
				_script.setCutscenePos(_script.cutscenePos() + 1);

				if (_speech && _speechTicks > 0) {
					// The line's own procedure runs as it is about to come down,
					// which is how a speaker stops moving on the last word
					// rather than when the next one starts. The original guards
					// it on [0xacf8] as well as on the countdown (0c55:1738),
					// and [0xacf8] is raised where a line is popped only when
					// it is the last of its chain (0251:3c46) -- so a speaker
					// whose step carries several entries keeps his mouth going
					// between them.
					if (speaking && !tailRun && _speechTicks <= kLineTailTicks &&
						_queueNext >= _queueCount) {
						runCutsceneProc(rec->subProcs[speaker == 0 ? 2 : 3]);
						tailRun = true;
					}

					if (--_speechTicks == 0) {
						// The rest of the step's chain first; the stream only
						// moves on once the chain is spent.
						if (nextCutsceneLine()) {
							tailRun = false;
						} else {
							speaking = false;
							advance = true;
						}
					}
				}
			}

			if ((tick & kAnimTickMask) == 0 && hold > 0 && --hold == 0)
				advance = true;

			// A finished slot that leaves its frame behind goes into the plate
			// here too: the scene loop calls the same drawer a room's does, and
			// the drawer is where the stamp happens (see AnimSlots::bake).
			_anims.bake(_background, _clipBottom);
		}

		bool entered = false;	// a step was entered this pass, so the screen moved
		while (advance && !skipped) {
			advance = false;
			entered = true;

			if (cursor >= stepCount) {
				skipped = true;
				break;
			}

			// [0xa498], which the original walks through the stream a byte at a
			// time: a scene procedure reads it to tell which line of the scene
			// it is running for, so it has to hold the offset of the step being
			// entered while that step's procedures run.
			_script.setScenePos(streamPos);
			const CutsceneStep &step = steps[cursor++];
			streamPos += step.op == kStepPause ? 2 : 1;
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
				// The record's own colour for this speaker, and then the entry
				// itself: a scene never re-uploads its palette, so writing the
				// port's copy alone left every line in whatever colour the
				// plate happened to put at index 65.
				setTextColor(rec->colors[speaker * 3 + 0], rec->colors[speaker * 3 + 1],
							 rec->colors[speaker * 3 + 2]);
				uploadTextColor();
				speakCutsceneLine(step.arg, rec->points[speaker * 2 + 0],
								  rec->points[speaker * 2 + 1]);
				speaking = true;
				tailRun = false;
				hold = 0;
				break;

			case kStepBeat: {
				// A wordless beat: it runs its procedure and the stream moves on
				// with nothing to wait for. The beat counter is what tells that
				// one procedure which beat this is -- the original steps it
				// after the call, so the first beat sees zero -- and dropping it
				// made every beat of a scene run every arm of its procedure at
				// once.
				runCutsceneProc(rec->subProcs[4]);
				const byte beats = _script.flag(RoomScript::kSceneBase);
				_script.setFlag(RoomScript::kSceneBase, (byte)(beats + 1));
				debugC(2, kDebugCutscene, "cutscene %u: beat %d", number, beats);
				advance = true;
				break;
			}

			default:
				warning("cutscene %u: step %u has opcode %d", number, cursor - 1, step.op);
				advance = true;
				break;
			}
		}

		// The seventh procedure is not a per-step one: the original calls it on
		// every pass of the scene loop, past the step block and whether or not a
		// step was entered (0c55:18b3). Only two records carry one, and both are
		// timelines that wait on the clock rather than on the stream.
		runCutsceneProc(rec->subProcs[6], true);

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

	// Every teardown in CUTSCENE raises [0xa6d3] -- 0c55:0086, 0x09df, 0x0c6f,
	// 0x0e1b and 0x18f8 all end with it -- and room 26's own code is the one
	// that reads it, taking a branch of its own on the first tick after a scene
	// and clearing it again (ovr_1a_0eaf:0x069f).
	_script.setFlag(kScenePlayed, 1);

	if (room > 0 && loadRoom(room)) {
		_ben.place(benX, benY, benFacing);

		// The room is being put back, not entered: the original reaches its
		// triggers from the enter routine the scene was raised inside, and that
		// routine is not run again on the way back. Leaving the scan armed here
		// loops any scene whose arm has no latch of its own -- room 35's scene 4
		// is one, and its guards are the transition globals, which the hand-back
		// does not change, so it would raise itself for as long as the room is
		// open.
		_pendingCutscenes = false;
	}
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
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

/**
 * One of a record's eight procedures, or nothing for the empty slot 0.
 *
 * `quiet` is for the one the loop runs on every pass rather than on a step:
 * how many times that is depends on the clock, so printing it would say
 * something tools/check_cutscenes.py cannot mirror. What the procedure does is
 * still visible -- its effects run through the same interpreter as any other.
 */
void AlienEngine::runCutsceneProc(uint proc, bool quiet) {
	uint count = 0;
	const ScriptEffect *effects = cutsceneProcEffects(proc, count);
	if (!count)
		return;

	if (!quiet)
		debugC(2, kDebugCutscene, "cutscene: proc %u (0c55:%04x), %u effects", proc,
			   cutsceneProcAddr(proc), count);
	_script.runEffects(effects, count);
}

/**
 * Puts one speaker step's dialog up at that speaker's own anchor.
 *
 * The number a speaker step carries is an outcome code, not a dialog id. The
 * call the step makes, DIALOG:sub_0b63a (0c55:1847 for the first speaker and
 * 0c55:1893 for the second), is the loader queue_event is: it reads the 11-byte
 * zone 1 record at code*11 and memmoves that record's ids into the click queue
 * at 0x3370, so one step can carry up to ten entries. Taking the code for an
 * entry index was wrong twice over -- MW11's b:20 is entry 30, not entry 20 --
 * and it cut every chain to its first sentence, which is why MW1's "Sorry
 * Boss." lost the two lines that finish it and the entries past the last code
 * were never said at all.
 */
void AlienEngine::speakCutsceneLine(uint code, int anchorX, int anchorY) {
	const TalFile::Outcome &chain = _tal.outcome(code);

	_speechTal = &_tal;
	// [0xacf6], which the loader writes before it does anything else.
	_lastEvent = (byte)code;
	_queueCount = MIN<uint>(chain.count, TalFile::kMaxOutcomeIds);
	_queueNext = 0;
	for (uint i = 0; i < _queueCount; i++)
		_queue[i] = chain.ids[i];

	_speechX = anchorX;
	_speechY = anchorY;
	nextCutsceneLine();
}

/**
 * The next id of the chain a speaker step raised, false once it is spent.
 *
 * OBJ:sub_06140 pops one id per line and OBJ:sub_08486 counts the chain down in
 * [0xad14], raising the pulse a scene advances on only when the last of them
 * has come down (0251:5f9a and 0251:5fbb) -- so a step is not over until its
 * whole chain is, and the loop stays on it meanwhile.
 */
bool AlienEngine::nextCutsceneLine() {
	if (_queueNext >= _queueCount) {
		stopSpeech();
		return false;
	}

	const uint id = _queue[_queueNext++];
	const TalFile::Entry &entry = _speechTal->entry(id);

	_dialogId = id;
	_speechCustom = false;
	// An id whose slot holds no text still takes its turn: it is held for a tick
	// and stepped over, rather than stopping the scene on an empty line.
	_speech = true;
	_speechTicks = entry.lines.empty()
		? tunable("cutscene.emptyLineTicks", kEmptyLineTicks)
		: speechTicksFor(*_speechTal, id);
	// A scene's own colours are the record's, and they have already been
	// uploaded for this speaker; a row that names one for this line wins.
	applyTextOverride(_speechTal->textOverride(id));
	_dirty = true;

	debugC(2, kDebugCutscene, "cutscene: line %u at %d,%d, %u lines, %d ticks", id,
		   _speechX, _speechY, entry.lines.size(), _speechTicks);
	return true;
}

} // End of namespace Alien
