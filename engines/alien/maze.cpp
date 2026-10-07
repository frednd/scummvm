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

#include "common/file.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/mazetables.h"
#include "alien/resources.h"

namespace Alien {

// Rooms 43 and 44, the two mazes.
//
// Every other room in the game is a room number. These two are a cell index in
// [0xa77c], and they are two *different* mazes rather than two halves of one:
// `disasm/ovr_2b_0f8d_rooms_43_44_45_-_maze_+_crystal_entry.asm` serves both
// from one stub -- entry 1 is room 43's hotspot program, entry 2 is room 44's,
// entries 3 and 4 their two inits and loops -- and each has its own pair of
// tables in the resident data segment, lifted by tools/gen_maze.py into
// mazetables.cpp. Room 45, the crystal entry, shares the stub and nothing else.
//
// Three things are per cell, and the lift can express none of them, because
// each guard is an indexed compare (`cmp byte ptr [di + 0x6450], 1`) rather
// than the flag compare the generators know how to read:
//
//   the background   seven pieces of corridor, each copied from one of three
//                    MAZBLK plates (CHARANIM:sub_13ede, 13b8:035e)
//   the arrows       the same seven bytes decide which of the six directional
//                    rectangles entry 1 or 2 registers
//   the torch        slot 0's MAZ_TOR1 loop runs only where the far wall is an
//                    opening (0x066a, 0x0967)
//
// So tools/gen_hotspots.py, gen_roominit.py and gen_plates.py all dropped the
// guard and then attributed the same rows to all three room numbers. This file
// takes rooms 43 and 44 back off them: buildMazeHotspots() rebuilds the table
// from the cell, mazeBackground() composes the plate, and startMaze() silences
// the torch where the original never starts it. Room 45's copies are left
// alone -- it is out of this pass, and its own way out to room 32 currently
// rides on the over-copied object 2.
//
// Stepping is not in the overlay either. `seg_main.asm` does it after the room
// loop returns (0000:0050 for room 43, 0000:00e0 for room 44): read the armed
// submode as a direction, index the cell's four neighbours, write the new cell
// and re-enter the same room without ever reaching the transition chain. The
// four submodes that do leave, and the crystal door in between, are the tests
// that run around it -- see mazeExit().
static const int kMazeRoomA = 43;
static const int kMazeRoomB = 44;
static const int kShoreRoom = 41;	///< the only room either of them is entered from

/// [0xa77c], the cell. In the state block, so it survives the room reload a
/// step performs, and a save taken inside the maze.
static const uint16 kCell = 0xa77c;
/// [0xa77d], whether the crystal door has been opened. Shared with rooms 31
/// and 40, which read it as plain story progress.
static const uint16 kDoorOpen = 0xa77d;
/// [0xa77e], whether the pick-axe is still in maze B's alcove.
static const uint16 kAxeThere = 0xa77e;
/// [0xa77f], which of room 41's three mouths he comes back out of.
static const uint16 kShoreMouth = 0xa77f;

/// The cells the two mazes open on, and the one each leaves by.
static const byte kCellCrystalDoor = 0x23;	///< maze A's far end, the crystal door
static const byte kCellAxe = 0x12;			///< maze B's alcove, where the pick-axe is
static const byte kCellFarMouth = 0x0e;		///< maze B's second way back to the shore
static const byte kNoCell = 0xff;

// CHARANIM:sub_13b80 (13b8:0000), the whole of it: an armed submode is a
// direction, and two submodes share each of forward and back.
static const byte kDirForward = 1;
static const byte kDirRight = 2;
static const byte kDirBack = 3;
static const byte kDirLeft = 4;
static const byte kNoDir = 0;

/// The seven pieces of the view, in the order the tile bytes have them
/// (CHARANIM:sub_13d0f / sub_13de3, through sub_13ce1).
static const struct { int16 x, y, w, h; } kMazePieces[7] = {
	{   0, 13, 64, 142 },	// far left column
	{  64, 13, 64, 121 },	// left of centre
	{ 128, 13, 64, 142 },	// centre: the far wall, the torch, the crystal door
	{ 192, 13, 64, 121 },	// right of centre
	{ 256, 13, 64, 142 },	// far right column
	{  64, 134, 64, 21 },	// foreground, bottom left
	{ 192, 134, 64, 21 }	// foreground, bottom right
};

/// The three plates a piece is copied from, indexed by its tile byte
/// (13b8:0337, 0x344, 0x351). The base is also what fills the strip above the
/// view and the strip below it, which no piece covers.
static const char *const kMazePlates[3] = { "MAZBLK21.PCX", "MAZBLK11.PCX", "MAZBLK31.PCX" };

/// What a registration is conditional on.
enum MazeSpotGuard {
	kSpotOpen = 0,		///< the piece is an opening
	kSpotNotWall,		///< the piece is an opening or a decor block
	kSpotDecor,			///< the piece is a decor block
	kSpotAxe			///< a decor block, and [0xa77e]: the pick-axe is still there
};

/**
 * One rectangle of the two hotspot programs, with what it is conditional on.
 *
 * Entry 1 (room 43, 0x0290-0x03b4) and entry 2 (room 44, 0x03e1-0x0564) are the
 * same eight rectangles in the same order, differing only in their label
 * numbering, plus the three room 44 has of its own: the skeleton in the alcove,
 * the pick-axe on it, and the cap beside it. `piece` is the tile byte the
 * registration is guarded on.
 */
struct MazeSpot {
	byte guard;
	byte piece;
	int16 x1, y1, x2, y2;
	byte labelA;		///< entry 1's slot in R43.TAL
	byte labelB;		///< entry 2's, for the same rectangle in R44.TAL
	byte obj;
	byte verb;
	byte outcomeCount;
	byte outcomes[4];
	bool roomBOnly;
};

static const MazeSpot kMazeSpots[] = {
	// The six arrows, and the far wall between them. Verb 5 is "walk to".
	{ kSpotOpen,    0,   6,  22,  45, 111, 1, 1,  2, 5, 1, {  1, 0, 0, 0 }, false },
	{ kSpotNotWall, 0,  40,  54,  67,  81, 2, 2,  8, 5, 1, { 17, 0, 0, 0 }, false },
	{ kSpotOpen,    1,  70,  21, 126, 115, 3, 1,  3, 5, 1, {  1, 0, 0, 0 }, false },
	{ kSpotOpen,    2, 147,  55, 164,  91, 4, 3,  1, 5, 3, {  2, 3, 4, 0 }, false },
	{ kSpotOpen,    3, 200,  27, 254, 110, 3, 1,  4, 5, 1, {  1, 0, 0, 0 }, false },
	{ kSpotOpen,    4, 274,  35, 319, 122, 3, 1,  5, 5, 1, {  1, 0, 0, 0 }, false },
	// Maze B's alcove: the skeleton, the pick-axe on it, and the cap. All three
	// sit inside the one block guarded on the alcove's piece (0x04c8, whose
	// `jne 0x51f` skips past the cap's registration at 0x050a too), so none of
	// them can be picked up from any other cell.
	{ kSpotDecor,   4, 251,  76, 304, 126, 0, 4, 10, 5, 4, { 14, 24, 25, 26 }, true },
	{ kSpotAxe,     4, 259,  57, 291,  88, 0, 5, 11, 1, 1, {  0, 0, 0, 0 }, true },
	{ kSpotDecor,   4, 262,  89, 273,  97, 0, 6, 20, 5, 1, { 20, 0, 0, 0 }, true },
	// And the two ways back, which are drawn in the foreground rather than up
	// the corridor, so they come last.
	{ kSpotOpen,    5,  64, 134, 127, 159, 6, 7,  6, 5, 1, { 11, 0, 0, 0 }, false },
	{ kSpotOpen,    6, 192, 134, 255, 159, 6, 7,  7, 5, 1, { 12, 0, 0, 0 }, false }
};

/// The torch, where the far wall is an opening: slot 0's MAZ_TOR1 (0x0679).
static const uint kTorchSlot = 0;
/// The crystal door's own bank, slot 2 in room 43 and the axe's in room 44.
static const uint kDoorSlot = 2;
static const int kDoorFrames = 0x0d;
static const int kDoorRate = 2;
static const int kDoorMode = 2;

// The crystal-door scene, [0xa49f] 3..8 at 0x0722-0x07ec, and the two lines it
// speaks out of ROOM43.TAL.
static const byte kOutcomeWhat = 0x16;	///< "What the..."
static const byte kOutcomeWow = 0x17;	///< "Wow..."
static const int16 kDoorWalkFromX = 0xf7, kDoorWalkY = 0x82;
static const int16 kDoorWalkToX = 0x111;
static const int16 kDoorStepX = 0x120;
static const byte kDoorFacing = 2;
static const byte kDoorTurnFacing = 3;	///< 0x0731, the turn toward the door

static const byte kStepDoorPlay = 3;
static const byte kStepWhat = 4;
static const byte kStepWalk = 5;
static const byte kStepWow = 6;
static const byte kStepStep = 7;
static const byte kStepLeave = 8;

/// transitions.cpp: room 43, submode 10 -> room 45. seg_main rewrites the
/// submode 5 the scene arms into this one (0000:0085).
static const byte kCrystalExitSubmode = 10;
/// And the three mouths back to room 41: submodes 1 and 2 (transitions.cpp).
static const byte kShoreSubmodeA = 1;
static const byte kShoreSubmodeB = 2;

/// Where a cell is entered from, by the submode he left the last one by:
/// CHARANIM:sub_13f90 (13b8:0410), as sprite origins.
static const struct { byte pose; int16 x, y; byte facing; } kMazeArrivals[] = {
	{ 1, 0x109, 0x37, 4 },
	{ 2, 0x05b, 0x46, 1 },
	{ 4, 0x0d7, 0x48, 1 },
	{ 5, 0x020, 0x35, 2 },
	{ 6, 0x061, 0x38, 3 },
	{ 7, 0x0ce, 0x34, 3 }
};
static const int16 kMazeArrivalX = 0xd3, kMazeArrivalY = 0x43;
static const byte kMazeArrivalFacing = 1;

static bool isMaze(int room) {
	return room == kMazeRoomA || room == kMazeRoomB;
}

/// CHARANIM:sub_13b80: submode to direction, or zero for a submode that is not
/// one of the six the geometry arms.
static byte mazeDirection(byte submode) {
	switch (submode) {
	case 1:
		return kDirLeft;
	case 2:
	case 4:
		return kDirForward;
	case 5:
		return kDirRight;
	case 6:
	case 7:
		return kDirBack;
	default:
		return kNoDir;
	}
}

const MazeCell *AlienEngine::mazeCell(int room) const {
	uint count = 0;
	const MazeCell *cells = mazeCells(room, count);
	const byte cell = _script.flag(kCell);
	if (!cells || cell >= count)
		return nullptr;

	return &cells[cell];
}

/**
 * The seven pieces of corridor, composed into the room's background.
 *
 * CHARANIM:sub_13ede loads one plate at a time and copies every piece that
 * names it, which keeps three plates off the heap at once; the port holds the
 * base for its palette anyway, so it does the same walk per plate.
 *
 * Returns false for any other room, and for a maze whose art is missing -- and
 * then loadRoom falls back on roomPlate()'s FADE43.PCX, which is what both
 * inits open on before their first swap.
 */
bool AlienEngine::mazeBackground(int room, Graphics::Surface &plate, byte *palette) {
	if (!isMaze(room))
		return false;

	const MazeCell *cell = mazeCell(room);
	if (!cell)
		return false;

	// The base plate is also the palette, and fills the strips above and below
	// the view that no piece covers.
	if (!loadGamePCX(Common::Path(kMazePlates[0]), plate, palette))
		return false;

	for (uint value = 0; value < ARRAYSIZE(kMazePlates); value++) {
		bool wanted = false;
		for (uint piece = 0; piece < ARRAYSIZE(kMazePieces); piece++)
			wanted |= cell->tile[piece] == value;
		// Value 0 is the base, which is already in place.
		if (!wanted || value == 0)
			continue;

		Graphics::Surface source;
		byte unused[256 * 3];
		if (!loadGamePCX(Common::Path(kMazePlates[value]), source, unused)) {
			debugC(1, kDebugResource, "maze: room %d has no %s", room, kMazePlates[value]);
			continue;
		}

		for (uint piece = 0; piece < ARRAYSIZE(kMazePieces); piece++) {
			if (cell->tile[piece] != value)
				continue;

			const Common::Rect rect(kMazePieces[piece].x, kMazePieces[piece].y,
									kMazePieces[piece].x + kMazePieces[piece].w,
									kMazePieces[piece].y + kMazePieces[piece].h);
			if (rect.right > source.w || rect.bottom > source.h ||
				rect.right > plate.w || rect.bottom > plate.h)
				continue;

			for (int16 y = rect.top; y < rect.bottom; y++)
				memcpy(plate.getBasePtr(rect.left, y), source.getBasePtr(rect.left, y),
					   rect.width());
		}

		source.free();
	}

	debugC(1, kDebugRooms, "maze: room %d cell 0x%02x is %d%d%d%d%d%d%d", room,
		   _script.flag(kCell), cell->tile[0], cell->tile[1], cell->tile[2],
		   cell->tile[3], cell->tile[4], cell->tile[5], cell->tile[6]);
	return true;
}

/**
 * A walk in a room with no walk mask: the two-point route the maze needs.
 *
 * Neither maze ships a KIERRA file -- there is no `KIER43`, no `KIER44`, and
 * the overlay does not even name the walk-nowhere default -- so the router has
 * no ring to search and no mask to test, and the port's ordinary walk goes
 * nowhere at all. The original has a second way to move him that needs neither:
 * `1021:sub_1023c` writes a route of exactly two points and the arrival turn
 * straight into the mover's own globals (`walk_route_len [0xa87c] := 2`), and it
 * is what the maze's own crystal-door scene uses, in this same maskless room.
 * So that is what an arrow click gets here: a straight line to the approach
 * point the geometry named, which is all the corridor has room for anyway.
 */
bool AlienEngine::mazeWalkTo(int x, int y, int arrivalFacing) {
	if (!isMaze(_room))
		return false;

	straightWalkTo(x, y, arrivalFacing);
	return true;
}

/**
 * The cell's rectangles, in place of the ones the lift attributed to the room.
 *
 * Runs after RoomScript::buildHotspots, which is where every other room's table
 * comes from, so the maze's own table replaces it outright rather than being
 * filtered: the guards that decide these are not flags, and the lift dropped
 * every one of them.
 */
void AlienEngine::buildMazeHotspots(int room) {
	if (!isMaze(room))
		return;

	const MazeCell *cell = mazeCell(room);
	if (!cell)
		return;

	_spots.clear();

	for (uint i = 0; i < ARRAYSIZE(kMazeSpots); i++) {
		const MazeSpot &spot = kMazeSpots[i];
		if (spot.roomBOnly && room != kMazeRoomB)
			continue;

		const byte tile = cell->tile[spot.piece];
		bool wanted = false;
		switch (spot.guard) {
		case kSpotOpen:
			wanted = tile == 1;
			break;
		case kSpotNotWall:
			wanted = tile == 1 || tile == 2;
			break;
		case kSpotDecor:
			wanted = tile == 2;
			break;
		case kSpotAxe:
			wanted = tile == 2 && _script.flag(kAxeThere) == 1;
			break;
		default:
			wanted = true;
			break;
		}

		if (!wanted)
			continue;

		Hotspot out;
		out.x1 = spot.x1;
		out.y1 = spot.y1;
		out.x2 = spot.x2;
		out.y2 = spot.y2;
		out.label = room == kMazeRoomA ? spot.labelA : spot.labelB;
		out.obj = spot.obj;
		out.verb = spot.verb;
		out.outcomeCount = spot.outcomeCount;
		for (uint o = 0; o < ARRAYSIZE(out.outcomes); o++)
			out.outcomes[o] = spot.outcomes[o];

		_spots.push_back(out);
	}

	debugC(1, kDebugHotspots, "maze: room %d cell 0x%02x registers %u rectangles",
		   room, _script.flag(kCell), _spots.size());
}

/**
 * Where in the maze the way in from the shore lands.
 *
 * Runs before the room's plate and its rectangles, because both are read from
 * the cell: 0x05c8 for maze A, 0x08a5 and 0x08bd for maze B's two mouths, each
 * also naming which side of the screen he walks in from. Without it a maze
 * re-entered would open on whatever cell it was last left at, which the
 * original never does -- inside the maze nothing but the step moves [0xa77c].
 */
void AlienEngine::mazeEnter(int room) {
	if (!isMaze(room) || _mode != kShoreRoom)
		return;

	if (room == kMazeRoomA && _lastSubmode == 3) {
		_script.setFlag(kCell, 0);
		_mazePose = 4;
	} else if (room == kMazeRoomB && _lastSubmode == 1) {
		_script.setFlag(kCell, 0);
		_mazePose = 2;
	} else if (room == kMazeRoomB && _lastSubmode == 2) {
		_script.setFlag(kCell, kCellFarMouth);
		_mazePose = 4;
	} else {
		return;
	}

	debugC(1, kDebugRooms, "maze: room %d opens on cell 0x%02x, entered as %u",
		   room, _script.flag(kCell), _mazePose);
}

/**
 * Where the cell stands him: CHARANIM:sub_13f90 (13b8:0410), keyed on [0xa87f].
 *
 * He walks in from the side opposite the one he left by, and neither maze has a
 * char_place of its own in roominit.cpp -- the original's inits call this
 * instead, both for a step inside the maze and for the way in from the shore.
 */
void AlienEngine::mazePlace() {
	int16 x = kMazeArrivalX, y = kMazeArrivalY;
	byte facing = kMazeArrivalFacing;
	for (uint i = 0; i < ARRAYSIZE(kMazeArrivals); i++) {
		if (kMazeArrivals[i].pose != _mazePose)
			continue;
		x = kMazeArrivals[i].x;
		y = kMazeArrivals[i].y;
		facing = kMazeArrivals[i].facing;
		break;
	}
	_ben.placeSprite(x, y, facing);
}

/// Every arrival: where the way in stands him, the room's own dialog file, and
/// the torch the cell may not have.
void AlienEngine::startMaze() {
	if (!isMaze(_room))
		return;

	// The step places him itself, once the room it re-entered is up; this is
	// the way in from the shore, whose pose mazeEnter() has already chosen.
	if (_mode == kShoreRoom)
		mazePlace();

	// The generic room open reads one script name out of the shared overlay's
	// own manifest and gets room 45's rather than 43's -- the overlay serves
	// three room numbers and the reader was never written to tell them apart.
	// Both mazes speak out of room43.tal (the string pools at 0x570 and 0x84e).
	const Common::Path script("room43.tal");
	if (Common::File::exists(script))
		_tal.load(script, &_pack);

	// The torch burns on the far wall, so it burns only where there is one to
	// see: the original's play sits under the same guard the centre arrow does
	// (0x066a for room 43, 0x0967 for room 44), and roominit.cpp has it
	// unconditional because that guard is an indexed compare. Taken down
	// rather than stopped: a mode 1 play cut short leaves its frame in the
	// plate, a torch standing on a wall that has none.
	const MazeCell *cell = mazeCell(_room);
	if (cell && cell->tile[2] != 1)
		_anims.takeDown(kTorchSlot);
}

/**
 * A submode armed in the maze, before the transition chain sees it.
 *
 * This is `seg_main.asm` 0000:0019-0000:00db, in its order: the mouths back to
 * room 41 first, because they rewrite the submode into one the chain answers
 * and suppress the step; then the crystal door, which the room's own loop
 * reaches before MAIN gets the chance (0x07fb); then the step itself, which
 * re-enters the room and never returns to the chain at all.
 *
 * Returns true when the maze has answered the submode, and leaves `submode`
 * rewritten when it has not.
 */
bool AlienEngine::mazeExit(byte &submode) {
	if (!isMaze(_room))
		return false;

	const MazeCell *cell = mazeCell(_room);
	if (!cell)
		return false;

	const byte at = _script.flag(kCell);

	// 0000:0019, 0000:00a6 and 0000:00c3: the three mouths. Each names which
	// of room 41's three views he comes out into.
	if (_room == kMazeRoomA && at == 0 && submode == 7) {
		_script.setFlag(kShoreMouth, 2);
		submode = kShoreSubmodeA;
		return false;
	}
	if (_room == kMazeRoomB && at == kCellFarMouth && submode == 7) {
		_script.setFlag(kShoreMouth, 1);
		submode = kShoreSubmodeA;
		return false;
	}
	if (_room == kMazeRoomB && at == 0 && submode == 6) {
		_script.setFlag(kShoreMouth, 0);
		submode = kShoreSubmodeB;
		return false;
	}

	// 0x07fb: the first arrival made in front of the crystal door, whichever
	// arrow it was, is the scene instead of a step. [0xa77d] makes it once
	// only, and its own exit comes back through here with the flag set, which
	// is the submode 5 the test below rewrites.
	if (_room == kMazeRoomA && at == kCellCrystalDoor && !_script.flag(kDoorOpen)) {
		_script.setFlag(kDoorOpen, 1);
		_mazeStep = kStepDoorPlay;
		CursorMan.showMouse(false);
		// CRY_DMOR is the door alone: he stands in front of it the whole time
		// (nothing in 0x0722-0x07ec touches [0xa94d]).
		_anims.play(kDoorSlot, 1, kDoorFrames, kDoorRate, kDoorMode);
		debugC(1, kDebugRooms, "maze: the crystal door opens");
		return true;
	}

	// 0000:0077: and with the door already open, the one submode that leaves.
	if (_room == kMazeRoomA && at == kCellCrystalDoor && submode == 5) {
		submode = kCrystalExitSubmode;
		return false;
	}

	// 0000:0050: otherwise a step. The arrow he walked to is a direction, and
	// the cell's four neighbours say where it goes.
	const byte dir = mazeDirection(submode);
	if (dir == kNoDir)
		return false;

	const byte next = cell->next[dir - 1];
	if (next == kNoCell) {
		debugC(1, kDebugRooms, "maze: room %d cell 0x%02x has no way out in direction %u",
			   _room, at, dir);
		return true;
	}

	debugC(1, kDebugRooms, "maze: room %d cell 0x%02x -> 0x%02x, direction %u",
		   _room, at, next, dir);
	_script.setFlag(kCell, next);
	_mazePose = submode;

	// The room's loop ends for a step exactly as it does for a real exit, and
	// ending it is what sets game_mode to the room being left (OBJ:sub_0879a,
	// called from 0x0847 either way). So the re-entry sees the maze's own
	// handler code rather than the shore's, and mazeEnter() leaves the cell
	// this step just wrote alone.
	const int room = _room;
	_mode = (byte)room;
	_lastSubmode = submode;
	if (!loadRoom(room))
		return true;

	mazePlace();

	return true;
}

// --- Room 45, the crystal entry -------------------------------------------
//
// The room the crystal door at maze A's far end opens into, and the one place
// in the game the phone number (item 40) comes from. Its overlay entry is the
// same file the two mazes use, but it is not a room in the ordinary sense: it
// is entry 5 (0x0aa8), a scene with a loop of its own that never registers a
// hotspot, never walks and leaves by writing game_submode itself. So it is all
// one [0xa49f] machine, the shape store.cpp's whole room already is.
//
// Nine states at 0x0bee, in the order they run:
//
//   3     the screen wipe (OBJ:sub_02f27) and "What is this place..", which
//         is outcome 1 of room45.tal
//   4     wait for it to come down
//   0x0e  0x19 ticks later, CRY_ENT1's first fifteen frames: Awlox coming out
//         of the crystal
//   0x0f  on the last of them, [0xa784] -- which is what turns the looping
//         relaunch of that slot on -- and the thirty frames he stands in
//   0x10  0x28 ticks later, "Uh..." (outcome 2)
//   0x12  and when that is down, the conversation: DLGREQ:sub_0c4d1 with
//         three outcomes from 0x14, Awlox first
//   0x13  run it; when it ends, ENT_NOTE -- seventy-four frames of him holding
//         the number out
//   0x14  wait for those
//   0x19  and then, the moment the standing loop comes round to its own first
//         frame again ([0xa4ca] == 15, which only a relaunch can make true),
//         inv_add(40) and ENT_BENB: Ben taking it
//   0x28  four frames into that the input gate closes, and on the last one the
//         room writes submode 1 and leaves
//
// The count the runner is handed is three, not the ten lines the file holds,
// because outcome 0x16 chains 0x16..0x1c in room45.tal's own outcome table --
// six of Awlox's speech are one entry as far as the machine is concerned.
//
// What the port does not do here is the wipe: OBJ:sub_02f27 walks the EMS work
// pointer up the screen a line at a time, and there is nothing behind it to
// reveal, the room having just been loaded.
//
// Two things the generators carry rather than this file. gen_roominit.py used
// to lift all five of the machine's plays into room 45's opening table, where
// they fired in a row the moment the room loaded; room 45 is now TICK_OWNED
// alongside 22 and 32, so the opening is the two effects in front of the loop
// (ENT_DROP, and the placement) and nothing else. And the relaunch that makes
// [0xa4ca] come back to 15 is anims.cpp's loop row for slot 0, which is
// lifted without its [0xa784] guard -- gen_anims.py only reads guards out of
// the [0xa53a] loop-flag block. Harmless: the slot has nothing playing on it
// until state 0x0e, and both of the plays that follow want the relaunch.
static const int kCrystalRoom = 45;

static const byte kCrystalWhat = 3;
static const byte kCrystalWhatDown = 4;
static const byte kCrystalComeOut = 0x0e;
static const byte kCrystalStandIn = 0x0f;
static const byte kCrystalUh = 0x10;
static const byte kCrystalUhDown = 0x12;
static const byte kCrystalTalking = 0x13;
static const byte kCrystalNote = 0x14;
static const byte kCrystalTake = 0x19;
static const byte kCrystalGo = 0x28;

static const uint kCryDoorSlot = 0;		///< CRY_ENT1, Awlox out of the crystal
static const uint kCryNoteSlot = 4;		///< ENT_NOTE, the number held out
static const uint kCryBenSlot = 5;		///< ENT_BENB, Ben taking it
static const uint kCryDropSlot = 6;		///< ENT_DROP, which the opening starts

static const int kCryOutFirst = 1, kCryOutCount = 15;
static const int kCryStandFirst = 15, kCryStandCount = 30;
static const int kCryNoteCount = 74;
static const int kCryBenCount = 10;
static const int kCryRate = 2;

/// [0xa4ca], slot 0's current frame: the standing loop is back at its first.
static const int kCryStandFrame = 15;

static const byte kCryWhatLine = 1;		///< "What is this place.."
static const byte kCryUhLine = 2;		///< "Uh..."
static const byte kCryTalkFirst = 0x14;	///< "Greetings, Ben."
static const byte kCryTalkCount = 3;

/// The two anchors and inks of the thirteen immediates at 0x0c90. Awlox stands
/// off to the right and speaks in the pale blue every other alien does; Ben
/// answers low and left, in white.
static const int kAwloxX = 0xdf, kAwloxY = 0x30;
static const byte kAwloxInk[3] = { 0x2d, 0x2d, 0x3f };
static const int kCryBenX = 0x34, kCryBenY = 0x3f;
static const byte kCryBenInk[3] = { 0x3f, 0x3f, 0x3f };

/// [0xa49c] again: 0x19 before he comes out, 0x28 before Ben answers.
static const uint kCryOutWait = 0x19;
static const uint kCryUhWait = 0x28;

static const byte kCryPhoneNumber = 40;	///< OBJ:inv_add(0x28)
static const byte kCrystalExit = 1;		///< game_submode 1, back to the cemetery

static const int kCryForward = 1;		///< mode 1: forward, left behind (anim.h)

/// [0xa784]: not a puzzle flag but the switch on the looping relaunch of slot
/// 0, which state 0x19 below is timed to.
static const uint16 kCrystalLoopOn = 0xa784;

/// DLGREQ:sub_0c432, one pass, the same alternating runner sluggs.cpp explains.
void AlienEngine::crystalSpeak() {
	if (!_crystalLeft) {
		_crystalSpeaking = false;
		return;
	}

	const bool awlox = _crystalSpeaker == 0;
	const byte *ink = awlox ? kAwloxInk : kCryBenInk;
	setTextColor(ink[0], ink[1], ink[2]);
	uploadTextColor();
	queueOutcome(_tal, _crystalLine, awlox ? kAwloxX : kCryBenX,
				 awlox ? kAwloxY : kCryBenY, !awlox);

	debugC(1, kDebugRooms, "crystal: %s says outcome 0x%02x",
		   awlox ? "Awlox" : "Ben", _crystalLine);

	_crystalSpeaker = _crystalSpeaker ? 0 : 1;
	_crystalLine++;
	_crystalLeft--;
}

/// One of the two lines the machine speaks on its own, outside the runner.
void AlienEngine::crystalSay(byte outcome) {
	setTextColor(kCryBenInk[0], kCryBenInk[1], kCryBenInk[2]);
	uploadTextColor();

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);
	queueOutcome(_tal, outcome, anchorX, anchorY);
}

