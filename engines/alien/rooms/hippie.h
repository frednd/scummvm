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

#ifndef ALIEN_ROOMS_HIPPIE_H
#define ALIEN_ROOMS_HIPPIE_H

#include "alien/tal.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Room 23's hippie, chained to the tree: the six dialog files his conversation
/// swaps between, and the walkman traded for his game.
class HippieRoom : public Room {
public:
	explicit HippieRoom(AlienEngine *vm);

	void reset() override;
	void start() override;
	void tickPair() override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool replyOwed() const override;
	bool busy() const override { return _hippieAnswer || _hippieHandOff >= 0; }

private:
	bool armHippie(int obj, byte verb, int item);
	void hippiePick();
	void hippieAnswer();
	void hippiePose(byte pose);
	void loadHippieScript(byte which);

	/// Room 23's [0xa49f] machine and the answer standing beside it: the step,
	/// its counter, the dialog id owed to the pick that was just made, whether
	/// it is still waiting for that pick's own line to come down, and whether
	/// the hippie has a line standing.
	byte _hippieStep;
	uint16 _hippiePos;
	byte _hippieReply;
	TalFile::Entry _hippieReplyEntry;
	int _hippieReplyTicks;
	bool _hippieAnswer;
	int _hippieHandOff;		///< the file topic 6 swaps in once its reply is down, or -1
	bool _hippieTalking;
};

} // End of namespace Alien

#endif
