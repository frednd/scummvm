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
#include "alien/play.h"
#include "alien/resources.h"

namespace Alien {

// The lab computer, and the lift it drives.
//
// The bedroom closet is a lift car, and which floor the car is parked on is
// [0xa700]. Walking into the closet is room 7's object 32, and that rectangle
// registers only under [0xa6fa] == 1, [0xa6fe] == 0 *and* [0xa700] == 0
// (hotspots.cpp); with the car in the basement the same rectangle is object 31,
// which walks Ben into the closet and no further. So the shaft is gated on
// [0xa700], and no room script anywhere writes it -- its only writers are room
// 13's own overlay, MAIN (1, at new game) and the panel ported here.
//
// The panel is reached from the lab, in three hops the lifted tables carry none
// of:
//
//   1. The floppy goes into the floppy toaster (room 3 obj 2 under item 14),
//      which is the one script body in the chain: it sets [0xa6e1].
//   2. The computer is used (room 3 obj 1, with the password note or on its
//      own). Room 3 answers that in code rather than in a body --
//      ovr_03_0e57:0x0000, called from both halves of entry 3's dispatch, the
//      item-use path at 0x00cf and the plain-verb one at 0x0230. With [0xa6e1]
//      set it raises event 0x23, or 0x28 once [0xa701] says the panel has been
//      seen, and takes the cursor away; with the drive empty it raises 0x1b
//      instead and nothing further happens.
//   3. When that line comes down -- [0xa956] == 0x4e2a with [0xacf6] still
//      0x23 or 0x28 -- entry 3 gives the cursor back and calls 15f3:sub_15fe9,
//      the panel itself (0x0083-0x0097). It then sets [0xa701] and leaves by
//      submode 0x6f, which the transition chain routes room 3 -> room 3: the
//      lab is simply reloaded underneath.
//
// sub_15fe9 is a modal screen of its own, like a scene or a clip: it turns the
// room's tick off, owns the framebuffer, and runs its own event loop. Two
// phases. The first is a boot sequence -- 64scr1.pcx under 64INIT.DL1 for
// seventy animation ticks. The second is the console proper: 64SCR2.pcx with
// COMPELE1.DL1 (the shaft, whose fourteen frames are the car moving),
// COMPELE2.DL1 (the button panel and its indicator) and 64CURSOR.DL1 (the
// blinking prompt) in slots 0, 1 and 2.
//
// Three rectangles answer a click, in cursor coordinates, bounds exclusive as
// the original's `jle`/`jge` pairs are:
//
//   0xb7,0x2f .. 0x100,0x50   call the car up   -> case 1 when [0xa700] == 1
//   0xb7,0x7f .. 0x100,0xa0   send the car down -> case 2 when [0xa700] == 0
//   0x0a,0xa4 .. 0x3c,0xc8    quit              -> case 0xff
//
// A button that is asked for the floor the car is already on gives case 5, the
// refusal. Every hit plays the press flash and then waits 0x64 animation ticks
// before the case runs, which is the console "thinking"; nothing else is
// accepted while that countdown, the refusal hold or a pending case is standing.
static const int kLabRoom = 3;
static const byte kComputer = 1;			///< room 3's object 1
static const uint16 kTerminalUsed = 0xa6e1;	///< the floppy is in the toaster
static const uint16 kPanelSeen = 0xa701;	///< [0xa701], set once the panel has run
static const uint16 kCarFloor = 0xa700;		///< 0 = the car is at the bedroom

static const byte kLineNoDisk = 0x1b;		///< the drive is empty
static const byte kLineFirst = 0x23;		///< the computer answers, first time
static const byte kLineAgain = 0x28;		///< and every time after

static const char *const kBootPlate = "64SCR1.PCX";
static const char *const kPanelPlate = "64SCR2.PCX";
static const char *const kBootBank = "64INIT.DL1";
static const char *const kShaftBank = "COMPELE1.DL1";
static const char *const kPanelBank = "COMPELE2.DL1";
static const char *const kCursorBank = "64CURSOR.DL1";

static const uint kShaftSlot = 0;
static const uint kPanelSlot = 1;
static const uint kCursorSlot = 2;

// The panel plays everything through MIDAS:sub_18810 and sub_188b9, which are
// modes 4 and 5 -- forward and backward, neither leaving its last frame behind.
// That works there because the console never repaints: it blits its plate once
// and the slots paint onto the page over it, so a one-frame play of the button
// panel stays put and the car stays where it stopped. The port redraws the
// background every frame, so the same calls would show one frame and then an
// empty screen. Modes 1 and 3 are the same two directions with the last frame
// left behind, which is what the original's page comes to here.
static const int kForward = 1;
static const int kBackward = 3;

static const int kBootTicks = 0x46;		///< how long 64INIT runs before the console
static const int kThinkTicks = 0x64;	///< between a button going down and its case
static const int kRefuseTicks = 0x1e;	///< how long the refusal indicator stands

static const byte kCaseUp = 1;
static const byte kCaseDown = 2;
static const byte kCaseRefuse = 5;
static const byte kCaseQuit = 0xff;

/// Loop passes before a scripted run that has stopped clicking leaves by itself.
static const uint kScriptedGiveUp = 3000;

static bool inBox(int x, int y, int x1, int y1, int x2, int y2) {
	return x > x1 && y > y1 && x < x2 && y < y2;
}

/// Which of the three rectangles a click landed in, or 0 for none. `carFloor`
/// picks the refusal for the button that asks for the floor the car is on.
static byte liftBoxAt(int x, int y, byte carFloor) {
	if (inBox(x, y, 0xb7, 0x2f, 0x100, 0x50))
		return carFloor == 1 ? kCaseUp : kCaseRefuse;
	if (inBox(x, y, 0xb7, 0x7f, 0x100, 0xa0))
		return carFloor == 0 ? kCaseDown : kCaseRefuse;
	if (inBox(x, y, 0x0a, 0xa4, 0x3c, 0xc8))
		return kCaseQuit;
	return 0;
}

/**
 * The computer has been clicked: raise the line that opens the panel.
 *
 * ovr_03_0e57:0x0000. This runs from the click hook rather than from the script
 * table because the table has no body for it at all -- room 3 obj 1 under the
 * password note is a bare set_action_handled(1).
 */
bool AlienEngine::armLiftCall(int obj, int anchorX, int anchorY) {
	if (_room != kLabRoom || obj != kComputer)
		return false;

	if (_script.flag(kTerminalUsed) != 1) {
		// 0x003f: the drive is empty, so the computer has nothing to say beyond
		// the one line. No panel, and the cursor stays.
		queueOutcome(_tal, kLineNoDisk, anchorX, anchorY);
		debugC(1, kDebugLift, "lift: the drive is empty, outcome 0x%02x", kLineNoDisk);
		return true;
	}

	// The original has one test more: 0x000a asks OBJ:sprite_find_slot(0xc)
	// whether sprite 12 is on screen, and takes the 0x22 arm when it is not.
	// Inside [0xa6e1] == 1 that sprite is the disk in the drive, so the arm is
	// unreachable on the way in and only answers a drive that has been emptied
	// again -- which nothing in the lifted data does. It is left out here rather
	// than guessed at; the panel is otherwise open to anyone who has loaded the
	// toaster, note or no note.
	const byte code = _script.flag(kPanelSeen) == 1 ? kLineAgain : kLineFirst;
	queueOutcome(_tal, code, anchorX, anchorY);

	// 0x0038: the cursor goes away for the length of the line, and the panel
	// takes the screen when it comes down.
	CursorMan.showMouse(false);
	_liftPending = true;
	debugC(1, kDebugLift, "lift: the computer answers with outcome 0x%02x", code);
	return true;
}

/**
 * A line has just come down: if it was the computer's, open the panel.
 *
 * Room 3's entry 3 tests [0xa956] == 0x4e2a -- the dialog unit's "a line has
 * cleared" word, the same one room 15 answers the front door with -- against
 * [0xacf6], the id the last queue_event was raised with.
 */
void AlienEngine::stepLiftCall() {
	if (!_liftPending || _room != kLabRoom)
		return;
	if (_lastEvent != kLineFirst && _lastEvent != kLineAgain)
		return;
	if (!speechDone())
		return;

	_liftPending = false;
	playLiftPanel();
}

/**
 * The console, as 15f3:sub_15fe9 runs it.
 *
 * This blocks, the way a cutscene does: the room it interrupts is reloaded
 * afterwards, which is what submode 0x6f comes to.
 */
void AlienEngine::playLiftPanel() {
	const int room = _room;
	const int benX = _ben.walkX();
	const int benY = _ben.walkY();
	const int benFacing = _ben.facing();

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kBootPlate), plate, palette)) {
		plate.free();
		warning("lift: could not load %s", kBootPlate);
		CursorMan.showMouse(true);
		return;
	}

	stopSpeech();

	// [0xa604] is the flag a menu raises to say it owns the input and [0x7924]
	// turns the room's own tick off; the port's modal screens carry both as
	// _cutscene. The pointer stays, because unlike a scene this one is clicked.
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));
	CursorMan.showMouse(true);

	const char *bootBanks[] = { kBootBank };
	_anims.loadBanks(bootBanks, ARRAYSIZE(bootBanks));
	_anims.play(kShaftSlot, 1, 0xf, 1, kForward);

	playMusicSlot(3);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugLift, "lift: the console opens, the car is %s",
		   _script.flag(kCarFloor) == 0 ? "at the bedroom" : "in the basement");

	static const uint32 kTickMillis = 1000 / 70;
	uint32 last = g_system->getMillis();
	uint32 tick = 0;

	bool booting = true;
	int bootLeft = kBootTicks;

	byte pending = 0;		// [bp-1], the case a button has asked for
	int think = 0;			// [bp-2], the countdown before it runs
	bool indicator = false;	// [bp-3], the indicator is off its rest frame
	int hold = 0;			// [bp-4], the refusal's own hold
	bool restore = false;	// [bp-5], and whether it still has to be put back
	bool quit = false;		// [bp-6]
	uint scripted = 0;		///< passes a scripted run has spent with nothing to click
	uint shot = 0;			///< frames still to be written by the debug channel

	while (!shouldQuit() && !_quit && !quit) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			const bool click = event.type == Common::EVENT_LBUTTONDOWN ||
							   event.type == Common::EVENT_RBUTTONDOWN;

			// A headless run has no mouse, so the keyboard leaves the panel the
			// way the quit box does.
			if (event.type == Common::EVENT_KEYDOWN &&
				event.kbd.keycode == Common::KEYCODE_ESCAPE)
				quit = true;

			// 0x02b0: both buttons are read, and nothing is accepted while a
			// case is pending or either counter is standing.
			if (!click || booting || pending || think > 0 || hold > 0)
				continue;

			const byte hit = liftBoxAt(event.mouse.x, event.mouse.y,
									   _script.flag(kCarFloor));
			if (hit) {
				// 0x02fa: the press flash, and then the console takes its time.
				pending = hit;
				think = kThinkTicks;
				_anims.play(kPanelSlot, 4, 5, 4, kForward);
			}
		}

		// A scripted run has no mouse, so the play script's own clicks are read
		// here instead -- the panel owns the loop while it is up, and the main
		// one that would step the script is not running. Waits and settles are
		// stepped over; anything else is left for the room the panel hands back
		// to. A script that never clicks the quit box would hang a headless run,
		// so the panel leaves on its own after kScriptedGiveUp.
		if (_playActive && !booting && !pending && think == 0 && hold == 0) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					_playIndex++;
					continue;
				}
				if (cmd.type != PlayCommand::kClick &&
					cmd.type != PlayCommand::kRightClick)
					break;

				_playIndex++;
				scripted = 0;
				const byte hit = liftBoxAt(cmd.a, cmd.b, _script.flag(kCarFloor));
				debugC(1, kDebugLift, "lift: play: %u: click %d,%d -> case %u",
					   cmd.sourceLine, cmd.a, cmd.b, hit);
				if (hit) {
					pending = hit;
					think = kThinkTicks;
					_anims.play(kPanelSlot, 4, 5, 4, kForward);
				}
				break;
			}

			if (++scripted > kScriptedGiveUp) {
				warning("lift: the script left the panel open; closing it");
				quit = true;
			}
		}

		const uint32 now = g_system->getMillis();
		while (now - last >= kTickMillis) {
			last += kTickMillis;
			tick++;

			if ((tick & 3) != 0)
				continue;

			// Everything below is on the animation tick, which is the clock
			// [0xa5f8] gates and the one both counters are measured in.
			_anims.tick();

			if (booting) {
				if (--bootLeft > 0)
					continue;

				// The boot sequence is over: the console plate and its three
				// banks go in over it (0x01c9-0x0257).
				booting = false;
				Graphics::Surface panel;
				byte panelPalette[256 * 3];
				if (loadGamePCX(Common::Path(kPanelPlate), panel, panelPalette)) {
					_background.free();
					_background = panel;
					memcpy(_palette, panelPalette, sizeof(_palette));
					g_system->getPaletteManager()->setPalette(_palette, 0, 256);
				} else {
					warning("lift: could not load %s", kPanelPlate);
				}

				const char *panelBanks[] = { kShaftBank, kPanelBank, kCursorBank };
				_anims.loadBanks(panelBanks, ARRAYSIZE(panelBanks));
				_anims.play(kCursorSlot, 1, 4, 9, kForward);
				_anims.play(kPanelSlot, 1, 1, 0, kForward);

				// 0x0243: a car already parked here is drawn at the top of the
				// shaft rather than played there.
				if (_script.flag(kCarFloor) == 0)
					_anims.play(kShaftSlot, 0xe, 1, 0, kForward);

				_dirty = true;
				shot = 1;
				continue;
			}

			if (think > 0)
				think--;
			if (hold > 0)
				hold--;

			if (pending && think == 0) {
				switch (pending) {
				case kCaseUp:
					// 0x03be: the car rises, and this is the write the whole
					// closet depends on.
					_anims.play(kShaftSlot, 1, 0xe, 7, kForward);
					_script.setFlag(kCarFloor, 0);
					_anims.play(kPanelSlot, 2, 1, 0, kForward);
					indicator = true;
					debugC(1, kDebugLift, "lift: the car comes up, [0xa700] = 0");
					break;

				case kCaseDown:
					// 0x03e7: the same fourteen frames backwards.
					_anims.play(kShaftSlot, 0xe, 0xf, 7, kBackward);
					_script.setFlag(kCarFloor, 1);
					_anims.play(kPanelSlot, 2, 1, 0, kForward);
					indicator = true;
					debugC(1, kDebugLift, "lift: the car goes down, [0xa700] = 1");
					break;

				case kCaseRefuse:
					// 0x0410: the car is already on that floor.
					_anims.play(kPanelSlot, 3, 1, 0, kForward);
					hold = kRefuseTicks;
					restore = true;
					debugC(1, kDebugLift, "lift: refused, the car is already there");
					break;

				case kCaseQuit:
					quit = true;
					break;

				default:
					break;
				}
				pending = 0;
				shot = 1;
			}

			// 0x0433 and 0x0450: the indicator goes back to its rest frame, once
			// the refusal's hold has run out and once the car has stopped.
			if (hold == 0 && restore) {
				_anims.play(kPanelSlot, 1, 1, 0, kForward);
				restore = false;
			}
			if (indicator && !_anims.isBusy(kShaftSlot)) {
				_anims.play(kPanelSlot, 1, 1, 0, kForward);
				indicator = false;
			}

			_dirty = true;
		}

		if (_dirty)
			redraw();

		// Level 3 writes one frame where the screen has just changed -- the
		// console opening and each case -- which is the only way to look at the
		// panel from a headless run.
		if (shot && debugChannelSet(3, kDebugLift)) {
			dumpScreen(Common::String::format("lift-%u.png", tick));
			shot = 0;
		}

		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	// 0x0486: the panel hands the input back, and room 3's entry 3 marks it seen
	// before it leaves by submode 0x6f -- which the chain routes back to room 3,
	// so the lab is reloaded exactly as a scene's room is.
	stopMusic();
	_cutscene = false;
	_script.setFlag(kPanelSeen, 1);

	debugC(1, kDebugLift, "lift: the console closes, the car is %s",
		   _script.flag(kCarFloor) == 0 ? "at the bedroom" : "in the basement");

	if (room > 0 && loadRoom(room)) {
		_ben.place(benX, benY, benFacing);
		_pendingCutscenes = false;
	}
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
}

} // End of namespace Alien
