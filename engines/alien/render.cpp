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
#include "image/png.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

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

void AlienEngine::drawSpotOverlay() {
	// The companion to drawWalkOverlay: every rectangle entry 1 registered for
	// the room in the state the game is in, which is exactly what the cursor can
	// find. Same ink colour, for the same reason -- it is the one entry the text
	// layer reprograms, so it reads against any plate.
	//
	// The boxes are room-space, like the ones updateHover tests, so the scroll
	// offset comes off again here. Every box is dashed and the hovered one is
	// solid: where two overlap it is the one registered last that answers, so
	// which of them the cursor actually found is half of what the view is for.
	byte *pixels = (byte *)_screen.getPixels();
	const int bottom = MIN<int>(kPlayfieldBottom, _screen.h - 1);

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		const int x1 = spot.x1 - _scrollX, x2 = spot.x2 - _scrollX;
		const int y1 = spot.y1, y2 = spot.y2;
		const bool hovered = (int)i == _hover;

		// Dashed, so overlapping boxes stay legible; the hovered one is solid.
		for (int x = MAX(x1, 0); x <= MIN(x2, _screen.w - 1); x++) {
			if (!hovered && (x & 1))
				continue;
			if (y1 >= 0 && y1 <= bottom)
				pixels[y1 * _screen.pitch + x] = Font::kInkColor;
			if (y2 >= 0 && y2 <= bottom)
				pixels[y2 * _screen.pitch + x] = Font::kInkColor;
		}

		for (int y = MAX(y1, 0); y <= MIN(y2, bottom); y++) {
			if (!hovered && (y & 1))
				continue;
			if (x1 >= 0 && x1 < _screen.w)
				pixels[y * _screen.pitch + x1] = Font::kInkColor;
			if (x2 >= 0 && x2 < _screen.w)
				pixels[y * _screen.pitch + x2] = Font::kInkColor;
		}
	}
}

void AlienEngine::bakeSlots() {
	if (!_anims.bakePending())
		return;

	if (_background.w <= kScreenWidth) {
		_anims.bake(_background, _scrollX, _clipBottom);
		return;
	}

	// Only the window can change: the blit is clipped to it.
	const int x0 = MAX(_scrollX, 0), x1 = MIN<int>(_scrollX + kScreenWidth, _background.w);
	Common::Array<byte> under;
	under.resize((x1 - x0) * _background.h);
	for (int y = 0; y < _background.h; y++)
		memcpy(&under[y * (x1 - x0)], _background.getBasePtr(x0, y), x1 - x0);

	_anims.bake(_background, _scrollX, _clipBottom);

	if (_bakedPlate != _background.getPixels()) {
		_baked.clear();
		_bakedPlate = _background.getPixels();
	}
	for (int y = 0; y < _background.h; y++) {
		const byte *row = (const byte *)_background.getBasePtr(x0, y);
		for (int x = x0; x < x1; x++) {
			const byte old = under[y * (x1 - x0) + x - x0];
			if (row[x - x0] == old)
				continue;
			BakedPixel p;
			p.x = (int16)x;
			p.y = (int16)y;
			p.under = old;
			_baked.push_back(p);
		}
	}
}

void AlienEngine::dropBakedOutOfView() {
	if (_baked.empty())
		return;

	// A new plate since: nothing of the old one's bakes is left to undo.
	if (_bakedPlate != _background.getPixels()) {
		_baked.clear();
		return;
	}

	// Later bakes sit on top of earlier ones, so the list is undone newest
	// first.
	for (int i = (int)_baked.size() - 1; i >= 0; i--) {
		const BakedPixel &p = _baked[i];
		if (p.x >= _scrollX && p.x < _scrollX + kScreenWidth)
			continue;
		*(byte *)_background.getBasePtr(p.x, p.y) = p.under;
		_baked.remove_at(i);
	}
}

