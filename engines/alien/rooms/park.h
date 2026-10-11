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

#ifndef ALIEN_ROOMS_PARK_H
#define ALIEN_ROOMS_PARK_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 22's arrival scene: the voice in the booth, and the wreck it leaves
/// behind.
class ParkRoom : public Room {
public:
	explicit ParkRoom(AlienEngine *vm);

	void start() override;
	void holdCamera() override;
	void tickPair() override;
	void walkPhase() override;

private:

	/// The step of its machine, and ticks left of
	/// whichever of its two waits is running.
	byte _parkStep;
	uint _parkWait;
	bool _parkLock;				///< [0xa8a4]: the scene holds the camera
};

} // End of namespace Alien

#endif
