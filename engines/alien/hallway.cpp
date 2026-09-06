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
#include "alien/resources.h"

namespace Alien {

// The look through the front door's glass: the scene the hallway plays the
// first time the player tries to open the door.
//
// It is a cutscene, but not one of the fifteen records. Like the TV news studio
// it is written out by hand in the CUTSCENE segment -- CUTSCENE:sub_0cfb0,
// which hardcodes its own filenames in the code segment (0c55:0a2c) instead of
// reading them out of a record: aliecon1.pcx, aliecon1.DL1 into slot 0,
// aliecon2.DL1 into slot 1, aliecon1.tal and music slot 0. The plate is the
// fisheye view through the door's glass with an alien standing on either side
// of it, and the two banks are those aliens.
//
// **What raises it.** Room 15's entry 3 opens with the dialog unit's own code
// (ovr_0f_0e5b:0x0017): while [0xa956] holds 0x4e2a -- which OBJ:0x5fbb writes
// when a line comes down -- and [0xacf6], the id the last queue_event was
// called with, is 15 or 16, the room sets [0xa6d7] and calls the scene. Those
// two ids are the door's own refusals, both guarded on [0xa6d7] being clear:
// 15 is the plain Open (ovr_0f_0e5b:0x0188, under verb 3), 16 is any item used
// on it (0x0086). Outcome 15 is the three lines "I'd rather n...", "..wait a
// minute...", "I think I heard someone talking outside..." -- the lead-in --
// so the scene answers the last of them.
//
// **And what it leaves behind.** [0xa6d7] = 1, which is what makes the door
// answer with the alien lines from then on rather than the lead-in, plus the
// scene's own latch [0x33a4]; then game_submode 0x6f and the room loop ends,
// which is submode 111, the link that names room 15 itself -- the room comes
// back through the transition chain rather than being redrawn in place. The
// latch is the hand-off to that re-entry: room 15's init reads it, puts the
// character back at the door and speaks outcome 20, then clears it.

static const int kHallway = 15;

/// The two ids the door queues, and the only two the hook answers.
static const byte kDoorRefusalOpen = 15;
static const byte kDoorRefusalItem = 16;

/// [0xa6d7]: he has seen what is on the other side of the door.
static const uint16 kSeenAliens = 0xa6d7;
/// [0x33a4]: the scene's own one-shot latch, in the latch block a save carries.
static const uint16 kPeepholeLatch = 0x33a4;

/// Submode 111, the link that names the room itself (transitions.h).
static const byte kSelfSubmode = 111;

static const char *const kPeepholePlate = "aliecon1.pcx";
static const char *const kPeepholeTal = "aliecon1.tal";
static const char *const kPeepholeBanks[] = { "aliecon1.DL1", "aliecon2.DL1" };
static const uint kPeepholeMusic = 0;

/// The scene's two speakers, one on each side of the glass ([0x98fe], 0x3c).
static const int kLeftAnchorX = 0x46, kRightAnchorX = 0xee, kAnchorY = 0x3c;

/// [0xad16] at the two points the scene tests it: when a line is due, and when
/// the scene is over. It counts animation ticks since the last line came down.
static const int kLineDue = 0x1e, kSceneEnd = 0x82;

/// Three turns of the two speakers, which is what [bp-3] counts up to.
static const int kExchanges = 3;

/// The talk cycle a speaker runs while his line is up, and the closing play the
/// scene leaves the right-hand alien on.
static const int kTalkFirst = 1, kTalkCount = 8, kTalkRate = 4;
static const int kExitFirst = 10, kExitCount = 9, kExitRate = 3;

/// How long before a line comes down the speakers close their mouths
/// ([0xad1e] == 0xf, ovr the scene loop at 0c55:0bd1).
static const int kMouthCloseTicks = 0xF;

/// The animation gate a finished slot is stamped into the plate under, the same
/// divider a room's tick bakes on.
static const uint kAnimTickMask = 3;

/**
 * The hallway's answer to a line coming down, as room 15's entry 3 makes it.
 *
 * The port has no [0xa956], so the shape is the same one turned inside out:
 * the tick calls this when the speech queue has just run dry, and the id the
 * queue was raised with stands in for [0xacf6].
 */
void AlienEngine::stepHallway() {
	if (_room != kHallway || _speech || _queueNext < _queueCount)
		return;
	if (_lastEvent != kDoorRefusalOpen && _lastEvent != kDoorRefusalItem)
		return;

	// Both arms that queue those two ids are guarded on [0xa6d7] being clear
	// (ovr_0f_0e5b:0x006a and 0x0167), so the scene is a one-shot. The guard is
	// repeated here because the port's lift of the item arm lost it -- the row
	// that queues 16 came out unguarded, so the door answers with it on a plain
	// Open as well, and without this the scene would play again on every click.
	if (_script.flag(kSeenAliens))
		return;

	// The original leaves [0xacf6] standing and relies on [0xa6d7] to keep the
	// door from queueing either id again; clearing it here says the same thing
	// without depending on that, since any other line would overwrite it.
	_lastEvent = 0;

	debugC(1, kDebugCutscene, "hallway: the door's refusal ended, looking through the glass");
	_script.setFlag(kSeenAliens, 1);

	playPeephole();

	// [0xa87e] = 0x6f with the room loop ended: the chain takes room 15 and
	// submode 111 back to room 15, which is what puts the room's plate, banks
	// and palette back -- the scene left all three holding its own.
	//
	// The room's own init is what puts the character down again, and the latch
	// the scene set is how it knows to: under [0x33a4] == 1 it places him at
	// 55,58 facing 4 -- back at the door, looking at it -- clears the latch and
	// speaks outcome 20 (ovr_0f_0e5b:0x066c, lifted into roominit.cpp). So the
	// latch is a hand-off and not a "seen it" flag, and it reads 0 again by the
	// time the room is up.
	takeExit(kSelfSubmode);
	CursorMan.showMouse(true);
}

/**
 * CUTSCENE:sub_0cfb0, the look through the glass.
 *
 * The scene has no step stream: its whole timeline is the idle counter
 * [0xad16], which OBJ:0x61b8 advances once per animation tick pair while no
 * line is on screen and OBJ:0x5fd8 zeroes as each line comes down. Thirty
 * ticks after the last line went, the next one goes up; a hundred and thirty
 * after the last of them, the scene ends. Everything else is bookkeeping the
 * loop does around that: which of the two speakers is talking, which anchor
 * his line is drawn at, and the talk cycle that runs on his slot while it is
 * up.
 */
void AlienEngine::playPeephole() {
	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kPeepholePlate), plate, palette)) {
		plate.free();
		warning("hallway: could not load the peephole plate %s", kPeepholePlate);
		return;
	}

	stopSpeech();

	CursorMan.showMouse(false);
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));

	_tal.load(Common::Path(kPeepholeTal));
	_speechTal = &_tal;
	_dialogBand = false;
	_anims.loadBanks(kPeepholeBanks, ARRAYSIZE(kPeepholeBanks));
	playMusicSlot(kPeepholeMusic);

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugCutscene, "hallway: %s, %s, music %u", kPeepholePlate, kPeepholeTal,
		   kPeepholeMusic);

	static const uint32 kTickMillis = 1000 / 70;
	uint32 last = g_system->getMillis();
	uint32 tick = 0;

	int idle = 0;			// [0xad16], the counter the whole scene is paced by
	int exchanges = 0;		// [bp-3]: how many turns the two have had
	int speaker = 1;		// [bp-2], which the first line's advance turns to 0
	uint next = 1;			// [bp-1], the dialog id the next line takes
	bool ended = false;		// [0xad1c]: a line has come down at least once
	bool talking = false;	// [0x9926]: the speaker's cycle is running
	bool skipped = false;

	while (!shouldQuit() && !_quit && !skipped) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			// The original ends the scene on both mouse buttons ([0xa900] and
			// [0xa901] together, 0c55:0c51); a keypress does it here too,
			// because a headless run has neither.
			if (event.type == Common::EVENT_KEYDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN)
				skipped = true;
		}

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
					// Both mouths close a moment before the line goes, which is
					// the scene's own reset play rather than anything the
					// dialog unit does.
					if (talking && _speechTicks <= kMouthCloseTicks) {
						_anims.play(0, 1, 1, 0, 1);
						_anims.play(1, 1, 1, 0, 1);
						talking = false;
					}

					if (--_speechTicks == 0) {
						stopSpeech();
						ended = true;
						idle = 0;
					}
				} else {
					idle++;
				}

				// The talk cycle repeats for as long as the line is up: the
				// scene re-issues the slot's own play the way a room's tick
				// does (MIDAS:snd_func_112d).
				if (talking)
					_anims.relaunch((uint)speaker);
			}

			// A line is due thirty ticks after the last one came down, and the
			// speakers alternate: the advance is what counts the exchanges.
			if (!_speech && idle == kLineDue && exchanges < kExchanges) {
				if (++speaker == 2) {
					speaker = 0;
					exchanges++;
				}

				const int anchorX = speaker == 1 ? kRightAnchorX : kLeftAnchorX;
				speakCutsceneLine(next++, anchorX, kAnchorY);
				_anims.play((uint)speaker, kTalkFirst, kTalkCount, kTalkRate, 1);
				talking = true;

				// Level 3 writes one frame per line, the only way to look at
				// the scene from a headless run -- what the record player's
				// step dumps do for a scene that has steps.
				if (debugChannelSet(3, kDebugCutscene)) {
					redraw();
					dumpScreen(Common::String::format("hallway-line-%u.png", next - 1));
				}
			}

			// Once they have finished, the right-hand alien holds the frame the
			// closing play starts on: the original re-issues it every pass, so
			// it never advances past its first frame.
			if (ended && exchanges == kExchanges)
				_anims.play(1, kExitFirst, kExitCount, kExitRate, 1);

			if (exchanges == kExchanges && idle == kSceneEnd)
				skipped = true;

			if ((tick & kAnimTickMask) == 0)
				_anims.bake(_background, _clipBottom);
		}

		if (_dirty)
			redraw();

		g_system->updateScreen();
		if (!_cutsceneFast)
			g_system->delayMillis(10);
	}

	debugC(1, kDebugCutscene, "hallway: the glass scene ran %u lines", next - 1);

	stopSpeech();
	_cutscene = false;
	_script.setFlag(kPeepholeLatch, 1);
	_dirty = true;

	// The room is not put back here: the original ends its loop instead and
	// lets the transition chain re-enter room 15 (see stepHallway). The scene's
	// music is left playing, its teardown at 0c55:0c65 having no stop of any
	// kind, and room 15 starts none of its own.
}

} // End of namespace Alien
