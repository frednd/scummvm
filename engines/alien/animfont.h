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

#ifndef ALIEN_ANIMFONT_H
#define ALIEN_ANIMFONT_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace Alien {

/**
 * The font the video player sets its subtitles in.
 *
 * ANIMPLAY.EXE carries a face of its own -- the game's own atlas is never
 * loaded while a clip is up -- as two tables in its data object, and the
 * subtitle renderer `sub_11da1` walks them:
 *
 *     0x30d00  code -> glyph map: u8 code, u8 yAdjust, terminated by code 0
 *     0x30de4  the glyph bank: u32, then one u32 offset per glyph, then
 *              records of i16 xOfs, i16 yOfs, i16 width, u16 rows | 0x8000,
 *              one u16 row offset per row, then the pixel runs
 *
 * A run is a signed opcode: n > 0 copies n literal bytes, n < 0 skips -n
 * pixels, and 0x80 ends the row. The rows are streamed rather than looked up:
 * the blitter reads the first word of the row table and jumps by it, which is
 * the table's own size, and every row after that follows its predecessor's
 * terminator. The rest of the table is where it jumps in when the top of a
 * glyph is clipped away. Every literal in the bank is palette index
 * 255 -- the near-white every CDA2 palette reserves for it -- so the colour of
 * a subtitle is not a runtime decision the way the game's speech colour is.
 *
 * The record's xOfs and yOfs place the glyph on the sheet it was authored on
 * and the renderer subtracts them again before it blits, so a glyph lands at
 * (pen, lineY + yAdjust) and the pen advances by width + 1. See finding #90.
 */
class AnimFont {
public:
	/// What the renderer steps by for a line break, and what a space costs.
	static const int kLineHeight = 8;
	static const int kSpaceWidth = 4;

	struct Glyph {
		byte code;					///< the byte in the subtitle text this draws
		byte yAdjust;				///< rows below the line's top the glyph starts at
		int width;
		int rows;
		Common::Array<byte> pixels;	///< width * rows, 0 where the glyph has a gap
	};

	AnimFont();

	/** Read the two tables out of ANIMPLAY.EXE. */
	bool load();

	bool isLoaded() const { return !_glyphs.empty(); }

	int lineHeight() const { return kLineHeight; }

	/** Width in pixels of one line, following the renderer's advances. */
	int measure(const Common::String &line) const;

	/** Draw one line with its pen at x and the top of the line at y. */
	void drawString(Graphics::Surface &dest, const Common::String &line, int x, int y) const;

	/// The glyphs in table order, for the video channel's dump.
	uint glyphCount() const { return _glyphs.size(); }
	const Glyph &glyph(uint index) const { return _glyphs[index]; }

private:
	static const uint32 kMapAddr = 0x30d00;
	static const uint32 kBankAddr = 0x30de4;
	static const uint32 kDataBase = 0x30000;

	/** Flatten ANIMPLAY.EXE's data object at its load address. */
	static bool readDataObject(Common::Array<byte> &out);

	bool decodeGlyph(const byte *data, uint32 size, uint32 rec, byte code, byte yAdjust);

	/// The glyph a code draws in, or 0 for one the map does not hold: the
	/// renderer's search wraps to the first entry rather than skipping.
	const Glyph *find(byte code) const;

	Common::Array<Glyph> _glyphs;
	int _byCode[256];				///< -1 where the map has no entry
};

} // End of namespace Alien

#endif
