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

// Room 28's two modal screens.
//
// Both actions were reported as doing nothing but walking Ben to the bottom
// right. That is one symptom with two causes.
//
// Object 4 (Use, "the telescope") and object 2 (Look at, with the dome open --
// [0xa75f] == 1 -- "the monitor") both end in the lifted table with nothing but
// game_submode = 111 (docs/room_scripts.md). Submode 111 is the original's own
// way of naming "this room, reloaded": the transition chain routes room 28's
// 111 back to room 28 (transitions.cpp), which is right -- the two real bodies
// are calls the script lift cannot carry, and each of them ends by reloading
// the room underneath itself, exactly as a cutscene does. What was missing is
// the two calls themselves:
//
//   ovr_1c_0eb7:0x0308 -> CUTSCENE:sub_0d209   the view through the telescope
//   ovr_1c_0eb7:0x0331 -> 15f3:sub_165d3       the observatory computer
//
// And the reload landing Ben at the bottom right is the other half: room 28's
// only opening effect is a char_place guarded on [0xa880] == 19 (the room left
// being the observatory's own stairs), which cannot hold on a *self* reload, so
// AlienEngine::loadRoom fell back to the room's first walk node -- roughly
// 217,150, the low right corner of KIERRA28. The original never moves him on a
// reload with no matching char_place; the two screens below restore his
// position by hand instead, the way playLiftPanel() does (lift.cpp).

/// The room both screens belong to, and the two objects.
static const int kRoom = 28;
static const byte kTelescopeObj = 4;
static const byte kMonitorObj = 2;

static const byte kVerbUse = 10;
static const byte kVerbLookAt = 5;

/// The submode both lifted rows arm.
static const byte kSelfSubmode = 111;

/// [0xa6d3]: a scene has just played, which every CUTSCENE teardown raises
/// (cutsceneplay.cpp) -- 0c55:0086 is the shared one both screens end through,
/// directly for the computer (CUTSCENE:sub_0c5c9) and inlined for the view
/// (0c55:0e1b).
static const uint16 kScenePlayed = 0xa6d3;

/// [0xa760]: the telescope lever, room 28 object 9's Pull.
static const uint16 kLever = 0xa760;
/// [0xa75f]: the dome, room 28 object 6's Pull.
static const uint16 kDomeOpen = 0xa75f;
/// [0xa761]: the disk is in the drive, room 28 object 10 with item 20.
static const uint16 kDiskIn = 0xa761;
/// [0xa75e]: the lens is in the telescope, room 28 object 3 with item 30.
static const uint16 kLensIn = 0xa75e;
/// [0x33b6]: the teleporter destination -- ovr_16_0ea3_room_22:0x0c41 only
/// sends Ben anywhere once this is 2, and nothing before this wrote it
/// (teleport.cpp names it as the flag it has no writer for).
static const uint16 kTeleportDest = 0x33b6;
/// [0x3092]: written alongside it, inside a resident table nothing is ever seen
/// reading back. Kept for save-state fidelity; docs/room_scripts.md.
static const uint16 kMystery3092 = 0x3092;

/// Loop passes before a scripted run that has stopped clicking leaves by
/// itself, as lift.cpp's own console does.
static const uint kScriptedGiveUp = 3000;

// --- The view through the telescope: CUTSCENE:sub_0d209 --------------------

static const uint kViewSlot = 0;

static const char *const kView0Plate = "TELEVIE0.PCX";
static const char *const kView0Bank = "TELEVIE0.DL1";
static const char *const kView1Plate = "TELEVIE1.PCX";
static const char *const kView1Bank = "TELEVIE1.DL1";

static const int kForward = 1;		///< mode 1: forward, left behind (see anim.h)

/**
 * 0x02fa..0x031c of ovr_1c_0eb7's entry 3: object 4, verb 10 (Use), no guard.
 *
 * Which view plays, and whether it loops or runs once, both follow [0xa760]:
 * lowered (0) shows the sky through glass, a 29-frame one-shot that ends the
 * scene by itself; raised (1) shows a fixed sight line, nine frames looped
 * forever ([0xa53a], MIDAS:snd_func_112d) until the player clicks it away.
 * Either way a click ends it regardless -- [0xa94a], the *unmasked* left/right
 * click MIDAS:0251:6726 copies before [0xa948] takes the cursor away, which is
 * why a screen with no visible pointer can still be dismissed.
 */
