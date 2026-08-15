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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/error.h"
#include "common/events.h"
#include "common/path.h"
#include "common/file.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/palette.h"
#include "graphics/paletteman.h"
#include "image/png.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

// Scaffolding for the first render milestone: the kitchen, its background
// plate and the first frame of the fridge animation. Room selection becomes
// table-driven once the room plate tables in the data segment are read.
static const char *const kStartBackground = "GAME10.PCX";
static const char *const kStartSprite = "FRIDGE_1.DL1";

AlienEngine::AlienEngine(OSystem *syst, const ADGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _spriteFrame(0), _dirty(true), _quit(false) {
	memset(_palette, 0, sizeof(_palette));
}

AlienEngine::~AlienEngine() {
	_screen.free();
	_background.free();
}

Common::Error AlienEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);

	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (!loadRoom(10))
		return Common::Error(Common::kReadingFailed, "Could not load the starting room");

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);

	while (!shouldQuit() && !_quit) {
		handleEvents();
		if (_dirty)
			redraw();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

bool AlienEngine::loadRoom(int room) {
	_background.free();

	if (!loadGamePCX(Common::Path(kStartBackground), _background, _palette))
		return false;

	if (!_sprite.load(Common::Path(kStartSprite)))
		return false;

	debugC(1, kDebugResource, "room %d: %dx%d plate, sprite with %u frames",
		   room, _background.w, _background.h, _sprite.frameCount());

	_spriteFrame = 0;
	_dirty = true;
	return true;
}

void AlienEngine::redraw() {
	_screen.fillRect(Common::Rect(0, 0, _screen.w, _screen.h), 0);

	if (_background.getPixels()) {
		int w = MIN<int>(_background.w, _screen.w);
		int h = MIN<int>(_background.h, _screen.h);
		for (int y = 0; y < h; y++)
			memcpy(_screen.getBasePtr(0, y), _background.getBasePtr(0, y), w);
	}

	_sprite.drawFrame(_spriteFrame, _screen);

	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
	_dirty = false;

	dumpScreen();
}

void AlienEngine::dumpScreen() {
	// Writes the composed staging buffer out so it can be diffed against the
	// renders the reverse-engineering tools produce, which is the only way to
	// check the decoders while the engine has no interactive state yet.
	if (!debugChannelSet(3, kDebugGraphics))
		return;

	Common::DumpFile out;
	if (!out.open(Common::Path("alien-screen.png"))) {
		warning("could not open the screen dump for writing");
		return;
	}

	Graphics::Palette pal(_palette, 256);
	Image::writePNG(out, _screen, pal);
}

void AlienEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		switch (event.type) {
		case Common::EVENT_KEYDOWN:
			// Frame stepping, so the DL1 decoder can be eyeballed against the
			// reference renders while the rest of the engine is missing.
			if (event.kbd.keycode == Common::KEYCODE_ESCAPE) {
				_quit = true;
			} else if (event.kbd.keycode == Common::KEYCODE_SPACE ||
					   event.kbd.keycode == Common::KEYCODE_RIGHT) {
				if (_sprite.frameCount())
					_spriteFrame = (_spriteFrame + 1) % _sprite.frameCount();
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_LEFT) {
				if (_sprite.frameCount())
					_spriteFrame = (_spriteFrame + _sprite.frameCount() - 1) % _sprite.frameCount();
				_dirty = true;
			}
			break;
		default:
			break;
		}
	}
}

} // End of namespace Alien
