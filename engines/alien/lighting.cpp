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

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

// The light maps, and the one thing in the palette they drive.
//
// FADE<n>.PCX is not a palette and not a picture: it is a per-pixel brightness
// plate for the room, and the only thing that reads it is the character. The
// original loads it with the PCX loader's palette gate off (OBJ:sub_06961 opens
// with `mov byte ptr [0xd12c], 0`), copies 48000 bytes of it -- 320x150, the
// viewport, starting thirteen rows in -- into EMS pages 0-2, and puts a second
// plate into pages 3-5 with the twin sub_069af. Then every room's tick calls
// OBJ:sub_069fd once a frame:
//
//   [0x7d9d] = [0x7d9c]                  ; last frame's level
//   [0x7d9c] = 0xff                      ; default: full brightness
//   x = [0xa8ec] + 0xa                   ; the character's feet
//   y = [0xa8ee] + 0x40 - 0xd            ; less the thirteen rows skipped
//   clamp y to 0..0x8e, x to 0..[0xa0c0]
//   if x >= 0x140: x -= 0x140, sample the second plate, else the first
//   if the plate byte is non-zero: [0x7d9c] = byte << 2
//
// So the level is the room's own light map read where Ben is standing, and a
// zero byte means "leave it alone", which is why NOFADE.PCX -- 64000 zero
// bytes -- is a room that is evenly lit. Every shipped byte is <= 63, so the
// shift lands in 0..252.
//
// What the level scales is palette entries 1 through 24, which are Ben's and
// nobody else's (docs/playthrough_findings.md finding #50). OBJ:sub_073bf keeps
// the 24 triples of the character palette at [0x7cc4], OBJ:sub_0a6f0 scales that
// block into [0x7d0c] by `(component * level) >> 8` -- the same arithmetic the
// room fades use -- and sub_0a717 pushes the 0x48 bytes out at DAC index 1 and
// mirrors them into the live palette. The room tick only calls it when the level
// actually moved:
//
//   mov al, [0x7d9c] / cmp [0x7d9d], al / je skip
//   cmp byte ptr [0xa884], 0 / je skip          ; not before the first frame
//   lcall OBJ:sub_0a717
//
// The [0xa884] half of that gate is the HUD bar's own flag (finding #57's
// neighbour, the "two Bens" entry), and its whole life is the single composed
// frame after a room opens; the port has no such frame, so it is not modelled.

/// The plate is copied from row 13 of the PCX, 320x150 of it.
static const int kLightSkipRows = 13;
static const int kLightWidth = 320;
static const int kLightHeight = 150;

/// The clamp sub_069fd puts on the sampled row, 0..0x8e.
static const int kLightMaxRow = 0x8e;

/// The character's own entries, [0x7cc4]: 24 triples starting at index 1.
static const int kCharFirst = 1;
static const int kCharCount = 24;

/**
 * Which plate each room loads, read off its own call sites.
 *
 * Every one of these is a literal pushed at an OBJ:sub_06961 (left) or
 * sub_069af (right) call in the room's overlay; the table is what those call
 * sites resolve to, room by room. Rooms 23 and 25 are the two overlays with no
 * call at all -- they inherit whatever level the room before them left, and the
 * port does the same by not touching it.
 *
 * Rooms 43, 44 and 45 share one overlay, and its three bodies name FADE43,
 * FADE43 and NOFADE in that order (ovr_2b_0f8d entries 3, 4 and 5, each next to
 * its own room<n>.tal). Room 7 carries a second name rather than a second half:
 * its plate follows the light switch, the way its background does (roomPlate,
 * bedroom.cpp), so `dark` is what it reads while [0xa6fa] is clear.
 */
