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

#include "common/archive.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/fs.h"
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
static const char *const kStartScript = "ROOM10.TAL";

// One install ships all four text languages side by side. Picking the set is a
// launcher option the engine does not have yet, so the English tree is wired up
// for now.
static const char *const kTextDir = "TALFILES/ENG";
static const char *const kLabelDir = "NAMEROOM/ENG";

// docs/dialog_system.md 3A: the block is placed against the midpoint of the
// speaking object's bounding box. Until hotspots exist, the fridge's own centre
// stands in for it.
static const int kAnchorX = 96;
static const int kAnchorY = 90;

AlienEngine::AlienEngine(OSystem *syst, const ADGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _spriteFrame(0), _dialogId(1),
		_dialogBand(false), _dirty(true), _quit(false) {
	memset(_palette, 0, sizeof(_palette));
}

AlienEngine::~AlienEngine() {
	_screen.free();
	_background.free();
}

Common::Error AlienEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);

	// The text and label trees are subdirectories, so they need registering
	// before Common::File can reach into them by name.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, kTextDir);
	SearchMan.addSubDirectoryMatching(gameDataDir, kLabelDir);

	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (!_font.load())
		return Common::Error(Common::kReadingFailed, "Could not load the font");

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

	if (!_tal.load(Common::Path(kStartScript)))
		return false;

	// White is what the dialog unit resets the text entry to before every
	// line; the per-speaker colours are set by the room code that triggers
	// the line, and none of that exists yet.
	setTextColor(0x3F, 0x3F, 0x3F);

	debugC(1, kDebugResource, "room %d: %dx%d plate, sprite with %u frames, %u dialog entries",
		   room, _background.w, _background.h, _sprite.frameCount(), _tal.usedEntries());

	_spriteFrame = 0;
	_dirty = true;
	return true;
}

void AlienEngine::setTextColor(byte r, byte g, byte b) {
	// The original reprograms a single DAC entry per speaker; the values in
	// the disassembly are the VGA 6-bit ones, so they are widened here.
	_palette[Font::kInkColor * 3 + 0] = r * 255 / 63;
	_palette[Font::kInkColor * 3 + 1] = g * 255 / 63;
	_palette[Font::kInkColor * 3 + 2] = b * 255 / 63;
}

void AlienEngine::drawSpeech(const TalFile::Entry &entry, int anchorX, int anchorY) {
	// docs/dialog_system.md 3A. Lines are individually centred on the anchor,
	// and the block is clamped to keep it inside the playfield.
	const int lines = (int)entry.lines.size();
	if (!lines)
		return;

	int top = anchorY - lines * 10;
	if (top < 14)
		top = 14;
	if (top + lines * 11 > 155)
		top = 155 - lines * 11;

	for (int i = 0; i < lines; i++) {
		const Common::String &line = entry.lines[i];
		int x = anchorX - _font.measure(line) / 2;
		if (x < 0)
			x = 0;
		_font.drawString(_screen, line, x, top + i * 11);
	}
}

void AlienEngine::drawBand(const TalFile::Entry &entry) {
	// docs/dialog_system.md 3B: the narration layout, left margin 20, four
	// lines at most, 14 px apart with the last line always at 140.
	const int lines = MIN<int>(entry.lines.size(), 4);
	if (!lines)
		return;

	const int firstY = 140 - (lines - 1) * 14;
	for (int i = 0; i < lines; i++)
		_font.drawString(_screen, entry.lines[i], 20, firstY + i * 14);
}

void AlienEngine::showDialog(uint id) {
	_dialogId = id;
	_dirty = true;

	const TalFile::Entry &e = _tal.entry(id);
	debugC(1, kDebugGraphics, "dialog %u: %u lines, count byte %d",
		   id, e.lines.size(), e.lineCount);
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

	const TalFile::Entry &dialog = _tal.entry(_dialogId);
	if (_dialogBand)
		drawBand(dialog);
	else
		drawSpeech(dialog, kAnchorX, kAnchorY);

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
			} else if (event.kbd.keycode == Common::KEYCODE_DOWN) {
				showDialog((_dialogId + 1) % TalFile::kEntryCount);
			} else if (event.kbd.keycode == Common::KEYCODE_UP) {
				showDialog((_dialogId + TalFile::kEntryCount - 1) % TalFile::kEntryCount);
			} else if (event.kbd.keycode == Common::KEYCODE_TAB) {
				_dialogBand = !_dialogBand;
				_dirty = true;
			}
			break;
		default:
			break;
		}
	}
}

} // End of namespace Alien
