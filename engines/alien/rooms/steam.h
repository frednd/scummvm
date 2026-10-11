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

#ifndef ALIEN_ROOMS_STEAM_H
#define ALIEN_ROOMS_STEAM_H

#include "alien/rooms/room.h"

namespace Alien {

/// Rooms 49 and 50: the ladder between them, the steam over room 50's right
/// half, and room 49's light.
class SteamRoom : public Room {
public:
	explicit SteamRoom(AlienEngine *vm);

	void start() override;
	void tick() override;
	void tickPair() override;
	void leftClick(int roomX, int y) override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;

private:
	void armSteam(int obj, byte verb);

	/// Rooms 49 and 50's [0xa49f] step, their [0xa49c] count, and the steam
	/// flag as the last tick saw it.
	byte _steamStep;
	uint _steamPos;
	byte _steamWas;

	/// A ladder arrival still owes the character his new room's light.
	bool _steamRelight;

	/// Room 49's light: passes left of a flicker burst, and the palette entries
	/// the level darkens, as the room loaded them.
	uint _steamBurst;
	byte _steamLight[0x1a * 3];
};

} // End of namespace Alien

#endif
