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

#ifndef ALIEN_ROOMS_SEWER_H
#define ALIEN_ROOMS_SEWER_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 35, the sewer: the water it opens full of, the valve that drains it,
/// the hatch that ends it, and the ladder back up.
class SewerRoom : public Room {
public:
	explicit SewerRoom(AlienEngine *vm);

	void reset() override { _sewerStep = 0; }
	void enter(int room) override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool holdsCharacter() const override { return _sewerStep != 0; }

private:
	void armSewer(int obj, byte verb);
	void sewerValve();

	/// The room's [0xa49f] machine. Zero when nothing is running.
	byte _sewerStep;

	/// The sewer's water, which the original keeps as four words of its own in
	/// the data segment rather than in the state block: [0x4380] is the phase
	/// into the thirty-entry ripple table at [0x4362], [0x4382] the depth the
	/// drain has taken off, [0x4384] the divider that lets the depth move once
	/// every third animation frame, and [0x4385] whether the drain is running.
	/// GAME.EXE ships them as 0, 0x32, 0, 0 and nothing else in the game reads
	/// or writes them, so they are here rather than in RoomScript.
	uint16 _sewerPhase;
	uint16 _sewerDepth;
	byte _sewerDivider;
	byte _sewerDraining;
};

} // End of namespace Alien

#endif
