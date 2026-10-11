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

#ifndef ALIEN_ROOMS_LIFT_H
#define ALIEN_ROOMS_LIFT_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 3's computer and the lift car it parks: the panel the computer opens
/// once its line is down.
class LiftRoom : public Room {
public:
	explicit LiftRoom(AlienEngine *vm);

	void tickPair() override { stepLiftCall(); }
	void lineDown() override { stepLiftCall(); }
	void afterQueue(int obj, int anchorX, int anchorY) override;

private:
	bool armLiftCall(int obj, int anchorX, int anchorY);
	void stepLiftCall();
	void playLiftPanel();
	bool liftWipe(const Graphics::Surface &to);

	bool _liftPending;			///< the computer's line is up; the panel follows it
};

} // End of namespace Alien

#endif