static const struct LightMapEntry {
	int room;
	const char *left;
	const char *right;
	const char *dark;
} kLightMaps[] = {
	{  3, "FADE3A.PCX", "FADE3B.PCX", nullptr       },
	{  6, "FADE6.PCX",  nullptr,      nullptr       },
	{  7, "FADE7.PCX",  nullptr,      "FADE7X.PCX"  },
	{  8, "FADE8A.PCX", "FADE8B.PCX", nullptr       },
	{ 10, "FADE10.PCX", nullptr,      nullptr       },
	{ 11, "FADE11.PCX", nullptr,      nullptr       },
	{ 13, "FADE13.PCX", nullptr,      nullptr       },
	{ 14, "FADE14.PCX", nullptr,      nullptr       },
	{ 15, "FADE15A.PCX", "FADE15B.PCX", nullptr       },
	{ 17, "FADE17.PCX", nullptr,      nullptr       },
	{ 18, "FADE18.PCX", nullptr,      nullptr       },
	{ 19, "FADE19.PCX", nullptr,      nullptr       },
	{ 21, "FADE21A.PCX", "FADE21B.PCX", nullptr       },
	{ 22, "NOFADE.PCX", "NOFADE.PCX", nullptr       },
	{ 26, "FADE26.PCX", nullptr,      nullptr       },
	{ 27, "FADE27.PCX", "NOFADE.PCX", nullptr       },
	{ 28, "FADE28.PCX", nullptr,      nullptr       },
	{ 30, "FADE30.PCX", nullptr,      nullptr       },
	{ 31, "FADE31.PCX", nullptr,      nullptr       },
	{ 32, "NOFADE.PCX", "NOFADE.PCX", nullptr       },
	{ 33, "FADE33.PCX", nullptr,      nullptr       },
	{ 34, "FADE34.PCX", nullptr,      nullptr       },
	{ 35, "NOFADE.PCX", nullptr,      nullptr       },
	{ 40, "FADE40.PCX", nullptr,      nullptr       },
	{ 41, "NOFADE.PCX", nullptr,      nullptr       },
	{ 43, "FADE43.PCX", nullptr,      nullptr       },
	{ 44, "FADE43.PCX", nullptr,      nullptr       },
	{ 45, "NOFADE.PCX", nullptr,      nullptr       },
	{ 46, "NOFADE.PCX", nullptr,      nullptr       },
	{ 48, "FADE48.PCX", nullptr,      nullptr       },
	{ 49, "FADE49.PCX", nullptr,      nullptr       },
	{ 50, "FADE50.PCX", nullptr,      nullptr       },
	{ 51, "FADE51.PCX", nullptr,      nullptr       },
	{ 52, "FADE52A.PCX", "FADE52B.PCX", nullptr       },
	{ 53, "NOFADE.PCX", "NOFADE.PCX", nullptr       },
	{ 54, "NOFADE.PCX", nullptr,      nullptr       },
	{ 55, "FADE55.PCX", nullptr,      nullptr       },
	{ 56, "NOFADE.PCX", nullptr,      nullptr       },
	{ 57, "NOFADE.PCX", "NOFADE.PCX", nullptr       },
	{ 58, "NOFADE.PCX", "NOFADE.PCX", nullptr       },
	{ 59, "NOFADE.PCX", nullptr,      nullptr       },
};

// Room 31, the cliff: the sampler runs and the room then throws its answer away
// (ovr_1f_0e87:0x0511). The level is full unless [0xa737] is set, and 0x96 while
// it is -- the cliff after dark.
static const int kCliffRoom = 31;
static const uint16 kCliffFlag = 0xa737;
static const byte kCliffLevel = 0x96;

// Room 46, the diving area: same shape, except that it writes both halves of
// the pair (ovr_2e_0ec3:0x0490), so the level never counts as having moved and
// the character palette is never re-uploaded there at all.
static const int kDivingRoom = 46;

// Room 49, the engine room: the far half of it has a second character palette
// (finding #38). MANPAL2 is loaded first and stashed at [0x7d54], VAKIPAL is
// loaded over the live buffer, and the room's tick picks between them by where
// Ben is standing -- sub_0a717 below x 0xce, sub_0a74c above 0xcd. It also
// drives the level itself past that line, from its own counter [0xa792]
// (ovr_31_0f85:0x0578).
/// Room 7's light flag, the same [0xa6fa] the switch and the plates follow.
static const uint16 kLightFlag = 0xa6fa;

static const int kEngineRoom = 49;
static const int kEngineSplit = 0xcd;
static const uint16 kEngineCounter = 0xa792;
static const char *const kEngineAltPalette = "MANPAL2.PCX";

