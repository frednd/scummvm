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

#ifndef ALIEN_ROOMS_YODLE_H
#define ALIEN_ROOMS_YODLE_H

#include "alien/tal.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Room 21's Yodle: the picklock, the menu, and the teleporter he builds out of
/// what is brought to him, and the puddle by his hut.
class YodleRoom : public Room {
public:
	explicit YodleRoom(AlienEngine *vm);

	void start() override;
	void holdCamera() override;
	void tickPair() override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	void drawOverSlots(Graphics::Surface &dest) const override;
	bool conversationRunning() const override { return _yodleSpeaking; }
	bool replyOwed() const override;

private:
	bool armYodle(int obj, byte verb, int item);
	void yodleArm(byte which);
	void yodleCheckIn(byte &first, byte &count);
	void loadYodleScript(byte which);
	void yodleTalk(byte speaker, byte line, byte count);
	void yodleSpeak();
	void yodlePick();
	bool yodleRunDone() const;
	void yodlePose(byte pose);
	void yodleSounds(byte which);
	void loadYodleRoomScript();
	void yodleRoomScriptFor(int obj, byte verb, byte item);
	void stepYodle();
	void stepYodleWater();
	void yodleStoryWait();

	/// The step of his machine, [0xa49c] beside it, the run
	/// standing in [0xa4a2] and the menu reply owed.
	byte _yodleStep;
	uint _yodlePos;
	byte _yodleLine;
	byte _yodleLeft;
	byte _yodleSpeaker;
	bool _yodleSpeaking;
	byte _yodleReply;
	TalFile::Entry _yodleReplyEntry;
	int _yodleReplyTicks;
	bool _yodleAnswer;
	bool _yodleTalking;			///< his talk loop is up until the line comes down
	byte _yodleWater;			///< [0xa5eb], the puddle's frame, 1..30
	bool _yodleWaterDue;		///< [0xa5f9], the tick pair it waits out between frames
};

} // End of namespace Alien

#endif
