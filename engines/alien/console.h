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

#ifndef ALIEN_CONSOLE_H
#define ALIEN_CONSOLE_H

#include "common/array.h"
#include "common/str.h"

#include "gui/debugger.h"

namespace Alien {

class AlienEngine;

/**
 * Something the console asked for that runs the game's own code -- a room
 * load, a scene -- and so has to wait until the console has closed. The
 * console holds the engine paused, and with it the engine's clock, so a fade
 * or a scene started from inside it would wait on a clock that never moves.
 */
struct ConsoleAction {
	enum Kind {
		kRoom,
		kScene,
		kExit,
		kAnims,
		kGeometry,
		kDosImport,
		kEnding
	};

	Kind kind;
	int arg;
	bool flag;
	Common::String file;

	ConsoleAction(Kind k, int a = 0, bool f = false) : kind(k), arg(a), flag(f) {}
};

class AlienConsole : public GUI::Debugger {
public:
	explicit AlienConsole(AlienEngine *vm);
	~AlienConsole() override {}

	/// Runs one command line outside the console window (a playscript's
	/// `console` line). False when no command has that name.
	bool runLine(const Common::String &line);

	bool cmdRoom(int argc, const char **argv);
	bool cmdFlag(int argc, const char **argv);
	bool cmdItems(int argc, const char **argv);
	bool cmdGive(int argc, const char **argv);
	bool cmdTake(int argc, const char **argv);
	bool cmdScene(int argc, const char **argv);
	bool cmdMusic(int argc, const char **argv);
	bool cmdHotspots(int argc, const char **argv);
	bool cmdExits(int argc, const char **argv);
	bool cmdLeave(int argc, const char **argv);
	bool cmdSpots(int argc, const char **argv);
	bool cmdWalk(int argc, const char **argv);
	bool cmdAnims(int argc, const char **argv);
	bool cmdGeometry(int argc, const char **argv);
	bool cmdBank(int argc, const char **argv);
	bool cmdFrame(int argc, const char **argv);
	bool cmdDialog(int argc, const char **argv);
	bool cmdBand(int argc, const char **argv);
	bool cmdDosImport(int argc, const char **argv);
	bool cmdTimescale(int argc, const char **argv);
	bool cmdMarkers(int argc, const char **argv);
	bool cmdEnding(int argc, const char **argv);

private:
	AlienEngine *_vm;
	bool _scripted;				///< runLine() is running a command

	void say(const char *format, ...) GCC_PRINTF(2, 3);
};

} // End of namespace Alien

#endif
