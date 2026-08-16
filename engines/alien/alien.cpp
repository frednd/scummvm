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

// The kitchen is where the game itself starts, and it is the room the render
// milestones were checked against.
static const int kStartRoom = 10;

// One install ships all four text languages side by side. Picking the set is a
// launcher option the engine does not have yet, so the English tree is wired up
// for now.
// NAMEROOM/ENG holds the hover labels and is not registered yet: both trees end
// in the same directory name, and SearchMan keys an archive by that name.
static const char *const kTextDir = "TALFILES/ENG";

// docs/dialog_system.md 3A: the block is placed against the midpoint of the
// speaking object's bounding box. Until hotspots exist, the fridge's own centre
// stands in for it.
static const int kAnchorX = 96;
static const int kAnchorY = 90;

AlienEngine::AlienEngine(OSystem *syst, const ADGameDescription *gameDesc) :
		Engine(syst), _gameDescription(gameDesc), _spriteFrame(0), _spriteBank(0),
		_room(0), _secondPlate(false), _walkX(0), _walkY(0), _showWalk(false),
		_dialogId(1), _dialogBand(false), _dirty(true), _quit(false) {
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

	_screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());

	if (!_tables.load())
		return Common::Error(Common::kReadingFailed, "Could not read the tables in GAME.EXE");

	if (!_overlays.load())
		return Common::Error(Common::kReadingFailed, "Could not index the scene overlays");

	if (!_font.load())
		return Common::Error(Common::kReadingFailed, "Could not load the font");

	if (!loadRoom(kStartRoom))
		return Common::Error(Common::kReadingFailed, "Could not load the starting room");

	while (!shouldQuit() && !_quit) {
		handleEvents();
		if (_dirty)
			redraw();
		g_system->updateScreen();
		g_system->delayMillis(10);
	}

	return Common::kNoError;
}

bool AlienEngine::loadRoom(int room, bool secondPlate) {
	const Common::String &plate = secondPlate ? _tables.secondPlate(room)
											  : _tables.background(room);
	if (plate.empty()) {
		debugC(1, kDebugResource, "room %d has no %s plate", room,
			   secondPlate ? "second" : "background");
		return false;
	}

	// Eleven of the names in the table belong to cut rooms whose art never
	// shipped, so a failure here is expected and must leave the current room
	// standing.
	Graphics::Surface loaded;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(plate), loaded, palette)) {
		loaded.free();
		return false;
	}

	_background.free();
	_background = loaded;
	memcpy(_palette, palette, sizeof(_palette));

	// The sprite banks and the dialog file are named by the room's own scene
	// overlay. Rooms driven from a resident segment have no overlay, and those
	// keep an empty manifest until their code is understood.
	_overlays.readRoom(room, _assets);
	loadSpriteBank(0);

	// The walk mask and the node ring, so a click can be routed. Rooms with no
	// KIERRA files have no free movement at all, and those keep an empty mask.
	_walk.load(room, _assets);
	_route.count = 0;

	// Ben is not on screen yet, so a route has to start somewhere: the first
	// walk node stands in for him until the character sprites are ported.
	if (_walk.nodes().count()) {
		_walkX = _walk.nodes().x(0);
		_walkY = _walk.nodes().y(0);
	} else {
		_walkX = kScreenWidth / 2;
		_walkY = 140;
	}

	// Most rooms name a room<n>.tal, but several speak through a shared file,
	// and the ones without an overlay fall back to the naming convention.
	Common::Path script(_assets.script);
	if (_assets.script.empty())
		script = Common::Path(Common::String::format("ROOM%d.TAL", room));

	if (Common::File::exists(script))
		_tal.load(script);
	else
		_tal.unload();

	// White is what the dialog unit resets the text entry to before every
	// line; the per-speaker colours are set by the room code that triggers
	// the line, and none of that exists yet.
	setTextColor(0x3F, 0x3F, 0x3F);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);

	debugC(1, kDebugResource, "room %d %c: %s, %dx%d plate, %u sprite frames, %u dialog entries",
		   room, secondPlate ? 'B' : 'A', plate.c_str(), _background.w, _background.h,
		   _sprite.frameCount(), _tal.usedEntries());

	_room = room;
	_secondPlate = secondPlate;
	_spriteFrame = 0;
	_dialogId = 1;
	_dirty = true;
	return true;
}

void AlienEngine::stepRoom(int delta) {
	// Walks past the slots that hold no plate — those are the rooms that
	// never had a background of their own, see docs/rooms.md.
	for (int i = 0; i < StaticTables::kRoomCount; i++) {
		int room = (_room - 1 + delta * (i + 1)) % StaticTables::kRoomCount;
		if (room < 0)
			room += StaticTables::kRoomCount;
		if (loadRoom(room + 1))
			return;
	}
}

