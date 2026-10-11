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

#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {


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

/// A room in the middle of something of its own (rooms/).
bool AlienEngine::roomBusy() const {
	for (const Room *r : _rooms)
		if (r->busy())
			return true;
	return false;
}

bool AlienEngine::playIdle() const {
	// The speech queue counts as well as the line standing on screen: a body
	// that queued two lines is between them for a tick or two, and a click
	// landing in that gap is eaten as "cut the line short" rather than acted
	// on -- which is what made a scripted second click on the same box look
	// like it had hit nothing.
	// Only the one-shot animations count. Since the room tick started relaunching
	// looping slots (the candles, the blinking cursor) isBusy() never goes false
	// again, so waiting on it made every settle time out.
	// And the cursor: a room's [0xa49f] machine is idle by every test above
	// while it sits between two of its own plays, but the original takes no
	// input at all until it hands the cursor back, so a click
	// scripted into that gap is dropped rather than acted on. Waiting for
	// [0xa948] is what the player does.
	// Room 31's climb is an arrival waiting to fire and then a machine of its
	// own, exactly as _armed and the cursor are for every other room: the walk
	// to the rock ends with the character idle for the tick before the climb
	// starts, and a click scripted into that gap is taken at the wrong ledge.
	// And the camera: room 22's booth scene fires only once a pan has come to
	// rest ([0xa0cc] == 0), so a settle that ended with the view still gliding
	// checked for it a tick too soon.
	// And a conversation between a pick and the next topic's options: the
	// room's reply to the pick goes up a tick after the pick's own line comes
	// down, and a click scripted into that tick lands on no option at all.
	// Room 46's swims are the same: the edge a click armed only fires once
	// the stroke is over, and the room's own steps hide the cursor between.
	// Room 58's guard leaves the cursor up while he walks off to check the
	// Boss's line and comes back, so his machine has to be asked directly.
	// And the hippie's topic 6: the menu has gone when his reply is still owed
	// and the next file's first topic is still to open (hippie.cpp). And a
	// shore ledge with a click to answer once the walk is over (shore.cpp).
	// And the tick a room was entered on: on the original the room's loop has
	// already made its first pass -- its machine started, the cursor taken
	// away -- by the time anything can be asked of it (rooms 45 and 60, and
	// the camera).
	if (_tick == _roomEnterTick)
		return false;
	const bool chatWaiting = _chat.isActive() && (!_chat.isLive() || chatReplyOwed());
	return !_ben.isWalking() && !_ben.isTurning() && !_speech &&
		   _queueNext >= _queueCount && !_anims.isBusyOnce() && _pending < 0 &&
		   !_armed && !_cliffClimb && !_cliffStep &&
		   _scrollVel == 0 && !scrollWouldPan() && !roomBusy() &&
		   !chatWaiting &&
		   CursorMan.isVisible();
}

static const int kHurryTicks = 12;	///< how long a hurried line stands, in half ticks

void AlienEngine::stepPlayScript() {
	// Paced on the master tick, not the loop iteration -- the loop spins
	// faster than 70Hz while it waits for stepClock()'s own gate.
	if (_tick == _playLastTick)
		return;
	_playLastTick = _tick;

	// A speedrun reads nothing: every line is clicked away a beat after it
	// goes up. Shortening the countdown rather than calling nextSpeech() lets
	// the line end down the ordinary path, so a machine waiting on it sees
	// exactly what a player's click would have given it.
	if (_playHurry && _speech && _speechTicks > kHurryTicks)
		_speechTicks = kHurryTicks;

	if (_playWaitTicks > 0) {
		_playWaitTicks--;
		return;
	}

	if (_playUntilTimer >= 0) {
		if (cutsceneTimerDue((byte)_playUntilTimer)) {
			debugC(1, kDebugPlay, "play: %u: timer for scene %d due at tick %u",
				   _play.commands()[_playIndex - 1].sourceLine, _playUntilTimer, _tick);
			_playUntilTimer = -1;
		} else if (--_playSettleTimeout <= 0) {
			_playUntilTimer = -1;
			debugC(1, kDebugPlay, "play: %u: STUCK, the timer never came due",
				   _play.commands()[_playIndex - 1].sourceLine);
			playFailed(_play.commands()[_playIndex - 1].sourceLine);
			if (!_playActive)
				return;
		} else {
			return;
		}
	}

	if (_playUntilRoom >= 0) {
		if (_room == _playUntilRoom) {
			debugC(1, kDebugPlay, "play: %u: room %d reached at tick %u",
				   _play.commands()[_playIndex - 1].sourceLine, _playUntilRoom, _tick);
			_playUntilRoom = -1;
		} else if (--_playSettleTimeout <= 0) {
			debugC(1, kDebugPlay, "play: %u: STUCK, room %d never came (in room %d)",
				   _play.commands()[_playIndex - 1].sourceLine, _playUntilRoom, _room);
			_playUntilRoom = -1;
			playFailed(_play.commands()[_playIndex - 1].sourceLine);
			if (!_playActive)
				return;
		} else {
			return;
		}
	}

	if (_playSettling) {
		if (playIdle()) {
			_playSettling = false;
		} else if (--_playSettleTimeout <= 0) {
			_playSettling = false;
			debugC(1, kDebugPlay, "play: %u: STUCK, gave up waiting to settle",
				   _play.commands()[_playIndex - 1].sourceLine);
			debugC(1, kDebugPlay, "play: walking %d turning %d speech %d queue %d/%d anims %d "
				   "pending %d armed %d scroll %d/%d cursor %d",
				   _ben.isWalking(), _ben.isTurning(), _speech, _queueNext, _queueCount,
				   _anims.isBusyOnce(), _pending, _armed, _scrollVel, scrollWouldPan(),
				   CursorMan.isVisible());
			playFailed(_play.commands()[_playIndex - 1].sourceLine);
			if (!_playActive)
				return;
		} else {
			return;
		}
		// Where the run stands once it has come to rest: the tick it got there
		// on and a hash of everything a save would keep. Two runs of one script
		// at different speeds have to print the same lines (clock.cpp).
		debugC(1, kDebugPlay, "play: %u: state tick %u room %d hash %08x",
			   _play.commands()[_playIndex - 1].sourceLine, _tick, _room, stateHash());
	}

	if (_playBarBusy) {
		stepPlayBar();
		return;
	}

	if (_playIndex >= _play.commands().size()) {
		debugC(1, kDebugPlay, "play: script complete, %u assertion failure(s)", _playFails);
		playQuit();
		return;
	}

	if (_playPaused) {
		if (!_playStep)
			return;
		_playStep = false;
		playMessage(Common::String::format("line %u", _play.commands()[_playIndex].sourceLine));
	}

	runPlayCommand(_play.commands()[_playIndex++]);
}

