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

#ifndef ALIEN_RESOURCES_H
#define ALIEN_RESOURCES_H

#include "common/scummsys.h"

namespace Common {
class Path;
}

namespace Graphics {
struct Surface;
}

namespace Alien {

/**
 * Load one of the game's PCX plates into an 8bpp surface plus a 256-entry
 * RGB palette.
 *
 * The shipped files carry the signature FF 05 FF 08 where a standard PCX has
 * 0A 05 01 08, presumably to discourage viewing them with off-the-shelf
 * tools. Repairing those two bytes yields a valid standard PCX, so the
 * decoding itself is Image::PCXDecoder's job.
 *
 * @param path     file name inside the game directory
 * @param surface  receives the decoded image; caller owns and must free it
 * @param palette  receives 256 * 3 bytes of RGB
 */
bool loadGamePCX(const Common::Path &path, Graphics::Surface &surface, byte *palette);

} // End of namespace Alien

#endif
