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

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/error.h"
#include "common/events.h"
#include "common/path.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"
#include "alien/cutscenes.h"
#include "alien/occlusion.h"
#include "alien/roominit.h"

namespace Alien {

// Where a new game opens. MAIN's boot run -- everything between `entry_point`
// and `main_loop_start`, the same span the initial state block is lifted from --
// ends by setting handler_code to 3 and dispatching it, and a handler code is
// its room number in plain decimal, so the game starts in uncle's lab. The
// kitchen, which this used to say, is only where the render milestones were
// checked; `-b 10` still goes there.
static const int kStartRoom = 3;

// How far the debug room tour walks before it stops. The mansion's exits lead
// back into each other, so a tour never ends on its own.
static const uint kTourHops = 20;

// Ticks per frame for the debug run-everything animation, slow enough to watch.
static const int kDebugAnimRate = 4;

// One install ships all four text languages side by side. Picking the set is a
// launcher option the engine does not have yet, so the English tree is wired up
// for now. Both trees end in a directory of the same name, and SearchMan keys
// an archive by that last name alone, so they are registered by hand under
// names of the engine's own choosing rather than through
// addSubDirectoryMatching.
static const char *const kLanguageDir = "ENG";
static const char *const kScriptTree = "TALFILES";
static const char *const kLabelTree = "NAMEROOM";

// The music sweeps: the rate the traces are rendered at, how many rows of each
// slot's track the sequencer trace walks, and how long the loudness render runs.
static const int kMusicRate = 22050;
static const uint kMusicRows = 64;
static const uint kMusicSeconds = 10;

// The clips. The elevator sequence installs with the game; the two CDA2
// cutscenes live on the CD, under CDA/, and are reached through --extrapath
// when the disc's files are kept somewhere of their own.
static const char *const kLiftClip = "ANIMS/SHIPLIFT.MA1";
static const char *const kCutscenes[] = { "ALINTRO.CDA", "ALIEND.CDA" };

// Where the CDA2 players put a subtitle: the records in the file carry y = 172
// themselves, and a negative x means the line is centred.
static const int kSubtitleY = 172;

// How many frames of each cutscene the checksum sweep decodes. The intro is
// 74 MB; a prefix proves the codec and keeps the run short.
static const int kSweepFrames = 200;

// The rooms the alien ship's lift serves: the lobby and the two hallways whose
// overlays open with the 0FAE:sub_101a6 call that plays the clip.
static const int kLiftRooms[] = { 51, 53, 57 };

// docs/game_logic.md: the status line the hover label is set in. The text is
// centred on x = 150 and its glyphs are blitted with their top row at y = 163.
static const int kLabelCenterX = 150;
static const int kLabelY = 163;
static const int kLabelLeft = 46;

// docs/dialog_system.md 3A: the block is placed against the midpoint of the
// speaking object's bounding box. Dialog stepped through from the keyboard has
// no object behind it and is anchored at the middle of the playfield.
static const int kAnchorX = 160;
static const int kAnchorY = 90;

// TALKALL.TAL holds the answers that belong to no room: the descriptions of the
// carried items, and the two refusals the generic click path falls back on when
// an object's outcome code is zero -- 10c9:sub_11c10 and 10c9:0x257 raise the
// dialog branches that DIALOG:sub_0b776 turns into these two codes.
static const char *const kSharedScript = "TALKALL.TAL";
// Code 3 there is the other one, "Using these things together doesn't seem to
// work.", which the item-use verb needs once inventory is ported.
static const byte kOutcomeNothingSpecial = 4;	///< "I can't see anything special about it."
static const byte kOutcomeNoCombination = 3;	///< "Using these things together doesn't seem to work."

// The status-line verb whose zero outcome falls back to a canned line. Any
// other verb with a zero code is the room's own business.
static const byte kVerbLookAt = 5;

// "Use to", the verb an item in hand puts the click under.
static const byte kVerbUseTo = 2;

// docs/game_logic.md 3A: the playfield ends here. A click below it belongs to
// the bar and the status line, and never sends the character anywhere.
static const int kPlayfieldBottom = 0x9f;

// SearchMan names a directory archive after the directory's own last component,
// so registering TALFILES/ENG and NAMEROOM/ENG the usual way would have the
// second one clash with the first and be dropped. The language directory is
// found by hand instead -- caselessly, since the install writes ENG and Eng
// alike -- and registered under the tree's name.
static bool addTextTree(const Common::FSNode &gameDataDir, const char *tree) {
	Common::FSList children;
	if (!gameDataDir.getChildren(children, Common::FSNode::kListDirectoriesOnly))
		return false;

	for (const auto &treeNode : children) {
		if (treeNode.getName().compareToIgnoreCase(tree))
			continue;

		Common::FSList languages;
		if (!treeNode.getChildren(languages, Common::FSNode::kListDirectoriesOnly))
			return false;

		for (const auto &language : languages) {
			if (language.getName().compareToIgnoreCase(kLanguageDir))
				continue;
			SearchMan.addDirectory(Common::String(tree), language);
			return true;
		}
	}

	warning("could not find %s/%s in the game directory", tree, kLanguageDir);
	return false;
}

AlienEngine::AlienEngine(OSystem *syst, const ADGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _spriteFrame(0), _spriteBank(0),
		_room(0), _secondPlate(false), _roomWidth(kScreenWidth), _scrollX(0), _musicSlot(-1), _liftPlayed(false), _showWalk(false), _lastTick(0), _tick(0),
		_hover(-1), _hoverSlot(-1), _hoverArrow(Inventory::kArrowNone),
		_heldItem(Inventory::kNoItem), _pendingItem(Inventory::kNoItem),
		_pending(-1), _pendingOutcome(0),
		_armed(0), _armedX(0), _armedY(0), _armedFacing(Walker::kFacingKeep), _mode(0),
		_lastSubmode(0),
		_queueCount(0), _queueNext(0), _speechTal(nullptr), _labelSlot(0), _dialogId(1), _dialogBand(false),
		_speech(false), _speechTicks(0), _speechX(kAnchorX), _speechY(kAnchorY),
		_dirty(true), _quit(false), _cutscene(false), _cutsceneFast(false),
		_endingStep(0), _endingPos(0), _endingLoop(false), _won(false),
		_playIndex(0), _playActive(false), _playLastTick(0), _playWaitTicks(0),
		_playSettleTimeout(0), _playSettling(false), _playFails(0) {
	memset(_palette, 0, sizeof(_palette));
	memset(_outcomeCounter, 0, sizeof(_outcomeCounter));
	memset(_queue, 0, sizeof(_queue));

	// An anim_play effect in a room script drives the slots directly, the way the
	// overlay's own body calls MIDAS, and inv_add / inv_remove work the list.
	_script.setAnims(&_anims);
	_script.setInventory(&_inventory);
	_script.setSound(&_sound);
	_script.setEngine(this);
}

AlienEngine::~AlienEngine() {
	_screen.free();
	_background.free();
	_occluder.free();
}

Common::Error AlienEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	_sound.init(_mixer);

	// The text and label trees are subdirectories, so they need registering
	// before Common::File can reach into them by name.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	addTextTree(gameDataDir, kScriptTree);
	addTextTree(gameDataDir, kLabelTree);

	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (!_tables.load())
		return Common::Error(Common::kReadingFailed, "Could not read the tables in GAME.EXE");

	if (!_overlays.load())
		return Common::Error(Common::kReadingFailed, "Could not index the scene overlays");

	if (!_font.load())
		return Common::Error(Common::kReadingFailed, "Could not load the font");

	if (!_labelFont.load(Font::kLabel))
		return Common::Error(Common::kReadingFailed, "Could not load the label font");

	// The shared script is resident in the original for the whole game, and the
	// generic click path reads it whatever room the player is in.
	if (!_talkall.load(Common::Path(kSharedScript)))
		warning("could not load %s: objects with no outcome will stay silent", kSharedScript);

	// The icon page, the bar's chrome and the item names. A failure here leaves
	// the bar empty rather than stopping the game.
	_inventory.load();

	// The player character's frames are one set among six and live outside any
	// room, so they are read once and kept for the whole session.
	if (!_ben.load("BENANI"))
		warning("could not load the player character's animation set");

	// --boot-param picks the room to open in, which is how the room-by-room
	// checks are driven without clicking through the game to get there.
	const int start = ConfMan.hasKey("boot_param") ? ConfMan.getInt("boot_param")
												   : kStartRoom;
	// AI.COM runs ANIMPLAY on the intro before it starts the game, so the intro
	// belongs to a plain new game and to nothing else: a run that names a room
	// or turns a debug channel on is a check, and goes straight to the room.
	// It plays before the first room for the same reason it did there.
	if (!ConfMan.hasKey("boot_param") && gDebugLevel <= 0 && !playCutscene(kCutscenes[0]))
		warning("%s is not in the search path, so the intro is being skipped: it ships "
				"on the CD rather than in the installed game, and reaching it needs "
				"--extrapath pointed at the CDA directory", kCutscenes[0]);

	if (!loadRoom(start))
		return Common::Error(Common::kReadingFailed, "Could not load the starting room");

	if (debugChannelSet(2, kDebugRooms))
		tourRooms();

	// Nothing in a scripted run reaches the pod with [0xa7d2] set yet, so the
	// channel starts the sequence itself: --debugflags=ending -b 59.
	if (debugChannelSet(-1, kDebugEnding))
		armEnding();

	// The items channel prints what tools/check_inventory.py mirrors. Level 4 is a
	// job of its own -- it clicks the room and needs the list a new game leaves --
	// so the levels below it, which prime the list to show the bar paging and
	// rotate the look counters, do not run under it.
	if (debugChannelSet(4, kDebugItems)) {
		sweepClicks();
	} else if (debugChannelSet(-1, kDebugItems)) {
		if (debugChannelSet(2, kDebugItems)) {
			for (byte item = 1; item <= 13; item++)
				_inventory.add(item);
		}
		dumpItems();
		dumpItemUses();

		// Level 3 rotates every item's own click counter past its end, which is
		// the half of the record a single look never shows.
		if (debugChannelSet(3, kDebugItems))
			sweepItemLooks();
	}

	// The sound channel prints what tools/check_sfx.py mirrors: the banks and
	// their samples with the room-to-bank table, and at level 2 every trigger the
	// script table holds with the sample its own room's bank gives it.
	if (debugChannelSet(-1, kDebugSound)) {
		dumpSfx();
		if (debugChannelSet(2, kDebugSound))
			sweepSounds();
		if (debugChannelSet(3, kDebugSound))
			sweepVoices();
	}

	// The save channel prints what tools/check_save.py mirrors: what the
	// original's own saves hold, and at level 3 the engine's state taken through
	// a save and back.
	if (debugChannelSet(-1, kDebugSave)) {
		dumpSaves();
		if (debugChannelSet(3, kDebugSave))
			checkSaveRoundTrip();
		// Level 4 is the one the install's own saves cannot reach: they all carry
		// an empty inventory, so the list is patched into a real block first.
		if (debugChannelSet(4, kDebugSave))
			checkDosItemImport();
	}

	// The cutscene channel prints what tools/check_cutscenes.py mirrors: the
	// lifted tables, and at level 2 every record played end to end -- the same
	// step interpreter a triggered scene runs, with the clock taken out.
	if (debugChannelSet(-1, kDebugCutscene)) {
		dumpCutscenes();
		if (debugChannelSet(2, kDebugCutscene))
			sweepCutscenes();
	}

	// The occlusion channel prints what tools/check_occlusion.py mirrors: every
	// foreground rectangle a room stamps back over the character.
	if (debugChannelSet(-1, kDebugOcclusion))
		dumpOcclusion();

	// The music channel prints what tools/check_music.py mirrors: the module and
	// slot tables, then the sequencer walked row by row, then the loudness of the
	// rendered output second by second.
	if (debugChannelSet(-1, kDebugMusic)) {
		dumpMusic();
		sweepMusicCues();
		if (debugChannelSet(2, kDebugMusic))
			sweepMusicRows();
		if (debugChannelSet(3, kDebugMusic))
			renderMusic();
	}

	// The video channel prints what tools/check_video.py mirrors: what the two
	// containers say about themselves, then a checksum of every decoded frame,
	// then every subtitle line in every language the files carry.
	if (debugChannelSet(-1, kDebugVideo)) {
		dumpVideo();
		if (debugChannelSet(2, kDebugVideo))
			sweepVideoFrames();
		if (debugChannelSet(3, kDebugVideo))
			sweepSubtitles();
	}

	// The play channel drives the game from a command file instead of the
	// event queue -- see play.h and docs/port_plan.md, milestone S -- because
	// a headless run has no interactive input to script against.
	if (debugChannelSet(-1, kDebugPlay)) {
		// No generic --key=value escape hatch exists on the ScummVM command
		// line (unrecognized options abort with "usage"), so the script path
		// comes from the target's config file: -c a-throwaway.ini with a
		// "playscript" key in the [alien] section.
		if (!ConfMan.hasKey("playscript"))
			warning("play: no 'playscript' key in the config; nothing to run");
		else if (loadPlayScript(ConfMan.get("playscript")))
			_playActive = true;
	}

	while (!shouldQuit() && !_quit) {
		handleEvents();
		stepClock();
		if (_playActive)
			stepPlayScript();
		if (_dirty)
			redraw();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	// AI.COM plays the ending clip when GAME.EXE leaves with 0x7b, so the port
	// plays it after its own loop has ended rather than inside the last room
	// (ending.cpp, docs/playthrough_findings.md finding #19).
	// A debug run is a check rather than a game, so it stops at the win the way
	// it starts without the intro instead of sitting through eleven minutes of
	// video.
	if (_won && !shouldQuit() && gDebugLevel <= 0)
		playCutscene(kCutscenes[1]);
	else if (_won)
		debugC(1, kDebugEnding, "ending: %s not played in a debug run", kCutscenes[1]);

	return Common::kNoError;
}

bool AlienEngine::loadPlayScript(const Common::String &path) {
	if (!_play.load(path))
		return false;
	_playIndex = 0;
	_playLastTick = _tick;
	_playWaitTicks = 0;
	_playSettling = false;
	_playFails = 0;
	debugC(1, kDebugPlay, "play: %u commands loaded from %s", _play.commands().size(),
		   path.c_str());
	return true;
}

bool AlienEngine::playIdle() const {
	// The speech queue counts as well as the line standing on screen: a body
	// that queued two lines is between them for a tick or two, and a click
	// landing in that gap is eaten as "cut the line short" rather than acted
	// on -- which is what made a scripted second click on the same box look
	// like it had hit nothing.
	return !_ben.isWalking() && !_ben.isTurning() && !_speech &&
		   _queueNext >= _queueCount && !_anims.isBusy() && _pending < 0 && !_armed;
}

void AlienEngine::stepPlayScript() {
	// Paced on the master tick, not the loop iteration -- the loop spins
	// faster than 70Hz while it waits for stepClock()'s own gate.
	if (_tick == _playLastTick)
		return;
	_playLastTick = _tick;

	if (_playWaitTicks > 0) {
		_playWaitTicks--;
		return;
	}

	if (_playSettling) {
		if (playIdle()) {
			_playSettling = false;
		} else if (--_playSettleTimeout <= 0) {
			_playSettling = false;
			debugC(1, kDebugPlay, "play: %u: STUCK, gave up waiting to settle",
				   _play.commands()[_playIndex - 1].sourceLine);
		} else {
			return;
		}
	}

	if (_playIndex >= _play.commands().size()) {
		debugC(1, kDebugPlay, "play: script complete, %u assertion failure(s)", _playFails);
		_playActive = false;
		_quit = true;
		return;
	}

	runPlayCommand(_play.commands()[_playIndex++]);
}

void AlienEngine::runPlayCommand(const PlayCommand &cmd) {
	switch (cmd.type) {
	case PlayCommand::kClick:
		debugC(1, kDebugPlay, "play: %u: click %d,%d", cmd.sourceLine, cmd.a, cmd.b);
		clickAt(cmd.a, cmd.b, false);
		break;

	case PlayCommand::kRightClick:
		debugC(1, kDebugPlay, "play: %u: rclick %d,%d", cmd.sourceLine, cmd.a, cmd.b);
		clickAt(cmd.a, cmd.b, true);
		break;

	case PlayCommand::kUse:
		debugC(1, kDebugPlay, "play: %u: use %d (%s)", cmd.sourceLine, cmd.a,
			   _inventory.name((byte)cmd.a).c_str());
		holdItem((byte)cmd.a);
		break;

	case PlayCommand::kUnuse:
		debugC(1, kDebugPlay, "play: %u: unuse", cmd.sourceLine);
		holdItem(Inventory::kNoItem);
		break;

	case PlayCommand::kWait:
		debugC(1, kDebugPlay, "play: %u: wait %d", cmd.sourceLine, cmd.a);
		_playWaitTicks = cmd.a;
		break;

	case PlayCommand::kSettle:
		debugC(1, kDebugPlay, "play: %u: settle (timeout %d)", cmd.sourceLine, cmd.a);
		_playSettling = true;
		_playSettleTimeout = cmd.a;
		break;

	case PlayCommand::kExpectRoom:
		if (_room == cmd.a) {
			debugC(1, kDebugPlay, "play: %u: PASS room %d", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL room: expected %d, got %d", cmd.sourceLine,
				   cmd.a, _room);
			_playFails++;
		}
		break;

	case PlayCommand::kExpectItem:
		if (_inventory.has((byte)cmd.a)) {
			debugC(1, kDebugPlay, "play: %u: PASS item %d held", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL item: expected %d held", cmd.sourceLine, cmd.a);
			_playFails++;
		}
		break;

	case PlayCommand::kExpectNoItem:
		if (!_inventory.has((byte)cmd.a)) {
			debugC(1, kDebugPlay, "play: %u: PASS noitem %d", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL noitem: %d unexpectedly held", cmd.sourceLine,
				   cmd.a);
			_playFails++;
		}
		break;

	case PlayCommand::kExpectFlag: {
		const byte got = _script.flag((uint16)cmd.a);
		if (got == (byte)cmd.b) {
			debugC(1, kDebugPlay, "play: %u: PASS flag 0x%04x == %d", cmd.sourceLine, cmd.a,
				   cmd.b);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL flag 0x%04x: expected %d, got %d",
				   cmd.sourceLine, cmd.a, cmd.b, got);
			_playFails++;
		}
		break;
	}

	case PlayCommand::kCutscene:
		debugC(1, kDebugPlay, "play: %u: cutscene %d", cmd.sourceLine, cmd.a);
		triggerCutscene((byte)cmd.a);
		break;

	case PlayCommand::kSnap: {
		const Common::String name =
			cmd.s.empty() ? Common::String::format("play-%u.png", cmd.sourceLine)
						  : cmd.s + ".png";
		debugC(1, kDebugPlay, "play: %u: snap %s", cmd.sourceLine, name.c_str());
		redraw();
		dumpScreen(name);
		break;
	}

	case PlayCommand::kSpots:
		debugC(1, kDebugPlay, "play: %u: spots: room %d, %u registered", cmd.sourceLine, _room,
			   _spots.size());
		for (uint i = 0; i < _spots.size(); i++) {
			const Hotspot &spot = _spots[i];
			Common::String outcomes;
			for (uint o = 0; o < spot.outcomeCount; o++)
				outcomes += Common::String::format("%s%u", o ? "," : "", spot.outcomes[o]);
			debugC(1, kDebugPlay, "play: spot: %3d,%3d..%3d,%3d obj %3u verb %3u label %3u -> %s",
				   spot.x1, spot.y1, spot.x2, spot.y2, spot.obj, spot.verb, spot.label,
				   outcomes.c_str());
		}
		break;

	case PlayCommand::kQuit:
		debugC(1, kDebugPlay, "play: %u: quit", cmd.sourceLine);
		_playActive = false;
		_quit = true;
		break;
	}
}

/**
 * Room's total pixel width, `[0xa0c0]` as set by that room's own overlay init
 * code (default 0x140 = 320, `seg_main.asm:544`). Found by grepping every
 * overlay for `[0xa0c0]`; see docs/playthrough_findings.md. 15 rooms are wider
 * than the 320px screen and pan with `_scrollX` (sub_13bce); the rest are a
 * no-op override (<= 320) and stay fixed.
 */
int AlienEngine::roomWidth(int room) {
	switch (room) {
	case 3:  return 0x18e;
	case 8:  return 0x17c;
	case 15: return 0x260;
	case 21: return 0x268;
	case 22: return 0x27e;
	case 27: return 0x1f0;
	case 32: return 0x1e0;
	case 50: return 0x190;
	case 52: return 0x27e;
	case 53:
	case 54:
	case 55:
	case 56:
	case 57: return 0x1b8;
	case 58: return 0x27e;
	default: return kScreenWidth;
	}
}

const char *AlienEngine::charPaletteFile(int room) {
	// Ben's own colours (palette indices 1..24) are not part of the room's
	// background plate -- the original reloads them from a separate PCX after
	// every room's palette fade (seg_util.asm sub_02091, fed by
	// load_char_palette in seg_obj.asm), which is why a background PCX is
	// free to put anything unrelated in that range. VAKIPAL.PCX is the
	// default (set at boot, seg_main.asm); room 52 is the one confirmed
	// override found so far (SEC_BPAL.PCX, ovr_34_0f96). MANPAL0/1/2.PCX also
	// ship but their room association hasn't been traced in disasm/ yet --
	// left as a TODO rather than guessed.
	switch (room) {
	case 52: return "SEC_BPAL.PCX";
	default: return "VAKIPAL.PCX";
	}
}

void AlienEngine::applyCharPalette(int room) {
	Graphics::Surface dummy;
	byte charPalette[256 * 3];
	if (!loadGamePCX(Common::Path(charPaletteFile(room)), dummy, charPalette)) {
		dummy.free();
		return;
	}
	dummy.free();

	// Indices 1..24 only -- everything else in this file is unused filler.
	memcpy(_palette + 1 * 3, charPalette + 1 * 3, 24 * 3);
}

bool AlienEngine::loadRoom(int room, bool secondPlate) {
	const int width = roomWidth(room);
	const bool wide = width > kScreenWidth;

	// Wide rooms show both plates stitched side by side and panned by the
	// scroll offset; the manual A/B toggle (the 'b' debug key) only makes
	// sense for the narrow rooms whose B plate is a genuine alternate view,
	// not a second half.
	const Common::String plate = (secondPlate && !wide) ? _tables.secondPlate(room)
														 : roomPlate(room);
	if (plate.empty()) {
		debugC(1, kDebugResource, "room %d has no %s plate", room,
			   secondPlate ? "second" : "background");
		return false;
	}

	// Eleven of the names in the table belong to cut rooms whose art never
	// shipped, so a failure here is expected and must leave the current room
	// standing.
	Graphics::Surface loaded;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(plate), loaded, palette)) {
		loaded.free();
		return false;
	}

