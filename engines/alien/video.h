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

#ifndef ALIEN_VIDEO_H
#define ALIEN_VIDEO_H

#include "audio/audiostream.h"
#include "common/array.h"
#include "common/rational.h"
#include "common/str.h"
#include "graphics/surface.h"
#include "video/video_decoder.h"

namespace Alien {

/**
 * The two video formats the game ships, both described in
 * docs/file_formats.md and mirrored by tools/ma1.py and tools/cda2.py.
 *
 * MA1 is the in-game elevator clip: one palette, 2x2-block deltas, played at a
 * fixed 17.5 fps. CDA2 is the intro and the ending: block-coded deltas with an
 * LZSS wrapper, 16-bit PCM speech and music, and subtitles in four languages,
 * and it is clocked by its own audio rather than by a frame rate.
 */

/**
 * ANIMS/SHIPLIFT.MA1, the alien-ship elevator sequence.
 *
 * The original streams the payload into EMS a page at a time, which is why a
 * frame is addressed as (page, offset) and why the block data skips the last
 * few bytes of a page. Both are kept here: the addressing is the file's, not an
 * artefact of how the original happened to hold it.
 */
class MA1Decoder : public Video::VideoDecoder {
public:
	/// The player shows a frame every fourth vsync tick: 70 Hz over 4.
	static const int kFrameRateNum = 35;
	static const int kFrameRateDen = 2;

	MA1Decoder();

	bool loadStream(Common::SeekableReadStream *stream) override;

private:
	class MA1VideoTrack : public FixedRateVideoTrack {
	public:
		MA1VideoTrack(byte *data, uint32 size, uint16 frameCount, byte fullPages,
					  uint16 tail, uint32 indexOffset, uint32 payloadOffset);
		~MA1VideoTrack() override;

		uint16 getWidth() const override { return kWidth; }
		uint16 getHeight() const override { return kHeight; }
		Graphics::PixelFormat getPixelFormat() const override;
		int getCurFrame() const override { return _curFrame; }
		int getFrameCount() const override { return _frameCount; }
		const Graphics::Surface *decodeNextFrame() override;
		const byte *getPalette() const override;
		bool hasDirtyPalette() const override { return _dirtyPalette; }
		bool isRewindable() const override { return true; }
		bool rewind() override;

	protected:
		Common::Rational getFrameRate() const override {
			return Common::Rational(kFrameRateNum, kFrameRateDen);
		}

	private:
		static const int kWidth = 320;
		static const int kHeight = 200;
		static const uint32 kPageSize = 64000;

		/// Past this offset in a page the player maps the next one, so up to 34
		/// bytes at the end of every page are padding the block data steps over.
		static const uint32 kPageLimit = 0xF9DE;

		static const uint32 kMaskSize = 2000;
		static const int kBlockCols = 20;	///< 20 mask bytes * 8 blocks * 2 px
		static const int kBlockRows = 100;

		uint32 at(uint page, uint32 offset) const {
			return _payloadOffset + page * kPageSize + offset;
		}

		byte *_data;
		uint32 _size;
		uint32 _indexOffset;
		uint32 _payloadOffset;
		int _frameCount;
		int _curFrame;

		Graphics::Surface _surface;
		byte _palette[256 * 3];
		mutable bool _dirtyPalette;
	};
};

/**
 * CD/CDA/ALINTRO.CDA and ALIEND.CDA, the intro and the ending.
 *
 * Not CD audio, whatever the extension says: these are the containers
 * ANIMPLAY.EXE plays. The file is streamed rather than read whole -- the intro
 * is 74 MB -- and only the frame size table and the subtitle block are resident.
 */
class CDA2Decoder : public Video::VideoDecoder {
public:
	/// English, German, French, Finnish, in the order the text block holds them.
	enum Language {
		kEnglish = 0,
		kGerman = 1,
		kFrench = 2,
		kFinnish = 3
	};

	CDA2Decoder();

	bool loadStream(Common::SeekableReadStream *stream) override;

	uint frameCount() const { return _frameCount; }
	uint languageCount() const { return _languages; }
	uint sampleRate() const { return _rate; }

	/// Which language the subtitle calls answer in; out-of-range falls to English.
	void setLanguage(uint language) { _language = language < _languages ? language : 0; }
	uint language() const { return _language; }

	/// One subtitle record: the line and the place the file asks for it.
	struct Subtitle {
		Common::String line;	///< '@' already turned into a newline
		int16 x;				///< negative asks the player to centre the line
		int16 y;				///< negative centres it vertically as well