void AlienEngine::playTelescopeView() {
	const int room = _room;
	const int benX = _ben.walkX();
	const int benY = _ben.walkY();
	const int benFacing = _ben.facing();

	const bool lowered = _script.flag(kLever) == 0;
	const char *const plateName = lowered ? kView0Plate : kView1Plate;
	const char *const bankName = lowered ? kView0Bank : kView1Bank;

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(plateName), plate, palette)) {
		plate.free();
		warning("telescope: could not load %s", plateName);
		return;
	}

	stopSpeech();
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;

	const int clip = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	// 0x02e3: [0xa948] := 0. Unlike the lift's console this screen is never
	// clicked at a position -- any click anywhere ends it -- so the cursor
	// stays away for the whole of it.
	CursorMan.showMouse(false);

	const char *banks[] = { bankName };
	_anims.loadBanks(banks, ARRAYSIZE(banks));
	_anims.play(kViewSlot, 1, lowered ? 0x1d : 9, lowered ? 5 : 4, kForward);

	playMusicSlot(lowered ? 10 : 9);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugTelescope, "telescope: the view opens, lever %s",
		   lowered ? "down" : "up");

	static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;
	uint32 last = millis();
	uint32 tick = 0;
	bool quit = false;
	uint scripted = 0;
	uint shot = 0;

	while (!shouldQuit() && !_quit && !quit) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
				event.kbd.keycode == Common::KEYCODE_ESCAPE)
				quit = true;
			if (event.type == Common::EVENT_LBUTTONDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN)
				quit = true;
		}

		// A scripted run has no mouse; either kind of click in the play script
		// ends the view exactly as a real one would.
		if (_playActive && !quit) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					_playIndex++;
					continue;
				}
				if (cmd.type != PlayCommand::kClick && cmd.type != PlayCommand::kRightClick)
					break;

				_playIndex++;
				scripted = 0;
				quit = true;
				debugC(1, kDebugTelescope, "telescope: play: %u: click closes the view",
					   cmd.sourceLine);
				break;
			}

			if (++scripted > kScriptedGiveUp) {
				warning("telescope: the script left the view open; closing it");
				playLeftOpen("the telescope view");
				quit = true;
			}
		}

		const uint32 now = millis();
		while (now - last >= kTickMillis) {
			last += kTickMillis;
			tick++;
			sceneClockTick();

			if ((tick & 3) != 0)
				continue;

			_anims.tick();

			if (!lowered)
				_anims.relaunch(kViewSlot);			// [0xa53a]: TELEVIE1 loops
			else if (_anims.remaining(kViewSlot) == 0)	// [0xa760]==0 && [0xa4ea]==0
				quit = true;

			shot = 1;
			_dirty = true;
		}

		if (_dirty)
			redraw();

		if (shot && debugChannelSet(3, kDebugTelescope)) {
			dumpScreen(Common::String::format("telescope-%u.png", tick));
			shot = 0;
		}

		present();
		sleep(10);
	}

	// 0c55:0e11-0e26: the tail every CUTSCENE teardown shares.
	stopMusic();
	_cutscene = false;
	_clipBottom = clip;
	_script.setFlag(kScenePlayed, 1);

	debugC(1, kDebugTelescope, "telescope: the view closes");

	if (room > 0 && loadRoom(room)) {
		_ben.place(benX, benY, benFacing);
		_pendingCutscenes = false;
	}
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
}

// --- The observatory computer: 15f3:sub_165d3 -------------------------------

/// The mode byte, [0xbf16]. Named states rather than the bare numbers the
/// original counts in.
enum ScreenMode {
	kModeNoDisk = 1,	///< [0xa761] == 0: nothing to show but the idle screen
	kModeMenu = 2,		///< the three-item menu
	kModeScanning = 3,	///< SCAN clicked, waiting on the transition
	kModeSaving = 4,	///< SAVE clicked, waiting on the transition
	kModeExiting = 5	///< EXIT clicked, waiting on the transition
};