	if (wide) {
		// Plate A supplies the left 320 columns, plate B the remainder
		// (docs/room_deck.md), mirroring the two EMS pages the original pans
		// between. A room missing its B plate just shows padding past 320.
		Graphics::Surface stitched;
		stitched.create(width, loaded.h, loaded.format);
		stitched.fillRect(Common::Rect(0, 0, width, loaded.h), 0);
		for (int y = 0; y < loaded.h; y++)
			memcpy(stitched.getBasePtr(0, y), loaded.getBasePtr(0, y), loaded.w);

		const Common::String &secondName = _tables.secondPlate(room);
		if (!secondName.empty()) {
			Graphics::Surface second;
			byte palette2[256 * 3];
			if (loadGamePCX(Common::Path(secondName), second, palette2)) {
				const int copyW = MIN<int>(second.w, width - kScreenWidth);
				const int copyH = MIN<int>(second.h, loaded.h);
				for (int y = 0; y < copyH; y++)
					memcpy(stitched.getBasePtr(kScreenWidth, y), second.getBasePtr(0, y), copyW);
			}
			second.free();
		}

		loaded.free();
		loaded = stitched;
	}

	_background.free();
	_background = loaded;
	_roomWidth = wide ? width : kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));
	applyCharPalette(room);
	loadOccluder(room);

	// The sprite banks and the dialog file are named by the room's own scene
	// overlay. Rooms driven from a resident segment have no overlay, and those
	// keep an empty manifest until their code is understood.
	_overlays.readRoom(room, _assets);
	loadSpriteBank(0);

	// The walk mask and the node ring, so a click can be routed. Rooms with no
	// KIERRA files have no free movement at all, and those keep an empty mask.
	_walk.load(room, _assets);
	_route.count = 0;

	_spots.clear();
	_hover = -1;
	_hoverSlot = -1;
	_hoverArrow = Inventory::kArrowNone;
	_pending = -1;
	_pendingItem = Inventory::kNoItem;
	_armed = 0;
	stopSpeech();

	// The room's sample bank, and with it the queue flush the original does on
	// the way out of a room. Loaded before the script is entered, in case the
	// room's opening setup triggers an effect.
	if (_sound.enterRoom(_tables, room))
		debugC(1, kDebugSound, "room %d: bank %d %s, %u slots", room, _sound.bankIndex(),
			   _sound.bank().file().c_str(), _sound.bank().slotCount());

	// The banks the room's animation slots play, from the overlay's own load
	// calls. Loaded before the script is entered, because entering it runs the
	// room's opening plays against these slots.
	_anims.loadRoom(room);

	// What the room does with a click on its own account, lifted out of its
	// overlay by tools/gen_roomscripts.py, plus the opening frame of every slot
	// from tools/gen_roominit.py. The state block is not touched here: puzzle
	// flags outlive the room they were set in.
	_script.enterRoom(room);

	// The rectangles the room registers, by running entry 1 of its overlay as
	// tools/gen_hotspots.py lifted it. Which ones exist depends on the puzzle
	// state, so this runs after the script has the room bound and again
	// whenever a click has moved the state.
	_script.buildHotspots(room, _spots);

	// With the anim channel on, the room opens with everything in it moving
	// rather than in its opening state -- a way to see every bank a room holds
	// without hunting for the click that plays it. The 'a' key repeats it.
	if (debugChannelSet(-1, kDebugAnim))
		_anims.playAll(kDebugAnimRate);

	// Where the character enters a room is the room script's business, and none
	// of that is ported, so he is put on the first walk node -- a place the
	// room itself says is floor.
	if (_walk.nodes().count())
		_ben.place(_walk.nodes().x(0), _walk.nodes().y(0));
	else
		_ben.place(kScreenWidth / 2, 140);

	// Most rooms name a room<n>.tal, but several speak through a shared file,
	// and the ones without an overlay fall back to the naming convention.
	Common::Path script(_assets.script);
	if (_assets.script.empty())
		script = Common::Path(Common::String::format("ROOM%d.TAL", room));

	if (Common::File::exists(script))
		_tal.load(script);
	else
		_tal.unload();

	// The hover names live in a file of their own, laid out like the script but
	// with only the text zone filled in. Rooms whose objects were never given
	// names have no file at all.
	const Common::Path labels(Common::String::format("R%d.TAL", room));
	if (Common::File::exists(labels))
		_labels.load(labels);
	else
		_labels.unload();

	// White is what the dialog unit resets the text entry to before every
	// line; the per-speaker colours are set by the room code that triggers
	// the line, and none of that exists yet.
	setTextColor(0x3F, 0x3F, 0x3F);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);

	debugC(1, kDebugResource,
		   "room %d %c: %s, %dx%d plate, %u sprite frames, %u dialog entries, "
		   "%u labels, %u hotspots",
		   room, secondPlate ? 'B' : 'A', plate.c_str(), _background.w, _background.h,
		   _sprite.frameCount(), _tal.usedEntries(), _labels.usedEntries(), _spots.size());

	_room = room;

	// The escape pod runs its own sequence as it opens, once the pod is ready
	// to leave; every other room, and the pod before then, does nothing here.
	startEnding();

	// And the scenes the room raises on entry, which is the last thing the
	// original's enter routine does that the port had not got to.
	roomCutscenes(room);

	// And with the walk channel on, every hotspot of the room is clicked on
	// paper and the resolved walk target printed, which is what
	// tools/check_walkgeom.py --sweep prints from the table it generated. The
	// 'g' key repeats it.
	if (debugChannelSet(-1, kDebugWalk))
		sweepWalkGeometry();
	if (debugChannelSet(-1, kDebugHotspots))
		dumpHotspots();
	if (debugChannelSet(-1, kDebugRooms))
		dumpExits();
	_secondPlate = secondPlate;
	_spriteFrame = 0;
	_dialogId = 1;
	_labelSlot = 0;
	_dirty = true;

	// The elevator clip, on the way into one of the rooms the lift serves. The
	// original plays it from the top of those rooms' overlay entry 2, which is
	// the room's own opening code, so it runs after everything else is in place.
	for (uint i = 0; i < ARRAYSIZE(kLiftRooms); i++) {
		if (room == kLiftRooms[i] && gDebugLevel <= 0) {
			playLift();
			break;
		}
	}

	updateScroll();

	return true;
}

