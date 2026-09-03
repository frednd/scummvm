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

#ifndef ALIEN_PLAY_H
#define ALIEN_PLAY_H

#include "common/array.h"
#include "common/str.h"

namespace Alien {

/**
 * One line of a scripted playthrough (see docs/port_plan.md, milestone S).
 * The engine has no interactive input in a headless run, so a walkthrough
 * is driven from a text command file instead of an event queue: one command
 * per line, blank lines and '#' comments skipped.
 *
 *   click X Y            left click at a playfield point -- walks, and arms
 *                        an exit; it performs no verb
 *   rclick X Y           right click: performs the hovered object's verb, and
 *                        does nothing at all over bare floor
 *
 * Both take **room** coordinates, the ones check_hotspots.py prints, not screen
 * ones: a wide room's camera moves with the character, and the engine puts the
 * scroll back before it dispatches the click.
 *   use N                take inventory item N into the hand
 *   unuse                put down whatever is in the hand
 *   wait N                advance N master ticks with nothing happening
 *   settle [N]            advance ticks until Ben, speech, anims and any
 *                          armed exit are all idle, or N ticks pass (default
 *                          700, about 10s of game time) -- logs STUCK on
 *                          timeout and moves on rather than hanging the run
 *   expect room N          assert the current room
 *   expect item N           assert the item is held in the inventory
 *   expect noitem N         assert the item is not held
 *   expect flag ADDR VAL    assert a state-block byte (hex address, e.g.
 *                           0xa650) equals VAL
 *   cutscene N              raise scene id N, the way a room's own code does
 *   snap NAME               write the composed screen to NAME.png
 *   spots                   log the room's current hotspot list on the play
 *                           channel (independent of --debugflags=hotspots),
 *                           for checking what a state change revealed
 *   quit                    stop the script early
 */
struct PlayCommand {
	enum Type {
		kClick,
		kRightClick,
		kUse,
		kUnuse,
		kWait,
		kSettle,
		kExpectRoom,
		kExpectItem,
		kExpectNoItem,
		kExpectFlag,
		kCutscene,
		kSnap,
		kSpots,
		kQuit
	};

	Type type = kQuit;
	int a = 0;
	int b = 0;
	int c = 0;
	Common::String s;
	uint sourceLine = 0;
};

/// Parses a playthrough command file into a list of PlayCommands.
class PlayScript {
public:
	bool load(const Common::String &path);
	const Common::Array<PlayCommand> &commands() const { return _commands; }

private:
	Common::Array<PlayCommand> _commands;
};

} // End of namespace Alien

#endif