		Subtitle() : x(-1), y(-1) {}
		bool empty() const { return line.empty(); }
	};

	/// The record on a frame, or an empty line for the empty record the file
	/// uses to mean "nothing showing here".
	Subtitle subtitleAt(uint frame) const;

	/// The record for the frame on screen now.
	Subtitle subtitleAt() const;

	/// The line showing on a frame, with '@' already turned into a newline, or
	/// empty when the frame's record is the empty one the file uses for "none".
	Common::String subtitle(uint frame) const { return subtitleAt(frame).line; }

	/// The line for the frame on screen now.
	Common::String subtitle() const { return subtitleAt().line; }

private:
	static const int kWidth = 320;
	static const int kHeight = 200;
	static const uint32 kHeaderSize = 20;
	static const uint32 kPaletteSize = 768;
	static const int kBlockW = 32;
	static const int kBlockH = 40;
	static const int kSampleBytes = 2;

	// How far ahead of the picture the audio is handed to the mixer. A frame
	// carries one 53ms block of its own, so timing the next frame off the audio
	// already queued leaves the mixer with nothing in hand and it stutters on
	// any hitch; this runs the decode a few blocks early instead.
	static const uint32 kAudioLeadMs = 120;

	// Frame flag bits, named after what the parser does with them.
	enum {
		kFlagPalette = 0x01,
		kFlagAudio = 0x02,
		kFlagPacked = 0x04,
		kFlagHold = 0x08,
		kFlagClear = 0x10,
		kFlagFade = 0x20
	};

	class CDA2AudioTrack : public AudioTrack {
	public:
		CDA2AudioTrack(uint rate);
		~CDA2AudioTrack() override;

		bool endOfTrack() const override { return _finished && _stream->numQueuedStreams() == 0; }
		void queue(const byte *data, uint32 size);
		void finish() { _finished = true; }

	protected:
		Audio::AudioStream *getAudioStream() const override { return _stream; }

	private:
		Audio::QueuingAudioStream *_stream;
		bool _finished;
	};

	/**
	 * The image track. Its pacing comes from the audio it has handed the mixer
	 * so far: a frame stays up for as long as its own audio block, which varies
	 * frame to frame, so the times have to be accumulated and cannot be assumed.
	 */
	class CDA2VideoTrack : public VideoTrack {
	public:
		CDA2VideoTrack(CDA2Decoder *decoder, Common::SeekableReadStream *stream,
					   uint frameCount, uint rate, CDA2AudioTrack *audio);
		~CDA2VideoTrack() override;

		uint16 getWidth() const override { return kWidth; }
		uint16 getHeight() const override { return kHeight; }
		Graphics::PixelFormat getPixelFormat() const override;
		int getCurFrame() const override { return _curFrame; }
		int getFrameCount() const override { return _frameCount; }
		uint32 getNextFrameStartTime() const override { return _nextFrameStartTime; }
		const Graphics::Surface *decodeNextFrame() override;
		const byte *getPalette() const override;
		bool hasDirtyPalette() const override { return _dirtyPalette; }

		/// Where the frames start and how long each one is, read from the tables
		/// in front of them.
		bool readSizes(uint32 dataStart);

	private:
		void drawImage(const byte *payload, uint32 size);
		uint32 drawMasked(const byte *payload, uint32 size, uint32 at, int dst, int cell);
		bool unpackLZSS(const byte *src, uint32 size, uint32 at, uint32 tokens);

		CDA2Decoder *_decoder;
		Common::SeekableReadStream *_stream;
		CDA2AudioTrack *_audio;

		Common::Array<uint32> _sizes;
		uint32 _dataStart;
		uint32 _frameAt;			///< file offset of the frame decoded next

		int _frameCount;
		int _curFrame;
		uint _rate;
		uint32 _samples;			///< audio handed over so far, the clock
		uint32 _nextFrameStartTime;

		Common::Array<byte> _frame;		///< the frame as read from the file
		Common::Array<byte> _unpacked;	///< the same frame with the LZSS undone

		Graphics::Surface _surface;
		byte _palette[256 * 3];
		mutable bool _dirtyPalette;
	};

	CDA2VideoTrack *_video;

	uint _frameCount;
	uint _rate;
	uint _languages;
	uint _language;
	Common::Array<byte> _text;		///< the subtitle block, offsets and all
};

} // End of namespace Alien

#endif