/**
 * Recompute the live scroll offset the way `sub_13bce` does every frame:
 * `clamp(ben.x - 160, 0, roomWidth - 320)`, camera centered on Ben. Narrow
 * rooms (`_roomWidth == kScreenWidth`) always resolve to zero.
 */
void AlienEngine::updateScroll() {
	int scroll = 0;
	if (_roomWidth > kScreenWidth) {
		scroll = _ben.walkX() - kScreenWidth / 2;
		if (scroll < 0)
			scroll = 0;
		if (scroll > _roomWidth - kScreenWidth)
			scroll = _roomWidth - kScreenWidth;
	}
	if (scroll != _scrollX) {
		_scrollX = scroll;
		_dirty = true;
	}
}

void AlienEngine::stepRoom(int delta) {
	// Walks past the slots that hold no plate — those are the rooms that
	// never had a background of their own, see docs/rooms.md.
	for (int i = 0; i < StaticTables::kRoomCount; i++) {
		int room = (_room - 1 + delta * (i + 1)) % StaticTables::kRoomCount;
		if (room < 0)
			room += StaticTables::kRoomCount;
		if (loadRoom(room + 1))
			return;
	}
}

void AlienEngine::loadSpriteBank(uint bank) {
	// One bank at a time: which of a room's banks are on screen, and at which
	// frame, is what the room's own code decides, and none of that is ported
	// yet, so drawing them all at once would only pile doors on top of doors.
	_sprite.unload();
	_spriteBank = bank;
	_spriteFrame = 0;
	_dirty = true;

	if (bank >= _assets.spriteCount)
		return;

	if (!_sprite.load(Common::Path(_assets.sprites[bank])))
		return;

	debugC(1, kDebugResource, "sprite bank %u/%u: %s, %u frames", bank + 1,
		   _assets.spriteCount, _assets.sprites[bank].c_str(), _sprite.frameCount());
}

void AlienEngine::stepSpriteBank(int delta) {
	if (!_assets.spriteCount)
		return;

	const int count = (int)_assets.spriteCount;
	int bank = ((int)_spriteBank + delta) % count;
	if (bank < 0)
		bank += count;
	loadSpriteBank((uint)bank);
}

void AlienEngine::walkTo(int x, int y, int arrivalFacing) {
	if (!_walk.plotRoute(_ben.walkX(), _ben.walkY(), x, y, _route)) {
		debugC(1, kDebugGraphics, "walk to %d,%d: room %d has no walk mask", x, y, _room);
		return;
	}

	debugC(1, kDebugGraphics, "walk %d,%d -> %d,%d: %u waypoints, target %s, facing %d",
		   _ben.walkX(), _ben.walkY(), x, y, _route.count,
		   _walk.mask().blocked(x, y) ? "blocked" : "walkable", arrivalFacing);

	_ben.follow(_route, x, y, arrivalFacing);
	_dirty = true;
}

void AlienEngine::dumpHotspots() {
	// What entry 1 registered for this room in the state the game is in.
	// tools/check_hotspots.py --sweep prints the same lines out of the table it
	// generated, so the interpreter can be diffed against the extraction.
	debugC(1, kDebugHotspots, "spots: room %d, %u registered", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		Common::String outcomes;
		for (uint o = 0; o < spot.outcomeCount; o++)
			outcomes += Common::String::format("%s%u", o ? "," : "", spot.outcomes[o]);

		debugC(1, kDebugHotspots, "spot: %3d,%3d..%3d,%3d obj %3u verb %3u label %3u -> %s",
			   spot.x1, spot.y1, spot.x2, spot.y2, spot.obj, spot.verb, spot.label,
			   outcomes.c_str());
	}
}

void AlienEngine::sweepWalkGeometry() {
	// Resolve a click on the middle of every hotspot in the room and print what
	// the geometry made of it. tools/check_walkgeom.py --sweep prints the same
	// lines straight from the table, so the two can be diffed to check the
	// interpreter rather than only the extraction.
	debugC(1, kDebugWalk, "geom: room %d, %u hotspots", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		const int x = (spot.x1 + spot.x2) / 2;
		const int y = (spot.y1 + spot.y2) / 2;

		WalkTarget target;
		if (!_script.walkTarget(x, y, spot.obj, target))
			continue;

		debugC(1, kDebugWalk, "geom: obj %3u click %3d,%3d -> %3d,%3d facing %2u submode %u",
			   spot.obj, x, y, target.x, target.y, target.facing, target.submode);

		// And the route the walker would take there from where he stands, so the
		// router can be diffed against tools/kierra.py the same way.
		WalkRoute route;
		if (!_walk.plotRoute(_ben.walkX(), _ben.walkY(), target.x, target.y, route))
			continue;

		Common::String path;
		for (uint p = 0; p < route.count; p++)
			path += Common::String::format("%s%d,%d", p ? " " : "",
										   route.points[p].x, route.points[p].y);
		debugC(1, kDebugWalk, "route: %s", path.c_str());
	}
}

void AlienEngine::stepClock() {
	// docs/timing.md: the master tick is the ~70 Hz retrace, and two dividers
	// sit under it. Animation runs on every fourth tick, roughly 17.5 Hz, while
	// the dialog countdown is decremented under the first divider only
	// (OBJ:0x617c, gated on the tick pair), so it runs on every second tick.
	static const uint32 kTickMillis = 1000 / 70;

	const uint32 now = g_system->getMillis();
	if (now - _lastTick < kTickMillis)
		return;
	_lastTick = now;
	_tick++;

	// The delay queue is serviced every tick, but its countdowns only step on the
	// tick pair -- INPUT:0x5D9 tests the same [0xa5fc] the slots do.
	_sound.tick((_tick & 1) == 0);

	if ((_tick & 1) == 0) {
		// The elapsed-time counters the timed scenes run off, which the original
		// advances from the same tick pair (OBJ:sub_029ac).
		tickCutsceneTimers();

		// The animation slots advance under the same tick-pair gate as the
		// dialog countdown -- MIDAS:0x1a6a tests [0xa5fc], not the animation
		// gate -- so a slot's rate is in half ticks, about 35 Hz.
		if (_anims.isBusy()) {
			_anims.tick();
			_dirty = true;
		}

		if (_speech && _speechTicks > 0 && --_speechTicks == 0)
			nextSpeech();
	}

	if (_tick & 3)
		return;

	stepEnding();

	if (_ben.isWalking() || _ben.isTurning()) {
		_ben.tick();
		updateScroll();
		_dirty = true;
	} else if (_pending >= 0) {
		// He has arrived at what he was sent to; the outcome speaks now.
		finishAction();
	} else {
		// And if what he was sent to was a way out of the room, the exit fires
		// on the same arrival -- after the outcome, because the original tests
		// it at the end of the room's tick.
		checkExit();
	}
}

void AlienEngine::updateHover(int x, int y) {
	// Hotspot boxes are room-space (an object past x=320 in a wide room keeps
	// its authored coordinates); the incoming x is screen-space, so the scroll
	// offset goes back in before testing them. The bar below the playfield is
	// never panned, so it keeps the raw screen x.
	const int roomX = x + _scrollX;

	// The original registers a room's rectangles one after another and each
	// registration overwrites the globals on a hit, so where two overlap the
	// one registered last is the one that answers -- hence the whole table is
	// scanned rather than stopping at the first match.
	int hit = -1;
	for (uint i = 0; i < _spots.size(); i++) {
		if (_spots[i].contains(roomX, y))
			hit = (int)i;
	}

	const int slot = _inventory.slotAt(x, y);
	const Inventory::Arrow arrow = _inventory.arrowHover(x, y);

	if (hit == _hover && slot == _hoverSlot && arrow == _hoverArrow)
		return;

	_hover = hit;
	_hoverSlot = slot;
	_hoverArrow = arrow;
	_labelSlot = hit >= 0 ? _spots[hit].label : 0;
	_dirty = true;
}

