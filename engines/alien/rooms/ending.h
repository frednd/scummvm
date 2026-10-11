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

#ifndef ALIEN_ROOMS_ENDING_H
#define ALIEN_ROOMS_ENDING_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 59, the escape pod: the sequence it runs as it opens once the pod is
/// ready to leave, and the win.
class EndingRoom : public Room {
public:
	explicit EndingRoom(AlienEngine *vm);

	void start() override { startEnding(); }
	void machine() override { stepEnding(); }
	bool conversationRunning() const override { return _endingStep != 0; }
	bool holdsCharacter() const override { return _endingStep != 0; }

	/// The console's "ending": the sequence, from wherever the game is.
	void armEnding();

private:
	void startEnding();
	void stepEnding();
	void speakEnding(byte code);
	bool endingLineDone() const;
	void winGame();

	/// The sequence's step [0xa49f], the timeline [0xa49c] one step waits on,
	/// and the slot-4 cycle [0xa53e] leaves running.
	byte _endingStep;
	uint _endingPos;
	bool _endingLoop;
};

} // End of namespace Alien

#endif