static const uint kSlotMenu = 0;	///< OBS_SCR1.DL1 (mode 1) or OBS_MENU.DL1 (2-5)
static const uint kSlotCursor = 1;	///< OBS_CURS.DL1, blinks throughout the menu
static const uint kSlotSaveOk = 2;	///< OBS_SAOK.DL1
static const uint kSlotSaveEr = 3;	///< OBS_SAER.DL1
static const uint kSlotScan = 4;	///< O_SCANV1.DL1 or O_SCANV2.DL1, loaded mid-run

static const char *const kScreenPlate = "OBS_SCR.PCX";
static const char *const kNoDiskBank = "OBS_SCR1.DL1";
static const char *const kMenuBank = "OBS_MENU.DL1";
static const char *const kCursorBank = "OBS_CURS.DL1";
static const char *const kSaveOkBank = "OBS_SAOK.DL1";
static const char *const kSaveErBank = "OBS_SAER.DL1";
static const char *const kScanBank1 = "O_SCANV1.DL1";
static const char *const kScanBank2 = "O_SCANV2.DL1";

/// The frame lists 271a:0x6728 (SCAN), 0x6736 (SAVE), 0x6744 (EXIT) and
/// 0x6752/0x6762 (the SAVE payoff, lens in or out). All five are slot 0, mode
/// 7 in the original -- forward, taken away -- except the payoffs, which are
/// slots 2 and 3. The port plays all of them as the persisting mode 6 instead
/// (see kMenuMode's comment) and clears the slot by hand on the way back to
/// the menu, the way lift.cpp substitutes mode 1 for mode 4.
static const byte kScanTransition[] = { 3, 4, 5, 6, 7, 7, 8, 8, 7, 7, 9, 0xa, 0xb };
static const byte kSaveTransition[] = { 0xc, 0xd, 0xe, 0xf, 0x10, 0x10, 0x11, 0x11, 0x10, 0x10, 0x12, 0x13, 0xb };
static const byte kExitTransition[] = { 0x14, 0x15, 0x16, 0x17, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18, 0x1a, 0x1b, 0xb };
static const byte kSaveOkFrames[] = {
	1, 2, 3, 4, 3, 4, 3, 4, 3, 4, 3, 4, 3, 4, 3, 4
};
static const byte kSaveErFrames[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 9, 0xa, 0xb, 0xc, 9, 0xa, 0xb, 0xc, 9, 0xa, 0xb, 0xc
};

/// 271a:0x66c4: the no-disk screen's own idle loop, [0xbf16] == kModeNoDisk,
/// slot 0 -- three sweeps of a fourteen-frame cycle and then a straight climb
/// through the rest of the bank, looped forever ([0xa53a]).
static const byte kNoDiskIdleFrames[] = {
	0x04, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
	0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04
};

/// mode 6: forward, frame list, left behind -- the substitute for the
/// original's mode 7 (see the comment above the frame lists).
static const int kTransitionMode = 6;
/// mode 1: forward, left behind -- the substitute for the original's mode 4,
/// used for the "back to the menu" play and the scan payoff.
static const int kPersistMode = 1;

/// The three rectangles, [0xbf16] == kModeMenu only, cursor coordinates,
/// bounds exclusive (OBJ:sub_08996).
struct ScreenBox {
	int x1, y1, x2, y2;
	ScreenMode mode;
};
static const ScreenBox kScreenBoxes[] = {
	{ 59, 113, 107, 127, kModeExiting },	// EXIT, 15f3:sub_1653f
	{ 60, 75, 219, 86, kModeScanning },		// SCAN, 15f3:sub_1649a
	{ 59, 93, 212, 108, kModeSaving },		// SAVE, 15f3:sub_16519
};

static bool inBox(int x, int y, const ScreenBox &box) {
	return x > box.x1 && y > box.y1 && x < box.x2 && y < box.y2;
}

/**
 * The observatory computer, as 15f3:sub_165d3 runs it.
 *
 * Reached with object 2 under Look at once the dome is open. Structured like
 * playLiftPanel(): a modal that owns the framebuffer and the loop, scriptable,
 * and giving up on its own if a scripted run stops clicking.
 */
