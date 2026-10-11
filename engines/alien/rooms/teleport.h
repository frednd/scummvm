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

#ifndef ALIEN_ROOMS_TELEPORT_H
#define ALIEN_ROOMS_TELEPORT_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 22's phone, which dials the teleporter, and room 56's chamber at the
/// other end: the arrival, the pad and the trip back.
class TeleportRoom : public Room {
public:
	explicit TeleportRoom(AlienEngine *vm);

	void chooseView(int room) override;
	void start() override;
	void raiseScenes(int room) override;
	void tick() override;
	void tickPair() override;
	void walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) override;
	bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) override;

private:
	bool armTeleportPhone(int obj, byte verb, byte item);
	void stepTeleport();
	void shipWalkTarget(byte obj, bool action, WalkTarget &target);
	void shipChamber(bool open);
	void stepShip();
	void startShipFlash();
	void shipFromJail();
	/// Room 22's [0xa49f] machine for the phone, ticks left of its current
	/// wait, and room 56's simplified return, clicks taken so far.
	byte _teleportStep;
	uint _teleportWait;

	/// Room 56's [0xa49f] machine and its [0xa49c] wait.
	byte _shipStep;
	uint _shipWait;

	/// The hum's flashes (the room's tick, 0x07cc): [0x33ea] flashes left,
	/// [0x33e8] the level, [0x33ec] the tick-pair count, [0x33ee] a mix due,
	/// and the palette they mix from.
	byte _shipFlashes;
	int _shipFlashLevel;
	uint _shipFlashCount;
	bool _shipFlashDue;
	byte _shipFlashSource[256 * 3];
};

} // End of namespace Alien

#endif