/// Room 45 crystal entry scene: the [0xa49f] machine at 0x0bee.
void AlienEngine::stepCrystal() {
	if (_room != kCrystalRoom) {
		_crystalStep = 0;
		_crystalSpeaking = false;
		return;
	}

	// 0x0b8c: the opening arms it, and takes the cursor away for the whole of
	// it -- nothing in this room is ever clicked.
	if (!_crystalStep) {
		_crystalStep = 1;
		_crystalWait = 0;
		_crystalSpeaking = false;
		_script.setFlag(RoomScript::kMachine, kCrystalWhat);
		CursorMan.showMouse(false);
	}

	// A conversation keeps itself going, one line per line that came down.
	if (_crystalSpeaking && speechDone())
		crystalSpeak();

	// [0xa49c], which LOGIC advances whether or not a state is reading it.
	_crystalWait++;

	switch (_script.flag(RoomScript::kMachine)) {
	case kCrystalWhat:
		// 0x0bf5: the wipe, then the first thing he says to himself.
		crystalSay(kCryWhatLine);
		_script.setFlag(RoomScript::kMachine, kCrystalWhatDown);
		break;

	case kCrystalWhatDown:
		// 0x0c0d: [0xad1c], the line being down.
		if (!speechDone())
			break;
		_script.setFlag(RoomScript::kMachine, kCrystalComeOut);
		_crystalWait = 0;
		break;

	case kCrystalComeOut:
		if (_crystalWait <= kCryOutWait)
			break;
		_anims.play(kCryDoorSlot, kCryOutFirst, kCryOutCount, kCryRate, kCryForward);
		_script.setFlag(RoomScript::kMachine, kCrystalStandIn);
		break;

	case kCrystalStandIn:
		// 0x0c45: on the last frame of it, and not after -- the relaunch below
		// would otherwise never be told to start.
		if (_anims.remaining(kCryDoorSlot) != 1)
			break;
		_script.setFlag(kCrystalLoopOn, 1);
		_anims.play(kCryDoorSlot, kCryStandFirst, kCryStandCount, kCryRate, kCryForward);
		_script.setFlag(RoomScript::kMachine, kCrystalUh);
		_crystalWait = 0;
		break;

	case kCrystalUh:
		if (_crystalWait <= kCryUhWait)
			break;
		crystalSay(kCryUhLine);
		_script.setFlag(RoomScript::kMachine, kCrystalUhDown);
		break;

	case kCrystalUhDown:
		if (!speechDone())
			break;
		_crystalSpeaker = 0;
		_crystalLine = kCryTalkFirst;
		_crystalLeft = kCryTalkCount;
		_crystalSpeaking = true;
		crystalSpeak();
		_script.setFlag(RoomScript::kMachine, kCrystalTalking);
		break;

	case kCrystalTalking:
		// 0x0cc1: [0xad3f], the run having nothing left to say.
		if (_crystalSpeaking || !speechDone())
			break;
		CursorMan.showMouse(false);
		_anims.play(kCryNoteSlot, 1, kCryNoteCount, kCryRate, kCryForward);
		_script.setFlag(RoomScript::kMachine, kCrystalNote);
		_crystalWait = 0;
		break;

	case kCrystalNote:
		if (_anims.remaining(kCryNoteSlot) != 0)
			break;
		_script.setFlag(RoomScript::kMachine, kCrystalTake);
		break;

	case kCrystalTake:
		// 0x0d02: the one moment in the scene that is timed to the standing
		// loop rather than to a play of its own.
		if (_anims.frame(kCryDoorSlot) != kCryStandFrame)
			break;
		_inventory.add(kCryPhoneNumber);
		_anims.stop(kCryDoorSlot);
		_anims.stop(kCryDropSlot);
		_anims.play(kCryDoorSlot, kCryStandFirst, 1, 0, kCryForward);
		_anims.play(kCryBenSlot, 1, kCryBenCount, kCryRate, kCryForward);
		_script.setFlag(RoomScript::kMachine, kCrystalGo);
		debugC(1, kDebugRooms, "crystal: item %u handed over", kCryPhoneNumber);
		break;

	case kCrystalGo:
		// 0x0d46 also drops [0xa94d] four frames in. The port has no home for
		// that byte and the cursor has been away since the opening, so there
		// is nothing here for it to close.
		if (_anims.remaining(kCryBenSlot) != 1)
			break;
		_script.setFlag(RoomScript::kMachine, 0);
		_crystalStep = 0;
		CursorMan.showMouse(true);
		takeExit(kCrystalExit);
		break;

	default:
		break;
	}
}

