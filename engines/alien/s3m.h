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

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

namespace Alien {

/**
 * A Scream Tracker 3 replayer, to the scope the game's own modules ask for.
 *
 * tools/s3mscan.py measured what the eleven music modules actually use, and
 * this implements that and no more: 14 of the 26 effect commands, `SDx` as the
 * only `Sxy` sub-command, at most 8 channels, mono, 8-bit unpacked samples with
 * loop points, and a start at an order the caller names rather than always at
 * order 0 -- INPUT:music_play_slot starts most tracks partway into the list.
 *
 * The missing twelve commands (K L M N P R U V W X Y Z) appear in no module the
 * game ships. A module that used one would play it as a no-op rather than as
 * something wrong, which is why they are left out rather than approximated.
 */
class S3MModule {
public:
	/// A pattern is 64 rows over as many channels as the module enables.
	static const int kRowsPerPattern = 64;
	static const int kMaxChannels = 32;

	/// Values a cell's note byte takes that are not a note.
	static const byte kNoteEmpty = 255;
	static const byte kNoteCut = 254;

	/// The volume column is empty at this value, not silent.
	static const byte kVolumeEmpty = 255;

	struct Sample {
		Common::String name;
		byte *data;			///< the PCM as signed 8-bit, converted on load
		uint32 length;
		uint32 loopStart;
		uint32 loopEnd;
		bool loops;
		byte volume;		///< 0..64, the sample's own default
		uint32 c2spd;		///< the rate C-4 plays at

		Sample() : data(nullptr), length(0), loopStart(0), loopEnd(0), loops(false),
				   volume(64), c2spd(8363) {}
	};

	struct Cell {
		byte note;			///< 0..119 as octave * 16 + semitone, or the two markers
		byte instrument;	///< 1-based, 0 for "keep the one playing"
		byte volume;
		byte command;		///< 1 = A, 2 = B, ... 0 for none
		byte info;

		Cell() : note(kNoteEmpty), instrument(0), volume(kVolumeEmpty), command(0), info(0) {}
	};

	S3MModule();
	~S3MModule();

	bool load(const Common::String &file);
	void unload();
	bool isLoaded() const { return _loaded; }

	const Common::String &file() const { return _file; }
	const Common::String &title() const { return _title; }
	uint channelCount() const { return _channels; }
	uint orderCount() const { return _orders.size(); }
	uint patternCount() const { return _patternCount; }
	uint sampleCount() const { return _samples.size(); }
	byte speed() const { return _speed; }
	byte tempo() const { return _tempo; }
	byte globalVolume() const { return _globalVolume; }
	byte masterVolume() const { return _masterVolume; }

	byte order(uint index) const;
	const Cell &cell(uint pattern, uint row, uint channel) const;
	const Sample *sample(uint index) const;

private:
	bool readPattern(const byte *data, uint32 size, uint pattern);

	Common::String _file;
	Common::String _title;
	uint _channels;
	uint _patternCount;
	byte _speed;
	byte _tempo;
	byte _globalVolume;
	byte _masterVolume;
	bool _loaded;

	Common::Array<byte> _orders;
	Common::Array<Cell> _patterns;		///< pattern * 64 * kMaxChannels, flat
	Common::Array<Sample> _samples;		///< 1-based; slot 0 is a filler
	Cell _empty;
};

/**
 * The player: a mono AudioStream over one module, started at a given order.
 *
 * Timing is the tracker's own -- `speed` ticks to a row, `tempo` deciding how
 * long a tick is -- so the mixer is fed one tick's worth of samples at a time
 * and the sequencer runs between them.
 */
class S3MPlayer : public Audio::AudioStream {
public:
	/// Where a row's worth of state can be read out without mixing anything, for
	/// the sequencer trace the checker mirrors.
	struct RowTrace {
		uint order;
		uint pattern;
		uint row;
	};

	S3MPlayer(const S3MModule *module, int rate, uint startOrder = 0);
	~S3MPlayer() override;

	// AudioStream
	int readBuffer(int16 *buffer, const int numSamples) override;
	bool isStereo() const override { return false; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return _ended; }

	/// Whether the song has run past its last order at least once. The original
	/// leaves MIDAS to restart at the top, and so does this.
	bool looped() const { return _looped; }

	/**
	 * Step the sequencer over one row without mixing, filling in where that row
	 * sat in the order list. Used by the trace, and by nothing else.
	 */
	bool traceRow(RowTrace &trace);

	uint order() const { return _order; }
	uint row() const { return _row; }
	byte tempo() const { return _tempo; }
	byte speed() const { return _speed; }

private:
	struct Channel {
		const S3MModule::Sample *sample;
		uint instrument;
		int period;
		int targetPeriod;		///< where a tone portamento is heading
		uint32 position;		///< fixed point 16.16 into the sample
		uint32 increment;
		byte volume;
		byte note;

		byte portaSpeed;
		byte volSlide;
		byte tonePorta;
		byte vibratoSpeed;
		byte vibratoDepth;
		byte vibratoPos;
		byte tremorMem;
		byte tremorCount;
		byte retrigMem;
		byte offsetMem;
		byte arpeggio;
		byte noteDelay;			///< SDx: ticks left before the note starts
		S3MModule::Cell delayed;

		Channel() { reset(); }
		void reset();
	};

	void startRow();
	void tickEffects();
	void advanceRow();
	void applyCell(Channel &channel, const S3MModule::Cell &cell);
	void triggerNote(Channel &channel, const S3MModule::Cell &cell);
	void updateIncrement(Channel &channel);
	int periodFor(byte note, uint32 c2spd) const;
	void renderTick(int16 *buffer, int samples);

	const S3MModule *_module;
	int _rate;

	Channel _channels[S3MModule::kMaxChannels];

	uint _startOrder;		///< where the slot put the song, and where it loops back to
	uint _order;
	uint _pattern;
	uint _row;
	byte _speed;
	byte _tempo;
	byte _tick;
	byte _globalVolume;
	byte _masterVolume;

	int _nextOrder;			///< Bxx, or -1
	int _nextRow;			///< Cxx, or -1

	int _samplesLeft;		///< samples still owed on the current tick
	bool _ended;
	bool _looped;
};

} // End of namespace Alien

#endif
