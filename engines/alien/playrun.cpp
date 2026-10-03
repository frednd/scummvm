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
#include "common/file.h"
#include "common/fs.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

/**
 * Strict mode: a scripted run that proves the game can be finished.
 *
 * The ordinary play channel is a tool for reaching a state -- it writes flags,
 * hands out items and raises scenes when that is quicker than playing up to
 * them. A strict run (docs/autoplaytest_plan.md, phase 2) can do none of that.
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
		debugC(1, kDebugPlay, "play: 0: FAIL strict: a playthrough starts in the lab, "
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
	clickAt(at.x, at.y, right);
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
	_playActive = false;
	_quit = true;
}

/**
 * A checkpoint: the state, at rest, in NAME.sav under `playstates`. The file
 * is the engine's save stream and nothing else -- no date, no thumbnail -- so
 * a checkpoint rebuilt from an unchanged route is the same file byte for byte
 * (docs/autoplaytest_plan.md, 4.6). The CHECKPOINT line carries what the
 * runner puts in the sidecar beside it.
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
	// A checkpoint stands in for a save from the menu, and the menu forgets
	// the way into the room (LOGIC:sub_12b08), so the run that goes on from
	// here is the one a resumed run sees.
	_mode = 0;
	saveGameStream(&out);
	out.finalize();
	debugC(1, kDebugPlay, "play: %u: CHECKPOINT %s tick %u room %d hash %08x seed %u file %s",
		   cmd.sourceLine, cmd.s.c_str(), _tick, _room, stateHash(), _rnd.getSeed(), file.c_str());

	// `playuntil` builds the states up to one checkpoint and stops there.
	if (ConfMan.hasKey("playuntil") && ConfMan.get("playuntil") == cmd.s) {
		debugC(1, kDebugPlay, "play: stopped at checkpoint %s, %u assertion failure(s)",
			   cmd.s.c_str(), _playFails);
		_playActive = false;
		_quit = true;
	}
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
	loadGameStream(in);
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
		save.finalize();
		debugC(1, kDebugPlay, "play: failure state written to %s.sav", base.c_str());
	} else {
		warning("play: could not write %s.sav", base.c_str());
	}

	redraw();
	dumpScreen(base + ".png");
}

} // End of namespace Alien
