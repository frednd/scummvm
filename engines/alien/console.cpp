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

#include "common/tokenizer.h"

#include "alien/console.h"
#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/rooms/ending.h"

namespace Alien {

/// A number as typed: decimal, or hex with a 0x prefix (the flag addresses).
static int parseNumber(const char *text) {
	return (int)strtol(text, nullptr, 0);
}

typedef bool (AlienConsole::*ConsoleCommand)(int argc, const char **argv);

static const struct {
	const char *name;
	ConsoleCommand method;
} kCommands[] = {
	{ "room", &AlienConsole::cmdRoom },
	{ "flag", &AlienConsole::cmdFlag },
	{ "items", &AlienConsole::cmdItems },
	{ "give", &AlienConsole::cmdGive },
	{ "take", &AlienConsole::cmdTake },
	{ "scene", &AlienConsole::cmdScene },
	{ "music", &AlienConsole::cmdMusic },
	{ "hotspots", &AlienConsole::cmdHotspots },
	{ "exits", &AlienConsole::cmdExits },
	{ "leave", &AlienConsole::cmdLeave },
	{ "spots", &AlienConsole::cmdSpots },
	{ "walk", &AlienConsole::cmdWalk },
	{ "anims", &AlienConsole::cmdAnims },
	{ "geometry", &AlienConsole::cmdGeometry },
	{ "bank", &AlienConsole::cmdBank },
	{ "frame", &AlienConsole::cmdFrame },
	{ "dialog", &AlienConsole::cmdDialog },
	{ "band", &AlienConsole::cmdBand },
	{ "dosimport", &AlienConsole::cmdDosImport },
	{ "timescale", &AlienConsole::cmdTimescale },
	{ "markers", &AlienConsole::cmdMarkers },
	{ "ending", &AlienConsole::cmdEnding }
};

AlienConsole::AlienConsole(AlienEngine *vm) : GUI::Debugger(), _vm(vm), _scripted(false) {
	for (uint i = 0; i < ARRAYSIZE(kCommands); i++)
		registerCmd(kCommands[i].name,
					new Common::Functor2Mem<int, const char **, bool, AlienConsole>(this, kCommands[i].method));
}

/**
 * Console output. A line run from a script has no window to print into, so
 * there it goes to the play channel's log instead.
 */
void AlienConsole::say(const char *format, ...) {
	va_list args;
	va_start(args, format);
	Common::String text = Common::String::vformat(format, args);
	va_end(args);
	if (_scripted) {
		if (text.hasSuffix("\n"))
			text.deleteLastChar();
		debugC(1, kDebugPlay, "console: %s", text.c_str());
	} else {
		debugPrintf("%s", text.c_str());
	}
}

/**
 * One command line run without the console window, for a scripted run's
 * `console` lines: the same handlers, split on spaces, no quoting.
 */
bool AlienConsole::runLine(const Common::String &line) {
	Common::Array<Common::String> words;
	Common::StringTokenizer tok(line, " ");
	while (!tok.empty()) {
		const Common::String word = tok.nextToken();
		if (!word.empty())
			words.push_back(word);
	}
	if (words.empty())
		return false;
	_scripted = true;

	const char *argv[16];
	const int argc = (int)MIN<uint>(words.size(), ARRAYSIZE(argv));
	for (int i = 0; i < argc; i++)
		argv[i] = words[i].c_str();

	for (uint i = 0; i < ARRAYSIZE(kCommands); i++) {
		if (words[0].equalsIgnoreCase(kCommands[i].name)) {
			(this->*kCommands[i].method)(argc, argv);
			_scripted = false;
			return true;
		}
	}
	_scripted = false;
	return false;
}

bool AlienConsole::cmdRoom(int argc, const char **argv) {
	if (argc < 2) {
		say("Room %d, plate %c\n", _vm->_room, _vm->_secondPlate ? 'B' : 'A');
		say("Usage: %s <1..%d> [b]\n", argv[0], StaticTables::kRoomCount);
		return true;
	}
	const int room = parseNumber(argv[1]);
	if (room < 1 || room > StaticTables::kRoomCount) {
		say("No room %d\n", room);
		return true;
	}
	const bool second = argc > 2 && scumm_stricmp(argv[2], "b") == 0;
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kRoom, room, second));
	return false;
}

