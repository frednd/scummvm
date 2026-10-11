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

#ifndef ALIEN_ROOMS_MANSION_H
#define ALIEN_ROOMS_MANSION_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 27's manhole: the climb up out of the sewer.
class MansionRoom : public Room {
public:
	explicit MansionRoom(AlienEngine *vm);

	void reset(int room) override { _mansionStep = 0; }
	void enter(int room) override;
	void tickPair() override;
	bool holdsCharacter() const override { return _mansionStep != 0; }

private:
	/// The room's [0xa49f]: 3 while the climb has the character.
	byte _mansionStep;
};

} // End of namespace Alien

#endif
