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

#ifndef ALIEN_DL1_H
#define ALIEN_DL1_H

#include "common/array.h"
#include "common/scummsys.h"

namespace Common {
class Path;
class SeekableReadStream;
}

namespace Graphics {
struct Surface;
}

namespace Alien {

/**
 * A DL1/DL2 sprite: a set of frames, each a list of horizontal pixel spans.
 *
 * The format is documented in docs/dl1_format.md of the reverse-engineering
 * repository; tools/dl1.py there is the reference implementation this decoder
 * is ported from. A span ("strip") carries a destination address in the 320
 * column staging buffer rather than an x/y pair, and gaps between spans on a
 * row are how the format encodes transparency. Palette index 0 inside a span
 * is transparent as well.
 */
class DL1Sprite {
public:
	struct Strip {
		uint16 addr;			///< y * 320 + x in the staging buffer
		uint16 length;			///< pixel count, always even
		uint32 pixelOffset;		///< into the owned file buffer
		uint16 roomX;			///< long form only: room-space column, else kNoRoomX
	};

	struct Frame {
		uint16 startAddr;
		bool hasBbox;
		uint16 bbox[4];			///< x1, y1, x2, y2 as stored
		Common::Array<Strip> strips;
	};

	static const uint16 kNoRoomX = 0xFFFF;
	static const int kScreenWidth = 320;

	DL1Sprite();
	~DL1Sprite();

	bool load(const Common::Path &path);
	bool loadStream(Common::SeekableReadStream &stream);

	uint frameCount() const { return _frames.size(); }
	const Frame &frame(uint index) const { return _frames[index]; }

	/** True when the file uses the wide-room strip form carrying column spans. */
	bool isLongForm() const { return _longForm; }

	/**
	 * Blit one frame onto an 8bpp surface, skipping gaps and index 0. The
	 * surface is addressed the way the original staging buffer was, so a
	 * strip's address maps straight to a pixel position.
	 */
	void drawFrame(uint index, Graphics::Surface &dest) const;

private:
	struct ParseResult {
		Common::Array<Frame> frames;
		uint32 endPos;
	};

	void clear();
	bool parse();
	bool parseFrames(uint32 bboxAt, uint nframes, bool longForm, ParseResult &out) const;
	bool longStripAt(uint32 pos) const;
	int32 score(const ParseResult &result) const;

	byte *_data;
	uint32 _size;
	uint8 _typeId;
	bool _longForm;
	Common::Array<Frame> _frames;
};

} // End of namespace Alien

#endif
