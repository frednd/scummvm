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

#ifndef ALIEN_ROOMS_BEDROOM_H
#define ALIEN_ROOMS_BEDROOM_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 7, the bedroom: the light switch, which swaps the room's whole set, and
/// the closet that is the lift car; and the landing outside, which walks him on.
class BedroomRoom : public Room {
public:
	explicit BedroomRoom(AlienEngine *vm);

	void reset() override { _bedroomLiftStep = 0; }
	void placed(int room) override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool hijackExit(byte submode) override;

private:
	void reloadPlates();
	void bedroomSwitch(int anchorX, int anchorY);
	bool isBedroomSwitch(int obj) const;

	/// The walk into the lift car: 0 idle, 1 his line, 2 WALKINC1.
	byte _bedroomLiftStep;
};

} // End of namespace Alien

#endif