void AlienEngine::playObservatoryScreen() {
	const int room = _room;
	const Walker::Spot spot = _ben.spot();

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kScreenPlate), plate, palette)) {
		plate.free();
		warning("telescope: could not load %s", kScreenPlate);
		return;
	}

	stopSpeech();
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;

	const int clip = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	CursorMan.showMouse(true);

	ScreenMode mode = _script.flag(kDiskIn) == 0 ? kModeNoDisk : kModeMenu;

	// The room's own slots go: 15f3:sub_165d3 draws only the five it loads,
	// and the room reloads on the way out. Left running, slot 7 -- OBU_SCRE,
	// the little monitor in the room, at 62,83 -- drew over the screen's text
	// as a blue-edged green block (manual playthrough #41/#51).
	for (uint i = 0; i < AnimSlots::kSlotCount; i++)
		_anims.takeDown(i);

	_anims.loadBank(kSlotMenu, mode == kModeNoDisk ? kNoDiskBank : kMenuBank);
	_anims.loadBank(kSlotCursor, kCursorBank);
	_anims.loadBank(kSlotSaveOk, kSaveOkBank);
	_anims.loadBank(kSlotSaveEr, kSaveErBank);

	_anims.play(kSlotCursor, 1, 3, 0x0a, kForward);
	if (mode == kModeNoDisk)
		_anims.play(kSlotMenu, 0, ARRAYSIZE(kNoDiskIdleFrames), 2, kTransitionMode,
					kNoDiskIdleFrames);
	else
		// 15f3:0753, sub_165d3 itself: the instant [0xbf16] settles on 2 it
		// calls sub_163f5 -- the same "back to the menu" play returnToMenu()
		// below uses -- to draw the menu the first time. Skipping this left
		// the plate up with no SCAN/SAVE/EXIT frame ever drawn over it.
		_anims.play(kSlotMenu, 1, 3, 7, kPersistMode);

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugTelescope, "telescope: the computer opens, %s",
		   mode == kModeNoDisk ? "no disk" : "the menu");

	static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;
	uint32 last = millis();
	uint32 tick = 0;

	bool clicksOn = true;		// [0xa949]
	uint16 sweep = 0;			// [0xbf14]
	bool quit = false;
	uint scripted = 0;
	uint shot = 0;

	// A scripted wait or settle has to hold up the reader below for real, the
	// way _playWaitTicks/_playSettling do for the outer driver: unlike the
	// view (one click, then it is over), the menu takes several clicks in a
	// row, each meant to land after the one before it has been answered, and
	// a bare skip would exhaust the whole script in the same pass.
	uint32 playWaitUntil = 0;

	// 15f3:sub_163f5, the way back to the menu from a transition state: the
	// slots the transition and its payoff drew are cleared -- the original
	// erases them by restoring the plate underneath, which the port's redraw
	// does for free once nothing is left drawing over it -- and the menu bank
	// plays its three-frame "appear".
	const auto returnToMenu = [&]() {
		_anims.takeDown(kSlotSaveOk);
		_anims.takeDown(kSlotSaveEr);
		_anims.takeDown(kSlotScan);
		_anims.loadBank(kSlotMenu, kMenuBank);
		_anims.play(kSlotMenu, 1, 3, 7, kPersistMode);
		mode = kModeMenu;
		debugC(1, kDebugTelescope, "telescope: back to the menu");
	};

	const auto handleClick = [&](int x, int y) {
		if (mode == kModeNoDisk) {
			quit = true;
			return;
		}
		if (mode == kModeScanning || mode == kModeSaving) {
			returnToMenu();
			return;
		}
		if (mode != kModeMenu || !clicksOn)
			return;

		for (uint i = 0; i < ARRAYSIZE(kScreenBoxes); i++) {
			const ScreenBox &box = kScreenBoxes[i];
			if (!inBox(x, y, box))
				continue;

			clicksOn = false;
			switch (box.mode) {
			case kModeExiting:
				mode = kModeExiting;
				_anims.play(kSlotMenu, 0, ARRAYSIZE(kExitTransition), 1,
							kTransitionMode, kExitTransition);
				debugC(1, kDebugTelescope, "telescope: EXIT clicked");
				break;
			case kModeScanning:
				mode = kModeScanning;
				_anims.play(kSlotMenu, 0, ARRAYSIZE(kScanTransition), 1,
							kTransitionMode, kScanTransition);
				debugC(1, kDebugTelescope, "telescope: SCAN clicked");
				break;
			case kModeSaving:
				mode = kModeSaving;
				_anims.play(kSlotMenu, 0, ARRAYSIZE(kSaveTransition), 1,
							kTransitionMode, kSaveTransition);
				debugC(1, kDebugTelescope, "telescope: SAVE clicked");
				break;
			default:
				break;
			}
			break;
		}
	};

	while (!shouldQuit() && !_quit && !quit) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
				event.kbd.keycode == Common::KEYCODE_ESCAPE)
				quit = true;
			if (event.type == Common::EVENT_LBUTTONDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN)
				handleClick(event.mouse.x, event.mouse.y);
		}

		if (_playActive && !quit && playWaitUntil != 0 && millis() >= playWaitUntil)
			playWaitUntil = 0;

		if (_playActive && !quit && playWaitUntil == 0) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					// Held for real, roughly as many milliseconds as the
					// original counts ticks -- close enough to keep the next
					// click from landing before this one's transition has
					// even started.
					_playIndex++;
					scripted = 0;
					// A wait is in master ticks, as the outer driver and the
					// original's runner count it; a settle has nothing to watch
					// here and holds for its timeout in milliseconds.
					playWaitUntil = millis() + (uint32)MAX(cmd.a, 0) *
						(cmd.type == PlayCommand::kWait ? kMasterTickMillis : 1);
					break;
				}
				if (cmd.type != PlayCommand::kClick && cmd.type != PlayCommand::kRightClick)
					break;

				_playIndex++;
				scripted = 0;
				debugC(1, kDebugTelescope, "telescope: play: %u: click %d,%d",
					   cmd.sourceLine, cmd.a, cmd.b);
				handleClick(cmd.a, cmd.b);
				break;
			}

			if (++scripted > kScriptedGiveUp) {
				warning("telescope: the script left the computer open; closing it");
				playLeftOpen("the telescope computer");
				quit = true;
			}
		}

		const uint32 now = millis();
		while (now - last >= kTickMillis) {
			last += kTickMillis;
			tick++;
			sceneClockTick();

			if ((tick & 3) != 0)
				continue;

			_anims.tick();

			// 0x0499, sub_163cc: the monitor's flicker. UTIL:sub_0232c(amount,
			// 0, 0, 0, 0x50, 0x40) blends the sixty-four entries from 0x50
			// toward black by amount/256, amount stepping 0, 10 .. 40. The
			// port used to tint entries 0..40 toward red instead, which is
			// where the pointer lives (33-37) -- the odd-coloured glyph on the
			// screen (manual playthrough #41).
			static const uint kSweepFirst = 0x50, kSweepCount = 0x40;
			byte swept[256 * 3];
			memcpy(swept, _palette, sizeof(swept));
			for (uint i = kSweepFirst; i < kSweepFirst + kSweepCount; i++) {
				for (uint c = 0; c < 3; c++)
					swept[i * 3 + c] = (byte)((_palette[i * 3 + c] * (256 - sweep)) >> 8);
			}
			g_system->getPaletteManager()->setPalette(swept, 0, 256);
			sweep += 10;
			if (sweep > 0x28)
				sweep = 0;

			// 0x07ad, sub_16560, read ahead of the two loops below: clicks
			// come back one frame before whichever of these ends. In the
			// menu's own loop (kSlotMenu, kSlotCursor) that one frame is
			// permanent -- AnimSlots::relaunch() re-issues the range as soon
			// as it sees it, in the same tick, which would otherwise mean
			// remaining() never reads 1 to this test at all once the menu is
			// looping. Reading it first, before relaunching, is what keeps
			// clicks alive through an idle menu the way the original's own
			// idle menu takes clicks throughout, not for one tick per cycle.
			if (_anims.remaining(kSlotMenu) == 1 || _anims.remaining(kSlotSaveOk) == 1 ||
				_anims.remaining(kSlotSaveEr) == 1 || _anims.remaining(kSlotScan) == 1)
				clicksOn = true;

			// 0x078a-0x07a6: the loops the screen keeps running. The code
			// relaunches the cursor only in the menu, but OBS_CURS ships two
			// frames and the play runs three, so each pass ends on the
			// terminator with [0xa5da] set -- the frame is stamped into the
			// page at one-left and the cursor never leaves the original's
			// screen. The manual playthrough saw it blinking on the no-disk
			// screen and through a scan (#40, #42); the port's full repaint
			// has no page to stamp into, so it keeps the slot running instead.
			if (mode == kModeNoDisk || mode == kModeMenu)
				_anims.relaunch(kSlotMenu);
			_anims.relaunch(kSlotCursor);

			// 0x07b0-0x07e5: the three payoffs, each one frame before its
			// transition ends.
			if (mode == kModeExiting && _anims.remaining(kSlotMenu) == 1) {
				quit = true;
			} else if (mode == kModeSaving && _anims.remaining(kSlotMenu) == 1) {
				// 15f3:sub_164c0
				clicksOn = false;
				// Slot 0's SAVE transition is done and left its last frame
				// behind (kTransitionMode); on the real page that frame just
				// sits under whatever draws next, but the port's redraw()
				// composites every slot fresh every tick, so leaving it
				// running kept the "Save co-ordinates" menu line showing
				// through the OK/error result instead of being replaced by
				// it. Taking the slot down here is what sub_163f5 already
				// does the other way, on the way back to the menu.
				_anims.takeDown(kSlotMenu);
				if (_script.flag(kLensIn) == 1) {
					_anims.play(kSlotSaveOk, 0, ARRAYSIZE(kSaveOkFrames), 1,
								kTransitionMode, kSaveOkFrames);
					if (_script.flag(kLever) == 0) {
						_script.setFlag(kTeleportDest, 1);
						_script.setFlag(kMystery3092, 0x60);
					} else {
						_script.setFlag(kTeleportDest, 2);
						_script.setFlag(kMystery3092, 0x61);
					}
					debugC(1, kDebugTelescope,
						   "telescope: co-ordinates saved, [0x33b6] = %u",
						   _script.flag(kTeleportDest));
				} else {
					_anims.play(kSlotSaveEr, 0, ARRAYSIZE(kSaveErFrames), 1,
								kTransitionMode, kSaveErFrames);
					debugC(1, kDebugTelescope, "telescope: no lens, the save fails");
				}
			} else if (mode == kModeScanning && _anims.remaining(kSlotMenu) == 1) {
				// 15f3:sub_16450
				clicksOn = false;
				// Same as the SAVE payoff below: slot 0's last SCAN-transition
				// frame stays behind under kPersistMode and, with the port's
				// full repaint, kept the menu text showing through the sky.
				_anims.takeDown(kSlotMenu);
				const bool lowered = _script.flag(kLever) == 0;
				_anims.loadBank(kSlotScan, lowered ? kScanBank1 : kScanBank2);
				_anims.play(kSlotScan, 1, lowered ? 0x29 : 0x25, 2, kPersistMode);
				debugC(1, kDebugTelescope, "telescope: scan shows the sky, lever %s",
					   lowered ? "down" : "up");
			}

			shot = 1;
			_dirty = true;
		}

		if (_dirty)
			redraw();

		if (shot && debugChannelSet(3, kDebugTelescope)) {
			dumpScreen(Common::String::format("observatory-%u.png", tick));
			shot = 0;
		}

		present();
		sleep(10);
	}

	stopMusic();
	_cutscene = false;
	_clipBottom = clip;
	_script.setFlag(kScenePlayed, 1);

	debugC(1, kDebugTelescope, "telescope: the computer closes");

	// The computer ends the room's loop the way an exit does, so game_mode is
	// the room itself when it opens again (OBJ:sub_0879a; the original's trace
	// reads `room 28 from 28`).
	if (room > 0)
		_mode = (byte)room;
	if (room > 0 && loadRoom(room)) {
		// Room 28's open places nobody when it is its own way in, so he keeps
		// the spot he had, fraction and all.
		_ben.putBack(spot);
		_pendingCutscenes = false;
	}
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
}