bool AlienConsole::cmdFlag(int argc, const char **argv) {
	if (argc < 2) {
		say("Usage: %s <address> [value]   (state block 0xa600..0xa7ff)\n", argv[0]);
		return true;
	}
	const int addr = parseNumber(argv[1]);
	if (addr < 0 || addr > 0xffff) {
		say("Bad address %s\n", argv[1]);
		return true;
	}
	if (argc > 2) {
		const int value = parseNumber(argv[2]);
		_vm->_script.setFlag((uint16)addr, (byte)value);
		_vm->_dirty = true;
	}
	const byte value = _vm->_script.flag((uint16)addr);
	say("[0x%04x] = %u (0x%02x)\n", addr, value, value);
	return true;
}

bool AlienConsole::cmdItems(int argc, const char **argv) {
	say("Held: %u %s\n", _vm->_heldItem, _vm->_inventory.name(_vm->_heldItem).c_str());
	for (int item = 1; item <= StaticTables::kItemInUse; item++) {
		const bool carried = _vm->_inventory.has((byte)item);
		if (carried || argc > 1)
			say("%c %2d %s\n", carried ? '*' : ' ', item,
						_vm->_inventory.name((byte)item).c_str());
	}
	if (argc < 2)
		say("(carried items only; '%s all' lists every item)\n", argv[0]);
	return true;
}

bool AlienConsole::cmdGive(int argc, const char **argv) {
	const int item = argc > 1 ? parseNumber(argv[1]) : 0;
	if (item < 1 || item > StaticTables::kItemInUse) {
		say("Usage: %s <1..%d>\n", argv[0], StaticTables::kItemInUse);
		return true;
	}
	if (!_vm->_inventory.has((byte)item))
		_vm->_inventory.add((byte)item);
	_vm->_dirty = true;
	say("Gave %d %s\n", item, _vm->_inventory.name((byte)item).c_str());
	return true;
}

bool AlienConsole::cmdTake(int argc, const char **argv) {
	const int item = argc > 1 ? parseNumber(argv[1]) : 0;
	if (item < 1 || item > StaticTables::kItemInUse) {
		say("Usage: %s <1..%d>\n", argv[0], StaticTables::kItemInUse);
		return true;
	}
	if (_vm->_inventory.has((byte)item))
		_vm->_inventory.remove((byte)item);
	_vm->_dirty = true;
	say("Took %d %s\n", item, _vm->_inventory.name((byte)item).c_str());
	return true;
}

bool AlienConsole::cmdScene(int argc, const char **argv) {
	if (argc < 2) {
		say("Usage: %s <record>\n", argv[0]);
		return true;
	}
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kScene, parseNumber(argv[1])));
	return false;
}

bool AlienConsole::cmdMusic(int argc, const char **argv) {
	if (argc < 2) {
		say("Slot %d playing\n", _vm->_musicSlot);
		say("Usage: %s <0..%d> | stop\n", argv[0], StaticTables::kMusicSlotCount - 1);
		return true;
	}
	if (scumm_stricmp(argv[1], "stop") == 0) {
		_vm->stopMusic();
		return true;
	}
	const int slot = parseNumber(argv[1]);
	if (slot < 0 || slot >= StaticTables::kMusicSlotCount) {
		say("No music slot %d\n", slot);
		return true;
	}
	_vm->playMusicSlot((uint)slot);
	return true;
}

bool AlienConsole::cmdHotspots(int argc, const char **argv) {
	say("Room %d, %u registered\n", _vm->_room, _vm->_spots.size());
	for (uint i = 0; i < _vm->_spots.size(); i++) {
		const Hotspot &spot = _vm->_spots[i];
		Common::String outcomes;
		for (uint o = 0; o < spot.outcomeCount; o++)
			outcomes += Common::String::format("%s%u", o ? "," : "", spot.outcomes[o]);
		say("%3d,%3d..%3d,%3d obj %3u verb %3u label %3u -> %s\n", spot.x1, spot.y1,
					spot.x2, spot.y2, spot.obj, spot.verb, spot.label, outcomes.c_str());
	}
	return true;
}

bool AlienConsole::cmdExits(int argc, const char **argv) {
	say("Room %d\n", _vm->_room);
	for (uint i = 0; i < _vm->_spots.size(); i++) {
		const Hotspot &spot = _vm->_spots[i];
		WalkTarget target;
		if (!_vm->_script.walkTarget((spot.x1 + spot.x2) / 2, (spot.y1 + spot.y2) / 2, spot.obj,
									 target) ||
			!target.submode)
			continue;
		say("obj %3u submode %3u at %3d,%3d facing %2u -> room %d\n", spot.obj,
					target.submode, target.x, target.y, target.facing,
					_vm->_script.nextRoom((byte)_vm->_room, target.submode));
	}
	return true;
}

