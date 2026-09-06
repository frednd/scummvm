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
#include "alien/detection.h"
#include "alien/plates.h"

namespace Alien {

/// The value an argument stands for: itself, or whatever the select holds.
static uint16 plateArg(uint16 arg, int sel) {
	return arg == kPlateSelect ? (uint16)sel : arg;
}

/**
 * The room's plate patch-up, as the resident 10c9 unit runs it.
 *
 * The room's enter routine ends by calling its own routine in that unit
 * (plates.h): every frame the puzzle state says belongs in the background page
 * is stamped there before the room is first drawn, and the few slots a room
 * opens with running are started. It is the last thing the enter routine does,
 * so it runs after the opening frames the room's init effects set.
 *
 * The original ends each of these routines with a page copy (10c9:sub_1136d)
 * that the port has no use for: the plate is drawn from every frame anyway.
 */
void AlienEngine::openRoomPlate(int room) {
	uint count = 0;
	const PlateStep *steps = plateSteps(room, count);
	if (!steps)
		return;

	// The original's [0x9926]/[0x98fe], which a run of guards writes and the
	// call after it reads back -- the kitchen's four fridge slots, the library
	// shelf's open frame.
	int sel = 0;

	for (uint i = 0; i < count; i++) {
		const PlateStep &step = steps[i];

		uint guardCount = 0;
		const ScriptCond *guards = plateGuards(step, guardCount);
		bool run = true;
		for (uint g = 0; g < guardCount && run; g++)
			run = _script.flag(guards[g].addr) == guards[g].value;
		if (!run)
			continue;

		const uint16 *args = step.args;
		switch (step.op) {
		case kPlateSel:
			sel = args[0];
			break;

		case kPlateStamp: {
			const uint slot = plateArg(args[0], sel);
			const int frame = plateArg(args[1], sel);
			_anims.stamp(slot, frame, _background);
			debugC(2, kDebugPlate, "plate room %d: slot %u holds frame %d", room,
				   slot, frame);
			break;
		}

		case kPlatePlay: {
			const uint slot = plateArg(args[0], sel);
			_anims.play(slot, plateArg(args[1], sel), args[2], args[3], args[4]);
			debugC(2, kDebugPlate, "plate room %d: slot %u runs %d frames", room,
				   slot, args[2]);
			break;
		}

		case kPlateFlag:
			// The slot loop flags at [0xa540], which Script::setFlag routes
			// into AnimSlots for the room's own tick to relaunch.
			_script.setFlag(args[0], (byte)args[1]);
			break;

		case kPlateMusic:
			playMusicSlot(args[0]);
			break;

		default:
			break;
		}
	}
}

/**
 * Every plate step the port knows, room by room.
 *
 * tools/check_plates.py mirrors this from the disassembly of the 10c9 unit,
 * which is where tools/gen_plates.py lifted it from in the first place.
 */
void AlienEngine::dumpPlates() {
	debugC(1, kDebugPlate, "plate: %u steps", plateStepCount());

	for (int room = 0; room <= StaticTables::kRoomCount; room++) {
		uint count = 0;
		const PlateStep *steps = plateSteps(room, count);
		for (uint i = 0; i < count; i++) {
			const PlateStep &step = steps[i];
			const uint16 *args = step.args;

			Common::String body;
			switch (step.op) {
			case kPlateStamp:
				body = Common::String::format("stamp slot %s frame %s",
											  argText(args[0]).c_str(),
											  argText(args[1]).c_str());
				break;
			case kPlatePlay:
				body = Common::String::format(
					"play slot %s first %s count %s rate %s mode %d",
					argText(args[0]).c_str(), argText(args[1]).c_str(),
					argText(args[2]).c_str(), argText(args[3]).c_str(), args[4]);
				break;
			case kPlateSel:
				body = Common::String::format("sel %d", args[0]);
				break;
			case kPlateFlag:
				body = Common::String::format("flag [0x%04x] = %d", args[0], args[1]);
				break;
			default:
				body = Common::String::format("music %d", args[0]);
				break;
			}

			uint guardCount = 0;
			const ScriptCond *guards = plateGuards(step, guardCount);
			for (uint g = 0; g < guardCount; g++)
				body += Common::String::format(" when [0x%04x] == %d", guards[g].addr,
											   guards[g].value);

			debugC(1, kDebugPlate, "plate room %2d %s", room, body.c_str());
		}
	}
}

/// A step's argument as the dump prints it: a number, or the select.
Common::String AlienEngine::argText(uint16 arg) {
	return arg == kPlateSelect ? Common::String("sel")
							   : Common::String::format("%d", arg);
}

} // End of namespace Alien