bool AlienEngine::clickBar(int x, int y, bool rightButton) {
	// Everything below the playfield is the bar and the status line: the two
	// scroll arrows, the six item slots, and dead space. None of it walks.
	if (y <= kPlayfieldBottom)
		return false;

	const Inventory::Arrow arrow = _inventory.arrowAt(x, y);
	if (arrow != Inventory::kArrowNone) {
		if (!rightButton && _inventory.arrowEnabled(arrow)) {
			const bool moved = arrow == Inventory::kArrowUp ? _inventory.pageUp()
														   : _inventory.pageDown();
			if (moved) {
				debugC(1, kDebugItems, "bar: page %u of %u", _inventory.page(),
					   _inventory.pageCount());
				updateHover(x, y);
				_dirty = true;
			}
		}
		return true;
	}

	// A right click in the bar looks at the slot it is over and puts down
	// whatever is in hand -- the original does the look first, then clears the
	// mode (1021:0x754 and 1021:0x8f6) -- and a left click takes the item into
	// the hand, which is what puts following clicks under "Use to".
	const int slot = _inventory.slotAt(x, y);

	if (rightButton) {
		if (!_heldItem && slot >= 0)
			lookAtItem(_inventory.slotItem((uint)slot));
		if (_heldItem)
			holdItem(Inventory::kNoItem);
		return true;
	}

	if (slot >= 0)
		holdItem(_inventory.slotItem((uint)slot));
	return true;
}

void AlienEngine::holdItem(byte item) {
	_heldItem = item;
	_dirty = true;
	debugC(1, kDebugItems, "bar: holding item %u (%s)", item,
		   _inventory.name(item).c_str());
}

void AlienEngine::lookAtItem(byte item) {
	if (!item)
		return;

	// The answers to a look at a carried item are in the shared file, not the
	// room's own, and Ben is the one saying them.
	const byte code = _inventory.lookOutcome(_tables, item);
	if (!code)
		return;

	queueOutcome(_talkall, code, _ben.walkX(), _ben.walkY() - Walker::kWalkPointY);
}

void AlienEngine::sweepClicks() {
	// Every hotspot of the room clicked in turn, with an empty hand, and the list
	// printed after each one. This runs the room's own bodies, so the puzzle state
	// moves as it would in play -- it is a diagnostic, not a way to play.
	// tools/check_inventory.py --clicks-of ROOM mirrors it from the same tables.
	debugC(1, kDebugItems, "click-sweep: room %d, %u hotspots", _room, _spots.size());

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot spot = _spots[i];
		const bool handled = _script.run(spot.obj, spot.verb);

		Common::String carried;
		for (uint index = 1; index < Inventory::kListSize; index++) {
			if (_inventory.at(index))
				carried += Common::String::format(" %u", _inventory.at(index));
		}

		debugC(1, kDebugItems, "click-sweep: object %3u verb %2u %s carrying:%s",
			   spot.obj, spot.verb, handled ? "handled" : "passed ", carried.c_str());
	}

	// And then every combination the room's own script has a body for, with the
	// item put in hand first: this is the path a click takes with something held,
	// so it exercises the third part of the block key end to end.
	uint count = 0;
	const ScriptBlock *blocks = scriptForRoom(_room, count);
	for (uint i = 0; i < count; i++) {
		if (!blocks || blocks[i].item < 0)
			continue;

		const byte item = (byte)blocks[i].item;
		const byte obj = blocks[i].obj < 0 ? 0 : (byte)blocks[i].obj;
		_inventory.add(item);
		const bool handled = _script.run(obj, kVerbUseTo, item);
		debugC(1, kDebugItems, "use-sweep: item %2u on object %3u %s, %s",
			   item, obj, handled ? "handled" : "passed ",
			   _inventory.has(item) ? "still carried" : "given up");
	}
}

void AlienEngine::dumpItemUses() {
	// Every item-use combination the room-script table holds, in the order the
	// overlays wrote them. This is the third part of a block's key, and it is
	// what a click with something in hand matches on.
	// tools/check_inventory.py --uses prints the same lines out of the overlays.
	for (int room = 1; room <= StaticTables::kRoomCount; room++) {
		uint count = 0;
		const ScriptBlock *blocks = scriptForRoom(room, count);
		if (!blocks)
			continue;

		for (uint i = 0; i < count; i++) {
			if (blocks[i].item < 0)
				continue;
			debugC(1, kDebugItems, "use: room %2d block %2u item %2d (%s) on object %3d",
				   room, i, blocks[i].item,
				   _inventory.name((byte)blocks[i].item).c_str(), blocks[i].obj);
		}
	}
}

void AlienEngine::sweepItemLooks() {
	// Look at every item until its counter has been round its record and then
	// some, so the wrap or the stop shows. tools/check_inventory.py --look-sweep
	// prints the same lines.
	for (int item = 1; item <= StaticTables::kItemInUse; item++) {
		const int clicks = _tables.itemArity(item) + 2;
		for (int i = 0; i < clicks; i++)
			_inventory.lookOutcome(_tables, (byte)item);
	}
}

void AlienEngine::dumpSfx() {
	debugC(1, kDebugSound, "banks: %d in the table", StaticTables::kSfxBankCount);

	for (int bank = 0; bank < StaticTables::kSfxBankCount; bank++) {
		SoundBank loaded;
		if (!loaded.load(_tables.sfxName(bank)))
			continue;

		uint samples = 0;
		for (uint slot = 1; slot <= loaded.slotCount(); slot++) {
			if (loaded.sample(slot))
				samples++;
		}
		debugC(1, kDebugSound, "bank %2d %-18s %2u slots %2u samples", bank,
			   loaded.file().c_str(), loaded.slotCount(), samples);

		for (uint slot = 1; slot <= loaded.slotCount(); slot++) {
			const SoundBank::Sample *sample = loaded.sample(slot);
			if (!sample)
				continue;
			debugC(1, kDebugSound, "sample %2d %2u %6u bytes %5u Hz %s", bank, slot,
				   sample->length, sample->c2spd, sample->name.c_str());
		}
	}

	// Which bank each room's effects come out of. Index 0 is also the table's
	// fill value, so a room reading MAN_FX_1 may simply never have been assigned.
	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		const byte bank = _tables.sfxBank(room);
		debugC(1, kDebugSound, "room %2d bank %3d %s", room, bank,
			   bank == StaticTables::kSfxBankNone ? "-" : _tables.sfxName(bank).c_str());
	}
}

/**
 * Start a music slot, the way INPUT:music_play_slot does.
 *
 * The slot is not a module: two 14-byte tables turn it into a module index and
 * the order in that module's own list the track starts at, which is why a search
 * for module names over the overlays misses most of the music.
 */
void AlienEngine::playMusicSlot(uint slot) {
	if (slot >= (uint)StaticTables::kMusicSlotCount)
		return;

	stopMusic();

	const byte module = _tables.musicSlotModule(slot);
	const byte order = _tables.musicSlotOrder(slot);
	const Common::String &name = _tables.musicName(module);
	if (name.empty() || !_music.load(name)) {
		warning("could not load the music module for slot %u", slot);
		return;
	}

	_musicSlot = (int)slot;
	debugC(1, kDebugMusic, "slot %u: module %d %s at order %d, %u channels", slot, module,
		   name.c_str(), order, _music.channelCount());

	// The player owns nothing but the module, which outlives it, so the mixer is
	// free to dispose of the stream when the track is stopped.
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle,
					   new S3MPlayer(&_music, _mixer->getOutputRate(), order));
}

void AlienEngine::stopMusic() {
	if (_mixer->isSoundHandleActive(_musicHandle))
		_mixer->stopHandle(_musicHandle);
	_musicSlot = -1;
}

void AlienEngine::dumpMusic() {
	debugC(1, kDebugMusic, "music: %d modules, %d slots", StaticTables::kMusicCount,
		   StaticTables::kMusicSlotCount);

	for (int i = 0; i < StaticTables::kMusicCount; i++) {
		S3MModule module;
		if (!module.load(_tables.musicName(i)))
			continue;

		uint samples = 0;
		for (uint s = 1; s < module.sampleCount(); s++) {
			if (module.sample(s))
				samples++;
		}
		debugC(1, kDebugMusic, "module %2d %-18s %2u channels %3u orders %2u patterns "
			   "%2u samples speed %2u tempo %3u", i, _tables.musicName(i).c_str(),
			   module.channelCount(), module.orderCount(), module.patternCount(), samples,
			   module.speed(), module.tempo());
	}

	for (int slot = 0; slot < StaticTables::kMusicSlotCount; slot++) {
		const byte module = _tables.musicSlotModule(slot);
		debugC(1, kDebugMusic, "slot %2d module %2d order %3d %s", slot, module,
			   _tables.musicSlotOrder(slot), _tables.musicName(module).c_str());
	}
}

/** One effect's or block's guards, as `when [0xa6fa] == 0`. */
static Common::String condText(const ScriptCond *conds, uint count) {
	Common::String out;
	for (uint i = 0; i < count; i++) {
		out += i ? " && " : " when ";
		// An item guard names an item id, not an address (ScriptCondKind).
		if (conds[i].kind == kCondItem)
			out += Common::String::format("item %d %s %d", conds[i].addr,
										  conds[i].negate ? "!=" : "==", conds[i].value);
		else
			out += Common::String::format("[0x%04x] %s %d", conds[i].addr,
										  conds[i].negate ? "!=" : "==", conds[i].value);
	}
	return out;
}

/** The lifted opcodes by name, so a dumped effect reads as its call did. */
static const char *opName(byte op) {
	static const char *const kNames[] = {
		"unsupported", "set_flag", "set_action_handled", "set_game_submode",
		"queue_event", "anim_play_mode1", "anim_play_mode2", "anim_play_mode3",
		"sound", "play_sample", "inv_add", "inv_remove", "inv_has",
		"music_play_slot"
	};
	return op < ARRAYSIZE(kNames) ? kNames[op] : "?";
}

/**
 * The cutscenes: the scene-id dispatch, the records, and the lifted procedures.
 *
 * Nothing here runs a scene yet -- this is the table half of the launcher, and
 * printing it is how tools/check_cutscenes.py can check the lift against the
 * data segment and the CUTSCENE listing it came from.
 */
