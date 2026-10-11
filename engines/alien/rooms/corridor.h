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

#ifndef ALIEN_ROOMS_CORRIDOR_H
#define ALIEN_ROOMS_CORRIDOR_H

#include "alien/tal.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Rooms 51/53/55/57, the ship's four elevator floors: the elevator panel
/// (elevator.cpp), room 53's maintenance man and his badge, room 57's
/// loudspeaker, its passing aliens and the network terminal behind its card
/// slot (terminal.cpp), and room 57's alarm.
class CorridorRoom : public Room {
public:
	explicit CorridorRoom(AlienEngine *vm);

	void approach(int room) override;
	void start() override;
	void tickPair() override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool walkUnrouted(int x, int y, int arrivalFacing) override;
	bool replyOwed() const override;

private:
	bool hallwayScene(int room);
	void stepLobbyCameras();
	void corridorArrival();
	void playElevatorPanel();
	bool armHallway(int obj, byte verb, int item);
	void armHallwayCard(int obj, int item);
	void stepShipDoors();
	void stepHallMan();
	void stepHallwayDoors();
	void stepHallAliens();
	void hallManPose(byte pose);
	void playNetTerminal();

	/// Room 53's maintenance man: the room's [0xa49f] machine, his pose byte
	/// [0xa52c], and the answer owed to the conversation's last pick, which
	/// waits for the pick's own line to come down.
	struct HallMan {
		byte step;
		byte pose;
		byte reply;
		TalFile::Entry replyEntry;
		int replyTicks;
		uint replyTopic;
		bool answer;
		bool ended;				///< [0xa60f]: the pick ended the conversation
		bool speaking;			///< a line was up last tick, for [0xad1a]

		HallMan() : step(0), pose(0), reply(0), replyTicks(0), replyTopic(0),
					answer(false), ended(false), speaking(false) {}
	};
	HallMan _hallMan;

	/// Room 57's [0xa49f]: the card has gone into the terminal's slot and its
	/// line is up; the terminal opens once it is down.
	byte _terminalStep;

	/// Room 57's passing aliens, locals of its tick (ovr_35_0f9e:0x08bc): the
	/// tick pairs since the last one, the count the next waits for, and which
	/// of the six went by last.
	uint16 _hallAlienCount, _hallAlienWait;
	byte _hallAlienLast;
};

} // End of namespace Alien

#endif
