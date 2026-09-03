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


// The palette fades a room change is made of.
//
// UTIL keeps two scalers and one uploader. sub_01f37(level) walks the 256
// triples of the live palette at 271a:0xe2e6, multiplies each byte by a level
// of 0..0xff and keeps the high byte of the product, staging the result at
// 0x79c4; sub_01fe4(level) does the same over a second copy of the palette at
// 271a:0xe5e6; and wait_tick_and_palette waits one master tick and pushes all
// 768 staged bytes at the DAC through 0x3c8. Six wrappers sit on that pair,
// and two of them are the room change:
//
//   OBJ:sub_0879a   ends the room being left and copies the live palette into
//                   the second buffer -- the picture still on the screen.
//   OBJ:sub_07db3   is called from room init (OBJ:sub_08567) once the new
//                   room's plate and palette are in place. [0xa883] is set at
//                   the tail of every room init and never cleared, so the test
//                   there always passes: sub_02259 fades that copy out over
//                   0x10 steps of -0x10, and only then are the new room's
//                   pixels uploaded to the VGA.
//   LOGIC:sub_132e4 and its identical twin 1021:sub_10c33 are called from the
//                   tail of every room's tick loop under `cmp [0xa884], 0`.
//                   Room init clears that byte and these two set it, so this
//                   runs exactly once per entry, on the first composed frame:
//                   sub_02163 fades the live palette in over 0x11 steps of
//                   +0x10. Twenty-nine rooms name one or the other, one call
//                   each.
//
// So a room change is: fade the old picture down, swap the pixels under a dark
// palette, then raise the new one. At the ~70 Hz tick that is a sixth of a
// second each way.
//
// The four wrappers this port does not use are the same shape: sub_02163's
// sibling util_func_2aa fades in without the bar reload, sub_021f9 fades out
// without it, sub_02229 fades out slowly over 0x40 steps of -4 (the escape
// pod), and sub_0213e cuts to black in a single upload (the bedroom light).

#include "common/events.h"
#include "common/system.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// sub_02259 and sub_02163: sixteen steps down, seventeen up, 0x10 at a time.
static const int kFadeStep = 0x10;
static const int kFadeOutSteps = 0x10;
static const int kFadeInSteps = 0x11;

/**
 * Stage the palette at a brightness and push it at the screen.
 *
 * The level is the original's: 0x100 is the palette untouched and 0 is black,
 * and the product's high byte is what reaches the DAC. The one departure is
 * the top of the range. sub_01f37 clamps its argument to 0xff, so the step the
 * fade in ends on leaves every component a 255th short of what the plate
 * carries and nothing ever puts the missing part back -- an artefact of the
 * level being one byte wide. Here the last step is a full 0x100, so a room
 * settles on exactly the palette its plate was authored with.
 */
void AlienEngine::uploadPalette(const byte *source, int level) {
	byte scaled[256 * 3];
	for (uint i = 0; i < ARRAYSIZE(scaled); i++)
		scaled[i] = (byte)((source[i] * level) >> 8);

	g_system->getPaletteManager()->setPalette(scaled, 0, 256);
}

/**
 * Wait the one master tick each step of a fade costs (UTIL:wait_tick).
 *
 * The game loop is not running while a fade is: the original spins on the
 * interrupt counter with everything else stopped, so the port sleeps rather
 * than stepping its own clock. Events are still drained, or the window would
 * stop answering for the length of the fade.
 */
static void fadeWait() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
	}

	g_system->updateScreen();
	g_system->delayMillis(AlienEngine::kMasterTickMillis);
}

void AlienEngine::fadeOut() {
	// The first room of a session has nothing to fade: no room has been loaded,
	// the screen is black and the palette is the memset one the constructor
	// left.
	if (_room == 0)
		return;

	debugC(1, kDebugRooms, "fade: room %d out", _room);

	int level = 0xff;
	for (int step = 0; step < kFadeOutSteps && !shouldQuit(); step++) {
		uploadPalette(_palette, level);
		fadeWait();
		level -= kFadeStep;
	}

	// Which is where it stops: sixteen steps of 0x10 from 0xff leave 0x0f, not
	// zero, so the new room's pixels go up under a palette that is dark rather
	// than dead. The fade in starts from black regardless, and the difference
	// is one frame of a very dim new room.
}

void AlienEngine::fadeIn() {
	_fadePending = false;

	debugC(1, kDebugRooms, "fade: room %d in", _room);

	// Seventeen steps of 0x10 from zero, so the last of them is a full 0x100.
	int level = 0;
	for (int step = 0; step < kFadeInSteps && !shouldQuit(); step++) {
		uploadPalette(_palette, level);
		fadeWait();
		level += kFadeStep;
	}

	// However far the loop got -- a quit can cut it short -- the room owns its
	// palette from here.
	uploadPalette(_palette, 0x100);
}

} // End of namespace Alien
