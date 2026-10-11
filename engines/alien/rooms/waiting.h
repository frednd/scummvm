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

#ifndef ALIEN_ROOMS_WAITING_H
#define ALIEN_ROOMS_WAITING_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 54: the ticket machine, which is not simplified -- the [0xa49f]
/// machine behind object 12 and the three numbers it gives out -- and the call
/// in to Jack's room: the way out through object 1, taken over once his number
/// is up, and the door's own refusal.
class WaitingRoom : public Room {
public:
	explicit WaitingRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool hijackExit(byte submode) override;

private:
	void playWaitingPose(uint slot, byte pose);
	void waitingCustomerSays(byte code);
	bool armWaitingCustomers(int obj, byte verb, byte item);
	void waitingArrival();

	bool armWaitingButton(int obj, byte verb);
	void speakWaiting(byte code);
	bool waitingLineDone() const;

	/// Room 54's [0xa49f] machine: the ticket machine, and the number that is
	/// finally called.
	byte _waitingStep;

	/// [0xa49c] as room 54 reads it, cleared where each of its steps starts.
	uint16 _waitingPos;
};

} // End of namespace Alien

#endif
