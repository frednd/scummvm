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

#ifndef ALIEN_ROOMS_SLUGGS_H
#define ALIEN_ROOMS_SLUGGS_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 34, the road after the blast: Sluggs, the one hand that gives out the
/// observatory keys.
class SluggsRoom : public Room {
public:
	explicit SluggsRoom(AlienEngine *vm);

	void reset(int room) override;
	void enter(int room) override;
	void tickPair() override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool conversationRunning() const override { return _sluggsSpeaking; }

private:
	bool armSluggs(int obj, byte verb, byte item);
	void sluggsTalk(byte speaker, byte line, byte count);
	void sluggsSpeak();
	void sluggsPose(byte pose);

	/// The room's [0xa49f] machine and the conversation running beside it: the
	/// step, its counter, the outcome the two speakers are taking turns over
	/// ([0xa4a2]), whose turn it is ([0xa4a3]), how many lines are still owed
	/// ([0xad40]) and whether Sluggs has a line standing.
	byte _sluggsStep;
	uint16 _sluggsPos;
	byte _sluggsLine;
	byte _sluggsSpeaker;
	byte _sluggsLeft;
	bool _sluggsSpeaking;
	bool _sluggsTalking;
};

} // End of namespace Alien

#endif
