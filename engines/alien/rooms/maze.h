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

#ifndef ALIEN_ROOMS_MAZE_H
#define ALIEN_ROOMS_MAZE_H

#include "alien/mazetables.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Rooms 43 and 44, the two mazes: their per-cell backgrounds, arrows and
/// routing, and the crystal door that ends maze A; and room 45, the crystal's
/// scene behind it.
class MazeRoom : public Room {
public:
	explicit MazeRoom(AlienEngine *vm);

	void preload(int room) override;
	bool loadBackground(int room, Graphics::Surface &plate, byte *palette) override;
	void start() override;
	void buildHotspots(int room) override;
	void tickPair() override;
	bool walkUnrouted(int x, int y, int arrivalFacing) override;
	bool turnExit(byte &submode) override;
	bool conversationRunning() const override { return _crystalSpeaking; }

private:
	void stepMaze();
	void stepCrystal();
	void crystalSpeak();
	void crystalSay(byte outcome);
	void mazePlace();
	const MazeCell *mazeCell(int room) const;

	/// The two mazes: the step of the crystal-door scene, and the submode the
	/// last cell was left by, which says where the next one is entered from. The cell itself is a state-block flag, [0xa77c].
	byte _mazeStep;
	byte _crystalStep;
	uint _crystalWait;

	/// Room 45's two-speaker run, the same four the DLGREQ runner keeps
	/// everywhere it appears (sluggs.cpp): whose turn, which outcome, how many
	/// are left, and whether a run is standing at all.
	byte _crystalSpeaker;
	byte _crystalLine;
	byte _crystalLeft;
	bool _crystalSpeaking;

	/// The pose room 45's crystal stands in.
	byte _mazePose;
};

} // End of namespace Alien

#endif
