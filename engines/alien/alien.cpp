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
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/translation.h"
#include "engines/util.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

// Where a new game opens. MAIN's boot run -- everything between `entry_point`
// and `main_loop_start`, the same span the initial state block is lifted from --
// ends by setting handler_code to 3 and dispatching it, and a handler code is
// its room number in plain decimal, so the game starts in uncle's lab. The
// kitchen, which this used to say, is only where the render milestones were
// checked; `-b 10` still goes there.
static const int kStartRoom = 3;

// The master tick is the ~70 Hz retrace, and two dividers sit
// under it. Animation runs on every fourth tick, roughly 17.5 Hz, while the
// dialog countdown is decremented under the first divider only (OBJ:0x617c,
// gated on the tick pair), so it runs on every second tick.
//
// The rate the original asked the timer for is not the rate it played at: the
// tick is raised from a vertical retrace, so on the hardware it ran on the whole
// loop went faster than the nominal 70 Hz, and every subsystem went with it
// because they are all dividers of this one counter. Scaling the rate by 1.5
// matches the original execution speed, so the nominal rate is scaled here,
// once, rather than in each divider.
static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;

// One install ships the dialogue and the hover names in English and Finnish
// side by side, and the detection entry picks the set. Both trees end in a
// directory of the same name, and SearchMan keys an archive by that last name
// alone, so they are registered by hand under names of the engine's own
// choosing rather than through addSubDirectoryMatching.
static const char *const kScriptTree = "TALFILES";
static const char *const kLabelTree = "NAMEROOM";

// The mouse pointer: one 11 by 14 outlined arrow in the right margin of
// OBJFILE.PCX, the same page the font atlas and the inventory chrome come from.
// The original blits it into the frame buffer itself,
// from (306,15), and never swaps it: there is no shape-change mechanism in the
// game at all, and the only INT 33h call it makes is the position set at boot.
// Index 0 is the transparent one, and the arrow is drawn in whatever palette the
// room left behind, which is why no cursor palette is pushed here either.
static const char *const kCursorPage = "OBJFILE.PCX";
static const int kCursorSrcX = 306;
static const int kCursorSrcY = 15;
static const int kCursorWidth = 11;
static const int kCursorHeight = 14;
static const byte kCursorTransparent = 0;

// TALKALL.TAL holds the answers that belong to no room: the descriptions of the
// carried items, and the two refusals the generic click path falls back on when
// an object's outcome code is zero -- 10c9:sub_11c10 and 10c9:0x257 raise the
// dialog branches that DIALOG:sub_0b776 turns into codes 4 and 5 (input.cpp).
static const char *const kSharedScript = "TALKALL.TAL";

const char *const AlienEngine::kLiftClip = "ANIMS/SHIPLIFT.MA1";
const char *const AlienEngine::kCutscenes[] = { "ALINTRO.CDA", "ALIEND.CDA" };

// SearchMan names a directory archive after the directory's own last component,
// so registering TALFILES/ENG and NAMEROOM/ENG the usual way would have the
// second one clash with the first and be dropped. The language directory is
// found by hand instead -- caselessly, since the install writes ENG and Eng
// alike -- and registered under the tree's name.
static bool addTextTree(const Common::FSNode &gameDataDir, const char *tree, const char *languageDir) {
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
			if (language.getName().compareToIgnoreCase(languageDir))
				continue;
			SearchMan.addDirectory(Common::String(tree), language);
			return true;
		}
	}

	warning("could not find %s/%s in the game directory", tree, languageDir);
	return false;
}

