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
#include "common/stream.h"
#include "graphics/surface.h"

#include "alien/detection.h"
#include "alien/dl1.h"

namespace Alien {

static const byte kMarker[8] = { 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00 };
static const uint kMaxFrames = 200;

DL1Sprite::DL1Sprite() : _data(nullptr), _size(0), _typeId(0), _longForm(false) {
}

DL1Sprite::~DL1Sprite() {
	clear();
}

void DL1Sprite::clear() {
	delete[] _data;
	_data = nullptr;
	_size = 0;
	_typeId = 0;
	_longForm = false;
	_frames.clear();
}

bool DL1Sprite::load(const Common::Path &path) {
	Common::File f;
	if (!f.open(path)) {
		warning("DL1: cannot open %s", path.toString().c_str());
		return false;
	}
	return loadStream(f);
}

bool DL1Sprite::loadStream(Common::SeekableReadStream &stream) {
	clear();

	int64 size = stream.size();
	if (size < 16 || size > 0x400000) {
		warning("DL1: implausible file size %d", (int)size);
		return false;
	}

	_size = (uint32)size;
	_data = new byte[_size];
	if (stream.read(_data, _size) != _size) {
		warning("DL1: short read");
		clear();
		return false;
	}

	if (!parse()) {
		clear();
		return false;
	}
	return true;
}

bool DL1Sprite::longStripAt(uint32 pos) const {
	if (pos + 8 > _size)
		return false;
	uint32 words = READ_LE_UINT16(_data + pos + 2);
	uint32 x1 = READ_LE_UINT16(_data + pos + 4);
	uint32 x2 = READ_LE_UINT16(_data + pos + 6);
	return words > 0 && x2 > x1 && (x2 - x1) == words * 2;
}

bool DL1Sprite::parseFrames(uint32 bboxAt, uint nframes, bool longForm, ParseResult &out) const {
	out.frames.clear();

	uint32 pos = bboxAt + nframes * 8;

	// The declared frame count is a hint only: a few files ship fewer frames
	// than they declare and the two large DL2 screens ship more, so the loop
	// runs until the data is exhausted rather than for a fixed count.
	while (pos + 6 <= _size) {
		uint frameIndex = out.frames.size();
		uint16 startAddr = READ_LE_UINT16(_data + pos);

		// A frame opens with its first strip's address repeated once, twice in
		// a couple of files. Consume the copies.
		while (pos + 4 <= _size && READ_LE_UINT16(_data + pos + 2) == startAddr)
			pos += 2;

		bool longStrips = longForm && longStripAt(pos);
		uint32 header = longStrips ? 8 : 4;

		Frame frame;
		frame.startAddr = startAddr;
		frame.hasBbox = frameIndex < nframes;
		if (frame.hasBbox) {
			for (int i = 0; i < 4; i++)
				frame.bbox[i] = READ_LE_UINT16(_data + bboxAt + frameIndex * 8 + i * 2);
		} else {
			for (int i = 0; i < 4; i++)
				frame.bbox[i] = 0;
		}

		while (pos + header <= _size) {
			uint16 addr = READ_LE_UINT16(_data + pos);
			uint16 words = READ_LE_UINT16(_data + pos + 2);

			// A strip's first word is a screen address and its second a small
			// word count, so the two are only ever equal at a frame boundary.
			if (!frame.strips.empty() && addr == words)
				break;
			if (words == 0)
				break;

			uint32 pixels = (uint32)words * 2;
			if (pos + header + pixels > _size) {
				// Ragged tail: keep what has been decoded so far.
				if (!frame.strips.empty() || !out.frames.empty())
					break;
				return false;
			}

			Strip strip;
			strip.addr = addr;
			strip.length = (uint16)pixels;
			strip.roomX = kNoRoomX;
			if (longStrips) {
				uint32 x1 = READ_LE_UINT16(_data + pos + 4);
				uint32 x2 = READ_LE_UINT16(_data + pos + 6);
				if (x2 - x1 != pixels)
					return false;
				strip.roomX = (uint16)x1;
			}
			strip.pixelOffset = pos + header;
			frame.strips.push_back(strip);

			pos += header + pixels;
		}

		if (frame.strips.empty()) {
			if (out.frames.empty())
				return false;
			pos -= 2;			// ran into the end-of-file padding
			break;
		}

		out.frames.push_back(frame);
	}

	out.endPos = pos;
	return !out.frames.empty();
}

int32 DL1Sprite::score(const ParseResult &result) const {
	// Unconsumed bytes are suspicious; bounding boxes that agree with the
	// decoded rows are strong evidence the right frame table was picked.
	int32 s = -(int32)(_size - result.endPos);

	for (uint i = 0; i < result.frames.size(); i++) {
		const Frame &frame = result.frames[i];
		if (!frame.hasBbox || frame.strips.empty())
			continue;

		int32 minY = 0x7FFFFFFF, maxY = -1;
		for (uint j = 0; j < frame.strips.size(); j++) {
			int32 y = frame.strips[j].addr / kScreenWidth;
			minY = MIN(minY, y);
			maxY = MAX(maxY, y);
		}

		int32 y1 = (int32)frame.bbox[1];
		int32 y2 = (int32)frame.bbox[3];
		if (y1 - 1 <= minY && maxY <= y2 + 1)
			s += 64;
	}
	return s;
}

bool DL1Sprite::parse() {
	_typeId = _data[0];

	// The stored frame count includes a terminator entry.
	int declared = (int)_data[1] - 1;
	if (declared < 1 || declared > (int)kMaxFrames) {
		warning("DL1: implausible frame count %d", (int)_data[1]);
		return false;
	}
	uint nframes = (uint)declared;

	bool haveBest = false;
	int32 bestScore = 0;
	ParseResult best;

	// The bounding-box table follows a fixed marker, but the marker byte
	// sequence also occurs inside the sprite editor residue at the head of the
	// file, so every occurrence is tried and the best-scoring one wins.
	uint32 searchFrom = 8;
	while (searchFrom + sizeof(kMarker) <= _size) {
		uint32 found = _size;
		for (uint32 i = searchFrom; i + sizeof(kMarker) <= _size; i++) {
			if (!memcmp(_data + i, kMarker, sizeof(kMarker))) {
				found = i;
				break;
			}
		}
		if (found == _size)
			break;
		searchFrom = found + 1;

		uint32 bboxAt = found + sizeof(kMarker);
		if (bboxAt + nframes * 8 > _size)
			continue;

		bool longForm = longStripAt(bboxAt + nframes * 8 + 2);
		bool order[2];
		order[0] = longForm;
		order[1] = !longForm;

		for (int i = 0; i < 2; i++) {
			ParseResult candidate;
			if (!parseFrames(bboxAt, nframes, order[i], candidate))
				continue;

			int32 sc = score(candidate);
			if (!haveBest || sc > bestScore) {
				haveBest = true;
				bestScore = sc;
				best = candidate;
				_longForm = order[i];
			}
			break;
		}
	}

	if (!haveBest) {
		warning("DL1: no usable frame table");
		return false;
	}

	_frames = best.frames;
	debugC(1, kDebugResource, "DL1: type %02x, %u frames, %s form",
		   _typeId, _frames.size(), _longForm ? "long" : "short");
	return true;
}

void DL1Sprite::drawFrame(uint index, Graphics::Surface &dest, int scrollX, int clipBottom) const {
	if (index >= _frames.size())
		return;

	const Frame &frame = _frames[index];
	for (uint i = 0; i < frame.strips.size(); i++) {
		const Strip &strip = frame.strips[i];
		int x = ((strip.roomX != kNoRoomX) ? (int)strip.roomX : (int)(strip.addr % kScreenWidth)) - scrollX;
		int y = strip.addr / kScreenWidth;
		if (y < 0 || y >= dest.h || y >= clipBottom)
			continue;

		const byte *src = _data + strip.pixelOffset;
		byte *dst = (byte *)dest.getBasePtr(0, y);

		// A strip is copied verbatim, zeros included: the original's blit
		// (MIDAS:sub_180d4, and sub_18006 for the background page) moves the
		// run with `rep movsw` and never tests a pixel. Transparency in this
		// format is the *gap* between strips, not a colour, and the zeros
		// inside a run are real black pixels -- the dark opening a door leaves
		// behind when it swings away. Skipping them left room 6's doors
		// looking shut however far the animation had run (finding #54).
		for (uint j = 0; j < strip.length; j++) {
			int col = x + (int)j;
			if (col < 0 || col >= dest.w)
				continue;
			dst[col] = src[j];
		}
	}
}

} // End of namespace Alien
