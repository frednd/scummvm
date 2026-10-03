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

#include "alien/play.h"

#include "common/debug.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/tokenizer.h"

namespace Alien {

static int parseInt(const Common::String &tok) {
	return (int)strtol(tok.c_str(), nullptr, 0);
}

bool PlayScript::load(const Common::String &path) {
	// The script lives on the host filesystem, not inside the game's own
	// SearchMan-indexed tree, so this reads it via FSNode rather than
	// Common::File.
	const Common::FSNode node((Common::Path(path)));
	Common::SeekableReadStream *stream = node.exists() ? node.createReadStream() : nullptr;
	if (!stream) {
		warning("play: could not open script %s", path.c_str());
		return false;
	}

	_commands.clear();
	_strict = false;
	uint lineNo = 0;

	while (!stream->eos()) {
		Common::String line = stream->readLine();
		lineNo++;

		const size_t hash = line.findFirstOf('#');
		if (hash != Common::String::npos)
			line = Common::String(line.c_str(), hash);

		Common::StringTokenizer tok(line);
		if (tok.empty())
			continue;

		const Common::String verb = tok.nextToken();
		PlayCommand cmd;
		cmd.sourceLine = lineNo;

		if (verb == "strict") {
			_strict = true;
			continue;
		} else if (verb == "click" || verb == "rclick") {
			cmd.type = verb == "click" ? PlayCommand::kClick : PlayCommand::kRightClick;
			cmd.a = parseInt(tok.nextToken());
			cmd.b = parseInt(tok.nextToken());
			// The label the status line has to read under the cursor first, in
			// quotes, so it can have spaces in it.
			const size_t open = line.findFirstOf('"');
			const size_t close = line.findLastOf('"');
			if (open != Common::String::npos && close > open)
				cmd.s = Common::String(line.c_str() + open + 1, close - open - 1);
		} else if (verb == "hover") {
			cmd.type = PlayCommand::kHover;
			cmd.a = parseInt(tok.nextToken());
			cmd.b = parseInt(tok.nextToken());
		} else if (verb == "use") {
			cmd.type = PlayCommand::kUse;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "combine") {
			cmd.type = PlayCommand::kCombine;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "look") {
			cmd.type = PlayCommand::kLook;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "unuse") {
			cmd.type = PlayCommand::kUnuse;
		} else if (verb == "wait") {
			cmd.type = PlayCommand::kWait;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "settle") {
			cmd.type = PlayCommand::kSettle;
			Common::String n = tok.nextToken();
			cmd.a = n.empty() ? 700 : parseInt(n);
		} else if (verb == "expect") {
			const Common::String what = tok.nextToken();
			if (what == "room") {
				cmd.type = PlayCommand::kExpectRoom;
				cmd.a = parseInt(tok.nextToken());
			} else if (what == "item") {
				cmd.type = PlayCommand::kExpectItem;
				cmd.a = parseInt(tok.nextToken());
			} else if (what == "noitem") {
				cmd.type = PlayCommand::kExpectNoItem;
				cmd.a = parseInt(tok.nextToken());
			} else if (what == "flag") {
				cmd.type = PlayCommand::kExpectFlag;
				cmd.a = parseInt(tok.nextToken());
				cmd.b = parseInt(tok.nextToken());
			} else if (what == "won") {
				cmd.type = PlayCommand::kExpectWon;
			} else {
				warning("play: %s:%u: unknown expect '%s'", path.c_str(), lineNo, what.c_str());
				continue;
			}
		} else if (verb == "cutscene") {
			cmd.type = PlayCommand::kCutscene;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "snap") {
			cmd.type = PlayCommand::kSnap;
			cmd.s = tok.nextToken();
		} else if (verb == "save" || verb == "load") {
			// The state through the engine's own save path and back, which is
			// the only way a script can reach what a restored game looks like.
			cmd.type = verb == "save" ? PlayCommand::kSave : PlayCommand::kLoad;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "flag") {
			// Not something the player can do: a way to reach a state the
			// script would otherwise have to play the whole chain up to.
			cmd.type = PlayCommand::kFlag;
			cmd.a = parseInt(tok.nextToken());
			cmd.b = parseInt(tok.nextToken());
		} else if (verb == "give") {
			cmd.type = PlayCommand::kGive;
			cmd.a = parseInt(tok.nextToken());
		} else if (verb == "checkpoint") {
			// A checkpoint is taken at rest, between clicks, the way the
			// original's own save is: a settle first, then the write.
			PlayCommand settle;
			settle.type = PlayCommand::kSettle;
			settle.a = 700;
			settle.sourceLine = lineNo;
			_commands.push_back(settle);
			cmd.type = PlayCommand::kCheckpoint;
			cmd.s = tok.nextToken();
			if (cmd.s.empty()) {
				warning("play: %s:%u: checkpoint needs a name", path.c_str(), lineNo);
				continue;
			}
		} else if (verb == "skip") {
			cmd.type = PlayCommand::kSkip;
		} else if (verb == "spots") {
			cmd.type = PlayCommand::kSpots;
		} else if (verb == "quit") {
			cmd.type = PlayCommand::kQuit;
		} else {
			warning("play: %s:%u: unknown command '%s'", path.c_str(), lineNo, verb.c_str());
			continue;
		}

		_commands.push_back(cmd);
	}

	delete stream;
	debug(1, "play: loaded %s: %u commands", path.c_str(), _commands.size());
	return true;
}

bool PlayCommand::isCheat() const {
	// What no player can do: write the state, conjure an item, raise a scene,
	// or jump to a saved game from inside a run that has to be played through.
	return type == kFlag || type == kGive || type == kCutscene || type == kLoad;
}

} // End of namespace Alien
