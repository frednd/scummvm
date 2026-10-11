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

#ifndef ALIEN_ROOMS_CAVE_H
#define ALIEN_ROOMS_CAVE_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 40, the cave entrance: its shared dialog files, the one item-use line it
/// has, and the two ways to the hole.
class CaveRoom : public Room {
public:
	explicit CaveRoom(AlienEngine *vm) : Room(vm) {}

	void scriptLoaded(int room) override;
	bool roomClock() override;
	void walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) override;
	Body body(int obj, byte verb, byte item, int anchorX, int anchorY) override;

private:
	void loadSharedTal(uint n);
	bool armCave(int obj, byte item, int anchorX, int anchorY);
};

} // End of namespace Alien

#endif