AlienEngine::AlienEngine(OSystem *syst, const ADGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _spriteFrame(0), _spriteBank(0),
		_room(0), _secondPlate(false), _bakedPlate(nullptr), _roomWidth(kScreenWidth), _scrollX(0),
		_scrollPos(0), _scrollVel(0), _scrollState(0xff), _scrollFocus(0), _scrollPlacements(0),
		_charPaletteAltLoaded(false), _lightLevel(0), _lightPrev(0), _lightArmed(false), _musicSlot(-1), _liftPlayed(false), _subtitles(true), _showWalk(false), _showSpots(false), _lastTick(0), _tick(0),
		_timescale(1), _virtualMillis(0), _clockRealBase(0), _clockVirtualBase(0), _presentLast(0),
		_hotspotStamp(0), _pausedMillis(0), _pauseStart(0), _clockPaused(false),
		_hover(-1), _hoverSlot(-1), _hoverArrow(Inventory::kArrowNone), _hoverMenu(false), _menuRequest(false), _inMenu(false), _rnd("alien"),
		_heldItem(Inventory::kNoItem), _pendingItem(Inventory::kNoItem),
		_pending(-1), _pendingOutcome(0),
		_armed(0), _armedX(0), _armedY(0), _armedFacing(Walker::kFacingKeep), _mode(0),
		_lastSubmode(0),
		_queueCount(0), _queueNext(0), _speechTal(nullptr), _labelSlot(0), _labelFading(false), _labelHold(0), _walkReported(false),
		_dialogId(1), _lastEvent(0), _speechCustom(false), _speechBen(true), _liftPending(false),
		_observatoryLook(false),
		_observatoryBreaker(false), _observatoryStep(0), _observatoryPos(0),
		_observatoryEncore(false), _stairsDescents(0), _restoring(false),
		_useLastItem(0), _useLastObj(0), _useRepeat(0), _refusalNext(0),
		_scrollHold(-1), _cameraLastX(0), _cameraPlacements(0), _roomEnterTick(0),
		_chatPickReply(0), _chatPickNext(0), _chatPickTopic(0), _chatPickChoice(0),
		_chatPickNew(false),
		_cursorX(0), _cursorY(0), _chatColorsHeld(false), _barHidden(false), _chatOwnsBar(false),
		_dialogBand(false),
		_speech(false), _speechTicks(0), _speechX(kAnchorX), _speechY(kAnchorY),
		_dirty(true), _quit(false), _cutscene(false), _cutsceneFast(false),
		_endingStep(0), _endingPos(0), _endingLoop(false),
		_openingStep(0), _openingPending(true),
		_labStep(0), _bedroomLiftStep(0), _mansionStep(0), _labPos(0), _labNearHole(false), _drawCharacter(true), _characterAnimSlots(0),
		_cursorWasVisible(true), _sewerStep(0),
		_basementStep(0), _basementClimbing(false), _basementWalkOff(false),
		_sewerPhase(0), _sewerDepth(kSewerDepthStart), _sewerDivider(0), _sewerDraining(0),
		_clipBottom(kPlayfieldBottom), _fadePending(false), _pendingCutscenes(false), _sceneHandBack(false), _darkLastX(-1), _darkLastY(-1), _won(false),
		_playIndex(0), _playActive(false), _playPaused(false), _playStep(false), _playSkippable(false), _playHurry(false),
		_playStrict(false), _playBarBusy(false), _playBarClicks(0), _playLastTick(0), _playWaitTicks(0),
		_playSettleTimeout(0), _playUntilTimer(-1), _playUntilRoom(-1), _playSettling(false), _playFails(0) {
	memset(_palette, 0, sizeof(_palette));
	memset(_charPalette, 0, sizeof(_charPalette));
	memset(_charPaletteAlt, 0, sizeof(_charPaletteAlt));
	_lightMap[0] = _lightMap[1] = nullptr;
	memset(_labelColors, 0, sizeof(_labelColors));
	memset(_outcomeCounter, 0, sizeof(_outcomeCounter));
	memset(_queue, 0, sizeof(_queue));
	memset(_chatPalette, 0, sizeof(_chatPalette));

	registerRooms();

	// The original's film player always shows the subtitle track, so a target
	// that never set the option gets them too.
	ConfMan.registerDefault("subtitles", true);

	// An anim_play effect in a room script drives the slots directly, the way the
	// overlay's own body calls MIDAS, and inv_add / inv_remove work the list.
	_script.setAnims(&_anims);
	_script.setInventory(&_inventory);
	_script.setSound(&_sound);
	_script.setEngine(this);
}

AlienEngine::~AlienEngine() {
	for (Room *room : _rooms)
		delete room;
	freeLightMap();
	_screen.free();
	_background.free();
	_occluder.free();
}

Common::Error AlienEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	setDebugger(new AlienConsole(this));
	_sound.init(_mixer);
	syncSoundSettings();

	// The scripted-run keys (playrun.cpp) are honoured only with the play
	// channel on, so a stray key in a player's config cannot change the game.
	// timescale is how fast the game runs (clock.cpp): 1 by default, 0 for
	// turbo, N for N times real time. The seed is fixed whenever one is given,
	// which a replay needs to take the same branches -- except under the event
	// recorder, which keeps the seed in the recording itself.
	if (debugChannelSet(-1, kDebugPlay)) {
		setTimescale(ConfMan.hasKey("timescale") ? (uint)MAX(ConfMan.getInt("timescale"), 0) : 1);
		_playSkippable = ConfMan.hasKey("skippable") && ConfMan.getBool("skippable");
		_playHurry = ConfMan.hasKey("hurry") && ConfMan.getBool("hurry");
		if (ConfMan.hasKey("seed") && !underRecorder())
			_rnd.setSeed((uint32)ConfMan.getInt("seed"));
	} else {
		setTimescale(1);
	}

	// The text and label trees are subdirectories, so they need registering
	// before Common::File can reach into them by name.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	const char *languageDir = getLanguage() == Common::FI_FIN ? "FIN" : "ENG";
	addTextTree(gameDataDir, kScriptTree, languageDir);
	addTextTree(gameDataDir, kLabelTree, languageDir);

	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (!_tables.load())
		return Common::Error(Common::kReadingFailed, "Could not read the tables in GAME.EXE");

	if (!_overlays.load())
		return Common::Error(Common::kReadingFailed, "Could not index the scene overlays");

	if (!_font.load())
		return Common::Error(Common::kReadingFailed, "Could not load the font");

	if (!_chatFont.load(Font::kChat))
		return Common::Error(Common::kReadingFailed, "Could not load the conversation font");

	if (!_labelFont.load(Font::kLabel))
		return Common::Error(Common::kReadingFailed, "Could not load the label font");

	if (!loadCursor())
		warning("could not cut the pointer out of OBJFILE.PCX: the game is playable "
				"but the mouse has no arrow");

	// The pack comes before anything that reads it, which the shared script
	// below already does. The cutscenes and the room scripts are only in the
	// pack, so without one the game would have no scenes and every click would
	// fall through to the generic answer.
	if (!_pack.load()) {
		switch (_pack.status()) {
		case AlienPack::kStatusVersion:
			GUIErrorMessageFormat(_("Incorrect version of the '%s' engine data file found. Expected %d but got %d."),
								  AlienPack::kFileName, AlienPack::kVersion, _pack.foundVersion());
			break;
		case AlienPack::kStatusCorrupt:
			GUIErrorMessageFormat(_("The '%s' engine data file is corrupt."), AlienPack::kFileName);
			break;
		default:
			GUIErrorMessageFormat(_("Unable to locate the '%s' engine data file."), AlienPack::kFileName);
			break;
		}
		return Common::kNoGameDataFoundError;
	}
	setCutscenePack(&_pack);
	setRoomScriptPack(&_pack);

	// A pack that read cleanly but holds no scenes or scripts is as good as a
	// damaged one.
	if (!cutscenesLoaded() || !roomScriptsLoaded()) {
		GUIErrorMessageFormat(_("The '%s' engine data file is corrupt."), AlienPack::kFileName);
		return Common::kNoGameDataFoundError;
	}

	// RoomScript's constructor runs before any of this, so the reset it does
	// there had no new-game state to write: the state block is the pack's now.
	// Everything that reads a flag -- which is every hotspot a room registers --
	// depends on this happening before the first room opens.
	_script.reset();

	// The shared script is resident in the original for the whole game, and the
	// generic click path reads it whatever room the player is in.
	if (!_talkall.load(Common::Path(kSharedScript), &_pack))
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
	// A run that names a room or turns a channel on skips the opening monologue
	// for the same reason it skips the intro: it is a test run, not a full game run.
	// A scripted run that names no room is the exception: it starts the way a new
	// game does, intro, monologue and all.
	const bool playRun = debugChannelSet(-1, kDebugPlay) && ConfMan.hasKey("playscript");
	// One resuming from a checkpoint is not a new game either: the state it
	// loads is past all of that.
	const bool newGame = !ConfMan.hasKey("boot_param") && !(playRun && ConfMan.hasKey("playfrom")) &&
						 (gDebugLevel <= 0 || playRun);
	if (!newGame)
		cancelOpening();

	if (newGame && !playCutscene(kCutscenes[0]))
		warning("%s is not in the search path, so the intro is being skipped: it ships "
				"on the CD rather than in the installed game, and reaching it needs "
				"--extrapath pointed at the CDA directory", kCutscenes[0]);

	if (!loadRoom(start))
		return Common::Error(Common::kReadingFailed, "Could not load the starting room");

	if (debugChannelSet(2, kDebugRooms))
		tourRooms();

	// The conversation trees (chat.cpp).
	if (debugChannelSet(2, kDebugChat))
		sweepChatTrees();

	// And every room's lines out of the files and the pack together (tal.cpp).
	if (debugChannelSet(1, kDebugDialog))
		sweepDialog();

	// The items channel prints the inventory items. Level 4 is a
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

	// The sound channel prints the banks and
	// their samples with the room-to-bank table, and at level 2 every trigger the
	// script table holds with the sample its own room's bank gives it.
	if (debugChannelSet(-1, kDebugSound)) {
		dumpSfx();
		if (debugChannelSet(2, kDebugSound))
			sweepSounds();
		if (debugChannelSet(3, kDebugSound))
			sweepVoices();
	}

	// The save channel prints what saves hold, and at level 3 the engine's state
	// taken through a save and back.
	if (debugChannelSet(-1, kDebugSave)) {
		dumpSaves();
		if (debugChannelSet(3, kDebugSave))
			checkSaveRoundTrip();
		// Level 4 is the one the install's own saves cannot reach: they all carry
		// an empty inventory, so the list is patched into a real block first.
		if (debugChannelSet(4, kDebugSave))
			checkDosItemImport();
	}

	// The cutscene channel prints the cutscene tables, and at level 2 every
	// record played end to end -- the same
	// step interpreter a triggered scene runs, with the clock taken out.
	if (debugChannelSet(-1, kDebugCutscene)) {
		dumpCutscenes();
		if (debugChannelSet(2, kDebugCutscene))
			sweepCutscenes();
	}

	// The occlusion channel prints every foreground rectangle a room stamps
	// back over the character.
	if (debugChannelSet(-1, kDebugOcclusion))
		dumpOcclusion();

	// The plate channel prints every step of every room's plate routine in the
	// resident 10c9 unit.
	if (debugChannelSet(-1, kDebugPlate))
		dumpPlates();

	// The light channel prints which plate each room reads its brightness from,
	// and at level 2 those plates sampled on a grid.
	if (debugChannelSet(-1, kDebugLight)) {
		dumpLighting();
		if (debugChannelSet(2, kDebugLight))
			sweepLighting();
	}

	// The scale channel prints what each room does about depth, and at level 2
	// that worked out row by row.
	if (debugChannelSet(-1, kDebugScale)) {
		dumpScale();
		if (debugChannelSet(2, kDebugScale))
			sweepScale();
	}

	// The music channel prints the module and
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

	// The video channel prints what the two
	// containers say about themselves, then a checksum of every decoded frame,
	// then every subtitle line in every language the files carry, then the face
	// those lines are set in.
	if (debugChannelSet(-1, kDebugVideo)) {
		dumpVideo();
		// Ahead of the sweeps rather than after them: the font is one table and
		// the two sweeps between them decode a thousand frames, so a run asked
		// for the font alone would have to sit through all of it.
		if (debugChannelSet(4, kDebugVideo))
			dumpVideoFont();
		if (debugChannelSet(2, kDebugVideo))
			sweepVideoFrames();
		if (debugChannelSet(3, kDebugVideo))
			sweepSubtitles();
	}

	// The play channel drives the game from a command file instead of the
	// event queue -- see play.h -- because a headless run has no interactive
	// input to script against.
	if (debugChannelSet(-1, kDebugPlay)) {
		// No generic --key=value escape hatch exists on the ScummVM command
		// line (unrecognized options abort with "usage"), so the script path
		// comes from the target's config file: -c a-throwaway.ini with a
		// "playscript" key in the [alien] section.
		if (!ConfMan.hasKey("playscript"))
			warning("play: no 'playscript' key in the config; nothing to run");
		else if (loadPlayScript(ConfMan.get("playscript")))
			_playActive = startPlayRun() && resumePlayRun();
	}

	while (!shouldQuit() && !_quit) {
		handleEvents();

		// The save button, a frame after the click that asked for it. The menu
		// draws over the screen and takes its own events, so the room is left
		// dirty for the frame that follows it.
		if (_menuRequest) {
			_menuRequest = false;
			debugC(1, kDebugSave, "save: menu requested from room %d", _room);
			// LOGIC:sub_12b08 opens the menu with game_mode [0xa880] zeroed,
			// whether or not anything is saved: the way in is forgotten.
			_mode = 0;

			// A scripted run drives the game, not the GUI, and the dialog would
			// sit waiting for a click that the script has no way to send.
			if (!_playActive) {
				// The dialog is modal, so a load started from it runs inside
				// the flag -- which is the whole point of the flag: ScummVM
				// autosaves on the way into every load (Engine::loadGameState),
				// and the slot it writes is the one the player is loading.
				_inMenu = true;
				openMainMenuDialog();
				_inMenu = false;
				_dirty = true;
			}
		}

		stepClock();

		// The original's registration pass runs every frame, so the rectangle
		// under the pointer answers again the moment [0xa948] comes back --
		// with no mouse movement needed. The port hovers on motion and after a
		// click, so the frame the cursor returns on is the one that has to ask
		// again by itself.
		const bool cursorVisible = CursorMan.isVisible();
		if (cursorVisible && !_cursorWasVisible) {
			const Common::Point mouse = g_system->getEventManager()->getMousePos();
			updateHover(mouse.x, mouse.y);
		}
		_cursorWasVisible = cursorVisible;

		if (_playActive)
			stepPlayScript();
		if (_dirty)
			redraw();
		present();

		// The scenes the room raises as it opens. The original reaches them from
		// inside entry 2 -- the room's tick, whose first run is the room's init
		// -- so they play over the room the player has just walked into rather
		// than over the one they left, but before its palette has come up: the
		// new room is composed under black and a scene that starts there never
		// shows it. Fading in first flashed the room before every such scene
		// A scene ends by reloading the room, which
		// asks for the fade again, so that frame is composed before it runs.
		if (_pendingCutscenes) {
			_pendingCutscenes = false;
			roomCutscenes(_room);
			if (_dirty) {
				redraw();
				g_system->updateScreen();
			}
		}

		// The room's first composed frame is on the screen now, which is where
		// the original raises the palette from black (fade.cpp).
		if (_fadePending)
			fadeIn();

		sleep(kLoopSleepMillis);
	}

	// AI.COM plays the ending clip when GAME.EXE leaves with 0x7b, so the port
	// plays it after its own loop has ended rather than inside the last room
	// (ending.cpp, .
	// A debug run is a check rather than a game, so it stops at the win the way
	// it starts without the intro instead of sitting through eleven minutes of
	// video.
	if (_won && !shouldQuit() && gDebugLevel <= 0)
		playCutscene(kCutscenes[1]);
	else if (_won)
		debugC(1, kDebugRooms, "ending: %s not played in a debug run", kCutscenes[1]);

	return Common::kNoError;
}