const char *AlienEngine::lightMapFile(int room, bool second) const {
	for (uint i = 0; i < ARRAYSIZE(kLightMaps); i++) {
		if (kLightMaps[i].room != room)
			continue;
		if (second)
			return kLightMaps[i].right;

		// The bedroom's light switch swaps the whole set of plates, the light
		// map with them: the dark name while [0xa6fa] is clear, the lit one
		// once the switch has set it.
		if (kLightMaps[i].dark && !_script.flag(kLightFlag))
			return kLightMaps[i].dark;
		return kLightMaps[i].left;
	}

	return nullptr;
}

void AlienEngine::freeLightMap() {
	for (uint half = 0; half < 2; half++) {
		delete[] _lightMap[half];
		_lightMap[half] = nullptr;
	}
}

/**
 * Load a room's light map, both halves of it when it has two.
 *
 * The 48000 bytes the original lifts into EMS are rows 13..162 of the plate, so
 * that is what is kept here; the sampler's y has the same thirteen subtracted
 * from it, which is what makes the two line up.
 */
void AlienEngine::loadLightMap(int room) {
	freeLightMap();

	for (uint half = 0; half < 2; half++) {
		const char *name = lightMapFile(room, half != 0);
		if (!name)
			continue;

		Graphics::Surface plate;
		byte palette[256 * 3];
		if (!loadGamePCX(Common::Path(name), plate, palette)) {
			// Not fatal: a room with no plate reads as evenly lit, which is
			// what the majority of them ship as anyway.
			warning("room %d: could not load the light map %s", room, name);
			plate.free();
			continue;
		}

		if (plate.w < kLightWidth || plate.h < kLightSkipRows + kLightHeight) {
			warning("room %d: light map %s is %dx%d", room, name, plate.w, plate.h);
			plate.free();
			continue;
		}

		_lightMap[half] = new byte[kLightWidth * kLightHeight];
		for (int y = 0; y < kLightHeight; y++)
			memcpy(_lightMap[half] + y * kLightWidth,
				   plate.getBasePtr(0, y + kLightSkipRows), kLightWidth);
		plate.free();
	}

	debugC(1, kDebugLight, "light: room %d maps %s %s", room,
		   lightMapFile(room, false) ? lightMapFile(room, false) : "-",
		   lightMapFile(room, true) ? lightMapFile(room, true) : "-");
}

/**
 * Stage the character's 24 entries at the level now in force and push them.
 *
 * `alt` is room 49's stash rather than the palette the room opened with; every
 * other room only ever has the one.
 */
void AlienEngine::uploadCharPalette(bool alt) {
	const byte *source = (alt && _charPaletteAltLoaded) ? _charPaletteAlt : _charPalette;

	for (int i = 0; i < kCharCount * 3; i++)
		_palette[kCharFirst * 3 + i] = (byte)((source[i] * _lightLevel) >> 8);

	g_system->getPaletteManager()->setPalette(_palette + kCharFirst * 3, kCharFirst, kCharCount);
	_dirty = true;

	debugC(2, kDebugLight, "light: char palette at level %d%s", _lightLevel,
		   (alt && _charPaletteAltLoaded) ? " (stash)" : "");
}

/**
 * OBJ:sub_069fd and the upload gate that follows it, once a tick.
 */
