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

#ifndef ALIEN_ROOMS_BOSS_H
#define ALIEN_ROOMS_BOSS_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 60, Jack's room: no overlay at all, and a conversation rather than a
/// fight -- the boss hands over his escape pod card.
class BossRoom : public Room {
public:
	explicit BossRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	bool walkUnrouted(int x, int y, int arrivalFacing) override;
	bool conversationRunning() const override { return _bossSpeaking; }

private:
	void bossSpeak();

	/// Room 60's [0xa49f] machine and [0xa49c] as it reads it
	byte _bossStep;
	uint16 _bossPos;

	/// And its conversation: whose turn it is, the outcome standing next, how
	/// many are left, and whether the boss's talking loop is up.
	byte _bossSpeaker;
	byte _bossLine;
	byte _bossLeft;
	bool _bossSpeaking;
	bool _bossTalking;
};

} // End of namespace Alien

#endif