void AlienEngine::dumpCutscenes() {
	debugC(1, kDebugCutscene, "cutscenes: %d arms, %d records, %d procedures",
		   cutsceneArmCount(), cutsceneRecordCount(), cutsceneProcCount());

	for (uint i = 0; i < cutsceneArmCount(); i++) {
		const CutsceneArm &arm = *cutsceneArmAt(i);
		Common::String records;
		for (uint r = 0; r < arm.recordCount; r++)
			records += Common::String::format("%s%d", r ? "," : "", arm.records[r]);
		if (arm.studio)
			records = "studio";

		Common::String flags;
		uint count = 0;
		const ScriptEffect *effects = cutsceneArmEffects(arm, count);
		for (uint e = 0; e < count; e++)
			flags += Common::String::format(" [0x%04x] = %d", effects[e].args[0],
											effects[e].args[1]);

		debugC(1, kDebugCutscene, "arm %2d latch 0x%04x records %-6s%s%s", arm.id,
			   arm.latch, records.c_str(),
			   condText(arm.guards, arm.guardCount).c_str(), flags.c_str());
	}

	for (uint i = 0; i < cutsceneTriggerCount(); i++) {
		const CutsceneTrigger &trigger = *cutsceneTriggerAt(i);
		debugC(1, kDebugCutscene, "trigger room %2d scene %2d%s", trigger.room,
			   trigger.scene, condText(trigger.guards, trigger.guardCount).c_str());
	}

	for (uint n = 1; n <= cutsceneRecordCount(); n++) {
		const CutsceneRecord &rec = *cutsceneRecord(n);
		Common::String banks;
		for (uint b = 0; b < ARRAYSIZE(rec.banks); b++)
			if (rec.banks[b])
				banks += Common::String::format(" %d:%s", b, rec.banks[b]);

		Common::String subs;
		for (uint s = 0; s < ARRAYSIZE(rec.subProcs); s++)
			subs += Common::String::format("%s%d", s ? "," : "", rec.subProcs[s]);

		debugC(1, kDebugCutscene,
			   "record %2d %-13s %-13s music %2d proc %2d subs %s"
			   " pts %d,%d,%d,%d rgb %d,%d,%d,%d,%d,%d banks%s",
			   n, rec.pcx, rec.tal ? rec.tal : "-", rec.music,
			   rec.mainProc, subs.c_str(),
			   rec.points[0], rec.points[1], rec.points[2], rec.points[3],
			   rec.colors[0], rec.colors[1], rec.colors[2], rec.colors[3],
			   rec.colors[4], rec.colors[5], banks.c_str());

		uint stepCount = 0;
		const CutsceneStep *steps = cutsceneSteps(rec, stepCount);
		Common::String stream;
		for (uint s = 0; s < stepCount; s++) {
			stream += s ? " " : "";
			if (steps[s].op == kStepPause)
				stream += Common::String::format("pause %d", steps[s].arg);
			else if (steps[s].op == kStepBeat)
				stream += "beat";
			else
				stream += Common::String::format("%c:%d",
												 steps[s].op == kStepSpeakA ? 'a' : 'b',
												 steps[s].arg);
		}
		debugC(1, kDebugCutscene, "steps %2d %s", n, stream.c_str());
	}

	for (uint p = 0; p < cutsceneProcCount(); p++) {
		uint count = 0;
		const ScriptEffect *effects = cutsceneProcEffects(p, count);
		for (uint e = 0; e < count; e++) {
			const ScriptEffect &effect = effects[e];
			Common::String args;
			for (uint a = 0; a < effect.argCount; a++)
				args += Common::String::format("%s%d", a ? ", " : "", effect.args[a]);
			debugC(1, kDebugCutscene, "proc %2d 0c55:%04x %s(%s)%s", p,
				   cutsceneProcAddr(p), opName(effect.op), args.c_str(),
				   condText(effect.guards, effect.guardCount).c_str());
		}
	}
}

/**
 * The room's background plate.
 *
 * Room 7 is the one room that swaps its whole set on [0xa6fa]: its loader
 * (ovr_07_0e63:0x3f) takes the X names -- GAME7X/MSCR7X/FADE7X, the bedroom
 * with the light off -- while the flag is clear, and the plain ones once the
 * light switch sets it (bedroom.cpp). The room is entered in the dark, so the
 * table's own name is the exception here rather than the rule. Every other room
 * takes the table entry.
 */
Common::String AlienEngine::roomPlate(int room) const {
	if (room == 7 && !_script.flag(0xa6fa))
		return "GAME7X.PCX";

	return _tables.background(room);
}

/**
 * The room's foreground sheet: the page the original keeps at [0xd136].
 *
 * A narrow room's sheet is the second-plate table entry -- which is what the
 * table holds for it, mscr<n>.pcx -- while a wide room's entry is its plate B,
 * and the sheet is named by the room's own overlay instead. Room 7's sheet
 * follows [0xa6fa] the same way its plate does, MSCR7X.PCX while the light is
 * off and MSCR7.PCX once it is on.
 */
Common::String AlienEngine::occluderPlate(int room) const {
	if (room == 7)
		return _script.flag(0xa6fa) ? "MSCR7.PCX" : "MSCR7X.PCX";

	const Common::String &second = _tables.secondPlate(room);
	if (second.hasPrefixIgnoreCase("mscr"))
		return second;

	return Common::String::format("MSCR%d.PCX", room);
}

void AlienEngine::loadOccluder(int room) {
	_occluder.free();

	uint count = 0;
	if (!occlusionRects(room, count))
		return;			// nothing in this room the character can walk behind

	Graphics::Surface sheet;
	byte palette[256 * 3];
	const Common::String name = occluderPlate(room);
	if (!loadGamePCX(Common::Path(name), sheet, palette)) {
		sheet.free();
		warning("room %d: could not load the foreground sheet %s", room, name.c_str());
		return;
	}

	// The sheet's own palette is not loaded: the original reads it as indices
	// into the room's, which is why the pieces match the plate they came from.
	_occluder = sheet;
	debugC(1, kDebugOcclusion, "room %d: foreground sheet %s, %dx%d, %u rectangles",
		   room, name.c_str(), _occluder.w, _occluder.h, count);
}

/**
 * Stamp the room's foreground back over the character, as OBJ:sub_03400 does.
 *
 * Called with the character already drawn, once per rectangle the room's tick
 * lists. Only the part of a rectangle the character reaches into is copied --
 * the original tests his bounding box first and clips to it -- so an animation
 * slot playing under the same foreground keeps whatever it drew.
 */
void AlienEngine::applyOcclusion() {
	uint count = 0;
	const OcclusionRect *rects = occlusionRects(_room, count);
	if (!count || !_occluder.getPixels())
		return;

	Common::Rect ben;
	if (!_ben.bounds(ben))
		return;

	for (uint i = 0; i < count; i++) {
		const OcclusionRect &rect = rects[i];

		if (rect.benYBelow >= 0 && _ben.spriteY() >= rect.benYBelow)
			continue;

		uint guardCount = 0;
		const ScriptCond *guards = occlusionGuards(rect, guardCount);
		bool guarded = false;
		for (uint g = 0; g < guardCount; g++)
			guarded |= (_script.flag(guards[g].addr) == guards[g].value) == guards[g].negate;
		if (guarded)
			continue;

		Common::Rect area(rect.dstX, rect.dstY,
						  rect.dstX + rect.width, rect.dstY + rect.height);
		area.clip(ben);
		if (area.isEmpty())
			continue;

		// The blit moves whole words, so an odd overlap loses its last column
		// rather than rounding up. Keeping that is the difference between the
		// port's edges and the original's.
		const int width = area.width() & ~1;
		if (width <= 0)
			continue;

		for (int y = 0; y < area.height(); y++) {
			const int srcY = rect.srcY + (area.top - rect.dstY) + y;
			const int dstY = area.top + y;
			if (srcY < 0 || srcY >= _occluder.h || dstY < 0 || dstY >= _screen.h)
				continue;

			const byte *in = (const byte *)_occluder.getBasePtr(0, srcY);
			byte *out = (byte *)_screen.getBasePtr(0, dstY);
			for (int x = 0; x < width; x++) {
				const int srcX = rect.srcX + (area.left - rect.dstX) + x;
				const int dstX = area.left + x - _scrollX;
				if (srcX < 0 || srcX >= _occluder.w || dstX < 0 || dstX >= _screen.w)
					continue;
				if (in[srcX])
					out[dstX] = in[srcX];
			}
		}

		debugC(2, kDebugOcclusion, "room %d: rect %u covers %d,%d..%d,%d of the character",
			   _room, i, area.left, area.top, area.right, area.bottom);
	}
}

/**
 * Every foreground rectangle the port knows, room by room.
 *
 * tools/check_occlusion.py mirrors this from the overlay disassembly, which is
 * where tools/gen_occlusion.py lifted it from in the first place.
 */
void AlienEngine::dumpOcclusion() {
	debugC(1, kDebugOcclusion, "occlusion: %u rectangles", occlusionRectCount());

	for (int room = 0; room <= StaticTables::kRoomCount; room++) {
		uint count = 0;
		const OcclusionRect *rects = occlusionRects(room, count);
		for (uint i = 0; i < count; i++) {
			const OcclusionRect &rect = rects[i];
			Common::String ben;
			if (rect.benYBelow >= 0)
				ben = Common::String::format(" y < %d", rect.benYBelow);

			uint guardCount = 0;
			const ScriptCond *guards = occlusionGuards(rect, guardCount);
			Common::String flags;
			for (uint g = 0; g < guardCount; g++)
				flags += Common::String::format(" [0x%04x] %s %d", guards[g].addr,
												guards[g].negate ? "!=" : "==",
												guards[g].value);

			debugC(1, kDebugOcclusion,
				   "rect room %2d src %3d,%3d dst %3d,%3d size %3dx%3d%s%s", room,
				   rect.srcX, rect.srcY, rect.dstX, rect.dstY, rect.width, rect.height,
				   ben.c_str(), flags.c_str());
		}
	}
}

/**
 * Every music cue the port can fire, room by room.
 *
 * The original has no room-to-music table: a room starts its theme from its own
 * code, so the cues are wherever the lift found them -- in a room's opening
 * effects, or in the body of a click. Printing them together is the only way to
 * see which rooms have music at all, and tools/check_music.py --cues mirrors it
 * from the disassembly.
 */
void AlienEngine::sweepMusicCues() {
	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		uint initCount = 0;
		const ScriptEffect *init = roomInitEffects(room, initCount);
		for (uint i = 0; init && i < initCount; i++) {
			if (init[i].op != kOpMusic)
				continue;
			debugC(1, kDebugMusic, "cue room %2d entry           slot %2d%s", room,
				   init[i].args[0], condText(init[i].guards, init[i].guardCount).c_str());
		}

		uint blockCount = 0;
		const ScriptBlock *blocks = scriptForRoom(room, blockCount);
		for (uint b = 0; blocks && b < blockCount; b++) {
			const ScriptBlock &block = blocks[b];
			for (uint e = 0; e < block.count; e++) {
				const ScriptEffect &effect = *scriptEffect(block.first + e);
				if (effect.op != kOpMusic)
					continue;

				// A click's cue is guarded twice over: by the state the whole
				// body sits under, and by the arm inside it the cue is in.
				Common::String conds = condText(block.conds, block.condCount);
				const Common::String arms = condText(effect.guards, effect.guardCount);
				if (conds.empty())
					conds = arms;
				else if (!arms.empty())
					conds += " && " + Common::String(arms.c_str() + 6);

				debugC(1, kDebugMusic, "cue room %2d obj %3d verb %2d slot %2d%s", room,
					   block.obj, block.verb, effect.args[0], conds.c_str());
			}
		}
	}
}

/**
 * Step every slot's track row by row and print what the sequencer sees.
 *
 * This is the half of a replayer that can be checked exactly: which pattern each
 * order names, where Bxx and Cxx send the song next, and what every cell of the
 * rows it walks through holds.
 */
void AlienEngine::sweepMusicRows() {
	for (int slot = 0; slot < StaticTables::kMusicSlotCount; slot++) {
		const byte index = _tables.musicSlotModule(slot);
		S3MModule module;
		if (!module.load(_tables.musicName(index)))
			continue;

		S3MPlayer player(&module, kMusicRate, _tables.musicSlotOrder(slot));
		for (uint i = 0; i < kMusicRows; i++) {
			S3MPlayer::RowTrace trace;
			if (!player.traceRow(trace))
				break;

			for (uint channel = 0; channel < module.channelCount(); channel++) {
				const S3MModule::Cell &cell = module.cell(trace.pattern, trace.row, channel);
				if (cell.note == S3MModule::kNoteEmpty && !cell.instrument &&
					cell.volume == S3MModule::kVolumeEmpty && !cell.command)
					continue;

				debugC(2, kDebugMusic, "row slot %2d order %3u pat %3u row %2u ch %u "
					   "note %3u ins %2u vol %3u fx %c%02X", slot, trace.order, trace.pattern,
					   trace.row, channel, cell.note, cell.instrument, cell.volume,
					   cell.command ? (char)('A' + cell.command - 1) : '-', cell.info);
			}
		}
	}
}

/**
 * Render each module from its own start and print the loudness second by second.
 *
 * A replayer can walk the right rows and still play them wrong, and that is not
 * something a table mirror can catch. What it does show up in is the shape of the
 * output, so tools/check_video.py's neighbour tools/check_music.py renders the
 * same modules with a known-good tracker and compares these numbers.
 */
void AlienEngine::renderMusic() {
	Common::Array<int16> buffer;
	buffer.resize(kMusicRate);

	for (int i = 0; i < StaticTables::kMusicCount; i++) {
		S3MModule module;
		if (!module.load(_tables.musicName(i)))
			continue;

		S3MPlayer player(&module, kMusicRate, 0);
		for (uint second = 0; second < kMusicSeconds; second++) {
			player.readBuffer(&buffer[0], kMusicRate);

			uint64 square = 0;
			for (int s = 0; s < kMusicRate; s++)
				square += (int64)buffer[s] * buffer[s];

			const uint rms = (uint)sqrt((double)(square / kMusicRate));
			debugC(3, kDebugMusic, "rms %-18s second %2u %5u", _tables.musicName(i).c_str(),
				   second, rms);
		}
	}
}

