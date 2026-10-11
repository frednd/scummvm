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

#ifndef ALIEN_ROOMS_CEMETERY_H
#define ALIEN_ROOMS_CEMETERY_H

#include "alien/rooms/room.h"

namespace Alien {

class CemeteryRoom : public Room {
public:
	explicit CemeteryRoom(AlienEngine *vm);

	void start() override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;

private:
	/// Room 32's statue, the phrase it wants and the cave it guards.
	void armCemeteryStatue(int obj, byte verb);
	void cemeteryArrival();
	void setCemeteryState(byte state);
	void cemeteryZap();
	void cemeteryStatuePick();

	/// Waiting for the look's own line to come down before
	/// the topic-0 conversation reopens.
	uint16 _cemeteryPos;		///< the room's [0xa49c]
	int _cemeteryZapPos;		///< [bp-4] after a zap, -1 when none is pending
	bool _cemeteryCursorOwed;	///< the phrase took the cursor; its line gives it back
	byte _cemeteryStep;	///< [0xa49f] while the cave is being walked into
};

} // End of namespace Alien

#endif