// --- The telescope lever: the machine that persists [0xa760] ---------------
//
// Object 9's Pull is in the lifted table twice, once per direction, and each
// row does no more than play the twenty-one frames of Ben working the lever --
// slot 1 raising it, slot 2 lowering it. The lever's own byte is never written
// there, which is why [0xa760] had no writer anywhere in the port and the
// observatory computer could only ever save co-ordinates 1
// (tools/playthrough_observatory.txt says so in its own comment). The write is
// two states further on: each row also sets [0xa49f], and the room's tick runs
// a six-state machine over it (ovr_1c_0eb7:0x09cb).
//
//   raise   3 -> slot 1 has three frames left        -> 5
//           5 -> ovr0eb7_sub_0000: slot 0 forward,
//                [0xa760] := 1, the servo sample     -> 6
//           6 -> slot 0 idle, cursor back            -> 0
//
//   lower  20 -> slot 2 has three frames left        -> 21
//          21 -> ovr0eb7_sub_002e: slot 0 backward,
//                [0xa760] := 0, the same sample
//                a little lower                      -> 22
//          22 -> slot 0 idle, cursor back            -> 0
//
// The three-frames-left test is what dovetails the two animations: the lever
// arm starts moving while Ben is still pulling, rather than after he lets go.
// The [0xa49f] write is invisible to the lift -- it is not a state address
// (STATE_FLAG in tools/roomlogic.py) -- so the row keeps the animation and the
// machine is hand-written, the same split as room 32's statue.

