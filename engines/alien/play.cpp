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

		if (verb == "click" || verb == "rclick") {
			cmd.type = verb == "click" ? PlayCommand::kClick : PlayCommand::kRightClick;
			cmd.a = parseInt(tok.nextToken());
			cmd.b = parseInt(tok.nextToken());
		} else if (verb == "hover") {
			cmd.type = PlayCommand::kHover;
			cmd.a = parseInt(tok.nextToken());
			cmd.b = parseInt(tok.nextToken());
		} else if (verb == "use") {
			cmd.type = PlayCommand::kUse;
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

} // End of namespace Alien
