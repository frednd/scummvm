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

#ifndef ALIEN_ROOMS_DIVING_H
#define ALIEN_ROOMS_DIVING_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 46, underwater: its two views, the swim, the chest and its key, the
/// propeller's bubbles and the water over all of it.
class DivingRoom : public Room {
public:
	explicit DivingRoom(AlienEngine *vm);

	void loadBanks(int room) override;
	void start() override;
	void tick() override;
	void tickPair() override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool walk(int x, int y, int arrivalFacing) override;
	void drawOverPlayfield(Graphics::Surface &dest) const override;
	bool sharedExit(int room) const override;
	bool busy() const override { return _divingExit || _divingStep; }

private:
	bool armDiving(int obj, byte item);
	void stepDiving();
	void stepDivingWater();
	bool divingTakeExit(byte submode);

	/// The [0xa49f] machine step, the submode a click armed
	/// ([0xa87d]), the two bubble pools at ds:0xb06a and ds:0xb51e, and the
	/// water's row table ([0xae28]) with its phase ([0xb066]).
	struct DivingBubble {
		uint16 x;		///< 8.8
		uint16 y;		///< 9.7, dead past 0xf0 rows
		uint16 vy;
		int16 drift;
	};
	enum { kDivingBubbles = 100, kDivingWaveRows = 0x11e };
	byte _divingStep;
	byte _divingExit;
	DivingBubble _divingBubbles[2][kDivingBubbles];
	int16 _divingWave[kDivingWaveRows];
	uint16 _divingWavePhase;
};

} // End of namespace Alien

#endif