/**
 * Hotkeys for controlling a scripted run: 'P' pauses before the next
 * command and 'N' advances one step, '+' and '-' change the playback speed,
 * and 'T' ends the script and returns control to the mouse.
 */
bool AlienEngine::playHotkey(const Common::KeyState &key) {
	static const uint kSpeeds[] = { 1, 2, 4, 8, 16, 32 };

	if (!_playActive)
		return false;

	switch (key.keycode) {
	case Common::KEYCODE_p:
		_playPaused = !_playPaused;
		_playStep = false;
		playMessage(_playPaused ? Common::String::format("paused before line %u",
						_playIndex < _play.commands().size() ? _play.commands()[_playIndex].sourceLine : 0)
								: Common::String("running"));
		return true;

	case Common::KEYCODE_n:
		if (_playPaused)
			_playStep = true;
		return true;

	case Common::KEYCODE_t:
		_playActive = false;
		playMessage(Common::String::format("taken over at line %u",
			_playIndex ? _play.commands()[_playIndex - 1].sourceLine : 0));
		return true;

	case Common::KEYCODE_PLUS:
	case Common::KEYCODE_EQUALS:
	case Common::KEYCODE_KP_PLUS:
	case Common::KEYCODE_MINUS:
	case Common::KEYCODE_KP_MINUS: {
		// Turbo has no speed to change; it is set from the start or not at all.
		if (turbo())
			return true;
		const bool up = key.keycode != Common::KEYCODE_MINUS && key.keycode != Common::KEYCODE_KP_MINUS;
		uint i = 0;
		while (i + 1 < ARRAYSIZE(kSpeeds) && kSpeeds[i] < _timescale)
			i++;
		if (up && i + 1 < ARRAYSIZE(kSpeeds))
			i++;
		else if (!up && i > 0)
			i--;
		setTimescale(kSpeeds[i]);
		playMessage(Common::String::format("speed %ux", _timescale));
		return true;
	}

	default:
		return false;
	}
}

/**
 * A run with `skippable` set skips each cutscene the way a player in a hurry
 * does: by pressing Escape as it starts, through the same event queue, so a
 * scene that would not take the key plays on regardless.
 */
void AlienEngine::offerSkip() {
	if (!_playSkippable)
		return;
	Common::Event event;
	event.type = Common::EVENT_KEYDOWN;
	event.kbd = Common::KeyState(Common::KEYCODE_ESCAPE, Common::ASCII_ESCAPE);
	g_system->getEventManager()->pushEvent(event);
}

/**
 * A scripted click. Under the event recorder it goes through the event queue
 * instead, as a player's would, so a recording made from a script holds every
 * click. On playback the recorder shuts every other source out of the queue
 * and hands back the clicks it holds, so the script, still running, checks
 * the same states without clicking twice.
 */