void AlienEngine::loadSpriteBank(uint bank) {
	// One bank at a time: which of a room's banks are on screen, and at which
	// frame, is what the room's own code decides, and none of that is ported
	// yet, so drawing them all at once would only pile doors on top of doors.
	_sprite.unload();
	_spriteBank = bank;
	_spriteFrame = 0;
	_dirty = true;

	if (bank >= _assets.spriteCount)
		return;

	if (!_sprite.load(Common::Path(_assets.sprites[bank])))
		return;

	debugC(1, kDebugResource, "sprite bank %u/%u: %s, %u frames", bank + 1,
		   _assets.spriteCount, _assets.sprites[bank].c_str(), _sprite.frameCount());
}

void AlienEngine::stepSpriteBank(int delta) {
	if (!_assets.spriteCount)
		return;

	const int count = (int)_assets.spriteCount;
	int bank = ((int)_spriteBank + delta) % count;
	if (bank < 0)
		bank += count;
	loadSpriteBank((uint)bank);
}

void AlienEngine::walkTo(int x, int y) {
	// The original converts the click into a walk target before routing --
	// walk_target = click + (10, 64) -- but that offset belongs to the click
	// pipeline, which is not ported, so the point is taken as it stands here.
	if (!_walk.plotRoute(_walkX, _walkY, x, y, _route)) {
		debugC(1, kDebugGraphics, "walk to %d,%d: room %d has no walk mask", x, y, _room);
		return;
	}

	debugC(1, kDebugGraphics, "walk %d,%d -> %d,%d: %u waypoints, target %s",
		   _walkX, _walkY, x, y, _route.count,
		   _walk.mask().blocked(x, y) ? "blocked" : "walkable");

	// The walk itself is animation the character sprites have to drive, so the
	// route only moves the stand-in to its end for now.
	if (_route.count) {
		_walkX = _route.points[_route.count - 1].x;
		_walkY = _route.points[_route.count - 1].y;
	}
	_dirty = true;
}

void AlienEngine::drawWalkOverlay() {
	// A debug view, not something the game ever drew: blocked pixels stippled,
	// the node ring marked, and the last plotted route joined up. The ink
	// colour is the one the text layer already reprograms, so it stands out
	// against any plate.
	byte *pixels = (byte *)_screen.getPixels();

	for (int y = 0; y < _screen.h; y++) {
		for (int x = 0; x < _screen.w; x++) {
			if (((x + y) & 3) == 0 && _walk.mask().blocked(x, y))
				pixels[y * _screen.pitch + x] = Font::kInkColor;
		}
	}

	for (uint i = 0; i < _walk.nodes().count(); i++) {
		const int nx = _walk.nodes().x(i);
		const int ny = _walk.nodes().y(i);
		for (int d = -2; d <= 2; d++) {
			if (nx + d >= 0 && nx + d < _screen.w)
				pixels[ny * _screen.pitch + nx + d] = Font::kInkColor;
			if (ny + d >= 0 && ny + d < _screen.h)
				pixels[(ny + d) * _screen.pitch + nx] = Font::kInkColor;
		}
	}

	for (uint i = 1; i < _route.count; i++) {
		const WalkRoute::Point &a = _route.points[i - 1];
		const WalkRoute::Point &b = _route.points[i];
		const int steps = MAX(ABS(b.x - a.x), ABS(b.y - a.y));
		for (int s = 0; s <= steps; s++) {
			const int x = steps ? a.x + (b.x - a.x) * s / steps : a.x;
			const int y = steps ? a.y + (b.y - a.y) * s / steps : a.y;
			if (x >= 0 && x < _screen.w && y >= 0 && y < _screen.h)
				pixels[y * _screen.pitch + x] = Font::kInkColor;
		}
	}
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

	if (_showWalk)
		drawWalkOverlay();

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
			} else if (event.kbd.keycode == Common::KEYCODE_PAGEDOWN) {
				stepRoom(1);
			} else if (event.kbd.keycode == Common::KEYCODE_PAGEUP) {
				stepRoom(-1);
			} else if (event.kbd.keycode == Common::KEYCODE_RIGHTBRACKET) {
				stepSpriteBank(1);
			} else if (event.kbd.keycode == Common::KEYCODE_LEFTBRACKET) {
				stepSpriteBank(-1);
			} else if (event.kbd.keycode == Common::KEYCODE_w) {
				_showWalk = !_showWalk;
				_dirty = true;
			} else if (event.kbd.keycode == Common::KEYCODE_b) {
				// The second plate is the B state, the right half of a wide
				// room or the close-up, depending on the room.
				loadRoom(_room, !_secondPlate);
			}
			break;
		case Common::EVENT_LBUTTONDOWN:
			walkTo(event.mouse.x, event.mouse.y);
			break;
		default:
			break;
		}
	}
}

} // End of namespace Alien