/**
 * Show a clip, from its first frame to its last, with everything else stopped.
 *
 * Both players in the original are the same shape: the screen belongs to the
 * clip, the game loop does not run, and a keypress or a click ends it early.
 */
void AlienEngine::playVideo(Video::VideoDecoder &video, CDA2Decoder *subtitles) {
	video.start();

	bool skipped = false;
	while (!shouldQuit() && !skipped && !video.endOfVideo()) {
		if (video.needsUpdate()) {
			const Graphics::Surface *frame = video.decodeNextFrame();
			if (frame) {
				const int w = MIN<int>(frame->w, _screen.w);
				const int h = MIN<int>(frame->h, _screen.h);
				for (int y = 0; y < h; y++)
					memcpy(_screen.getBasePtr(0, y), frame->getBasePtr(0, y), w);

				// getPalette() clears the dirty flag, so what it says has to be
				// asked before the palette is taken.
				const bool changed = video.hasDirtyPalette();
				const byte *palette = video.getPalette();
				if (changed)
					g_system->getPaletteManager()->setPalette(palette, 0, 256);

				if (subtitles)
					drawSubtitle(*subtitles, palette);

				g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0,
										   _screen.w, _screen.h);
			}
			g_system->updateScreen();
		}

		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN || event.type == Common::EVENT_LBUTTONDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN)
				skipped = true;
		}

		g_system->delayMillis(10);
	}

	video.stop();
	video.close();

	// The room the clip interrupted owns the screen again, palette and all.
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;
}

/**
 * Draw the line the clip is on, in the brightest index its own palette holds.
 *
 * The original's player has a font of its own; this uses the game's, which puts
 * the line in the same place but not in the same face.
 */
void AlienEngine::drawSubtitle(const CDA2Decoder &video, const byte *palette) {
	const Common::String line = video.subtitle();
	if (line.empty() || !palette)
		return;

	byte ink = 0;
	int best = -1;
	for (int i = 0; i < 256; i++) {
		const int sum = palette[i * 3] + palette[i * 3 + 1] + palette[i * 3 + 2];
		if (sum > best) {
			best = sum;
			ink = (byte)i;
		}
	}

	// y is 172 in the file's own records, and a line break is '@'.
	int y = kSubtitleY;
	uint start = 0;
	while (start <= line.size()) {
		uint end = start;
		while (end < line.size() && line[end] != '\n')
			end++;

		const Common::String part(line.c_str() + start, end - start);
		if (!part.empty()) {
			const int x = (kScreenWidth - _font.measure(part)) / 2;
			_font.drawStringInk(_screen, part, x, y, ink);
			y += _font.glyphHeight() + 1;
		}

		if (end >= line.size())
			break;
		start = end + 1;
	}
}

/**
 * The elevator clip, on the first entry into a room the lift serves.
 *
 * 0FAE:sub_101a6 guards it with [0x33de], so it plays once a session however
 * often the player rides the lift afterwards.
 */
bool AlienEngine::playLift() {
	if (_liftPlayed)
		return false;

	MA1Decoder video;
	if (!video.loadFile(Common::Path(kLiftClip))) {
		debugC(1, kDebugVideo, "could not open %s", kLiftClip);
		return false;
	}

	_liftPlayed = true;
	debugC(1, kDebugVideo, "%s: %d frames", kLiftClip, video.getFrameCount());
	playVideo(video);
	return true;
}

/**
 * One of the two CDA2 cutscenes, if the CD's files are reachable.
 *
 * They live on the CD rather than in the installed game directory, so a copy
 * that was installed without them simply has no intro, which is what the
 * original does when the disc is missing too.
 */
bool AlienEngine::playCutscene(const char *file) {
	CDA2Decoder video;
	if (!video.loadFile(Common::Path(file))) {
		debugC(1, kDebugVideo, "could not open %s", file);
		return false;
	}

	video.setLanguage(subtitleLanguage());
	debugC(1, kDebugVideo, "%s: %u frames, %u Hz, %u languages, showing %u", file,
		   video.frameCount(), video.sampleRate(), video.languageCount(), video.language());

	playVideo(video, &video);
	return true;
}

/// Which of the file's four subtitle tracks this run's language asks for.
uint AlienEngine::subtitleLanguage() const {
	switch (getLanguage()) {
	case Common::DE_DEU:
		return CDA2Decoder::kGerman;
	case Common::FR_FRA:
		return CDA2Decoder::kFrench;
	case Common::FI_FIN:
		return CDA2Decoder::kFinnish;
	default:
		return CDA2Decoder::kEnglish;
	}
}

void AlienEngine::dumpVideo() {
	// What the two containers say about themselves, mirrored by
	// tools/check_video.py --info.
	MA1Decoder lift;
	if (lift.loadFile(Common::Path(kLiftClip))) {
		debugC(1, kDebugVideo, "ma1 %s: %d frames %dx%d", kLiftClip, lift.getFrameCount(),
			   lift.getWidth(), lift.getHeight());
		lift.close();
	}

	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;
		debugC(1, kDebugVideo, "cda2 %s: %u frames %dx%d %u Hz %u languages",
			   kCutscenes[i], video.frameCount(), video.getWidth(), video.getHeight(),
			   video.sampleRate(), video.languageCount());
		video.close();
	}
}

/**
 * Decode frames without showing them and print a checksum of each.
 *
 * Frames are differences against the frames before them, so a checksum that
 * matches the Python decoder's for frame n says every frame up to n decoded the
 * same way, palette and all.
 */
static uint32 frameChecksum(const Graphics::Surface &frame, const byte *palette) {
	uint32 sum = 2166136261u;
	const byte *pixels = (const byte *)frame.getPixels();
	for (int i = 0; i < frame.w * frame.h; i++)
		sum = (sum ^ pixels[i]) * 16777619u;
	if (palette) {
		for (int i = 0; i < 256 * 3; i++)
			sum = (sum ^ palette[i]) * 16777619u;
	}
	return sum;
}

void AlienEngine::sweepVideoFrames() {
	MA1Decoder lift;
	if (lift.loadFile(Common::Path(kLiftClip))) {
		for (uint i = 0; i < lift.getFrameCount() && !shouldQuit(); i++) {
			const Graphics::Surface *frame = lift.decodeNextFrame();
			if (!frame)
				break;
			debugC(2, kDebugVideo, "ma1 frame %4u %08x", i, frameChecksum(*frame, lift.getPalette()));
		}
		lift.close();
	}

	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;

		// The intro is 74 MB and the ending 41 MB; a prefix is enough to prove
		// the codec, and the whole file is what tools/check_video.py --frames
		// takes when it is asked for it.
		for (int f = 0; f < kSweepFrames && !shouldQuit(); f++) {
			const Graphics::Surface *frame = video.decodeNextFrame();
			if (!frame)
				break;
			debugC(2, kDebugVideo, "cda2 %s frame %4d %08x", kCutscenes[i], f,
				   frameChecksum(*frame, video.getPalette()));
		}
		video.close();
	}
}

void AlienEngine::sweepSubtitles() {
	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;

		for (uint lang = 0; lang < video.languageCount(); lang++) {
			video.setLanguage(lang);
			Common::String last;
			for (uint frame = 0; frame < video.frameCount(); frame++) {
				const Common::String line = video.subtitle(frame);
				if (line == last)
					continue;
				last = line;
				if (line.empty())
					continue;

				Common::String flat(line);
				for (uint c = 0; c < flat.size(); c++) {
					if (flat[c] == '\n')
						flat.setChar('@', c);
				}
				debugC(3, kDebugVideo, "subtitle %s lang %u frame %4u %s", kCutscenes[i],
					   lang, frame, flat.c_str());
			}
		}
		video.close();
	}
}

void AlienEngine::sweepSounds() {
	// Every sound the script table can make, with the sample it names resolved
	// through the bank the room it belongs to loads. A trigger naming an empty
	// slot would be silent in the original too, so it is reported rather than
	// treated as an error.
	SoundBank bank;
	int loadedIndex = -1;

	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		uint blockCount = 0;
		const ScriptBlock *blocks = scriptForRoom(room, blockCount);
		if (!blocks)
			continue;

		const byte index = _tables.sfxBank(room);
		if (index != StaticTables::kSfxBankNone && (int)index != loadedIndex)
			loadedIndex = bank.load(_tables.sfxName(index)) ? (int)index : -1;

		for (uint b = 0; b < blockCount; b++) {
			const ScriptBlock &block = blocks[b];
			for (uint e = 0; e < block.count; e++) {
				const ScriptEffect &effect = *scriptEffect(block.first + e);
				if (effect.op != kOpSound && effect.op != kOpPlaySample)
					continue;

				const uint slot = effect.args[0];
				const bool queued = effect.op == kOpPlaySample;
				const uint32 rate = queued ? (((uint32)effect.args[1] << 16) | effect.args[2])
										   : SoundFX::kDefaultRate;
				const byte volume = queued ? (byte)effect.args[3] : SoundFX::kFullVolume;
				const int panning = queued ? (int)(int8)(int16)effect.args[4] : 0;
				const uint delay = queued ? effect.args[5] : 0;
				const SoundBank::Sample *sample = bank.sample(slot);

				debugC(1, kDebugSound, "trigger: room %2d obj %3d verb %2d slot %2u "
					   "%5u Hz vol %2u pan %3d delay %3u bank %3d %s", room, block.obj,
					   block.verb, slot, rate, volume, panning, delay, loadedIndex,
					   sample ? sample->name.c_str() : "(empty slot)");
			}
		}
	}
}

void AlienEngine::sweepVoices() {
	// The two things about playback that are behaviour rather than table: the
	// three voices are handed out round-robin, so every fourth effect reuses the
	// first, and a queued trigger waits out its countdown on the tick pair. Both
	// are driven here against the room's own bank.
	const SoundBank &bank = _sound.bank();
	debugC(1, kDebugSound, "voices: bank %d %s, %u slots", _sound.bankIndex(),
		   bank.file().c_str(), bank.slotCount());

	Common::Array<uint> filled;
	for (uint slot = 1; slot <= bank.slotCount(); slot++) {
		if (bank.sample(slot))
			filled.push_back(slot);
	}

	for (uint i = 0; i < filled.size(); i++)
		_sound.play(filled[i], SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);

	// Queued out of order on purpose: the queue is scanned slot by slot, so what
	// decides the firing order is the countdown, not when the trigger was made.
	static const uint16 kDelays[] = { 3, 1, 0, 2 };
	for (uint i = 0; i < ARRAYSIZE(kDelays) && i < filled.size(); i++)
		_sound.queue(filled[i], SoundFX::kDefaultRate, SoundFX::kFullVolume, 0, kDelays[i]);

	for (uint tick = 0; tick < 8; tick++) {
		const bool pairTick = (tick & 1) == 0;
		_sound.tick(pairTick);
		debugC(1, kDebugSound, "queue: tick %u %s, %u waiting", tick,
			   pairTick ? "pair" : "odd ", _sound.pending());
	}
}

void AlienEngine::dumpItems() {
	// The static half of the inventory: what each item is called, where its icon
	// is cut from, and the codes a repeated look rotates through.
	// tools/check_inventory.py prints the same lines out of GAME.EXE.
	debugC(1, kDebugItems, "items: %d in the tables", StaticTables::kItemInUse);
	for (int item = 1; item <= StaticTables::kItemInUse; item++) {
		const byte arity = _tables.itemArity(item);
		Common::String codes;
		for (int c = 1; c <= arity; c++)
			codes += Common::String::format(" %u", _tables.itemOutcome(item, c));

		debugC(1, kDebugItems, "item %2d %-24s icon %3d,%3d arity %u %s outcomes:%s",
			   item, _inventory.name((byte)item).c_str(), _tables.itemIconX(item),
			   _tables.itemIconY(item), arity,
			   _tables.itemCycles(item) ? "cycles" : "clamps", codes.c_str());
	}

	debugC(1, kDebugItems, "bar: page %u of %u showing", _inventory.page(),
		   _inventory.pageCount());
	for (uint page = 1; page <= _inventory.pageCount(); page++) {
		for (uint slot = 0; slot < Inventory::kSlotCount; slot++) {
			debugC(1, kDebugItems, "bar: page %u slot %u item %3u %s", page, slot,
				   _inventory.itemOn(page, slot),
				   _inventory.name(_inventory.itemOn(page, slot)).c_str());
		}
	}
}

