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

#include "common/debug.h"
#include "common/file.h"
#include "common/path.h"
#include "common/str.h"

#include "alien/detection.h"
#include "alien/font.h"
#include "alien/resources.h"

namespace Alien {

// The metric tables sit in data segment 0x271A, which starts at this file
// offset in the shipped executable. The detection entry pins GAME.EXE by MD5,
// so hardcoding the offsets is safe for every build the engine accepts.
static const uint32 kDataSegment = 0x29F70;
static const uint32 kTableX = 0x1E6A;		///< u16 per character
static const uint32 kTableY = 0x2022;		///< u8 per character
static const uint32 kTableW = 0x20EE;		///< u8 per character

// Below space nothing is ever printed, and past 0xEB the tables hold junk that
// would address outside the atlas.
static const uint kFirstCode = 0x20;
static const uint kLastCode = 0xEB;

static const int kAtlasWidth = 320;
static const int kAtlasHeight = 200;

Font::Font() {
	memset(_glyphs, 0, sizeof(_glyphs));
	memset(_atlasPalette, 0, sizeof(_atlasPalette));
}

Font::~Font() {
	_atlas.free();
}

bool Font::readMetrics() {
	Common::File exe;
	if (!exe.open(Common::Path("GAME.EXE"))) {
		warning("Alien::Font: could not open GAME.EXE for the glyph metrics");
		return false;
	}

	uint valid = 0;
	for (uint c = kFirstCode; c <= kLastCode; c++) {
		exe.seek(kDataSegment + kTableX + c * 2);
		uint16 x = exe.readUint16LE();
		exe.seek(kDataSegment + kTableY + c);
		byte y = exe.readByte();
		exe.seek(kDataSegment + kTableW + c);
		byte w = exe.readByte();

		if (exe.eos() || exe.err()) {
			warning("Alien::Font: GAME.EXE is too short for the metric tables");
			return false;
		}

		// (0, 0, 1) is the filler the tables carry for codes the game never
		// prints; anything reaching outside the atlas is not a glyph either.
		if (w <= 1 && x == 0 && y == 0)
			continue;
		if ((int)x + w + 1 > kAtlasWidth || (int)y + kGlyphHeight > kAtlasHeight)
			continue;

		_glyphs[c].x = x;
		_glyphs[c].y = y;
		_glyphs[c].width = w + 1;
		_glyphs[c].valid = true;
		valid++;
	}

	debugC(1, kDebugResource, "font: %u glyphs", valid);
	return valid > 0;
}

bool Font::load() {
	if (!readMetrics())
		return false;

	if (!loadGamePCX(Common::Path("OBJFILE.PCX"), _atlas, _atlasPalette))
		return false;

	if (_atlas.w < kAtlasWidth || _atlas.h < kAtlasHeight) {
		warning("Alien::Font: OBJFILE.PCX is %dx%d, too small to be the atlas",
				_atlas.w, _atlas.h);
		_atlas.free();
		return false;
	}

	return true;
}

int Font::measure(const byte *text, uint length) const {
	int width = 0;
	for (uint i = 0; i < length; i++) {
		const Glyph &g = _glyphs[text[i]];
		if (g.valid)
			width += g.width - 2;
	}
	return width;
}

int Font::measure(const Common::String &text) const {
	return measure((const byte *)text.c_str(), text.size());
}

void Font::drawString(Graphics::Surface &dest, const byte *text, uint length, int x, int y) const {
	if (!isLoaded())
		return;

	int pen = x;
	for (uint i = 0; i < length; i++) {
		const Glyph &g = _glyphs[text[i]];
		if (!g.valid)
			continue;

		for (int row = 0; row < kGlyphHeight; row++) {
			int dy = y + row;
			if (dy < 0 || dy >= dest.h)
				continue;

			const byte *src = (const byte *)_atlas.getBasePtr(g.x, g.y + row);
			byte *dst = (byte *)dest.getBasePtr(0, dy);
			for (int col = 0; col < g.width; col++) {
				int dx = pen + col;
				if (dx < 0 || dx >= dest.w)
					continue;
				// Index 0 is the atlas background; the glyph itself is drawn
				// in the ink index plus one darker index for its shadow.
				if (src[col])
					dst[dx] = src[col];
			}
		}

		// One pixel of deliberate overlap between neighbours.
		pen += g.width - 2;
	}
}

void Font::drawString(Graphics::Surface &dest, const Common::String &text, int x, int y) const {
	drawString(dest, (const byte *)text.c_str(), text.size(), x, y);
}

} // End of namespace Alien