static const byte kLeverObj = 9;
static const byte kVerbPull = 13;

/// [0xa75a] and [0xa759]: the two the lifted Pull rows are guarded on -- the
/// observatory has power and the dome is in a state to move. Without them the
/// row plays nothing, so the machine must not arm either.
static const uint16 kLeverReady1 = 0xa75a;
static const uint16 kLeverReady2 = 0xa759;

static const byte kLeverRaisePull = 3;		///< waiting on slot 1
static const byte kLeverRaiseMove = 5;		///< the arm goes up
static const byte kLeverRaiseDone = 6;		///< waiting on slot 0
static const byte kLeverLowerPull = 0x14;	///< waiting on slot 2
static const byte kLeverLowerMove = 0x15;	///< the arm comes down
static const byte kLeverLowerDone = 0x16;	///< waiting on slot 0

static const uint kLeverArmSlot = 0;	///< the lever arm itself, both directions
static const uint kRaisePullSlot = 1;	///< Ben pulling it up
static const uint kLowerPullSlot = 2;	///< Ben pulling it down

/// ovr0eb7_sub_0000 and sub_002e: the arm, mode 1 up and mode 3 (backward) down.
static const int kArmFirstUp = 1;
static const int kArmFirstDown = 0x20;
static const int kArmFrames = 0x20;
static const int kArmRate = 2;
static const int kArmModeUp = 1;
static const int kArmModeDown = 3;

