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

#ifndef ALIEN_ROOMS_SCANNER_H
#define ALIEN_ROOMS_SCANNER_H

#include "alien/rooms/room.h"

namespace Alien {

/// Room 52's security scanner: the resident gate in front of the room, its DL2
/// scan, and the robot's arrest.
class ScannerRoom : public Room {
public:
	explicit ScannerRoom(AlienEngine *vm);

	int redirect(int room) override;
	void start() override;
	void tickPair() override;

private:
	void playScannerScan(bool mask);

	/// The gate sent him in without the mask, and the room's [0xa49f]
	/// machine step that arrests him.
	bool _scannerArrest;
	byte _scannerStep;
};

} // End of namespace Alien

#endif
