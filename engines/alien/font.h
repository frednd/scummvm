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

#ifndef ALIEN_FONT_H
#define ALIEN_FONT_H

#include "common/scummsys.h"
#include "graphics/surface.h"

namespace Common {
class String;
}

namespace Alien {

/**
 * The game's proportional bitmap font.
 *
 * The glyph bitmaps live in OBJFILE.PCX, the same plate the cursor arrow is
 * cut from; at runtime the original keeps it in EMS page 4. The metrics are
 * three parallel tables hardcoded in the executable's data segment rather than
 * in any data file, so they are read straight out of GAME.EXE.
 *
 * Text is CP850: codes 0xB5..0xB7 draw as A-acute, A-circumflex, A-grave,
 * which is the CP850 assignment and not the CP437 one.
 */
class Font {
public:
	static const byte kInkColor = 65;		///< the one palette entry speaker color recolors

	/**
	 * Which of the two metric sets in the executable to read. The tables are
	 * laid out identically and sit 0x330 bytes apart: the speech font is ten
	 * rows tall and overlaps its glyphs by a pixel, the label font used for the
	 * status line is eight rows tall and does not.
	 */
	enum Variant {
		kSpeech,
		kLabel
	};

	Font();
	~Font();

	/** Read the metric tables from the executable and the atlas from the PCX. */
	bool load(Variant variant = kSpeech);

	int glyphHeight() const { return _variant == kLabel ? 8 : 10; }

	bool isLoaded() const { return _atlas.getPixels() != nullptr; }

	/** Width in pixels of a CP850 string, following the blitter's advances. */
	int measure(const byte *text, uint length) const;
	int measure(const Common::String &text) const;

	/**
	 * Draw a CP850 string with its top left corner at (x, y). Glyphs are
	 * copied as palette indices, so the speaker color follows from whatever
	 * the palette holds at kInkColor.
	 */
	void drawString(Graphics::Surface &dest, const byte *text, uint length, int x, int y) const;
	void drawString(Graphics::Surface &dest, const Common::String &text, int x, int y) const;

private:
	struct Glyph {
		uint16 x;		///< source column in the atlas
		byte y;			///< source row in the atlas
		byte width;		///< drawn width; the pen then advances width - 2
		bool valid;
	};

	static const uint kGlyphCount = 256;

	bool readMetrics();
	int advance(const Glyph &g) const { return _variant == kLabel ? g.width : g.width - 2; }

	Variant _variant;
	Glyph _glyphs[kGlyphCount];
	Graphics::Surface _atlas;
	byte _atlasPalette[256 * 3];
};

} // End of namespace Alien

#endif
