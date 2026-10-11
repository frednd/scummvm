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

#ifndef ALIEN_ROOMS_FOREST_H
#define ALIEN_ROOMS_FOREST_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 26: the parrot, whose brush-off and whose taste for moldy bread are
/// machines of its own, and the note.
class ForestRoom : public Room {
public:
	explicit ForestRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool takeSubmode(int obj, byte verb, byte submode) override;

private:
	void armForestParrot(int obj, byte verb, int item);
	void parrotSays(byte line);
	void parrotQuiet();
	void forestBenSays(byte line);
	void forestArm(byte step);

	/// The parrot's machine.
	byte _forestStep;
	uint _forestWait;
	bool _forestTalk;		///< a plain verb on the parrot waits for its line
};

} // End of namespace Alien

#endif