/// The servo, INPUT:sfx_play_delayed(1, 0, rate, 64, 20, 0) -- the same sample
/// either way, a little slower coming down.
static const uint kServoSample = 1;
static const uint32 kServoRateUp = 0x2af8;	///< 11000
static const uint32 kServoRateDown = 0x2710;	///< 10000
static const byte kServoVolume = 0x40;
static const int8 kServoPan = 0x14;

/// Frames left on the pull when the arm starts moving (0x09d2, 0x0a0f). The
/// original tests for exactly three, and the port's stepper does hold each
/// count for the rate's three ticks, so the equality would hold here too; the
/// test is `<=` so that a rate change could never step straight past it and
/// leave the lever armed for ever.
static const int kHandover = 3;

/**
 * 0x0251..0x02f9 of entry 3, after the lifted row has played its frames: the
 * [0xa49f] write the table cannot carry.
 *
 * The two rows are mutually exclusive on [0xa760], so reading it here picks the
 * same direction the row did. The room's own guards are re-tested rather than
 * assumed: a Pull the row refused (no power, wrong dome state) reaches here
 * too, and arming on it would move the lever with no animation under it.
 */
void AlienEngine::armTelescopeLever(int obj, byte verb) {
	if (_room != kRoom || obj != kLeverObj || verb != kVerbPull)
		return;
	if (_script.flag(kLeverReady1) != 1 || _script.flag(kLeverReady2) != 1)
		return;
	if (_script.flag(0xa49f) != 0)
		return;

	const bool raising = _script.flag(kLever) == 0;
	_script.setFlag(0xa49f, raising ? kLeverRaisePull : kLeverLowerPull);
	// 0x0286/0x028b and 0x02de/0x02e3: the cursor goes away for the length of
	// the pull, and the machine's last state is what gives it back. The walker
	// goes with it: the pull is Ben drawn into slots 1 and 2, and without the
	// [0xa94d] write there were two of him (manual playthrough #39).
	CursorMan.showMouse(false);
	hideCharacter();
	debugC(1, kDebugTelescope, "telescope: the lever is being %s",
		   raising ? "raised" : "lowered");
}

