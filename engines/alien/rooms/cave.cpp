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
#include "alien/rooms/cave.h"

namespace Alien {

// Room 40, the cave entrance, and the mummy in it (obj 10).
//
// The room has no TAL of its own. It speaks out of LOGIC's ten shared files,
// LOGIC:sub_124a7(n), which copies name n into [0xa9b2], loads it, and keeps n
// in [0x33db]. The room's init loads 1, MUMMY0.TAL (ovr_28_0ebb:0x0399).
//
// Entry 3's item-use half ([0xa956] == 0x4e22, 0x003e-0x0105) is four arms,
// and the lifted table carries three of them without their guards (blocks
// 151-153: obj 10 with any item queued MUMMY0's outcome 1, which the original
// never says). In the original's order:
//
//   [0x33d6] == 1, obj 10, any item but 0x1b and 1   MISC.TAL, outcome 1
//   [0x33d7] == 1, obj 10, the pipe (0x1b)           PIPEYES1.TAL, menu topic 0
//   [0x33d9] == 1, obj 10, the matches (1)           FIREYES1.TAL, menu topic 0
//   none of those, [0x33d6] == 0, any object         MISC.TAL, outcome 2:
//                                                    "I better not. He looks
//                                                    pretty suspicious."
//
// 0x33d6, 0x33d7 and 0x33d9 ship as 0 in GAME.EXE's latch block and nothing in
// the game writes them, so only the last arm is live. Nor is the
// mummy ever there to click: his rectangle (0x02ea) and his walk rows register
// only while [0xa78d] == 1, and the one write to that byte is MAIN's new-game
// zero (0000:0ad4). So the live arm is an item on the stairs, the hole or the
// way back out, and it is the mummy's line all the same. It also raises
// [0xa602], so the shared refusal (10c9:sub_119e2, 0x010c) never runs here.
//
// The other three are dead content, and so is everything behind the two menus.
// OBJ:sub_0967e is the only way into a conversation and room 40 calls it only
// from those two arms. The mummy's chat-tree hook OBJ:sub_04732 (run for
// handler 0x28 from sub_04817, as sub_046f8 is for the hippie) swaps MUMMY0 for
// PIPENO0/PIPEYES0 by the pipe and PIPEYES0/1 for FIRENO/FIREYES by the
// matches, but it only runs inside a conversation, so it never runs. This is left out.
//
// What the live arm leaves behind is ported too: MISC.TAL stays the room's file
// until the room is entered again, so a look or a talk after it reads MISC's
// outcome table rather than MUMMY0's, as it does in the original.
static const int kCaveRoom = 40;

static const uint16 kSharedTal = 0x33db;	///< which of LOGIC's files is loaded
static const uint16 kMiscArmed = 0x33d6;	///< never written: the dead arm's guard

static const uint kMummyFile = 1;		///< MUMMY0.TAL, the room's own
static const uint kMiscFile = 8;		///< MISC.TAL
static const byte kSuspicious = 2;		///< MISC.TAL: "I better not. He looks pretty suspicious."

/// LOGIC:sub_124a7's names, by the number it is called with.
static const char *const kSharedNames[] = {
	nullptr, "MUMMY0.TAL", "MUMMY1.TAL", "FIRENO.TAL", "FIREYES.TAL", "PIPENO0.TAL",
	"PIPEYES0.TAL", "PIPEYES1.TAL", "MISC.TAL", "FIREYES1.TAL", "MUMMY2.TAL"
};

void CaveRoom::loadSharedTal(uint n) {
	if (n == 0 || n >= ARRAYSIZE(kSharedNames))
		return;
	_vm->_tal.load(Common::Path(kSharedNames[n]), &_vm->_pack);
	_vm->_script.setFlag(kSharedTal, (byte)n);
	debugC(1, kDebugChat, "cave: %s is the room's file", kSharedNames[n]);
}

void CaveRoom::scriptLoaded(int room) {
	// The room being opened, not _room: _room is still the room being left
	// until loadRoom completes.
	if (room == kCaveRoom)
		loadSharedTal(kMummyFile);
}

bool CaveRoom::armCave(int obj, byte item, int anchorX, int anchorY) {
	if (_vm->_room != kCaveRoom || item == Inventory::kNoItem)
		return false;

	// 0x00e4: the last arm, and the only one the game can reach.
	if (_vm->_script.flag(kMiscArmed) != 0 || obj <= 0)
		return false;

	loadSharedTal(kMiscFile);
	_vm->queueOutcome(_vm->_tal, kSuspicious, anchorX, anchorY);
	return true;
}

// The hole (ovr_28_0ebb:0x04a7). Every frame [0xa78c] says
// whether his sprite is right of 0x88, and entry 0 reads it to choose where a
// click on the hole (obj 4) walks him: from the left, 0xae,0x7f facing right;
// from the right, 0x75,0x91 facing left (0x01e5-0x021c, a walk_to_object on
// [0x7dbc]/[0x7dbe]/[0x7db8], which the walk geometry lift cannot carry). The
// port walked him to the click.
static const uint16 kCaveRight = 0xa78c;
static const int kCaveSideX = 0x88;
static const byte kCaveHole = 4;

void CaveRoom::walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) {
	if (_vm->_room != kCaveRoom)
		return;
	const bool right = _vm->_ben.spriteX() >= kCaveSideX;
	_vm->_script.setFlag(kCaveRight, right ? 1 : 0);
	if (obj != kCaveHole)
		return;

	target.x = right ? 0x75 : 0xae;
	target.y = right ? 0x91 : 0x7f;
	target.facing = right ? 4 : 2;
}

/// The side [0xa78c] says he is on, every tick pair.
bool CaveRoom::roomClock() {
	if (_vm->_room == kCaveRoom)
		_vm->_script.setFlag(kCaveRight, _vm->_ben.spriteX() >= kCaveSideX ? 1 : 0);
	return false;
}

/// The room's item uses are its own dispatch, and the table's three rows for
/// them lost their guards.
Room::Body CaveRoom::body(int obj, byte verb, byte item, int anchorX, int anchorY) {
	return armCave(obj, item, anchorX, anchorY) ? kBodyReplaced : kBodyNone;
}

} // End of namespace Alien
