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

#ifndef ALIEN_ROOMS_LIBRARY_H
#define ALIEN_ROOMS_LIBRARY_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 8: the owl, whose talk is the room's own machine rather than a script
/// body, and the safe, which the stethoscope cracks.
class LibraryRoom : public Room {
public:
	explicit LibraryRoom(AlienEngine *vm);

	void reset(int room) override;
	void restorePlate(int room) override;
	bool roomClock() override;
	void tickPair() override;
	void frame() override;
	void walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) override;
	void preAction(int obj, byte verb, byte item) override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	Body body(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool holdsCharacter() const override { return _libraryStep != 0; }

private:
	bool armLibrary(int obj, byte verb, bool item, int anchorX, int anchorY);
	void armLibrarySafe(int obj, byte item);
	bool runLibraryBody(int obj, bool item);
	void standClearOfSafe(byte obj, WalkTarget &target) const;

	/// Room 8's [0xa49f] machine: talking to the owl, and cracking the safe.
	byte _libraryStep;

	/// [0xa49c] as room 8 reads it: the free-running counter its safe steps
	/// time themselves off, cleared where each of them starts.
	uint16 _libraryPos;
};

} // End of namespace Alien

#endif