void AlienEngine::playClick(int x, int y, bool right) {
	if (!underRecorder()) {
		clickAt(x, y, right);
		return;
	}
	Common::Event event;
	event.mouse = Common::Point(x, y);
	event.type = right ? Common::EVENT_RBUTTONDOWN : Common::EVENT_LBUTTONDOWN;
	g_system->getEventManager()->pushEvent(event);
	event.type = right ? Common::EVENT_RBUTTONUP : Common::EVENT_LBUTTONUP;
	g_system->getEventManager()->pushEvent(event);
}

/**
 * The end of a scripted run. Under the event recorder the run quits through
 * the event queue, so the recording ends on a quit of its own, the way a
 * player's ends on the one they asked for.
 */
void AlienEngine::playQuit() {
	_playActive = false;
	if (!underRecorder()) {
		_quit = true;
		return;
	}
	Common::Event event;
	event.type = Common::EVENT_QUIT;
	g_system->getEventManager()->pushEvent(event);
}

void AlienEngine::playMessage(const Common::String &text) {
	debugC(1, kDebugPlay, "play: %s", text.c_str());
	g_system->displayMessageOnOSD(Common::U32String(text));
}

/// The bar commands without the bar: what a click on the slot would do, done.
void AlienEngine::runBarShortcut(const PlayCommand &cmd) {
	switch (cmd.type) {
	case PlayCommand::kUse:
		debugC(1, kDebugPlay, "play: %u: use %d (%s)", cmd.sourceLine, cmd.a,
			   _inventory.name((byte)cmd.a).c_str());
		holdItem((byte)cmd.a);
		break;

	// The bar's own click path, minus the geometry -- which square the item
	// sits on says nothing about the combine; a strict run clicks it anyway.
	case PlayCommand::kCombine:
		debugC(1, kDebugPlay, "play: %u: combine %d (%s) with held item %d (%s)",
			   cmd.sourceLine, cmd.a, _inventory.name((byte)cmd.a).c_str(), _heldItem,
			   _inventory.name(_heldItem).c_str());
		if (_heldItem && _inventory.has((byte)cmd.a))
			combineItems(_heldItem, (byte)cmd.a);
		break;

	// The other half of the bar's click path, minus the same geometry: a look
	// at a carried item, which is where the item right-click script runs.
	case PlayCommand::kLook:
		debugC(1, kDebugPlay, "play: %u: look %d (%s)", cmd.sourceLine, cmd.a,
			   _inventory.name((byte)cmd.a).c_str());
		if (_inventory.has((byte)cmd.a))
			lookAtItem((byte)cmd.a);
		break;

	case PlayCommand::kUnuse:
		debugC(1, kDebugPlay, "play: %u: unuse", cmd.sourceLine);
		holdItem(Inventory::kNoItem);
		break;

	default:
		break;
	}
}