/// The machine itself, stepped from the room tick.
void AlienEngine::stepTelescopeLever() {
	if (_room != kRoom)
		return;

	const byte state = (byte)_script.flag(0xa49f);

	// 0x09d2: three frames before Ben lets go.
	if (state == kLeverRaisePull) {
		if (_anims.remaining(kRaisePullSlot) <= kHandover)
			_script.setFlag(0xa49f, kLeverRaiseMove);
		return;
	}

	// 0x09e4: Ben lets go ([0xa94d] = 1), the arm goes up, and the lever is up
	// from here on.
	if (state == kLeverRaiseMove) {
		showCharacter();
		_anims.play(kLeverArmSlot, kArmFirstUp, kArmFrames, kArmRate, kArmModeUp);
		_script.setFlag(kLever, 1);
		_sound.play(kServoSample, kServoRateUp, kServoVolume, kServoPan);
		_script.setFlag(0xa49f, kLeverRaiseDone);
		debugC(1, kDebugTelescope, "telescope: the lever is up, [0xa760] = 1");
		return;
	}

	// 0x0a0f, the same three frames on the other slot.
	if (state == kLeverLowerPull) {
		if (_anims.remaining(kLowerPullSlot) <= kHandover)
			_script.setFlag(0xa49f, kLeverLowerMove);
		return;
	}

	// 0x0a21: Ben lets go, and mode 3 runs the arm's frames backward.
	if (state == kLeverLowerMove) {
		showCharacter();
		_anims.play(kLeverArmSlot, kArmFirstDown, kArmFrames, kArmRate, kArmModeDown);
		_script.setFlag(kLever, 0);
		_sound.play(kServoSample, kServoRateDown, kServoVolume, kServoPan);
		_script.setFlag(0xa49f, kLeverLowerDone);
		debugC(1, kDebugTelescope, "telescope: the lever is down, [0xa760] = 0");
		return;
	}

	// 0x09f8 and 0x0a35: both directions end the same way, on the arm slot
	// running out, and both give the cursor back.
	if (state == kLeverRaiseDone || state == kLeverLowerDone) {
		if (_anims.remaining(kLeverArmSlot) == 0) {
			_script.setFlag(0xa49f, 0);
			CursorMan.showMouse(true);
		}
	}
}

/**
 * The dispatch finishAction() falls into on room 28's submode 111, before
 * takeExit() ever sees it -- the original's arrival test
 * (ovr_1c_0eb7:0x0128, [0xa956] == 0x4e25) is finishAction() itself.
 */
bool AlienEngine::runTelescopeScreen(int obj, byte verb, byte submode) {
	if (_room != kRoom || submode != kSelfSubmode)
		return false;

	if (obj == kTelescopeObj && verb == kVerbUse) {
		playTelescopeView();
		return true;
	}
	if (obj == kMonitorObj && verb == kVerbLookAt && _script.flag(kDomeOpen) == 1) {
		playObservatoryScreen();
		return true;
	}
	return false;
}

} // End of namespace Alien
