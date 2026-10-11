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

#include "alien/alien.h"
#include "alien/rooms/boss.h"
#include "alien/rooms/pool.h"
#include "alien/rooms/diving.h"
#include "alien/rooms/steam.h"
#include "alien/rooms/park.h"
#include "alien/rooms/yodle.h"
#include "alien/rooms/forest.h"
#include "alien/rooms/teleport.h"
#include "alien/rooms/corridor.h"
#include "alien/rooms/scanner.h"
#include "alien/rooms/store.h"
#include "alien/rooms/hippie.h"
#include "alien/rooms/library.h"
#include "alien/rooms/waiting.h"
#include "alien/rooms/jail.h"

namespace Alien {

/**
 * The rooms, in the order every hook runs them.
 *
 * Each hook answers only for its own rooms, so between files that serve
 * different rooms the order is immaterial. Where two share one, the earlier
 * here is the one whose code the original runs first.
 */
void AlienEngine::registerRooms() {
	_rooms.push_back(new PoolRoom(this));
	_rooms.push_back(new DivingRoom(this));
	_rooms.push_back(new SteamRoom(this));
	_rooms.push_back(new ParkRoom(this));
	_rooms.push_back(new YodleRoom(this));
	_rooms.push_back(new ForestRoom(this));
	_rooms.push_back(new TeleportRoom(this));
	_rooms.push_back(new CorridorRoom(this));
	_rooms.push_back(new ScannerRoom(this));
	_rooms.push_back(new StoreRoom(this));
	_rooms.push_back(new HippieRoom(this));
	_rooms.push_back(new LibraryRoom(this));
	_rooms.push_back(new WaitingRoom(this));
	_rooms.push_back(new BossRoom(this));
	_rooms.push_back(new JailRoom(this));
}

} // End of namespace Alien