void AlienEngine::runPlayCommand(const PlayCommand &cmd) {
	switch (cmd.type) {
	// Both click commands are written in *room* coordinates, which is what
	// check_hotspots.py prints and what the boxes in the script's own comments
	// are. A wide room's camera moves with the character, so a screen-space
	// script has to redo the arithmetic after every walk -- and gets it wrong
	// the moment a click walks somewhere it did not last time. The scroll goes
	// back in here instead. (The bar is never panned, but the bar is reached
	// through `use`/`unuse`, not through a click.)
	case PlayCommand::kClick:
	case PlayCommand::kRightClick: {
		const bool right = cmd.type == PlayCommand::kRightClick;
		debugC(1, kDebugPlay, "play: %u: %s %d,%d (scroll %d)", cmd.sourceLine,
			   right ? "rclick" : "click", cmd.a, cmd.b, _scrollX);
		if (!playClickAllowed(cmd))
			break;
		playClick(cmd.a - _scrollX, cmd.b, right);
		break;
	}

	case PlayCommand::kHover:
		debugC(1, kDebugPlay, "play: %u: hover %d,%d", cmd.sourceLine, cmd.a, cmd.b);
		updateHover(cmd.a, cmd.b);
		break;

	// A strict run works the bar the way the player does, a click at a time
	// (stepPlayBar); any other run goes straight to what the click would do.
	case PlayCommand::kUse:
	case PlayCommand::kCombine:
	case PlayCommand::kLook:
	case PlayCommand::kUnuse:
		if (_playStrict) {
			if (cmd.type == PlayCommand::kUnuse)
				debugC(1, kDebugPlay, "play: %u: bar: unuse, held %d", cmd.sourceLine, _heldItem);
			else
				debugC(1, kDebugPlay, "play: %u: bar: %s %d (%s), held %d", cmd.sourceLine,
					   playBarVerb(cmd.type), cmd.a, _inventory.name((byte)cmd.a).c_str(),
					   _heldItem);
			_playBar = cmd;
			_playBarBusy = true;
			_playBarClicks = 0;
		} else {
			runBarShortcut(cmd);
		}
		break;

	case PlayCommand::kWait:
		debugC(1, kDebugPlay, "play: %u: wait %d", cmd.sourceLine, cmd.a);
		_playWaitTicks = cmd.a;
		break;

	case PlayCommand::kConsole: {
		debugC(1, kDebugPlay, "play: %u: console %s", cmd.sourceLine, cmd.s.c_str());
		AlienConsole *console = static_cast<AlienConsole *>(getOrCreateDebugger());
		if (!console->runLine(cmd.s))
			warning("play: %u: no console command '%s'", cmd.sourceLine, cmd.s.c_str());
		// What the line queued runs now, as it would once the console closed.
		runConsoleActions();
		break;
	}

	case PlayCommand::kPause: {
		// Holds the game the way the GMM does, for a stretch of backend time,
		// and reports how many master ticks the engine thinks it is owed for
		// it -- none, since the pause stops the engine's clock.
		const uint32 before = masterTick();
		{
			PauseToken token = pauseEngine();
			g_system->delayMillis(cmd.a);
		}
		debugC(1, kDebugPlay, "play: %u: pause %d ms, ticks owed %u", cmd.sourceLine, cmd.a,
			   masterTick() - before);
		break;
	}

	case PlayCommand::kSettle:
		debugC(1, kDebugPlay, "play: %u: settle (timeout %d)", cmd.sourceLine, cmd.a);
		_playSettling = true;
		_playSettleTimeout = cmd.a;
		break;

	case PlayCommand::kUntilTimer:
		debugC(1, kDebugPlay, "play: %u: until timer %d (timeout %d)", cmd.sourceLine, cmd.a,
			   cmd.b);
		_playUntilTimer = cmd.a;
		_playSettleTimeout = cmd.b;
		break;

	case PlayCommand::kUntilRoom:
		debugC(1, kDebugPlay, "play: %u: until room %d (timeout %d)", cmd.sourceLine, cmd.a,
			   cmd.b);
		_playUntilRoom = cmd.a;
		_playSettleTimeout = cmd.b;
		break;

	case PlayCommand::kExpectRoom:
		if (_room == cmd.a) {
			debugC(1, kDebugPlay, "play: %u: PASS room %d", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL room: expected %d, got %d", cmd.sourceLine,
				   cmd.a, _room);
			playFailed(cmd.sourceLine);
		}
		break;

	case PlayCommand::kExpectItem:
		if (_inventory.has((byte)cmd.a)) {
			debugC(1, kDebugPlay, "play: %u: PASS item %d held", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL item: expected %d held", cmd.sourceLine, cmd.a);
			playFailed(cmd.sourceLine);
		}
		break;

	case PlayCommand::kExpectNoItem:
		if (!_inventory.has((byte)cmd.a)) {
			debugC(1, kDebugPlay, "play: %u: PASS noitem %d", cmd.sourceLine, cmd.a);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL noitem: %d unexpectedly held", cmd.sourceLine,
				   cmd.a);
			playFailed(cmd.sourceLine);
		}
		break;

	case PlayCommand::kGive:
		_inventory.add((byte)cmd.a);
		debugC(1, kDebugPlay, "play: %u: give item %d, page %u of %u", cmd.sourceLine, cmd.a,
			   _inventory.page(), _inventory.pageCount());
		break;

	case PlayCommand::kFlag:
		debugC(1, kDebugPlay, "play: %u: flag 0x%04x = %d", cmd.sourceLine, cmd.a, cmd.b);
		_script.setFlag((uint16)cmd.a, (byte)cmd.b);

		// Which rectangles a room registers is guarded on this block, so a
		// written flag has to build them again -- the engine does it after every
		// click for the same reason. Without it a script can set the state a
		// hotspot waits for and still not be able to click it.
		rebuildHotspots();
		break;

	case PlayCommand::kExpectFlag: {
		const byte got = _script.flag((uint16)cmd.a);
		if (got == (byte)cmd.b) {
			debugC(1, kDebugPlay, "play: %u: PASS flag 0x%04x == %d", cmd.sourceLine, cmd.a,
				   cmd.b);
		} else {
			debugC(1, kDebugPlay, "play: %u: FAIL flag 0x%04x: expected %d, got %d",
				   cmd.sourceLine, cmd.a, cmd.b, got);
			playFailed(cmd.sourceLine);
		}
		break;
	}

	case PlayCommand::kExpectWon:
		// A win ends the loop where it stands and answers this line itself
		// (playGameWon), so reaching it to run means the game was not won.
		debugC(1, kDebugPlay, "play: %u: FAIL won: the game has not been won", cmd.sourceLine);
		playFailed(cmd.sourceLine);
		break;

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

	case PlayCommand::kSave:
		debugC(1, kDebugPlay, "play: %u: save %d (room %d, scroll %d)", cmd.sourceLine,
			   cmd.a, _room, _scrollX);
		saveGameState(cmd.a, Common::String::format("play %u", cmd.sourceLine), false);
		break;

	case PlayCommand::kLoad:
		debugC(1, kDebugPlay, "play: %u: load %d", cmd.sourceLine, cmd.a);
		loadGameState(cmd.a);
		debugC(1, kDebugPlay, "play: %u: loaded room %d, scroll %d", cmd.sourceLine, _room,
			   _scrollX);
		break;

	case PlayCommand::kCheckpoint:
		writeCheckpoint(cmd);
		break;

	case PlayCommand::kSkip:
		// Only a line on screen takes the click as "read": anywhere else a
		// click is a walk, which is not what the script asked for.
		if (_speech) {
			debugC(1, kDebugPlay, "play: %u: skip", cmd.sourceLine);
			const Common::Point mouse = g_system->getEventManager()->getMousePos();
			playClick(mouse.x, mouse.y, false);
		} else {
			debugC(1, kDebugPlay, "play: %u: skip: no line to skip", cmd.sourceLine);
		}
		break;

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
		playQuit();
		break;
	}
}

