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

#ifndef ALIEN_ROOMS_BASEMENT_H
#define ALIEN_ROOMS_BASEMENT_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 13, the basement: the ladder down into it and back up, and the
/// battery.
class BasementRoom : public Room {
public:
	explicit BasementRoom(AlienEngine *vm);

	void reset(int room) override;
	void enter(int room) override;
	bool roomClock() override;
	void tickPair() override;
	void walkPhase() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool sharedExit(int room) const override;
	bool holdsCharacter() const override { return _basementStep != 0; }

private:
	void takeBasementBattery(int obj);

	/// The room's [0xa49f], which is the same byte in the original.
	byte _basementStep;

	/// Whether slot 0 is running the room's arrival climb, which is what the
	/// original can read off the slot itself.
	bool _basementClimbing;
	bool _basementWalkOff;		///< [0xa5fe]: walking off the ladder, cursor still away
};

} // End of namespace Alien

#endif
