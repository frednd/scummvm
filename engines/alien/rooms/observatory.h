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

#ifndef ALIEN_ROOMS_OBSERVATORY_H
#define ALIEN_ROOMS_OBSERVATORY_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 19, the observatory: the fuse-panel cover -- the rectangle the lift
/// could not carry because its width is computed, the look that opens it and the
/// move that toggles it -- the circuit breaker, and the stairs up.
class ObservatoryRoom : public Room {
public:
	explicit ObservatoryRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	void lineDown() override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool hijackExit(byte submode) override;

private:
	bool armObservatoryPanel(int obj, byte verb);
	void armObservatoryLook(int obj, byte verb);
	void armObservatoryBreaker(int obj, byte verb);
	void stepObservatoryBreaker();
	void stepObservatoryStairs();
	void stepObservatoryPanel();
	void openObservatoryPanel();
	void closeObservatoryPanel();

	/// The cover has been looked at and its line is still up. The
	/// cover comes off when the line comes down.
	bool _observatoryLook;
	bool _observatoryBreaker;	///< the room's [0xa49f] 0x14, the walker away
	byte _observatoryStep;		///< the room's [0xa49f]: 1-4 the slip, 0x64 the stairs
	uint16 _observatoryPos;		///< and its [0xa49c]
	bool _observatoryEncore;	///< the slip under way is the port's own
};

} // End of namespace Alien

#endif
