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

#ifndef ALIEN_ROOMS_STORE_H
#define ALIEN_ROOMS_STORE_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 30, the 24h antique store, which is one long [0xa49f] machine and no
/// rectangles at all: the conversation, the arrow traded for the diving suit,
/// and the walk out that is the room's only exit.
class StoreRoom : public Room {
public:
	explicit StoreRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;

private:
	void storeSpeak(byte code);
	void storeSalesmanLine(byte code);
	void storePose(byte pose);
	void loadStoreScript(const char *name);

	/// Room 30's own machine: the step, its counter, the line the two speakers
	/// are taking turns over and whose turn it is, and the item the player has
	/// offered over the counter. The line survives the room, the
	/// way [0xa4a2] does: the return visit with the suit picks up where the
	/// trade's own banter stopped.
	byte _storeStep;
	uint16 _storePos;
	byte _storeLine;
	byte _storeSpeaker;
	byte _storeOffer;
	bool _storeWalked;
	bool _storeTalking;
};

} // End of namespace Alien

#endif
