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

#ifndef ALIEN_ROOMS_SHORE_H
#define ALIEN_ROOMS_SHORE_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 41, the beach and its two ledges: the pick-axe that opens the way into
/// the second maze, the climbs between the cave mouths, and the diving suit on the
/// water, the way back down to room 46.
class ShoreRoom : public Room {
public:
	explicit ShoreRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	Body body(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool walk(int x, int y, int arrivalFacing) override;
	bool busy() const override;

private:
	bool armShoreAxe(int obj, byte item);
	bool armShoreSuit(int obj, byte item);
	void shoreArrival();
	byte shoreRefusal(byte view, byte obj) const;

	/// The room's own machines: the step of the pick-axe swing, or of whichever
	/// climb between the two cave mouths is running.
	byte _shoreStep;
};

} // End of namespace Alien

#endif
