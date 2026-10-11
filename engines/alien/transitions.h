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

#ifndef ALIEN_TRANSITIONS_H
#define ALIEN_TRANSITIONS_H

#include "alien/roomscripts.h"

namespace Alien {

/**
 * One link of the main loop's event chain: which room a given exit leads to.
 *
 * A room exit is not a hotspot outcome. Clicking a door arms a *submode* (see
 * walkgeom.h), and the arming only fires once the character has arrived on the
 * approach point facing the recorded way -- OBJ:sub_078dd copies the armed
 * submode into game_submode and ends the room's tick loop. The room then sets
 * game_mode to its own number, so the pair the main loop sees names the room
 * being left and the exit taken, and a chain of check_event() calls turns that
 * pair into the next room.
 *
 * `mode` is therefore the room left, not the room entered. A handful of pairs
 * appear twice under opposite guards, because the same door leads to different
 * rooms at different points in the game, and one submode value -- 111 -- names
 * the room itself: a close-up or a cutscene that re-enters the room it was
 * started from.
 *
 * The chain runs top to bottom and the first link that matches dispatches, so
 * the table is in the original's order and is scanned in that order.
 */
struct Transition {
	byte mode;			///< the room being left
	byte submode;		///< the submode its exit armed
	byte room;			///< where that pair leads
	byte guardCount;
	ScriptCond guards[3];
};

/** The whole chain, in the order the original tests it. */
const Transition *transitionTable(uint &count);

} // End of namespace Alien

#endif