/**
 * Strict mode: a scripted run that proves the game can be finished.
 *
 * The ordinary play channel is a tool for reaching a state -- it writes flags,
 * hands out items and raises scenes when that is quicker than playing up to
 * them. A strict run can do none of that.
 * It starts in the lab like a new game, every command is something a player
 * could do with the mouse, the bar is worked by clicking its arrows and slots,
 * and the first failure stops the run with what is needed to look at it: a
 * save to resume from just before, and the screen.
 */
bool AlienEngine::startPlayRun() {
	_playStrict = _play.strict() ||
				  (ConfMan.hasKey("playstrict") && ConfMan.getBool("playstrict"));
	if (!_playStrict)
		return true;

	debugC(1, kDebugPlay, "play: strict run");
	if (ConfMan.hasKey("boot_param")) {
		debugC(1, kDebugPlay, "play: 0: FAIL strict: a full run starts in the lab, "
			   "not in the room --boot-param names");
		playFailed(0);
		return false;
	}

	static const char *const kNames[] = { "flag", "give", "cutscene", "load" };
	for (uint i = 0; i < _play.commands().size(); i++) {
		const PlayCommand &cmd = _play.commands()[i];
		if (!cmd.isCheat())
			continue;
		const char *name = cmd.type == PlayCommand::kFlag ? kNames[0]
						 : cmd.type == PlayCommand::kGive ? kNames[1]
						 : cmd.type == PlayCommand::kCutscene ? kNames[2] : kNames[3];
		debugC(1, kDebugPlay, "play: %u: FAIL strict: '%s' is not something a player can do",
			   cmd.sourceLine, name);
		playFailed(cmd.sourceLine);
		return false;
	}
	return true;
}

/**
 * What a click has to get past before it is made. The cursor goes to the point
 * first, as a hand on the mouse puts it there, so the status line reads what
 * the player would read; a strict click with no cursor on the screen is one no
 * player could make, and a click that names a label has to find it there.
 */
bool AlienEngine::playClickAllowed(const PlayCommand &cmd) {
	const int x = cmd.a - _scrollX;
	const int y = cmd.b;

	// The point has to be one the cursor can reach. Room coordinates are only
	// a convenience of the script: the hotspot test would take an object a
	// screen away, past the camera, which no player can click. OBJ:sub_08a39
	// clamps the cursor to 0..0x134, 0..0xc7,
	// and the original harness refuses the same clicks.
	if (_playStrict && (x < 0 || x > kCursorMaxX || y < 0 || y > kCursorMaxY)) {
		debugC(1, kDebugPlay, "play: %u: FAIL strict: %d,%d is screen %d,%d at scroll %d, "
			   "out of the cursor's reach (0..%d, 0..%d)", cmd.sourceLine, cmd.a, cmd.b, x, y,
			   _scrollX, kCursorMaxX, kCursorMaxY);
		// What the script needs to walk the camera there first: where Ben
		// stands and which boxes a click on the way would hit instead.
		debugC(1, kDebugPlay, "play: %u: room %d, Ben at %d,%d", cmd.sourceLine, _room,
			   _ben.walkX(), _ben.walkY());
		for (uint i = 0; i < _spots.size(); i++)
			debugC(1, kDebugPlay, "play: spot: %3d,%3d..%3d,%3d obj %3u", _spots[i].x1,
				   _spots[i].y1, _spots[i].x2, _spots[i].y2, _spots[i].obj);
		playFailed(cmd.sourceLine);
		return false;
	}

	if (_playStrict || !cmd.s.empty())
		updateHover(x, y);

	if (_playStrict && !CursorMan.isVisible()) {
		debugC(1, kDebugPlay, "play: %u: FAIL strict: the cursor is hidden, so no player "
			   "could make this click", cmd.sourceLine);
		playFailed(cmd.sourceLine);
		return false;
	}

	if (!cmd.s.empty()) {
		Common::String reads = _labelText;
		Common::String wanted = cmd.s;
		reads.toLowercase();
		wanted.toLowercase();
		if (!reads.contains(wanted)) {
			debugC(1, kDebugPlay, "play: %u: FAIL label: expected \"%s\", the line reads \"%s\"",
				   cmd.sourceLine, cmd.s.c_str(), _labelText.c_str());
			playFailed(cmd.sourceLine);
			return false;
		}
		debugC(1, kDebugPlay, "play: %u: PASS label \"%s\"", cmd.sourceLine, _labelText.c_str());
	}
	return true;
}