bool AlienConsole::cmdLeave(int argc, const char **argv) {
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kExit));
	return false;
}

bool AlienConsole::cmdSpots(int argc, const char **argv) {
	_vm->_showSpots = !_vm->_showSpots;
	_vm->_dirty = true;
	say("Hotspot outlines %s\n", _vm->_showSpots ? "on" : "off");
	return true;
}

bool AlienConsole::cmdWalk(int argc, const char **argv) {
	_vm->_showWalk = !_vm->_showWalk;
	_vm->_dirty = true;
	say("Walk overlay %s\n", _vm->_showWalk ? "on" : "off");
	return true;
}

bool AlienConsole::cmdAnims(int argc, const char **argv) {
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kAnims));
	return false;
}

bool AlienConsole::cmdGeometry(int argc, const char **argv) {
	say("Results go to the 'walk' debug channel\n");
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kGeometry));
	return false;
}

bool AlienConsole::cmdBank(int argc, const char **argv) {
	const int delta = argc > 1 ? parseNumber(argv[1]) : 1;
	_vm->stepSpriteBank(delta < 0 ? -1 : 1);
	return true;
}

bool AlienConsole::cmdFrame(int argc, const char **argv) {
	const uint count = _vm->_sprite.frameCount();
	if (argc < 2 || !count) {
		say("Frame %u of %u\n", _vm->_spriteFrame, count);
		return true;
	}
	_vm->_spriteFrame = (uint)parseNumber(argv[1]) % count;
	_vm->_dirty = true;
	return true;
}

bool AlienConsole::cmdDialog(int argc, const char **argv) {
	if (argc < 2) {
		say("Usage: %s <0..%d>\n", argv[0], TalFile::kEntryCount - 1);
		return true;
	}
	_vm->showDialog((uint)parseNumber(argv[1]) % TalFile::kEntryCount);
	return true;
}

bool AlienConsole::cmdBand(int argc, const char **argv) {
	_vm->_dialogBand = !_vm->_dialogBand;
	_vm->_dirty = true;
	say("Dialog layout: %s\n", _vm->_dialogBand ? "bottom band" : "over the speaker");
	return true;
}

bool AlienConsole::cmdDosImport(int argc, const char **argv) {
	ConsoleAction action(ConsoleAction::kDosImport);
	action.file = argc > 1 ? argv[1] : "SAVEGAME.0";
	_vm->_consoleActions.push_back(action);
	return false;
}

bool AlienConsole::cmdTimescale(int argc, const char **argv) {
	if (argc < 2) {
		say("Timescale %u (0 turbo, 1 real time, N times faster)\n", _vm->_timescale);
		return true;
	}
	_vm->setTimescale((uint)MAX(parseNumber(argv[1]), 0));
	return true;
}

bool AlienConsole::cmdMarkers(int argc, const char **argv) {
	Common::Array<Graphics::HotspotInfo> markers;
	_vm->getHotspotPositions(markers);
	say("Room %d, %u markers\n", _vm->_room, markers.size());
	for (uint i = 0; i < markers.size(); i++)
		say("%3d,%3d %s %s\n", markers[i].position.x, markers[i].position.y,
			markers[i].type == Graphics::kHotspotExit ? "exit  " : "object",
			markers[i].name.encode(Common::kUtf8).c_str());
	return true;
}

bool AlienConsole::cmdEnding(int argc, const char **argv) {
	_vm->_consoleActions.push_back(ConsoleAction(ConsoleAction::kEnding));
	return false;
}

/**
 * What the console queued, run from the main loop once it has closed and the
 * engine's clock is moving again.
 */
void AlienEngine::runConsoleActions() {
	while (!_consoleActions.empty()) {
		const ConsoleAction action = _consoleActions.remove_at(0);
		switch (action.kind) {
		case ConsoleAction::kRoom:
			if (!loadRoom(action.arg, action.flag))
				warning("console: room %d has no plate to open", action.arg);
			break;
		case ConsoleAction::kScene:
			playCutsceneRecord((uint)action.arg);
			break;
		case ConsoleAction::kExit:
			if (!takeFirstExit())
				warning("console: room %d has no exit to take", _room);
			break;
		case ConsoleAction::kAnims:
			_anims.playAll(kDebugAnimRate);
			_dirty = true;
			break;
		case ConsoleAction::kGeometry:
			sweepWalkGeometry();
			break;
		case ConsoleAction::kDosImport:
			importDosSave(action.file, true);
			break;
		case ConsoleAction::kEnding:
			_endingRoom->armEnding();
			break;
		default:
			break;
		}
	}
}

} // End of namespace Alien
