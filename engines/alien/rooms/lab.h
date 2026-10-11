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

#ifndef ALIEN_ROOMS_LAB_H
#define ALIEN_ROOMS_LAB_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 3, the lab: the room's own [0xa49f] machine -- the plank, the hole and
/// the creaking floor -- less the opening monologue and the computer, which
/// opening.cpp and lift.cpp have.
class LabRoom : public Room {
public:
	explicit LabRoom(AlienEngine *vm);

	void reset() override;
	void tickPair() override { stepLab(); }
	void preAction(int obj, byte verb, byte item) override;
	bool holdsCharacter() const override { return _labStep != 0; }

private:
	void armLab(int obj, bool item);
	void stepLab();
	void stepLabHole();

	/// The room's [0xa49f] machine. Zero when nothing is running.
	byte _labStep;

	/// [0xa49c] as the room keeps it: a free-running counter its tick advances
	/// on every tick pair, which two of its steps time themselves off.
	uint16 _labPos;

	/// [0xa7a4]/[0xa7a5]: whether the character stood inside the room's creak
	/// rectangle on the last tick, which is the edge the two samples fire on.
	bool _labNearHole;
};

} // End of namespace Alien

#endif