const char *AlienEngine::playBarVerb(PlayCommand::Type type) {
	switch (type) {
	case PlayCommand::kUse:
		return "use";
	case PlayCommand::kCombine:
		return "combine";
	case PlayCommand::kLook:
		return "look";
	default:
		return "unuse";
	}
}

void AlienEngine::playBarClick(Common::Point at, bool right) {
	debugC(1, kDebugPlay, "play: %u: bar: %s %d,%d", _playBar.sourceLine,
		   right ? "rclick" : "click", at.x, at.y);
	updateHover(at.x, at.y);
	playClick(at.x, at.y, right);
}

/**
 * One tick of a bar command in a strict run: at most one click, and none
 * while the bar is still rolling to a page or the cursor is away. A held item
 * is put down with a right click first wherever the command needs an empty
 * hand, then the arrows are clicked until the item's page is on show, then
 * its slot -- left to take it or combine with what is held, right to look.
 */
void AlienEngine::stepPlayBar() {
	static const uint kGiveUpTicks = 400;

	const PlayCommand &cmd = _playBar;
	const byte item = (byte)cmd.a;

	if (++_playBarClicks > kGiveUpTicks) {
		debugC(1, kDebugPlay, "play: %u: FAIL bar: %s %d never got there", cmd.sourceLine,
			   playBarVerb(cmd.type), cmd.a);
		_playBarBusy = false;
		playFailed(cmd.sourceLine);
		return;
	}
	if (_inventory.isScrolling() || !CursorMan.isVisible())
		return;

	if (cmd.type == PlayCommand::kUse && _heldItem == item) {
		_playBarBusy = false;
		return;
	}

	if (_heldItem && cmd.type != PlayCommand::kCombine) {
		playBarClick(Inventory::slotPoint(0), true);
		if (cmd.type == PlayCommand::kUnuse)
			_playBarBusy = false;
		return;
	}
	if (cmd.type == PlayCommand::kUnuse) {
		_playBarBusy = false;
		return;
	}

	if (cmd.type == PlayCommand::kCombine && !_heldItem) {
		debugC(1, kDebugPlay, "play: %u: FAIL bar: combine %d with nothing in hand",
			   cmd.sourceLine, cmd.a);
		_playBarBusy = false;
		playFailed(cmd.sourceLine);
		return;
	}

	const uint page = _inventory.pageOf(item);
	if (page == 0) {
		debugC(1, kDebugPlay, "play: %u: FAIL bar: item %d (%s) is not carried",
			   cmd.sourceLine, cmd.a, _inventory.name(item).c_str());
		_playBarBusy = false;
		playFailed(cmd.sourceLine);
		return;
	}
	if (page != _inventory.page()) {
		playBarClick(Inventory::arrowPoint(page < _inventory.page() ? Inventory::kArrowUp
																	: Inventory::kArrowDown),
					 false);
		return;
	}

	for (uint slot = 0; slot < Inventory::kSlotCount; slot++) {
		if (_inventory.slotItem(slot) != item)
			continue;
		// The click on the slot is the command done. Not "the hand holds it"
		// for a use: in the store an item out of the bar is the offer, and
		// the hand is emptied the moment it is taken (store.cpp). What the
		// click led to is for the script's own expects to check.
		playBarClick(Inventory::slotPoint(slot), cmd.type == PlayCommand::kLook);
		_playBarBusy = false;
		return;
	}
}

/**
 * Every failed check comes through here. An ordinary run counts it and goes
 * on; a strict run stops at the first one, leaving the state behind it.
 */
void AlienEngine::playFailed(uint line) {
	_playFails++;
	if (!_playStrict)
		return;

	writeFailureState(line);
	debugC(1, kDebugPlay, "play: script stopped at line %u, %u assertion failure(s)", line,
		   _playFails);
	playQuit();
}

/**
 * The win ends the game loop wherever the script stands -- usually inside the
 * settle it waits for the pod with -- so the one line that can follow it is
 * `expect won`, answered here. Anything else after it is a line the run never
 * reached.
 */