void AlienEngine::stepLighting() {
	// Rooms 23 and 25 never call the sampler, so they keep the level the room
	// before them left -- and, with it, that room's brightness on the character.
	if (!lightMapFile(_room, false))
		return;

	_lightPrev = _lightLevel;
	_lightLevel = 0xff;

	int x = _ben.spriteX() + Walker::kWalkPointX;
	int y = _ben.spriteY() + Walker::kWalkPointY - kLightSkipRows;
	y = CLIP(y, 0, kLightMaxRow);
	x = CLIP(x, 0, _roomWidth);

	// Past the split it is the second plate, indexed from its own left edge.
	// A room with no second plate reads as zero there, i.e. as full brightness;
	// the original samples EMS pages 3-5 regardless and finds whatever the last
	// room that loaded a right-hand plate left in them, which is not something
	// a port can reproduce (the same rule walk.cpp applies to the walk mask).
	const uint half = (x >= kLightWidth) ? 1 : 0;
	if (half)
		x -= kLightWidth;

	const byte sample = _lightMap[half] ? _lightMap[half][y * kLightWidth + x] : 0;
	if (sample)
		_lightLevel = (byte)(sample << 2);

	// The two rooms that call the sampler and then overwrite its answer.
	if (_room == kCliffRoom) {
		_lightLevel = _script.flag(kCliffFlag) == 1 ? kCliffLevel : 0xff;
	} else if (_room == kDivingRoom) {
		_lightLevel = 0xff;
		_lightPrev = 0xff;
	} else if (_room == kEngineRoom && _ben.spriteX() > kEngineSplit) {
		_lightPrev = _lightLevel;
		_lightLevel = (byte)(0xff - _script.flag(kEngineCounter));
	}

	if (_lightLevel == _lightPrev)
		return;

	debugC(1, kDebugLight, "light: room %d feet %d,%d sample %d level %d", _room,
		   _ben.spriteX() + Walker::kWalkPointX, _ben.spriteY() + Walker::kWalkPointY,
		   sample, _lightLevel);

	uploadCharPalette(_room == kEngineRoom && _ben.spriteX() > kEngineSplit);
}

/**
 * Take a copy of the character's own entries, and room 49's second set.
 *
 * The unscaled block is what the original keeps at [0x7cc4]; sub_073bf leaves a
 * straight copy of it in the scaled buffer as well, so a room opens at full
 * brightness and the first level change is what starts scaling it.
 */
void AlienEngine::keepCharPalette(int room, const byte *palette) {
	memcpy(_charPalette, palette + kCharFirst * 3, sizeof(_charPalette));

	_charPaletteAltLoaded = false;
	if (room != kEngineRoom)
		return;

	Graphics::Surface dummy;
	byte alt[256 * 3];
	if (loadGamePCX(Common::Path(kEngineAltPalette), dummy, alt)) {
		memcpy(_charPaletteAlt, alt + kCharFirst * 3, sizeof(_charPaletteAlt));
		_charPaletteAltLoaded = true;
		debugC(1, kDebugLight, "light: room 49 stashes %s", kEngineAltPalette);
	}
	dummy.free();
}

/**
 * Print the whole table, which is what tools/check_lighting.py mirrors.
 */
void AlienEngine::dumpLighting() {
	debug("light: %d rooms", (int)ARRAYSIZE(kLightMaps));

	for (uint i = 0; i < ARRAYSIZE(kLightMaps); i++)
		debug("map room %2d left %s right %s dark %s", kLightMaps[i].room,
			  kLightMaps[i].left,
			  kLightMaps[i].right ? kLightMaps[i].right : "-",
			  kLightMaps[i].dark ? kLightMaps[i].dark : "-");
}

/**
 * Read every loadable map at a fixed grid of feet points and print the level
 * each one yields, so the mirror can check the plates themselves rather than
 * only their names.
 *
 * The overrides rooms 31, 46 and 49 put on the sampler's answer are not applied
 * here: they depend on flags the sweep has no state for, and what this is
 * checking is the map and the arithmetic over it. Room 7 is swept on whichever
 * of its two plates the light flag names, which with the switch untouched is
 * the dark one.
 */
void AlienEngine::sweepLighting() {
	for (uint i = 0; i < ARRAYSIZE(kLightMaps); i++) {
		const int room = kLightMaps[i].room;
		const int width = roomWidth(room);

		loadLightMap(room);

		for (int y = 0; y <= kLightMaxRow; y += 32) {
			for (int x = 0; x <= width; x += 40) {
				const uint half = (x >= kLightWidth) ? 1 : 0;
				const int column = half ? x - kLightWidth : x;
				const byte sample = _lightMap[half]
					? _lightMap[half][y * kLightWidth + column] : 0;
				debug("sample room %2d at %3d,%3d byte %3d level %3d", room, x, y,
					  sample, sample ? (byte)(sample << 2) : 0xff);
			}
		}
	}

	// The sweep left the last room's map behind; put the live room's back.
	loadLightMap(_room);
}

} // End of namespace Alien