void AlienEngine::dumpExits() {
	// Every way out of the room the player can click on: the object, the submode
	// its approach point arms, and where the main loop's chain sends that pair.
	// tools/check_transitions.py --sweep prints the same lines out of the tables,
	// so the two interpreters can be diffed.
	debugC(1, kDebugRooms, "exits: room %d, %u hotspots", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		const int x = (spot.x1 + spot.x2) / 2;
		const int y = (spot.y1 + spot.y2) / 2;

		WalkTarget target;
		if (!_script.walkTarget(x, y, spot.obj, target) || !target.submode)
			continue;

		debugC(1, kDebugRooms, "exit: obj %3u submode %3u at %3d,%3d facing %2u -> room %d",
			   spot.obj, target.submode, target.x, target.y, target.facing,
			   _script.nextRoom((byte)_room, target.submode));
	}
}

bool AlienEngine::takeFirstExit() {
	// The debug shortcut behind the 'x' key: leave by the first exit the room
	// has, without walking there. Puts the character on its approach point first
	// so the arrival test sees what a real walk would leave behind.
	return takeAnyExit(nullptr, 0);
}

bool AlienEngine::arriveAt(const WalkTarget &target) {
	// Put the character where the walk would have left him and arm the exit the
	// same way a click does, so the tour runs the real arrival test rather than
	// jumping straight to the chain.
	const int room = _room;
	_ben.place(target.x, target.y,
			   target.facing == Walker::kFacingKeep ? _ben.facing() : target.facing);
	_armed = target.submode;
	_armedX = target.x;
	_armedY = target.y;
	_armedFacing = target.facing;
	checkExit();
	return _room != room;
}

bool AlienEngine::takeAnyExit(const int *avoid, uint avoidCount) {
	// The first exit of the room, preferring one that does not lead to a room in
	// `avoid` -- which is how the tour covers ground instead of stepping through
	// the same door and back again.
	int fallback = -1;

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		WalkTarget target;
		if (!_script.walkTarget((spot.x1 + spot.x2) / 2, (spot.y1 + spot.y2) / 2,
								spot.obj, target) || !target.submode)
			continue;

		bool seen = false;
		const int room = _script.nextRoom((byte)_room, target.submode);
		for (uint a = 0; a < avoidCount; a++)
			seen = seen || avoid[a] == room;

		if (seen) {
			if (fallback < 0)
				fallback = (int)i;
			continue;
		}

		return arriveAt(target);
	}

	if (fallback >= 0) {
		const Hotspot &spot = _spots[fallback];
		WalkTarget target;
		if (_script.walkTarget((spot.x1 + spot.x2) / 2, (spot.y1 + spot.y2) / 2,
							   spot.obj, target))
			return arriveAt(target);
	}

	debugC(1, kDebugRooms, "exits: room %d has none", _room);
	return false;
}

void AlienEngine::tourRooms() {
	// --debugflags=rooms --debuglevel=2 walks the mansion instead of playing it:
	// leave every room by its first exit and say where that landed, which runs
	// the whole path -- geometry, arrival, chain, room load -- without a player.
	// tools/check_transitions.py --tour prints the same lines from the tables.
	int visited[kTourHops + 1];
	uint count = 0;
	visited[count++] = _room;

	for (uint hop = 0; hop < kTourHops; hop++) {
		const int from = _room;
		if (!takeAnyExit(visited, count))
			break;
		debugC(2, kDebugRooms, "tour: room %d -> room %d", from, _room);
		visited[count++] = _room;
	}
}

void AlienEngine::checkExit() {
	// OBJ:sub_078dd, the half of the exit mechanism the walk geometry does not
	// hold: an armed submode fires only once the route has run out, the arrival
	// turn has played, and the feet are within three pixels of the point the
	// geometry named. Anything else -- he was interrupted, or the click sent him
	// somewhere the router could not reach exactly -- leaves the exit armed.
	if (!_armed || _speech)
		return;

	if (ABS(_ben.walkX() - _armedX) > 3 || ABS(_ben.walkY() - _armedY) > 3)
		return;

	// The facing test is a plain comparison in the original, against a global
	// that holds 10 when no turn was owed -- and the character's own facing is
	// only ever 1..4, so an exit whose approach point names no facing can never
	// fire. Two rooms arm one of those; the original cannot take them either.
	if (_ben.facing() != (int)_armedFacing)
		return;

	const byte submode = _armed;
	_armed = 0;
	takeExit(submode);
}

bool AlienEngine::takeExit(byte submode) {
	// The room's tick loop ends here, and ending it is what sets game_mode to
	// the room being left (OBJ:sub_0879a). The main loop then walks its chain of
	// check_event() calls with that pair.
	_mode = (byte)_room;
	_lastSubmode = submode;

	const int room = _script.nextRoom(_mode, submode);
	if (!room) {
		debugC(1, kDebugRooms, "exit: room %d submode %u has no link in the chain",
			   _mode, submode);
		return false;
	}

	debugC(1, kDebugRooms, "exit: room %d submode %u -> room %d", _mode, submode, room);

	// Submode 111 names the room itself: a close-up or a cutscene that comes
	// back to where it started. Reloading is the closest the port gets until
	// those scenes are understood.
	if (!loadRoom(room)) {
		debugC(1, kDebugRooms, "exit: room %d would not load, staying in %d",
			   room, _mode);
		return false;
	}

	return true;
}

byte AlienEngine::rotateOutcome(const Hotspot &spot) {
	// LOGIC:sub_120d4: a per-object counter walks the hotspot's outcome codes
	// and wraps at its arity, which is what makes clicking the same thing twice
	// give a different line.
	const byte arity = MAX<byte>(spot.outcomeCount, 1);
	byte &counter = _outcomeCounter[spot.obj];
	if (counter >= arity)
		counter = 0;

	const byte code = spot.outcomes[counter];
	counter++;
	return code;
}

void AlienEngine::clickAt(int x, int y, bool rightButton) {
	// A click while someone is talking cuts the line short, the same as the
	// countdown running out.
	if (_speech) {
		nextSpeech();
		return;
	}

	updateHover(x, y);

	// The bar answers for itself, and a click there is never a walk.
	if (clickBar(x, y, rightButton))
		return;

	// A right click in the playfield with an item in hand is turned into a left
	// click at the same point (1021:0x6f7), so the item is used; with an empty
	// hand it does nothing at all.
	if (rightButton && !_heldItem)
		return;

	// The room's own walk geometry decides where the click sends him: an object
	// has an approach point and a facing, a floor rectangle snaps the point onto
	// the room's floor line. Without it he walks onto whatever he was sent to
	// use. See walkgeom.h and docs/walk_system.md.
	// The room's own walk geometry and a plain floor click both work in
	// room-space, same as the hotspot boxes updateHover just tested.
	const int roomX = x + _scrollX;

	const byte obj = _hover >= 0 ? _spots[_hover].obj : 0;
	WalkTarget target;
	// Every click rearms from scratch, as the original rewrites walk_submode
	// [0xa87d] each time entry 0 runs.
	_armed = 0;
	if (_script.walkTarget(roomX, y, obj, target)) {
		if (target.submode) {
			_armed = target.submode;
			_armedX = target.x;
			_armedY = target.y;
			_armedFacing = target.facing;
			debugC(1, kDebugRooms, "exit: object %u arms submode %u at %d,%d facing %u",
				   obj, target.submode, target.x, target.y, target.facing);
		}
		walkTo(target.x, target.y, target.facing);
	} else {
		walkTo(roomX, y);
	}

	_pending = _hover;
	if (_pending < 0) {
		// A click on the floor with something in hand is not an item use, and the
		// original leaves the item in the hand for the next click.
		return;
	}

	const Hotspot &spot = _spots[_pending];

	// With an item in hand the click is an item use: no outcome rotation, because
	// the answer comes from the room's own combination body or from the shared
	// refusal. LOGIC:sub_12336 sets the pair and walks; the body runs on arrival.
	_pendingItem = _heldItem;
	_pendingOutcome = _pendingItem ? 0 : rotateOutcome(spot);

	if (_pendingItem)
		debugC(1, kDebugItems, "click: item %u (%s) on object %u", _pendingItem,
			   _inventory.name(_pendingItem).c_str(), spot.obj);
	else
		debugC(1, kDebugGraphics, "click: object %u, verb %u (%s), outcome %u",
			   spot.obj, spot.verb, _tables.verb(spot.verb).c_str(), _pendingOutcome);

	// Rooms with no walk mask never start a route, so the action is due at once.
	if (!_ben.isWalking() && !_ben.isTurning())
		finishAction();
}

void AlienEngine::finishAction() {
	const int index = _pending;
	const byte item = _pendingItem;
	_pending = -1;
	_pendingItem = Inventory::kNoItem;
	if (index < 0 || index >= (int)_spots.size())
		return;

	// docs/dialog_system.md 1: the speech is anchored on the horizontal midpoint
	// of the clicked object's box, at its top edge. Taken by value because the
	// script below may rebuild the table this points into.
	const Hotspot spot = _spots[index];
	const int anchorX = (spot.x1 + spot.x2) / 2;
	const int anchorY = spot.y1;

	// Every overlay calls the generic dispatch (10c9:sub_11c10) before running
	// its own bodies, so the outcome speaks first. A code of zero means the room
	// answers for itself -- except under "Look at", where the shared script
	// supplies the canned line. An item use has no outcome of its own at all.
	if (_pendingOutcome != 0 && _pendingOutcome != 0xff)
		queueOutcome(_tal, _pendingOutcome, anchorX, anchorY);
	else if (_pendingOutcome == 0 && spot.verb == kVerbLookAt)
		queueOutcome(_talkall, kOutcomeNothingSpecial, anchorX, anchorY);

	// Then the room's own reaction. A body that queues an event of its own
	// speaks over whatever the generic path put up, as it does in the original:
	// both write the one queue, and the later call is the one that stands. An
	// item use runs under verb 2 with the item as the third part of the key.
	const byte verb = item ? kVerbUseTo : spot.verb;
	const bool handled = _script.run(spot.obj, verb, item);

	// One object in the game is answered by a hook of its room's own rather
	// than by a script body: room 7's light switch (bedroom.cpp).
	if (!item && isBedroomSwitch(spot.obj))
		bedroomSwitch(anchorX, anchorY);
	if (_script.queuedEvent() != RoomScript::kNoEvent)
		queueOutcome(_tal, _script.queuedEvent(), anchorX, anchorY);

	// A combination no room owns gets the shared refusal, and either way the hand
	// is empty once the click is spent.
	if (item) {
		if (!handled && _script.queuedEvent() == RoomScript::kNoEvent)
			queueOutcome(_talkall, kOutcomeNoCombination, anchorX, anchorY);
		holdItem(Inventory::kNoItem);
	}

	// The original re-registers every rectangle on the next frame, so a body
	// that opened a door has already changed what is clickable by the time the
	// player can click again.
	_script.buildHotspots(_room, _spots);
	_hover = -1;
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	updateHover(mouse.x, mouse.y);

	debugC(1, kDebugGraphics, "action: object %u verb %u item %u -> outcome %u, script %s",
		   spot.obj, verb, item, _pendingOutcome, handled ? "handled it" : "passed");

	// A body may end the scene on its own account rather than by arming an
	// approach point -- set_game_submode in the table -- and that leaves the room
	// as soon as the click is over, with no arrival to wait for.
	if (_script.submodeRequest() != RoomScript::kNoSubmode) {
		_armed = 0;
		takeExit(_script.submodeRequest());
	}
}

void AlienEngine::queueOutcome(const TalFile &tal, byte code, int anchorX, int anchorY) {
	// An outcome code does not name one line: it names a chain of up to ten
	// dialog ids in zone 1 of the room's TAL, played one after another.
	const TalFile::Outcome &chain = tal.outcome(code);
	_speechTal = &tal;

	_queueCount = MIN<uint>(chain.count, TalFile::kMaxOutcomeIds);
	_queueNext = 0;
	for (uint i = 0; i < _queueCount; i++)
		_queue[i] = chain.ids[i];

	_speechX = anchorX;
	_speechY = anchorY;

	debugC(1, kDebugGraphics, "outcome %u: %u dialog ids", code, _queueCount);
	nextSpeech();
}