void AlienEngine::playGameWon() {
	if (!_playActive)
		return;

	const Common::Array<PlayCommand> &cmds = _play.commands();
	if (_playIndex < cmds.size() && cmds[_playIndex].type == PlayCommand::kExpectWon) {
		debugC(1, kDebugPlay, "play: %u: PASS won", cmds[_playIndex].sourceLine);
		_playIndex++;
	}

	if (_playIndex < cmds.size()) {
		debugC(1, kDebugPlay, "play: %u: FAIL won: the game was won before this line",
			   cmds[_playIndex].sourceLine);
		playFailed(cmds[_playIndex].sourceLine);
		return;
	}

	debugC(1, kDebugPlay, "play: script complete, %u assertion failure(s)", _playFails);
	_playActive = false;
}

/**
 * A checkpoint: the state, at rest, in NAME.sav under `playstates`. The file
 * is the engine's save stream and nothing else -- no date, no thumbnail -- so
 * a checkpoint rebuilt from an unchanged route is the same file byte for byte.
 * The CHECKPOINT line carries what the runner puts in the sidecar beside it.
 */
void AlienEngine::writeCheckpoint(const PlayCommand &cmd) {
	const Common::String dir = (ConfMan.hasKey("playstates") ? ConfMan.get("playstates")
															 : Common::String(".")) + "/";
	const Common::String file = dir + cmd.s + ".sav";

	Common::DumpFile out;
	if (!out.open(Common::Path(file), true)) {
		debugC(1, kDebugPlay, "play: %u: FAIL checkpoint: could not write %s", cmd.sourceLine,
			   file.c_str());
		playFailed(cmd.sourceLine);
		return;
	}
	writeCheckpointScreen(cmd.s);

	// A checkpoint stands in for a save from the menu, and the menu forgets
	// the way into the room (LOGIC:sub_12b08), so the run that goes on from
	// here is the one a resumed run sees.
	const byte mode = _mode;
	_mode = 0;
	saveGameStream(&out);
	writePlayCamera(&out);
	out.finalize();
	out.close();

	// `playunbroken` goes on from the engine as it stands, carrying the
	// same fractions and clocks across the mark.
	if (ConfMan.hasKey("playunbroken") && ConfMan.getBool("playunbroken")) {
		_mode = mode;
		debugC(1, kDebugPlay, "play: %u: CHECKPOINT %s tick %u room %d hash %08x seed %u file %s",
			   cmd.sourceLine, cmd.s.c_str(), _tick, _room, stateHash(), _rnd.getSeed(), file.c_str());
		stopAtCheckpoint(cmd);
		return;
	}

	// And the run goes on from the file, not from the engine as it stands: a
	// resumed run can only see what the file holds, so the unbroken run has to
	// see the same, or anything the save leaves out -- how long he has been
	// standing, the pose he is in -- would make the two part company a few
	// ticks down the route. It also puts a restore under every checkpoint.
	Common::File in;
	if (!in.open(Common::FSNode(Common::Path(file)))) {
		debugC(1, kDebugPlay, "play: %u: FAIL checkpoint: could not read back %s", cmd.sourceLine,
			   file.c_str());
		playFailed(cmd.sourceLine);
		return;
	}
	// On the pass that ran a tick; the loop's own sleep follows.
	restorePlayState(&in, 0);
	debugC(1, kDebugPlay, "play: %u: CHECKPOINT %s tick %u room %d hash %08x seed %u file %s",
		   cmd.sourceLine, cmd.s.c_str(), _tick, _room, stateHash(), _rnd.getSeed(), file.c_str());

	stopAtCheckpoint(cmd);
}

/** `playuntil` builds the states up to one checkpoint and stops there. */
void AlienEngine::stopAtCheckpoint(const PlayCommand &cmd) {
	if (ConfMan.hasKey("playuntil") && ConfMan.get("playuntil") == cmd.s) {
		debugC(1, kDebugPlay, "play: stopped at checkpoint %s, %u assertion failure(s)",
			   cmd.s.c_str(), _playFails);
		playQuit();
	}
}

/**
 * The screen at a checkpoint, as the player sees it before the reload from the
 * file, in port/screens/NAME.png under `playout`.
 */
void AlienEngine::writeCheckpointScreen(const Common::String &name) {
	if (!ConfMan.hasKey("playout"))
		return;
	const Common::String base = ConfMan.get("playout") + "/port/screens/" + name;

	// The sidecar first: its open makes the directory the screen goes in.
	Common::DumpFile out;
	if (!out.open(Common::Path(base + ".json"), true)) {
		warning("play: could not write %s.json", base.c_str());
		return;
	}
	out.writeString(Common::String::format(
		"{\"name\": \"%s\", \"room\": %d, \"tick\": %u, \"scroll\": %d, "
		"\"ben\": [%d, %d, %d], \"frame\": %u, \"scale\": %u, \"light\": [%d, %d]}\n",
		name.c_str(), _room, _tick, _scrollX, _ben.spriteX(), _ben.spriteY(), _ben.facing(),
		_ben.frame(), _ben.scale(), _lightLevel, _lightPrev));
	out.finalize();
	out.close();

	redraw();
	dumpScreen(base + ".png");
}

