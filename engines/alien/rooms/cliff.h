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

#ifndef ALIEN_ROOMS_CLIFF_H
#define ALIEN_ROOMS_CLIFF_H

#include "alien/charanim.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Room 31, the cliff: its two ledges and the climb between them.
class CliffRoom : public Room {
public:
	explicit CliffRoom(AlienEngine *vm);

	void reset(int room) override;
	void tickPair() override;
	void walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) override;
	bool busy() const override { return _cliffClimb || _cliffStep; }

private:
	void armCliff(int clickX, int clickY, WalkTarget &target);

	/// The room's [0xa49f], and the [0xa738] beside it that says which climb an arrival
	/// is going to start. [0xa49c] is kept per room, the way the
	/// lab and the library keep theirs.
	byte _cliffStep;
	byte _cliffClimb;
	uint16 _cliffPos;

	/// The point the climb waits for him at, and -- for the way up -- the point
	/// the click that started it had asked for, the original's [0xa732],
	/// [0xa734] and [0xa736].
	int _cliffX, _cliffY, _cliffFacing;
	int _cliffResumeX, _cliffResumeY, _cliffResumeFacing;
};

} // End of namespace Alien

#endif
