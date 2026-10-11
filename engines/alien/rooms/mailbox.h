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

#ifndef ALIEN_ROOMS_MAILBOX_H
#define ALIEN_ROOMS_MAILBOX_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 25's mailbox full of dynamite, and room 33, the road it blows him
/// into.
class MailboxRoom : public Room {
public:
	explicit MailboxRoom(AlienEngine *vm);

	void reset(int room) override;
	void enter(int room) override;
	void tickPair() override;
	void lineDown() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;

private:
	void armMailbox(int obj, byte item);
	void stepMailbox();
	void stepTown();

	/// Room 25's [0xa49f] machine and the counter it times itself off, and
	/// room 33's own state for the landing at the other end.
	byte _mailboxStep;
	uint16 _mailboxPos;
	byte _townStep;
};

} // End of namespace Alien

#endif
