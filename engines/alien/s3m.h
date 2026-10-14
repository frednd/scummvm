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

#ifndef ALIEN_S3M_H
#define ALIEN_S3M_H

#include "common/array.h"
#include "common/str.h"

namespace Audio {
class AudioStream;
}

namespace Common {
class SeekableReadStream;
}

namespace Alien {

/**
 * A music module, read whole so that one song of it can be handed to the
 * shared tracker (audio/mods/mod_xm_s3m).
 *
 * Three of the eleven modules are banks of several songs with a 255 marker
 * between them, and INPUT:music_play_slot starts most tracks partway into the
 * order list. A track ends at the marker after it, and MIDAS starts it again
 * from where the slot put it rather than falling into its neighbour.
 *
 * The shared tracker cannot be told any of that: it steps over every marker and
 * always starts from the top. So the module is given to it with every order
 * outside the song replaced by a marker. The song's own orders keep their
 * places, which keeps every Bxx jump in it pointing where it did; the tracker
 * steps over the markers in front of the song to reach it, and over the ones
 * behind it to wrap back to the top, which is the start of the song again.
 */
class S3MModule {
public:
	S3MModule();

	bool load(const Common::String &file);
	void unload();
	bool isLoaded() const { return !_data.empty(); }

	/**
	 * The module with only the song that starts at `startOrder` left in its
	 * order list, or nullptr when nothing is loaded. The caller owns the stream.
	 * A start on a marker means the song after it.
	 */
	Common::SeekableReadStream *song(uint startOrder) const;

	uint channelCount() const { return _channels; }
	uint orderCount() const { return _orderCount; }
	uint patternCount() const { return _patternCount; }
	uint sampleCount() const { return _sampleCount; }	///< the PCM instruments present
	byte speed() const { return _speed; }
	byte tempo() const { return _tempo; }

private:
	/// Where the order list starts in the file.
	static const uint32 kOrdersAt = 0x60;
	/// An order list entry that ends a song; the shared tracker skips it.
	static const byte kOrderEnd = 255;

	Common::Array<byte> _data;
	uint _channels;
	uint _orderCount;
	uint _patternCount;
	uint _sampleCount;
	byte _speed;
	byte _tempo;
};

/**
 * One song of a module as a looping stream for the mixer, or nullptr when the
 * module is not loaded or the shared tracker refuses it.
 */
Audio::AudioStream *makeSongStream(const S3MModule &module, uint startOrder);

} // End of namespace Alien

#endif
