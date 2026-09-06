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

#include "common/system.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

// Room 7's light switch, and the only way into the bedroom's contents.
//
// The bedroom registers two rectangles in the dark -- the door out and the
// switch -- and every other object in the room is guarded on [0xa6fa]
// (ovr_07_0e63:0x771), so with the light off there is nothing else to click.
// That flag also chooses the room's whole set of plates (the same overlay's
// 0x3f): while it is clear the room loads GAME7X/MSCR7X/FADE7X, the bedroom in
// the dark, and once the switch sets it the plain GAME7/MSCR7/FADE7 -- so the
// flag is "the light is on", and the room is entered with it off.
//
// The switch is not an ordinary hotspot body. Clicking it runs a hook of room
// 7's own in the resident walk unit (1021:sub_1087b, reached from the click
// path at 1021:0727 on handler_code 7), which picks a code from the fuse flag
// [0xa6fd] and the light flag [0xa6fa] and leaves it in [0xa956]; the room's
// entry 3 then reads that code at its top (0x131-0x1fb) before any of the
// object bodies run. The codes are:
//
//   4  no fuse: "I can't see a blimmin' thing here." ([0xa6fc] is latched too)
//   5  fuse in, light off: the light goes on
//   6  fuse in, light on: the light goes off, and the dark room's theme with it
//   7  the first time the light goes on, spoken after code 5's line
//
// The fuse is item 6, which comes out of room 10 ([0x33a8], a latch that ships
// set) and goes into the lab's fuse box in room 3 (its object 16, which is what
// sets [0xa6fd]) -- so the bedroom cannot be lit before both of those.
//
// One deviation, noted where it happens: the original leaves code 7 standing in
// [0xa956] for the next pass of entry 3 rather than running it with code 5, and
// the port speaks both lines from the one click. The dialog queue serialises
// them either way.

/// The switch's own object number, the one 1021:sub_1087b tests for.
static const int kSwitchObject = 9;

static const int kBedroom = 7;
static const uint16 kFuseFlag = 0xa6fd;		///< the lab's fuse is in place
static const uint16 kLightFlag = 0xa6fa;	///< the light is on, and the X plates with it
static const uint16 kDarkLatch = 0xa6fc;	///< set the first time he gropes in the dark
static const uint16 kSpokenFlag = 0xa6f7;	///< the "Loud." line is a one-shot

static const byte kLineDark = 5;			///< ROOM7.TAL: "I can't see a blimmin' thing here."
static const byte kLineLoud = 16;			///< ROOM7.TAL: "Loud."

// The switch's click, sfx_play_delayed(2, 0, 0xcb20, 0x40, -0x14, 0).
static const uint kClickSample = 2;
static const uint32 kClickRate = 0xcb20;
static const byte kClickVolume = 0x40;
static const int8 kClickPanning = -20;

/**
 * Reload the room's plates, as room 7 does when its light changes.
 *
 * The original blacks the screen out (UTIL:sub_0213e, which zeroes the DAC
 * staging block and waits one tick -- a blackout, not a fade), calls the
 * overlay's own loader for whichever set [0xa6fa] now names, and lets the tick
 * put the whole framebuffer up again ([0xa820] = 1, read at ovr_07_0e63:0xd78).
 * Only the plate and the foreground sheet change: the walk mask, the banks and
 * the animation slots belong to the room and stay as they are, which is why
 * this is not a room load.
 *
 * The loader it calls is `OBJ:sub_07b70`, the room loader's own, and it runs
 * with `[0xd12c] = 1` -- the PCX loader's "take this file's palette" gate -- so
 * the new plate's colours reach the DAC as part of the swap. That is the half
 * the port had missing: it copied the palette into `_palette` and never
 * uploaded it, so the lit bedroom's pixels went up under the dark room's
 * colours (playtest report 6).
 */
void AlienEngine::reloadPlates() {
	Graphics::Surface loaded;
	byte palette[256 * 3];
	const Common::String name = roomPlate(_room);
	if (!loadGamePCX(Common::Path(name), loaded, palette)) {
		loaded.free();
		warning("room %d: could not load the plate %s", _room, name.c_str());
		return;
	}

	// Black first, for the one tick the original spends there. Nothing is
	// redrawn under it: the pixels on screen are the old room's and the palette
	// they are shown through is dead, which is what the blackout looks like.
	uploadPalette(_palette, 0);
	g_system->updateScreen();

	_background.free();
	_background = loaded;
	memcpy(_palette, palette, sizeof(_palette));
	applyCharPalette(_room);
	loadOccluder(_room);
	loadLightMap(_room);

	// And then the new plate's palette, whole, the way the loader installs it.
	// The status line's own three entries are the game's and not the plate's,
	// so they go back over it -- the same order loadRoom settles them in. The
	// speech ink looks after itself: the per-room pass re-applies it before the
	// next line is drawn.
	uploadPalette(_palette, 0x100);
	resetLabelColors();
	_dirty = true;

	debugC(1, kDebugBedroom, "bedroom: plates are now %s", name.c_str());
}

/**
 * The switch's click: pick the code 1021:sub_1087b would leave in [0xa956],
 * then run what room 7's entry 3 does with it.
 *
 * @param anchorX  the spoken line's anchor, the clicked box as everywhere else
 * @param anchorY  ditto
 */
void AlienEngine::bedroomSwitch(int anchorX, int anchorY) {
	const bool fuse = _script.flag(kFuseFlag) != 0;
	const bool lit = _script.flag(kLightFlag) != 0;

	// The hook has three arms and no else: with no fuse and the light somehow
	// on there is no code at all, and the click does nothing.
	byte code = 0;
	if (!fuse && !lit)
		code = 4;
	else if (fuse && !lit)
		code = 5;
	else if (fuse && lit)
		code = 6;

	debugC(1, kDebugBedroom, "bedroom: switch clicked, fuse %d light %d -> code %u",
		   fuse ? 1 : 0, lit ? 1 : 0, code);
	if (!code)
		return;

	_sound.queue(kClickSample, kClickRate, kClickVolume, kClickPanning, 0);

	switch (code) {
	case 4:
		queueOutcome(_tal, kLineDark, anchorX, anchorY);
		_script.setFlag(kDarkLatch, 1);
		break;

	case 5:
		_script.setFlag(kLightFlag, 1);
		reloadPlates();

		// Code 7, which the original leaves for the next pass: the first time
		// the light comes on he says how loud the switch is.
		if (!_script.flag(kSpokenFlag))
			queueOutcome(_tal, kLineLoud, anchorX, anchorY);
		_script.setFlag(kSpokenFlag, 1);

		_anims.play(4, 1, 7, 4, 1);

		// 10c9:sub_113f0 at 0x1b4, the room's own plate routine: every frame
		// the bedroom's puzzle flags say belongs in the background is stamped
		// into the plate that was just loaded. All seven of room 7's steps are
		// guarded on the light being on, so this is the one path that runs
		// them -- and without it the lit room came up with a bare plate.
		openRoomPlate(_room);
		break;

	case 6:
		_script.setFlag(kLightFlag, 0);
		reloadPlates();
		playMusicSlot(2);
		break;

	default:
		break;
	}

	// What is clickable changed with the light, so the room's rectangles are
	// registered again before the next click can land.
	_script.buildHotspots(_room, _spots);
}

/// True for the one click the room does not answer for itself.
bool AlienEngine::isBedroomSwitch(int obj) const {
	return _room == kBedroom && obj == kSwitchObject;
}

} // End of namespace Alien
