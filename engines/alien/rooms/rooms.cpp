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
#include "alien/rooms/basement.h"
#include "alien/rooms/bedroom.h"
#include "alien/rooms/boss.h"
#include "alien/rooms/cave.h"
#include "alien/rooms/cemetery.h"
#include "alien/rooms/chimney.h"
#include "alien/rooms/cliff.h"
#include "alien/rooms/corridor.h"
#include "alien/rooms/diving.h"
#include "alien/rooms/ending.h"
#include "alien/rooms/forest.h"
#include "alien/rooms/hallway.h"
#include "alien/rooms/hippie.h"
#include "alien/rooms/jail.h"
#include "alien/rooms/lab.h"
#include "alien/rooms/library.h"
#include "alien/rooms/lift.h"
#include "alien/rooms/living.h"
#include "alien/rooms/mailbox.h"
#include "alien/rooms/mansion.h"
#include "alien/rooms/maze.h"
#include "alien/rooms/observatory.h"
#include "alien/rooms/opening.h"
#include "alien/rooms/park.h"
#include "alien/rooms/pool.h"
#include "alien/rooms/scanner.h"
#include "alien/rooms/sewer.h"
#include "alien/rooms/shore.h"
#include "alien/rooms/sluggs.h"
#include "alien/rooms/steam.h"
#include "alien/rooms/store.h"
#include "alien/rooms/teleport.h"
#include "alien/rooms/telescope.h"
#include "alien/rooms/ufo.h"
#include "alien/rooms/waiting.h"
#include "alien/rooms/yodle.h"

namespace Alien {

/**
 * The rooms, in the order every hook runs them.
 *
 * Each hook answers only for its own rooms, so for most of them the order is
 * immaterial. Where it is not, it is the order the port has always run the
 * rooms' machines in on a tick pair: a machine that leaves its room mid-tick
 * lets the machines after it see the room it led to on that same tick, and
 * moving one changes when the next room's own machine starts. The first five,
 * which run no machine on the tick pair, are free; the escape pod's machine()
 * goes ahead of the opening's, as it always has.
 */
void AlienEngine::registerRooms() {
	_rooms.push_back(new UfoRoom(this));
	_rooms.push_back(new ChimneyRoom(this));
	_rooms.push_back(new CaveRoom(this));
	_endingRoom = new EndingRoom(this);
	_rooms.push_back(_endingRoom);
	_rooms.push_back(new OpeningRoom(this));
	_rooms.push_back(new HallwayRoom(this));
	_rooms.push_back(new LabRoom(this));
	_rooms.push_back(new BedroomRoom(this));
	_rooms.push_back(new MansionRoom(this));
	_rooms.push_back(new LiftRoom(this));
	_rooms.push_back(new ObservatoryRoom(this));
	_rooms.push_back(new SewerRoom(this));
	_rooms.push_back(new BasementRoom(this));
	_rooms.push_back(new CliffRoom(this));
	_rooms.push_back(new LivingRoom(this));
	_rooms.push_back(new MailboxRoom(this));
	_rooms.push_back(new SluggsRoom(this));
	_rooms.push_back(new CemeteryRoom(this));
	_rooms.push_back(new TelescopeRoom(this));
	_rooms.push_back(new MazeRoom(this));
	_rooms.push_back(new ShoreRoom(this));
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
