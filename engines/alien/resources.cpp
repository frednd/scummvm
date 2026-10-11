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
#include "common/memstream.h"
#include "common/path.h"
#include "common/textconsole.h"
#include "graphics/palette.h"
#include "graphics/surface.h"
#include "image/pcx.h"

#include "alien/resources.h"

namespace Alien {

bool loadGamePCX(const Common::Path &path, Graphics::Surface &surface, byte *palette) {
	Common::File f;
	if (!f.open(path)) {
		warning("PCX: cannot open %s", path.toString().c_str());
		return false;
	}

	uint32 size = (uint32)f.size();
	if (size < 128) {
		warning("PCX: %s is too small", path.toString().c_str());
		return false;
	}

	byte *raw = new byte[size];
	if (f.read(raw, size) != size) {
		warning("PCX: short read on %s", path.toString().c_str());
		delete[] raw;
		return false;
	}

	// Repair the tweaked signature: FF 05 FF 08 -> 0A 05 01 08.
	raw[0] = 0x0A;
	raw[2] = 0x01;

	Common::MemoryReadStream stream(raw, size);
	Image::PCXDecoder decoder;
	if (!decoder.loadStream(stream)) {
		warning("PCX: %s failed to decode", path.toString().c_str());
		delete[] raw;
		return false;
	}

	const Graphics::Surface *decoded = decoder.getSurface();
	if (!decoded || decoded->format.bytesPerPixel != 1) {
		warning("PCX: %s is not 8bpp", path.toString().c_str());
		delete[] raw;
		return false;
	}

	surface.copyFrom(*decoded);

	const Graphics::Palette &pal = decoder.getPalette();
	memset(palette, 0, 256 * 3);
	uint entries = MIN<uint>(pal.size(), 256);
	memcpy(palette, pal.data(), entries * 3);

	delete[] raw;
	return true;
}

} // End of namespace Alien
