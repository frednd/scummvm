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

#ifndef ALIEN_ROOMS_TELESCOPE_H
#define ALIEN_ROOMS_TELESCOPE_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 28: the telescope view and the observatory computer, both of which the
/// lifted table only ever arms as game_submode = 111, and the lever, the one
/// writer of [0xa760].
class TelescopeRoom : public Room {
public:
	explicit TelescopeRoom(AlienEngine *vm) : Room(vm) {}

	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool takeSubmode(int obj, byte verb, byte submode) override;

private:
	void armTelescopeLever(int obj, byte verb);
	void playTelescopeView();
	void playObservatoryScreen();
};

} // End of namespace Alien

#endif