/// The crystal-door scene, [0xa49f] 3..8 (0x0722-0x07ec).
void AlienEngine::stepMaze() {
	if (_room != kMazeRoomA || !_mazeStep)
		return;

	switch (_mazeStep) {
	case kStepDoorPlay:
		// 0x0722: the door's own thirteen frames come up first, and he turns
		// to face it.
		if (_anims.isBusy(kDoorSlot))
			break;
		_ben.faceTo(kDoorTurnFacing);
		_mazeStep = kStepWhat;
		break;

	case kStepWhat: {
		// 0x0741: "What the..."
		if (_ben.isTurning())
			break;
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeWhat, anchorX, anchorY);
		_mazeStep = kStepWalk;
		break;
	}

	case kStepWalk:
		// 0x0773: a route he is given rather than one he asked for,
		// 1021:sub_1023c -- from where he stands to the arrow's foot and on
		// to the doorway. He walks the first leg; he is not put there.
		if (!speechDone())
			break;
		walkHandRoute(kDoorWalkFromX, kDoorWalkY, kDoorWalkToX, kDoorWalkY, kDoorFacing);
		_mazeStep = kStepWow;
		break;

	case kStepWow: {
		// 0x079c: "Wow..." once he is there.
		if (_ben.isWalking() || _ben.isTurning())
			break;
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeWow, anchorX, anchorY);
		_mazeStep = kStepStep;
		break;
	}

	case kStepStep:
		// 0x07c5: and the step through it.
		if (!speechDone())
			break;
		walkTo(kDoorStepX, kDoorWalkY, kDoorFacing);
		_mazeStep = kStepLeave;
		break;

	case kStepLeave:
		// 0x07dd: submode 5, which seg_main rewrites into the 10 the chain
		// answers with room 45.
		if (_ben.isWalking() || _ben.isTurning())
			break;
		_mazeStep = 0;
		takeExit(5);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
