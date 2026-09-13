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
#include "common/textconsole.h"

#include "alien/animfont.h"
#include "alien/detection.h"

namespace Alien {

static const char *const kPlayerExe = "ANIMPLAY.EXE";

// The glyph bank's records carry their row count in the low fifteen bits; the
// top bit says the row offsets are 32-bit instead of 16-bit. This font has no
// such record -- its tallest glyph is eleven rows -- but the blitter tests the
// bit, so it is named here rather than masked away silently.
static const uint16 kWideRows = 0x8000;

AnimFont::AnimFont() {
	for (int i = 0; i < 256; i++)
		_byCode[i] = -1;
}

/**
 * Flatten ANIMPLAY.EXE's read/write object at the address it loads to.
 *
 * ANIMPLAY is the odd one out among the shipped binaries: a 32-bit Watcom build
 * behind a DOS/4GW stub, so it is an LE image rather than the real-mode MZ the
 * rest of the game is. Only the second object is wanted here, and its pages sit
 * back to back from the header's data offset -- the page map is the identity
 * for this file, as tools/animplay_disasm.py also assumes -- and no fixup lands
 * inside the font tables, so none has to be applied.
 */
bool AnimFont::readDataObject(Common::Array<byte> &out) {
	Common::File exe;
	if (!exe.open(Common::Path(kPlayerExe))) {
		warning("Alien::AnimFont: could not open %s for the subtitle font", kPlayerExe);
		return false;
	}

	exe.seek(0x3C);
	const uint32 leAt = exe.readUint32LE();
	exe.seek(leAt);
	if (exe.readByte() != 'L' || exe.readByte() != 'E') {
		warning("Alien::AnimFont: no LE header in %s", kPlayerExe);
		return false;
	}

	exe.seek(leAt + 0x14);
	const uint32 pageCountTotal = exe.readUint32LE();
	exe.seek(leAt + 0x28);
	const uint32 pageSize = exe.readUint32LE();
	const uint32 lastPageSize = exe.readUint32LE();
	exe.seek(leAt + 0x40);
	const uint32 objTable = leAt + exe.readUint32LE();
	const uint32 objCount = exe.readUint32LE();
	exe.seek(leAt + 0x80);
	const uint32 dataPages = exe.readUint32LE();

	if (exe.eos() || exe.err() || pageSize == 0 || objCount == 0 || objCount > 16) {
		warning("Alien::AnimFont: %s: implausible LE header", kPlayerExe);
		return false;
	}

	for (uint32 i = 0; i < objCount; i++) {
		exe.seek(objTable + i * 24);
		const uint32 vsize = exe.readUint32LE();
		const uint32 base = exe.readUint32LE();
		exe.readUint32LE();						// flags
		const uint32 firstPage = exe.readUint32LE();
		const uint32 pageCount = exe.readUint32LE();
		if (base != kDataBase)
			continue;

		out.resize(vsize);
		memset(out.begin(), 0, vsize);
		for (uint32 p = 0; p < pageCount; p++) {
			const uint32 at = dataPages + (firstPage + p - 1) * pageSize;
			const uint32 dst = p * pageSize;
			if (dst >= vsize)
				break;
			// The image's last page is short of a full one, and it is the
			// last page of this object: reading a whole page there runs off
			// the end of the file.
			const uint32 stored = (firstPage + p == pageCountTotal) ? lastPageSize : pageSize;
			const uint32 want = MIN<uint32>(stored, vsize - dst);
			exe.seek(at);
			if (exe.read(out.begin() + dst, want) != want) {
				warning("Alien::AnimFont: %s is short of page %u", kPlayerExe, firstPage + p);
				return false;
			}
		}
		return true;
	}

	warning("Alien::AnimFont: %s has no object at %06x", kPlayerExe, kDataBase);
	return false;
}

bool AnimFont::decodeGlyph(const byte *data, uint32 size, uint32 rec, byte code, byte yAdjust) {
	if (rec + 8 > size)
		return false;

	const int width = (int16)READ_LE_UINT16(data + rec + 4);
	const uint16 height = READ_LE_UINT16(data + rec + 6);
	const int rows = height & ~kWideRows;
	if (height & kWideRows) {
		warning("Alien::AnimFont: glyph %02x wants 32-bit row offsets", code);
		return false;
	}
	if (width <= 0 || width > 64 || rows <= 0 || rows > 64)
		return false;

	const uint32 rowTable = rec + 8;
	if (rowTable + (uint32)rows * 2 > size)
		return false;

	Glyph glyph;
	glyph.code = code;
	glyph.yAdjust = yAdjust;
	glyph.width = width;
	glyph.rows = rows;
	glyph.pixels.resize((uint)(width * rows));
	memset(glyph.pixels.begin(), 0, glyph.pixels.size());

	// The blitter reads one word off the row table and jumps by it -- past the
	// table itself, to the first row -- and then streams: a row ends on 0x80 and
	// the next row's runs follow straight after it. The rest of the table is
	// what it skips into when the top of a glyph is clipped away, which a
	// subtitle never is.
	uint32 at = rowTable + READ_LE_UINT16(data + rowTable);

	for (int row = 0; row < rows; row++) {
		int col = 0;
		while (at < size) {
			const int8 op = (int8)data[at++];

			// 0x80 ends the row; any other negative count is a gap the blitter
			// steps the pen over, and a positive one is that many literals.
			if (op == -128)
				break;
			if (op < 0) {
				col += -op;
				continue;
			}
			if (at + (uint32)op > size)
				return false;
			for (int i = 0; i < op; i++, col++) {
				if (col >= 0 && col < width)
					glyph.pixels[row * width + col] = data[at + i];
			}
			at += op;
		}
	}

	_byCode[code] = (int)_glyphs.size();
	_glyphs.push_back(glyph);
	return true;
}

bool AnimFont::load() {
	_glyphs.clear();
	for (int i = 0; i < 256; i++)
		_byCode[i] = -1;

	Common::Array<byte> data;
	if (!readDataObject(data))
		return false;

	const byte *raw = data.begin();
	const uint32 size = data.size();
	const uint32 map = kMapAddr - kDataBase;
	const uint32 bank = kBankAddr - kDataBase;
	if (map + 2 > size || bank + 8 > size)
		return false;

	// The map is read first: it names the glyphs and how many there are, and
	// the bank's offset table is indexed by the same position.
	for (uint32 at = map; at + 1 < size && raw[at]; at += 2) {
		const byte code = raw[at];
		const byte yAdjust = raw[at + 1];
		const uint32 index = (at - map) / 2;

		// The offset table follows one dword of its own, and a record's offset
		// is counted from there rather than from the bank's start.
		const uint32 entry = bank + 4 + index * 4;
		if (entry + 4 > size)
			break;
		const uint32 rec = bank + 4 + READ_LE_UINT32(raw + entry);
		if (!decodeGlyph(raw, size, rec, code, yAdjust)) {
			warning("Alien::AnimFont: glyph %u (code %02x) did not decode", index, code);
			_glyphs.clear();
			return false;
		}
	}

	debugC(1, kDebugResource, "subtitle font: %u glyphs from %s", _glyphs.size(), kPlayerExe);
	return isLoaded();
}

const AnimFont::Glyph *AnimFont::find(byte code) const {
	if (_glyphs.empty())
		return nullptr;

	// The renderer walks the map for the code and falls back to the first
	// glyph when it reaches the terminator, so an unmapped byte draws an A
	// rather than nothing at all.
	const int index = _byCode[code];
	return &_glyphs[index >= 0 ? (uint)index : 0];
}

int AnimFont::measure(const Common::String &line) const {
	int width = 0;
	for (uint i = 0; i < line.size(); i++) {
		const byte code = (byte)line[i];
		if (code == ' ') {
			width += kSpaceWidth;
			continue;
		}
		const Glyph *g = find(code);
		if (g)
			width += g->width + 1;
	}
	return width;
}

void AnimFont::drawString(Graphics::Surface &dest, const Common::String &line, int x, int y) const {
	int pen = x;
	for (uint i = 0; i < line.size(); i++) {
		const byte code = (byte)line[i];
		if (code == ' ') {
			pen += kSpaceWidth;
			continue;
		}

		const Glyph *g = find(code);
		if (!g)
			continue;

		for (int row = 0; row < g->rows; row++) {
			const int dy = y + g->yAdjust + row;
			if (dy < 0 || dy >= dest.h)
				continue;

			byte *dst = (byte *)dest.getBasePtr(0, dy);
			const byte *src = g->pixels.begin() + row * g->width;
			for (int col = 0; col < g->width; col++) {
				const int dx = pen + col;
				if (dx < 0 || dx >= dest.w)
					continue;
				// A gap in a run leaves the frame showing through; the literals
				// go down as they are, which in this bank is always index 255.
				if (src[col])
					dst[dx] = src[col];
			}
		}

		pen += g->width + 1;
	}
}

} // End of namespace Alien
