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

#include "alien/alien.h"
#include "alien/rooms/boss.h"

namespace Alien {

/**
 * The rooms, in the order every hook runs them.
 *
 * Each hook answers only for its own rooms, so between files that serve
 * different rooms the order is immaterial. Where two share one, the earlier
 * here is the one whose code the original runs first.
 */
void AlienEngine::registerRooms() {
	_rooms.push_back(new BossRoom(this));
}

} // End of namespace Alien
