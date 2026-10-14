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

#include "audio/audiostream.h"
#include "audio/mods/mod_xm_s3m.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/textconsole.h"

#include "alien/s3m.h"

namespace Alien {

S3MModule::S3MModule() {
	unload();
}

void S3MModule::unload() {
	_data.clear();
	_channels = 0;
	_orderCount = 0;
	_patternCount = 0;
	_sampleCount = 0;
	_speed = 6;
	_tempo = 125;
}

bool S3MModule::load(const Common::String &file) {
	unload();

	// The names in the table carry the DOS separator, and are padded out to the
	// 8.3 field they were typed into -- "MUSIC\\LEOOUT .S3M" -- so the padding
	// comes out before the name is split on the separator.
	Common::String name;
	for (uint i = 0; i < file.size(); i++) {
		if (file[i] != ' ')
			name += file[i];
	}

	Common::File s3m;
	if (!s3m.open(Common::Path(name, '\\')))
		return false;

	const uint32 size = (uint32)s3m.size();
	if (size < kOrdersAt) {
		warning("Alien::S3MModule: %s is not a Scream Tracker 3 module", file.c_str());
		return false;
	}
	_data.resize(size);
	if (s3m.read(&_data[0], size) != size) {
		_data.clear();
		return false;
	}
	const byte *d = &_data[0];

	if (memcmp(d + 0x2C, "SCRM", 4) != 0) {
		warning("Alien::S3MModule: %s is not a Scream Tracker 3 module", file.c_str());
		_data.clear();
		return false;
	}

	_orderCount = READ_LE_UINT16(d + 0x20);
	const uint insCount = READ_LE_UINT16(d + 0x22);
	_patternCount = READ_LE_UINT16(d + 0x24);
	_speed = d[0x31];
	_tempo = d[0x32];
	if (kOrdersAt + _orderCount + insCount * 2 + _patternCount * 2 > size) {
		unload();
		return false;
	}

	// A channel setting under 16 is a channel that plays; 16 to 31 are muted and
	// 255 is a channel the module does not have at all.
	for (uint i = 0; i < 32; i++) {
		if (d[0x40 + i] < 16)
			_channels = i + 1;
	}

	// Type 1 is a PCM sample; 0 is an empty slot and 2 and up are Adlib, which
	// no module ships.
	const byte *insPtr = d + kOrdersAt + _orderCount;
	for (uint i = 0; i < insCount; i++) {
		const uint32 off = READ_LE_UINT16(insPtr + i * 2) * 16;
		if (!off || off + 0x50 > size || d[off] != 1)
			continue;
		const uint32 length = READ_LE_UINT32(d + off + 0x10);
		const uint32 pcm = ((uint32)d[off + 0x0D] << 16 | READ_LE_UINT16(d + off + 0x0E)) * 16;
		if (length && pcm + length <= size)
			_sampleCount++;
	}

	return true;
}

Common::SeekableReadStream *S3MModule::song(uint startOrder) const {
	if (!isLoaded())
		return nullptr;

	const byte *source = &_data[kOrdersAt];
	uint first = MIN(startOrder, _orderCount);
	if (first < _orderCount && source[first] == kOrderEnd)
		first++;
	uint end = first;
	while (end < _orderCount && source[end] != kOrderEnd)
		end++;

	// A list of nothing but markers would keep the tracker looking for a
	// pattern forever.
	if (first == end)
		return nullptr;

	byte *copy = (byte *)malloc(_data.size());
	if (!copy)
		return nullptr;
	memcpy(copy, &_data[0], _data.size());

	byte *orders = copy + kOrdersAt;
	for (uint i = 0; i < _orderCount; i++) {
		if (i < first || i >= end)
			orders[i] = kOrderEnd;
	}

	return new Common::MemoryReadStream(copy, _data.size(), DisposeAfterUse::YES);
}

Audio::AudioStream *makeSongStream(const S3MModule &module, uint startOrder) {
	Common::SeekableReadStream *data = module.song(startOrder);
	if (!data)
		return nullptr;

	// A song that loops on its own Bxx plays on by itself, but one that runs into
	// the markers behind it can end the stream when it wraps; the rewind starts it
	// over from the top, which is the start of the song.
	Audio::RewindableAudioStream *song = Audio::makeModXmS3mStream(data, DisposeAfterUse::YES);
	if (!song)
		return nullptr;
	return Audio::makeLoopingAudioStream(song, 0);
}

} // End of namespace Alien
