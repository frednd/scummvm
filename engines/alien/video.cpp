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

#include "audio/decoders/raw.h"
#include "common/debug.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "alien/detection.h"
#include "alien/video.h"

namespace Alien {

// -- MA1 ---------------------------------------------------------------------

MA1Decoder::MA1Decoder() {
}

bool MA1Decoder::loadStream(Common::SeekableReadStream *stream) {
	close();

	if (!stream)
		return false;

	const uint32 size = (uint32)stream->size();
	if (size < 5) {
		delete stream;
		return false;
	}

	const uint16 frameCount = stream->readUint16LE();
	const byte fullPages = stream->readByte();
	const uint16 tail = stream->readUint16LE();

	const uint32 indexOffset = 5;
	const uint32 paletteOffset = indexOffset + frameCount * 3;
	const uint32 payloadOffset = paletteOffset + 768;
	if (frameCount == 0 || payloadOffset > size) {
		warning("Alien::MA1Decoder: header describes %u frames but the file is %u bytes",
				frameCount, size);
		delete stream;
		return false;
	}

	// The file is under a megabyte and every frame is a difference against the
	// one before it, so it is read whole rather than seeked around in.
	byte *data = (byte *)malloc(size);
	if (!data) {
		delete stream;
		return false;
	}
	stream->seek(0);
	const uint32 got = stream->read(data, size);
	delete stream;
	if (got != size) {
		free(data);
		return false;
	}

	addTrack(new MA1VideoTrack(data, size, frameCount, fullPages, tail,
							   indexOffset, payloadOffset));
	return true;
}

MA1Decoder::MA1VideoTrack::MA1VideoTrack(byte *data, uint32 size, uint16 frameCount,
										 byte fullPages, uint16 tail, uint32 indexOffset,
										 uint32 payloadOffset)
	: _data(data), _size(size), _indexOffset(indexOffset), _payloadOffset(payloadOffset),
	  _frameCount(frameCount), _curFrame(-1), _dirtyPalette(true) {
	(void)fullPages;
	(void)tail;

	_surface.create(kWidth, kHeight, Graphics::PixelFormat::createFormatCLUT8());

	// One palette for the whole file, 6-bit VGA triples as they went to the DAC.
	const byte *src = _data + _payloadOffset - 768;
	for (int i = 0; i < 256 * 3; i++)
		_palette[i] = src[i] * 255 / 63;
}

MA1Decoder::MA1VideoTrack::~MA1VideoTrack() {
	_surface.free();
	free(_data);
}

Graphics::PixelFormat MA1Decoder::MA1VideoTrack::getPixelFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

const byte *MA1Decoder::MA1VideoTrack::getPalette() const {
	_dirtyPalette = false;
	return _palette;
}

bool MA1Decoder::MA1VideoTrack::rewind() {
	_curFrame = -1;
	_dirtyPalette = true;
	memset(_surface.getPixels(), 0, kWidth * kHeight);
	return true;
}

const Graphics::Surface *MA1Decoder::MA1VideoTrack::decodeNextFrame() {
	_curFrame++;
	if (_curFrame >= _frameCount)
		return &_surface;

	const uint32 entry = _indexOffset + _curFrame * 3;
	uint page = _data[entry];
	uint32 offset = READ_LE_UINT16(_data + entry + 1);

	const uint32 maskAt = at(page, offset);
	if (maskAt + kMaskSize > _size) {
		warning("Alien::MA1Decoder: frame %d runs past the end of the file", _curFrame);
		return &_surface;
	}
	const byte *mask = _data + maskAt;
	offset += kMaskSize;

	byte *screen = (byte *)_surface.getPixels();
	int pos = 0;
	for (int row = 0; row < kBlockRows; row++) {
		for (int col = 0; col < kBlockCols; col++) {
			byte bits = mask[row * kBlockCols + col];
			for (int bit = 0; bit < 8; bit++) {
				if (bits & (0x80 >> bit)) {
					const uint32 src = at(page, offset);
					if (src + 4 > _size)
						return &_surface;
					// Two pixels of the block's top row, then two of the row
					// below: a 2x2 block is the codec's unit.
					screen[pos] = _data[src];
					screen[pos + 1] = _data[src + 1];
					screen[pos + kWidth] = _data[src + 2];
					screen[pos + kWidth + 1] = _data[src + 3];
					offset += 4;
					if (offset > kPageLimit) {
						page++;
						offset = 0;
					}
				}
				pos += 2;
			}
		}
		pos += kWidth;		// step over the block row's second scanline
	}

	return &_surface;
}

// -- CDA2 --------------------------------------------------------------------

CDA2Decoder::CDA2Decoder()
	: _video(nullptr), _frameCount(0), _rate(0), _languages(0), _language(kEnglish) {
}

bool CDA2Decoder::loadStream(Common::SeekableReadStream *stream) {
	close();
	_video = nullptr;
	_text.clear();
	_languages = 0;

	if (!stream)
		return false;

	if (stream->size() < (int64)kHeaderSize) {
		delete stream;
		return false;
	}

	byte magic[4];
	stream->read(magic, 4);
	if (memcmp(magic, "CDA2", 4) != 0) {
		warning("Alien::CDA2Decoder: not a CDA2 file");
		delete stream;
		return false;
	}

	_frameCount = stream->readUint32LE();
	stream->readUint16LE();				// nominal fps: 20, and never the real rate
	const uint16 width = stream->readUint16LE();
	const uint16 height = stream->readUint16LE();
	stream->readUint16LE();				// the audio block divisor, unused here
	_rate = stream->readUint16LE();
	const uint16 flags = stream->readUint16LE();

	if (width != kWidth || height != kHeight) {
		warning("Alien::CDA2Decoder: unexpected %dx%d", width, height);
		delete stream;
		return false;
	}

	if (flags & 1) {
		const uint32 size = stream->readUint32LE();
		_text.resize(size);
		if (size && stream->read(&_text[0], size) != size) {
			delete stream;
			return false;
		}
		if (size >= 4)
			_languages = READ_LE_UINT32(&_text[0]);
	}

	CDA2AudioTrack *audio = new CDA2AudioTrack(_rate);
	_video = new CDA2VideoTrack(this, stream, _frameCount, _rate, audio);
	if (!_video->readSizes((uint32)stream->pos())) {
		delete _video;
		delete audio;
		_video = nullptr;
		delete stream;
		return false;
	}

	addTrack(_video);
	addTrack(audio);
	return true;
}

Common::String CDA2Decoder::subtitle(uint frame) const {
	// Each record is i16 x, i16 y then the text; a negative x means the player
	// centres the line. Offsets are relative to the start of the offset table,
	// which sits just past the language count.
	if (_text.size() < 4 || _language >= _languages || frame >= _frameCount)
		return Common::String();

	const uint32 base = 4;
	const uint32 at = base + (_language * _frameCount + frame) * 4;
	if (at + 4 > _text.size())
		return Common::String();

	uint32 start = base + READ_LE_UINT32(&_text[at]) + 4;
	if (start >= _text.size())
		return Common::String();

	Common::String line;
	for (uint32 i = start; i < _text.size() && _text[i]; i++)
		line += _text[i] == '@' ? '\n' : (char)_text[i];
	return line;
}

Common::String CDA2Decoder::subtitle() const {
	if (!_video || _video->getCurFrame() < 0)
		return Common::String();
	return subtitle((uint)_video->getCurFrame());
}

CDA2Decoder::CDA2AudioTrack::CDA2AudioTrack(uint rate)
	: AudioTrack(Audio::Mixer::kSFXSoundType), _finished(false) {
	_stream = Audio::makeQueuingAudioStream(rate, false);
}

CDA2Decoder::CDA2AudioTrack::~CDA2AudioTrack() {
	delete _stream;
}

void CDA2Decoder::CDA2AudioTrack::queue(const byte *data, uint32 size) {
	if (!size)
		return;

	byte *copy = (byte *)malloc(size);
	if (!copy)
		return;
	memcpy(copy, data, size);
	_stream->queueBuffer(copy, size, DisposeAfterUse::YES,
						 Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN);
}

CDA2Decoder::CDA2VideoTrack::CDA2VideoTrack(CDA2Decoder *decoder,
											Common::SeekableReadStream *stream,
											uint frameCount, uint rate, CDA2AudioTrack *audio)
	: _decoder(decoder), _stream(stream), _audio(audio), _dataStart(0), _frameAt(0),
	  _frameCount(frameCount), _curFrame(-1), _rate(rate), _samples(0),
	  _nextFrameStartTime(0), _dirtyPalette(true) {
	_surface.create(kWidth, kHeight, Graphics::PixelFormat::createFormatCLUT8());
	memset(_palette, 0, sizeof(_palette));
}

CDA2Decoder::CDA2VideoTrack::~CDA2VideoTrack() {
	_surface.free();
	delete _stream;
}

Graphics::PixelFormat CDA2Decoder::CDA2VideoTrack::getPixelFormat() const {
	return Graphics::PixelFormat::createFormatCLUT8();
}

const byte *CDA2Decoder::CDA2VideoTrack::getPalette() const {
	_dirtyPalette = false;
	return _palette;
}

bool CDA2Decoder::CDA2VideoTrack::readSizes(uint32 tableStart) {
	// The CRC table comes first -- a non-standard variant that nothing here
	// reproduces -- and then the size of every frame.
	_stream->seek(tableStart + (uint32)_frameCount * 4);
	_sizes.resize(_frameCount);
	for (int i = 0; i < _frameCount; i++)
		_sizes[i] = _stream->readUint32LE();

	if (_stream->err() || _stream->eos())
		return false;

	_dataStart = (uint32)_stream->pos();
	_frameAt = _dataStart;
	return true;
}

bool CDA2Decoder::CDA2VideoTrack::unpackLZSS(const byte *src, uint32 size, uint32 at,
											 uint32 tokens) {
	// The classic 4096-byte ring starting at 0xFEE, control bits LSB first, a
	// match carrying a 12-bit offset and a 4-bit length plus three. The count in
	// the frame header is a token count, not an output length: the player
	// decrements it once per literal and once per match, however long it runs.
	byte ring[4096];
	memset(ring, 0, sizeof(ring));
	uint r = 0xFEE;
	uint control = 0;

	_unpacked.clear();
	while (tokens > 0) {
		if (!(control & 0x100)) {
			if (at >= size)
				return false;
			control = src[at++] | 0xFF00;
		}
		const bool literal = (control & 1) != 0;
		control >>= 1;

		if (literal) {
			if (at >= size)
				return false;
			const byte b = src[at++];
			_unpacked.push_back(b);
			ring[r] = b;
			r = (r + 1) & 0xFFF;
		} else {
			if (at + 2 > size)
				return false;
			const byte lo = src[at];
			const byte hi = src[at + 1];
			at += 2;
			const uint length = (hi & 0x0F) + 3;
			uint offset = lo | ((hi & 0xF0) << 4);
			for (uint i = 0; i < length; i++) {
				const byte b = ring[offset];
				offset = (offset + 1) & 0xFFF;
				_unpacked.push_back(b);
				ring[r] = b;
				r = (r + 1) & 0xFFF;
			}
		}
		tokens--;
	}
	return true;
}

const Graphics::Surface *CDA2Decoder::CDA2VideoTrack::decodeNextFrame() {
	_curFrame++;
	if (_curFrame >= _frameCount) {
		_audio->finish();
		return &_surface;
	}

	const uint32 size = _sizes[_curFrame];
	_frame.resize(size);
	_stream->seek(_frameAt);
	if (size && _stream->read(&_frame[0], size) != size) {
		_audio->finish();
		return &_surface;
	}
	_frameAt += size;

	const byte *data = size ? &_frame[0] : nullptr;
	uint32 at = 1;
	const byte flags = size ? data[0] : 0;

	if (flags & kFlagAudio) {
		const uint16 length = READ_LE_UINT16(data + at);
		at += 2;
		if (at + length <= size) {
			_audio->queue(data + at, length);
			_samples += length / kSampleBytes;
		}
		at += length;
	}
	if (flags & kFlagHold)
		at++;					// how many frame slots the image stays up
	if (flags & kFlagFade)
		at += 8;				// three step values and the mode bits

	// The clock is the audio the mixer has been handed, not a frame rate: a
	// frame stays up for exactly as long as the block it carries.
	_nextFrameStartTime = (uint32)((uint64)_samples * 1000 / _rate);

	uint32 payloadSize = size;
	if (flags & kFlagPacked) {
		if (at + 4 > size)
			return &_surface;
		const uint32 tokens = READ_LE_UINT32(data + at);
		if (!unpackLZSS(data, size, at + 4, tokens))
			return &_surface;
		data = _unpacked.empty() ? nullptr : &_unpacked[0];
		payloadSize = _unpacked.size();
		at = 0;
	}

	if (flags & kFlagPalette) {
		if (at + kPaletteSize > payloadSize)
			return &_surface;
		for (uint32 i = 0; i < kPaletteSize; i++)
			_palette[i] = data[at + i] * 255 / 63;
		_dirtyPalette = true;
		at += kPaletteSize;
	}

	if (flags & kFlagClear)
		memset(_surface.getPixels(), 0, kWidth * kHeight);
	else if (data)
		drawImage(data + at, payloadSize - at);

	if (_curFrame + 1 >= _frameCount)
		_audio->finish();

	return &_surface;
}

void CDA2Decoder::CDA2VideoTrack::drawImage(const byte *payload, uint32 size) {
	// 320x200 in 32x40 blocks, five across and five down. Each block's 3-bit
	// mode comes out of a nibble, two blocks to the byte, and a band's mode
	// bytes come before that band's block data.
	const int cols = kWidth / (kBlockW * 2);
	const int bands = kHeight / kBlockH;
	uint32 at = (uint32)(cols * bands);
	if (size < at)
		return;					// the last frames of a file are stubs

	byte *screen = (byte *)_surface.getPixels();
	for (int band = 0; band < bands; band++) {
		const int base = band * kBlockH * kWidth;
		for (int col = 0; col < cols; col++) {
			const byte modes = payload[band * cols + col];
			for (int half = 0; half < 2; half++) {
				const int mode = (modes >> (half == 0 ? 4 : 0)) & 7;
				const int dst = base + (col * 2 + half) * kBlockW;
				if (mode < 3) {
					const uint32 next = drawMasked(payload, size, at, dst, 1 << mode);
					if (next == (uint32)-1)
						return;		// a truncated tail frame
					at = next;
				} else if (mode == 3) {
					if (at + kBlockW * kBlockH > size)
						return;
					for (int row = 0; row < kBlockH; row++)
						memcpy(screen + dst + row * kWidth, payload + at + row * kBlockW,
							   kBlockW);
					at += kBlockW * kBlockH;
				}
				// Modes 4 to 7 leave the block as it was.
			}
		}
	}
}

uint32 CDA2Decoder::CDA2VideoTrack::drawMasked(const byte *payload, uint32 size, uint32 at,
											   int dst, int cell) {
	// Modes 0 to 2: a bitmask over cell by cell squares, then the pixels the set
	// bits call for. Only set cells cost payload.
	const int perRow = kBlockW / cell;		// cells across the block: 32, 16 or 8
	const int rows = kBlockH / cell;
	const int maskBytesPerRow = perRow / 8;
	const uint32 maskLen = (uint32)(rows * maskBytesPerRow);
	if (at + maskLen > size)
		return (uint32)-1;

	const byte *mask = payload + at;
	uint32 src = at + maskLen;
	byte *screen = (byte *)_surface.getPixels();

	for (int cy = 0; cy < rows; cy++) {
		const int rowBase = dst + cy * cell * kWidth;
		for (int index = 0; index < maskBytesPerRow; index++) {
			const byte bits = mask[cy * maskBytesPerRow + index];
			if (!bits)
				continue;
			for (int bit = 0; bit < 8; bit++) {
				if (!(bits & (0x80 >> bit)))
					continue;
				if (src + (uint32)(cell * cell) > size)
					return (uint32)-1;
				const int px = rowBase + (index * 8 + bit) * cell;
				for (int line = 0; line < cell; line++)
					memcpy(screen + px + line * kWidth, payload + src + line * cell, cell);
				src += cell * cell;
			}
		}
	}
	return src;
}

} // End of namespace Alien