/**
 * The camera, after the save stream of a checkpoint or failure state. A save
 * has no scroll of its own -- loading one puts the camera on the character,
 * as the original's does -- but a checkpoint is not a save the player made:
 * a run that goes on from it has to see the camera a player who never saved
 * would see, and so does the original's run of the same route
 * which keeps the whole machine across a
 * checkpoint. An ordinary save has no block, and loads as before.
 */
static const uint32 kPlayCameraTag = MKTAG('C', 'A', 'M', '1');

void AlienEngine::writePlayCamera(Common::WriteStream *out) const {
	out->writeUint32BE(kPlayCameraTag);
	out->writeSint32LE(_scrollX);
	out->writeSint32LE(_scrollPos);
	out->writeSint32LE(_scrollVel);
	out->writeByte(_scrollState);
	out->writeSint32LE(_scrollFocus);
}

void AlienEngine::readPlayCamera(Common::SeekableReadStream *in) {
	if (in->size() - in->pos() < 21 || in->readUint32BE() != kPlayCameraTag)
		return;
	_scrollX = in->readSint32LE();
	_scrollPos = in->readSint32LE();
	_scrollVel = in->readSint32LE();
	_scrollState = in->readByte();
	_scrollFocus = in->readSint32LE();
	// Placed, so the next pass pans on from here rather than snapping.
	_scrollPlacements = _ben.placements();
	_dirty = true;
}

/**
 * Load a play state the same way for a checkpoint and for a resume. The room's
 * fade in, which the loop would otherwise run under the first command, is
 * done here, and the clock is left standing `sinceTick` milliseconds after a
 * tick with no command due until the next one -- the panels and scenes that
 * wait in milliseconds rather than ticks read the clock between ticks, so the
 * phase has to match as well as the count.
 */
void AlienEngine::restorePlayState(Common::SeekableReadStream *in, uint32 sinceTick) {
	loadGameStream(in);
	readPlayCamera(in);
	if (_fadePending) {
		redraw();
		fadeIn();
	}
	_lastTick = millis() - sinceTick;
	_playLastTick = _tick;
}

/**
 * Start a run part way through its script: `playfrom` names a state written by
 * a checkpoint (or any save of the engine's own), and `playline` the script
 * line it was written at. The state is loaded before the first command, and the
 * script goes on with the first command after that line.
 */
bool AlienEngine::resumePlayRun() {
	if (!ConfMan.hasKey("playfrom"))
		return true;

	const Common::String file = ConfMan.get("playfrom");
	const Common::FSNode node((Common::Path(file)));
	Common::SeekableReadStream *in = node.exists() ? node.createReadStream() : nullptr;
	if (!in) {
		debugC(1, kDebugPlay, "play: 0: FAIL resume: could not open %s", file.c_str());
		playFailed(0);
		return false;
	}
	// Before the loop's first pass, which has no sleep in front of it.
	restorePlayState(in, kLoopSleepMillis);
	delete in;

	const uint line = ConfMan.hasKey("playline") ? (uint)ConfMan.getInt("playline") : 0;
	while (_playIndex < _play.commands().size() &&
		   _play.commands()[_playIndex].sourceLine <= line)
		_playIndex++;

	debugC(1, kDebugPlay, "play: resumed from %s after line %u: tick %u room %d hash %08x",
		   file.c_str(), line, _tick, _room, stateHash());
	return true;
}

/**
 * A screen of its own that a script stopped clicking in. An ordinary run is
 * let out by the port's own guard; for a strict run that guard would be doing
 * the player's part, so it is a failure instead.
 */
void AlienEngine::playLeftOpen(const char *what) {
	if (!_playStrict)
		return;
	const uint line = _playIndex ? _play.commands()[_playIndex - 1].sourceLine : 0;
	debugC(1, kDebugPlay, "play: %u: FAIL strict: the script left %s open", line, what);
	playFailed(line);
}

/**
 * FAIL-<line>.sav and FAIL-<line>.png in the `playout` directory (the working
 * directory without one). The save is the engine's own format, written
 * straight rather than through a slot, so a routing run can start from it
 * (phase 4) without touching the player's saves.
 */
void AlienEngine::writeFailureState(uint line) {
	// A bare file name has no parent the filesystem layer can resolve, so the
	// working directory is spelled out.
	const Common::String dir = (ConfMan.hasKey("playout") ? ConfMan.get("playout")
														  : Common::String(".")) + "/";
	const Common::String base = dir + Common::String::format("FAIL-%u", line);

	Common::DumpFile save;
	if (save.open(Common::Path(base + ".sav"), true)) {
		saveGameStream(&save);
		writePlayCamera(&save);
		save.finalize();
		debugC(1, kDebugPlay, "play: failure state written to %s.sav", base.c_str());
	} else {
		warning("play: could not write %s.sav", base.c_str());
	}

	redraw();
	dumpScreen(base + ".png");
}

} // End of namespace Alien