/// A room has the character to itself, in a machine of its own (rooms/).
bool AlienEngine::roomHoldsCharacter() const {
	for (const Room *r : _rooms)
		if (r->holdsCharacter())
			return true;
	return false;
}

/**
 * [0xa4a1]: a conversation is currently running. The runner sets it as
 * it starts and clears it after the last line completes. Idle animations
 * do not start while a conversation is active.
 */
bool AlienEngine::dlgreqRunning() const {
	for (const Room *r : _rooms)
		if (r->conversationRunning())
			return true;
	return _endingStep;
}

uint32 AlienEngine::masterTick() const {
	return _tick + (millis() - _lastTick) / kTickMillis;
}

void AlienEngine::stepClock() {
	const uint32 now = millis();
	if (now - _lastTick < kTickMillis)
		return;
	// [0x7926] is the timer interrupt's (INPUT:psub_01820), not the loop's: it
	// keeps counting through a fade, a scene or any other wait that holds the
	// loop up. The port's blocking waits let the clock run on without coming
	// back here, so the ticks they spent are counted now, without the work the
	// loop does on a tick.
	const uint32 missed = (now - _lastTick) / kTickMillis - 1;
	if (missed)
		_tick += missed;
	_lastTick = now;
	_tick++;

	// The delay queue is serviced every tick, but its countdowns only step on the
	// tick pair -- INPUT:0x5D9 tests the same [0xa5fc] the slots do.
	_sound.tick((_tick & 1) == 0);

	// The rooms' own (rooms/): room 46's water ripples a step every tick
	// (diving.cpp), room 49's light fades and flickers at the
	// same pace (steam.cpp), and room 56's flashes as the beam hums
	// (teleport.cpp).
	for (Room *r : _rooms)
		r->tick();

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

		// And then the room's own loop calls, which is where an animation that
		// repeats gets restarted. The original makes them from the tail of the
		// room's tick, after the stepper it calls a few instructions earlier.
		// Three rooms sound a loop's last frame just before it restarts.
		stepLoopSounds();
		_anims.stepLoops();

		// A slot that has run out and leaves its frame behind goes into the
		// plate, exactly as the original's drawer puts it on the background
		// page. Room 10 is the case that needs it: opening the full fridge
		// plays slot 3 and taking the bread plays slot 2, and while both were
		// still drawing from their slots the older, higher one put the bread
		// back on top of the shelf it had just been taken from.
		bakeSlots();

		// And the clock a few rooms run beside those calls, in the same part of
		// the same tick: the library owl, the chimney, the sitting room's UFO
		// (roomtick.cpp). It reads both gates itself, so it is called under the
		// finer of the two.
		stepRoomClock();

		// And room 35's own machine, which is the water it starts full of and
		// the hatch that ends it (sewer.cpp).
		stepLab();
		stepBedroomLift();
		stepMansion();

		// The panel the lab computer opens, which waits for the computer's own
		// line to come down -- by the countdown or by a click (lift.cpp).
		stepLiftCall();

		// And room 19's fuse-panel cover, which waits for the line the look at
		// it raised (observatory.cpp). Stepped here as well as off the line
		// coming down, because a line dismissed by a click ends between the
		// two and the cover would otherwise stay shut.
		stepObservatoryPanel();
		stepObservatoryBreaker();
		stepObservatoryStairs();
		stepSewer();
		stepBasement();

		// And the rooms' own machines (rooms/), in the order rooms.cpp gives.
		for (Room *r : _rooms)
			r->tickPair();

		if (_speech && _speechTicks > 0 && --_speechTicks == 0) {
			nextSpeech();

			// A line coming down is what room 15 answers the front door with:
			// the original's dialog unit writes [0xa956] = 0x4e2a here and the
			// room's entry 3 tests it at its top (hallway.cpp).
			stepHallway();

			// And room 3's entry 3 tests the same word for the computer's
			// line, which is what opens the lift panel (lift.cpp).
			stepLiftCall();

			// And room 19's, which takes the cover off the fuse box once the
			// line the look raised has come down (observatory.cpp).
			stepObservatoryPanel();

			// And the rooms' own (rooms/): room 25's, which lights the fuse
			// when the matches' line comes down (mailbox.cpp).
			for (Room *r : _rooms)
				r->lineDown();
		}

		// OBJ:0x86df drops the talk flag once the line has under 25 half ticks
		// left, so the mouth closes a moment before the text goes.
		_ben.setTalking(_speech && (_speechCustom || _speechBen) && (_speechTicks == 0 ||
									_speechTicks >= tunable("speech.talkStopTicks",
															kTalkStopTicks)));
	}

	// The room's light map, sampled where Ben is standing: the same once-a-tick
	// OBJ:sub_069fd call every scene overlay's entry 2 makes (lighting.cpp).
	stepLighting();

	// And how far away he is, out of the same position: the ladder or the
	// interpolator the room runs beside that sampler (scale.cpp).
	stepCharScale();

	// And the speaker colour a couple of rooms set beside it, in the same tick.
	stepTextColor();

	// The conversation menu's colour machine, and room 8's owl -- the port's
	// one caller of it (chat.cpp, library.cpp). Both are per frame rather than
	// per tick pair: room 8's tick calls OBJ:sub_0a89c, sub_0a865 and
	// sub_0acb9 unguarded, and its [0xa49f] machine sits in the same body.
	stepChat();
	for (Room *r : _rooms)
		r->frame();

	// The status line's fade runs off the frame, not off the animation gate.
	stepLabelFade();

	// And the bar's page roll, sub_0a688 in every room's pass. It waits while
	// the bar is off the screen, so a page an item was added on behind a
	// conversation still rolls into view once the bar is back.
	if (!_chat.isActive() && !_barHidden && !_cutscene && _inventory.stepScroll())
		_dirty = true;

	// OBJ:0x7bb1 calls the mover on every tick pair and only the walk phase
	// waits for the animation gate, so on the tick pair between two animation
	// ticks he steps once more. Arrival is still answered on the animation
	// tick below.
	if ((_tick & 3) == 2 && _ben.stepMove())
		_dirty = true;

	// The camera pans once a pass of the room loop, whatever else is gated,
	// and after the mover: LOGIC:sub_13015 comes late in every room's pass.
	if (_tick & 3) {
		stepCameraHold();
		updateScroll();
		stepBasementLadder();
		for (Room *r : _rooms)
			r->walkPhase();
		return;
	}

	stepEnding();
	stepOpening();

	// The character ticks whether or not he is going anywhere: standing still is
	// what drives OBJ:sub_098d1's idle machine, which fidgets him and turns him
	// back to face the player.
	const bool moving = _ben.isWalking() || _ben.isTurning();
	const uint wasFrame = _ben.frame();

	// He is only his own while nothing else is driving him: every room that
	// runs an [0xa49f] machine clears [0xa94d] as it hides the cursor and sets
	// it back at the step that ends the machine, and the dialog unit holds
	// [0xa4a1] up for a scene it requested. Without this he fidgets and turns
	// to face the player in the middle of one.
	_ben.setIdleAllowed(!_cutscene && !_chat.isActive() && !roomHoldsCharacter() && !_labStep &&
						!_sewerStep && !_mansionStep && !_basementStep && !_endingStep && !_openingStep &&
						!dlgreqRunning());

	_ben.tick(_script.flag(0xa605) != 0);

	// On the animation tick the mover steps here rather than in stepMove, so
	// the camera follows it here too; panning before it saw this step a tick
	// late, and the focus would latch 2 px off.
	stepCameraHold();
	updateScroll();
	stepBasementLadder();
	for (Room *r : _rooms)
		r->walkPhase();

	if (moving) {
		_dirty = true;
	} else {
		if (_ben.frame() != wasFrame)
			_dirty = true;

		// OBJ:sub_07a0a zeroes [0xac1e] the moment the feet reach walk_pos, so
		// the line that reported the walk gives the status line back at the end
		// of it rather than sitting out its whole count.
		if (_walkReported) {
			_walkReported = false;
			_labelHold = 0;
		}

		if (_pending >= 0) {
			// He has arrived at what he was sent to; the outcome speaks now.
			finishAction();
		} else {
			// And if what he was sent to was a way out of the room, the exit
			// fires on the same arrival -- after the outcome, because the
			// original tests it at the end of the room's tick.
			checkExit();
		}
	}
}

