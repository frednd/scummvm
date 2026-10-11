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

#ifndef ALIEN_ROOMS_OPENING_H
#define ALIEN_ROOMS_OPENING_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 3's opening: a new game opens with the character talking to himself
/// before the player gets the cursor.
class OpeningRoom : public Room {
public:
	explicit OpeningRoom(AlienEngine *vm);

	void start() override { startOpening(); }
	void machine() override { stepOpening(); }
	void skipOpening() override { cancelOpening(); }
	bool holdsCharacter() const override { return _openingStep != 0; }
	bool holdsInput() const override { return _openingStep != 0; }

private:
	void startOpening();
	void stepOpening();
	void cancelOpening();

	/// The step the machine is on, and [0x7dc4] -- whether the
	/// game has yet to say its first words. A restored save clears the flag the
	/// way MAIN's load branch does.
	byte _openingStep;
	bool _openingPending;
};

} // End of namespace Alien

#endif