void AlienEngine::nextSpeech() {
	// Ids whose slot holds no text are stepped over rather than shown as an
	// empty pause; the shipped files do carry a few of those.
	while (_queueNext < _queueCount) {
		const uint id = _queue[_queueNext++];
		const TalFile::Entry &entry = _speechTal->entry(id);
		if (entry.lines.empty())
			continue;

		uint length = 0;
		for (uint i = 0; i < entry.lines.size(); i++)
			length += entry.lines[i].size();

		_dialogId = id;
		_speech = true;
		_speechTicks = MAX<int>((int)length * kTicksPerCharacter, kMinSpeechTicks);
		_dirty = true;

		debugC(1, kDebugGraphics, "speech %u: %u lines, %d ticks",
			   id, entry.lines.size(), _speechTicks);
		return;
	}

	stopSpeech();
}

void AlienEngine::stopSpeech() {
	_speech = false;
	_speechTicks = 0;
	_queueCount = 0;
	_queueNext = 0;
	_dirty = true;
}

void AlienEngine::drawWalkOverlay() {
	// A debug view, not something the game ever drew: blocked pixels stippled,
	// the node ring marked, and the last plotted route joined up. The ink
	// colour is the one the text layer already reprograms, so it stands out
	// against any plate.
	byte *pixels = (byte *)_screen.getPixels();

	for (int y = 0; y < _screen.h; y++) {
		for (int x = 0; x < _screen.w; x++) {
			if (((x + y) & 3) == 0 && _walk.mask().blocked(x, y))
				pixels[y * _screen.pitch + x] = Font::kInkColor;
		}
	}

	for (uint i = 0; i < _walk.nodes().count(); i++) {
		const int nx = _walk.nodes().x(i);
		const int ny = _walk.nodes().y(i);
		for (int d = -2; d <= 2; d++) {
			if (nx + d >= 0 && nx + d < _screen.w)
				pixels[ny * _screen.pitch + nx + d] = Font::kInkColor;
			if (ny + d >= 0 && ny + d < _screen.h)
				pixels[(ny + d) * _screen.pitch + nx] = Font::kInkColor;
		}
	}

	for (uint i = 1; i < _route.count; i++) {
		const WalkRoute::Point &a = _route.points[i - 1];
		const WalkRoute::Point &b = _route.points[i];
		const int steps = MAX(ABS(b.x - a.x), ABS(b.y - a.y));
		for (int s = 0; s <= steps; s++) {
			const int x = steps ? a.x + (b.x - a.x) * s / steps : a.x;
			const int y = steps ? a.y + (b.y - a.y) * s / steps : a.y;
			if (x >= 0 && x < _screen.w && y >= 0 && y < _screen.h)
				pixels[y * _screen.pitch + x] = Font::kInkColor;
		}
	}
}

void AlienEngine::setTextColor(byte r, byte g, byte b) {
	// The original reprograms a single DAC entry per speaker; the values in
	// the disassembly are the VGA 6-bit ones, so they are widened here.
	_palette[Font::kInkColor * 3 + 0] = r * 255 / 63;
	_palette[Font::kInkColor * 3 + 1] = g * 255 / 63;
	_palette[Font::kInkColor * 3 + 2] = b * 255 / 63;
}

void AlienEngine::drawLabel() {
	// docs/action_system.md 1: the status line is the hovered hotspot's verb and
	// the object's name out of the room's label file, built as "<verb> <label>".
	// With nothing under the cursor it reads "Walk to" on its own.
	Common::String text = _tables.walkVerb();

	// With an item in hand the line is built the other way round: HOTSPOT:0x2a5
	// writes "USE <item> WITH <object>" instead of "<verb> <object>".
	if (_heldItem) {
		text = _tables.useVerb() + " " + _inventory.name(_heldItem);
		if (_hover >= 0) {
			const TalFile::Entry &entry = _labels.entry(_spots[_hover].label);
			if (!entry.lines.empty())
				text += " " + _tables.withVerb() + " " + entry.lines[0];
		}
	} else if (_hover >= 0) {
		const Hotspot &spot = _spots[_hover];
		const TalFile::Entry &entry = _labels.entry(spot.label);
		text = _tables.verb(spot.verb);
		if (!entry.lines.empty()) {
			text += " ";
			text += entry.lines[0];
		}
	}

	if (text.empty())
		return;

	int x = kLabelCenterX - _labelFont.measure(text) / 2;
	if (x < kLabelLeft)
		x = kLabelLeft;

	_labelFont.drawString(_screen, text, x, kLabelY);
}

void AlienEngine::drawSpeech(const TalFile::Entry &entry, int anchorX, int anchorY) {
	// docs/dialog_system.md 3A. Lines are individually centred on the anchor,
	// and the block is clamped to keep it inside the playfield.
	const int lines = (int)entry.lines.size();
	if (!lines)
		return;

	int top = anchorY - lines * 10;
	if (top < 14)
		top = 14;
	if (top + lines * 11 > 155)
		top = 155 - lines * 11;

	for (int i = 0; i < lines; i++) {
		const Common::String &line = entry.lines[i];
		int x = anchorX - _font.measure(line) / 2;
		if (x < 0)
			x = 0;
		_font.drawString(_screen, line, x, top + i * 11);
	}
}

void AlienEngine::drawBand(const TalFile::Entry &entry) {
	// docs/dialog_system.md 3B: the narration layout, left margin 20, four
	// lines at most, 14 px apart with the last line always at 140.
	const int lines = MIN<int>(entry.lines.size(), 4);
	if (!lines)
		return;

	const int firstY = 140 - (lines - 1) * 14;
	for (int i = 0; i < lines; i++)
		_font.drawString(_screen, entry.lines[i], 20, firstY + i * 14);
}

void AlienEngine::showDialog(uint id) {
	// Stepping through the file by hand, for checking the text layer against the
	// reference renders: no countdown, so the line stays up until the next key.
	_dialogId = id;
	_speechTal = &_tal;
	_speech = true;
	_speechTicks = 0;
	_speechX = kAnchorX;
	_speechY = kAnchorY;
	_dirty = true;

	const TalFile::Entry &e = _tal.entry(id);
	debugC(1, kDebugGraphics, "dialog %u: %u lines, count byte %d",
		   id, e.lines.size(), e.lineCount);
}

void AlienEngine::redraw() {
	_screen.fillRect(Common::Rect(0, 0, _screen.w, _screen.h), 0);

	if (_background.getPixels()) {
		int w = MIN<int>(_background.w - _scrollX, _screen.w);
		int h = MIN<int>(_background.h, _screen.h);
		for (int y = 0; y < h; y++)
			memcpy(_screen.getBasePtr(0, y), _background.getBasePtr(_scrollX, y), w);
	}

	// The animation slots come first: they are the room's own furniture, and the
	// character walks in front of them.
	_anims.draw(_screen, _scrollX);

	// A cutscene is the record's plate and its own slots and nothing else: the
	// character is not in it, and the original hides the bar for its duration.
	if (!_cutscene) {
		_sprite.drawFrame(_spriteFrame, _screen, _scrollX);
		_ben.draw(_screen, _scrollX);

		// And the foreground the room authored over him, which is the whole of
		// the original's depth model (occlusion.h).
		applyOcclusion();

		if (_showWalk)
			drawWalkOverlay();

		// The bar sits below the playfield, so it goes on after the room but
		// before the text layer, which is what the original's redraw order
		// comes to.
		_inventory.draw(_tables, _screen, _hoverSlot, _hoverArrow);

		drawLabel();
	}

	if (_speech) {
		const TalFile::Entry &dialog = (_speechTal ? _speechTal : &_tal)->entry(_dialogId);
		if (_dialogBand)
			drawBand(dialog);
		else
			drawSpeech(dialog, _speechX, _speechY);
	}

	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
	_dirty = false;

	dumpScreen();
}

void AlienEngine::dumpScreen(const Common::String &name) {
	// Writes the composed staging buffer out so it can be diffed against the
	// renders the reverse-engineering tools produce, which is the only way to
	// check the decoders while the engine has no interactive state yet. A
	// named call (from the play channel's "snap" command) always writes;
	// the default, unnamed call is gated on the graphics channel.
	if (name.empty() && !debugChannelSet(3, kDebugGraphics))
		return;

	Common::DumpFile out;
	const Common::String file = name.empty() ? Common::String("alien-screen.png") : name;
	if (!out.open(Common::Path(file))) {
		warning("could not open the screen dump for writing");
		return;
	}

	Graphics::Palette pal(_palette, 256);
	Image::writePNG(out, _screen, pal);
}

void AlienEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_KEYDOWN:
			// Frame stepping, so the DL1 decoder can be eyeballed against the
			// reference renders while the rest of the engine is missing.
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				_quit = true;
			} else if (event.kbd.keycode == Common::KEYCODE_SPACE ||
					   event.kbd.keycode == Common::KEYCODE_RIGHT) {
				if (_sprite.frameCount())
					_spriteFrame = (_spriteFrame + 1) % _sprite.frameCount();
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_LEFT) {
				if (_sprite.frameCount())
					_spriteFrame = (_spriteFrame + _sprite.frameCount() - 1) % _sprite.frameCount();
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_DOWN) {
				showDialog((_dialogId + 1) % TalFile::kEntryCount);
			} else if (event.kbd.keycode == Common::KEYCODE_UP) {
				showDialog((_dialogId + TalFile::kEntryCount - 1) % TalFile::kEntryCount);
			} else if (event.kbd.keycode == Common::KEYCODE_TAB) {
				_dialogBand = !_dialogBand;
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_PAGEDOWN) {
				stepRoom(1);
			} else if (event.kbd.keycode == Common::KEYCODE_PAGEUP) {
				stepRoom(-1);
			} else if (event.kbd.keycode == Common::KEYCODE_RIGHTBRACKET) {
				stepSpriteBank(1);
			} else if (event.kbd.keycode == Common::KEYCODE_LEFTBRACKET) {
				stepSpriteBank(-1);
			} else if (event.kbd.keycode == Common::KEYCODE_a) {
				// Everything in the room moves at once: the slots, running.
				_anims.playAll(kDebugAnimRate);
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_g) {
				sweepWalkGeometry();
			} else if (event.kbd.keycode == Common::KEYCODE_h) {
				dumpHotspots();
			} else if (event.kbd.keycode == Common::KEYCODE_x) {
				dumpExits();
				takeFirstExit();
			} else if (event.kbd.keycode == Common::KEYCODE_w) {
				_showWalk = !_showWalk;
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_i) {
				dumpItems();
			} else if (event.kbd.keycode == Common::KEYCODE_b) {
				// The second plate is the B state, the right half of a wide
				// room or the close-up, depending on the room.
				loadRoom(_room, !_secondPlate);
			} else if (event.kbd.keycode == Common::KEYCODE_F9) {
				// Pick up where the original left off: the state block of its own
				// save, applied to this engine. The room is not in that block --
				// it comes out of the slot's preview picture instead, matched
				// against the plates (AlienEngine::thumbnailRoom).
				importDosSave("SAVEGAME.0", true);
			} else if (event.kbd.keycode == Common::KEYCODE_m) {
				// Step through the music slots. Only two rooms start a theme on
				// entry and the rest of the tracks belong to scenes, so this is
				// still the quickest way to hear one on its own.
				const int next = _musicSlot + 1;
				if (next >= StaticTables::kMusicSlotCount)
					stopMusic();
				else
					playMusicSlot((uint)next);
			}
			break;
		case Common::EVENT_MOUSEMOVE:
			updateHover(event.mouse.x, event.mouse.y);
			break;
		case Common::EVENT_LBUTTONDOWN:
			clickAt(event.mouse.x, event.mouse.y);
			break;
		case Common::EVENT_RBUTTONDOWN:
			// The right button looks at what it is over and puts a held item back;
			// the original converts it into a left click while an item is in hand.
			clickAt(event.mouse.x, event.mouse.y, true);
			break;
		default:
			break;
		}
	}
}

} // End of namespace Alien