/**
 * One of the two CDA2 cutscenes, if the CD's files are reachable.
 *
 * They live on the CD rather than in the installed game directory, so a copy
 * that was installed without them simply has no intro, which is what the
 * original does when the disc is missing too.
 */
/**
 * Install the pointer, which is a crop of a plate rather than a resource.
 *
 * The original owns its cursor: it saves the pixels under the arrow, blits the
 * arrow, and puts the backing store back next frame, all in the same page the
 * art is cut from. The port hands the same 11 by 14 crop to the backend and lets
 * it do the compositing, which is the one place the two differ and the one place
 * it does not matter -- nothing in the game reads the pixels the arrow covers.
 */
bool AlienEngine::loadCursor() {
	Graphics::Surface page;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kCursorPage), page, palette)) {
		page.free();
		return false;
	}

	if (page.w < kCursorSrcX + kCursorWidth || page.h < kCursorSrcY + kCursorHeight) {
		warning("%s is %dx%d, too small to hold the pointer", kCursorPage, page.w, page.h);
		page.free();
		return false;
	}

	byte arrow[kCursorWidth * kCursorHeight];
	for (int y = 0; y < kCursorHeight; y++)
		memcpy(arrow + y * kCursorWidth, page.getBasePtr(kCursorSrcX, kCursorSrcY + y),
			   kCursorWidth);
	page.free();

	// The arrow's point is its top left corner: the original blits it straight
	// at cursor_x, cursor_y with no offset of its own.
	CursorMan.replaceCursor(arrow, kCursorWidth, kCursorHeight, 0, 0, kCursorTransparent);
	CursorMan.showMouse(true);
	return true;
}

void AlienEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_KEYDOWN:
			// The development views and shortcuts that used to sit on keys here
			// are console commands now (console.cpp).
			if (playHotkey(event.kbd))
				break;
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE)
				_quit = true;
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

	runConsoleActions();
}

void traceEvent(const char *format, ...) {
	if (!debugChannelSet(1, kDebugTrace))
		return;
	va_list args;
	va_start(args, format);
	const Common::String event = Common::String::vformat(format, args);
	va_end(args);
	const AlienEngine *vm = static_cast<const AlienEngine *>(g_engine);
	debugC(1, kDebugTrace, "trace: %u %s", vm ? vm->masterTick() : 0, event.c_str());
}

} // End of namespace Alien