void AlienEngine::redraw() {
	_screen.fillRect(Common::Rect(0, 0, _screen.w, _screen.h), 0);
	dropBakedOutOfView();

	if (_background.getPixels()) {
		int w = MIN<int>(_background.w - _scrollX, _screen.w);
		int h = MIN<int>(_background.h, _screen.h);
		for (int y = 0; y < h; y++)
			memcpy(_screen.getBasePtr(0, y), _background.getBasePtr(_scrollX, y), w);
	}

	// The animation slots come first: they are the room's own furniture, and the
	// character walks in front of them.
	_anims.draw(_screen, _scrollX, _clipBottom);

	// What the rooms draw by hand over the slots: room 21's puddle
	// (yodle.cpp).
	for (const Room *r : _rooms)
		r->drawOverSlots(_screen);

	// A cutscene is the record's plate and its own slots and nothing else: the
	// character is not in it, and the original hides the bar for its duration.
	if (!_cutscene) {
		// The room's DL2 sprite sheets are a viewer, not part of the room, and
		// nothing here draws one: a loose object in the world is painted into
		// the room's own plate, and taking it plays the patch frame that covers
		// it over -- `OBJ:sprite_add` is the *inventory* add, not
		// a world sprite. Drawing a sheet anyway put a door in the entrance hall
		// that the game only shows much later, in colours the hall's plate does
		// not use. It stays behind the anim channel, where the arrow keys page
		// through the banks.
		if (debugChannelSet(-1, kDebugAnim))
			_sprite.drawFrame(_spriteFrame, _screen, _scrollX, _clipBottom);

		// [0xa94d]: while a room plays the character's own action on one of its
		// slots, the walker is not drawn at all (lab.cpp).
		// What the rooms draw under him: room 58's force field, in the
		// corridor (jailguard.cpp).
		for (const Room *r : _rooms)
			r->drawUnderCharacter(_screen);

		if (_drawCharacter) {
			// OBJ:sub_06466 reads [0xa73a] on every draw: room 31's cliff and
			// room 41, whose ledges were drawn 15 px left of where the
			// pick-axe throw has him.
			if (_script.flag(0xa73a) == 1)
				_ben.setLedgeDraw(_room == 41 ? 0x0f : 0x0b, -3);
			else
				_ben.setLedgeDraw(0, 0);
			_ben.draw(_screen, _scrollX, _clipBottom);
		}

		// And the foreground the room authored over him, which is the whole of
		// the original's depth model (occlusion.h). Only over him: while a bank
		// plays him instead, the walker's box is not where anything of his is
		// drawn, and stamping there put a scrap of the mailbox's lamp over
		// PAL_BLOW's whiteout.
		if (_drawCharacter)
			applyOcclusion();

		// And over him: room 58's field in the cell, and its guard.
		for (const Room *r : _rooms)
			r->drawOverCharacter(_screen);

		// Room 46's bubbles over all of that, and then the water, which moves
		// the whole playfield (diving.cpp).
		drawDivingWater(_screen);

		if (_showWalk)
			drawWalkOverlay();

		if (_showSpots)
			drawSpotOverlay();

		// The bar sits below the playfield, so it goes on after the room but
		// before the text layer, which is what the original's redraw order
		// comes to. While a conversation is up the bar is not there at all:
		// OBJ:sub_09389 clears those forty rows and the options are listed in
		// them instead.
		if (_chat.isActive()) {
			_chat.draw(_chatFont, _screen);
		} else if (_barHidden) {
			_screen.fillRect(Common::Rect(0, kPlayfieldBottom + 1, _screen.w, _screen.h), 0);
		} else {
			_inventory.draw(_tables, _screen, _hoverSlot, _hoverArrow, _hoverMenu,
							_heldItem);
			drawLabel();
		}
	}

	if (_speech) {
		const TalFile::Entry &dialog = _speechCustom
			? _speechEntry
			: (_speechTal ? _speechTal : &_tal)->entry(_dialogId);
		if (_dialogBand)
			drawBand(dialog);
		else
			drawSpeech(dialog, _speechX, _speechY);
	}

	g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
	_dirty = false;

	dumpScreen();
}

void AlienEngine::dumpScreen(const Common::String &name) {
	// Writes the composed staging buffer out to an image file. A named call
	// (from the play channel's "snap" command) always writes; the default,
	// unnamed call is gated on the graphics channel.
	if (name.empty() && !debugChannelSet(3, kDebugGraphics))
		return;

	Common::DumpFile out;
	const Common::String file = name.empty() ? Common::String("alien-screen.png") : name;
	if (!out.open(Common::Path(file))) {
		warning("could not open the screen dump for writing");
		return;
	}

	Graphics::Palette pal(_palette, 256);
	Image::writePNG(out, _screen, pal);
}

} // End of namespace Alien
