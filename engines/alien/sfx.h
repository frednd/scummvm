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

#ifndef ALIEN_SFX_H
#define ALIEN_SFX_H

#include "audio/mixer.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Alien {

class StaticTables;

/**
 * One SFX sample bank: the samples of an SFX\*.S3M, without its pattern data.
 *
 * The 18 files under SFX/ are Scream Tracker 3 modules, but not songs. Each is a
 * bank of named one-shots -- doors, switches, the hammer through the window --
 * that a room's code triggers by instrument slot number. Their pattern data is
 * identical boilerplate in all 18 files, left over from the tracker the sound
 * designer used as a sample editor, and playing it would fire every effect in the
 * bank inside six seconds. So only the instrument headers are read.
 *
 * Every sample in every bank is 8-bit unsigned mono, unpacked, and none is
 * flagged looping, which is what makes this a header parse and not a replayer.
 * See docs/sound_reference.md and docs/sfx_catalog.md.
 */
class SoundBank {
public:
	/// Instrument slots per module, which bounds a bank's sample indices.
	static const uint kMaxSlots = 99;

	struct Sample {
		Common::String name;	///< the designer's own label, cp437
		byte *data;				///< 8-bit unsigned PCM, owned by the bank
		uint32 length;
		uint32 c2spd;			///< the stored rate; the trigger overrides it
	};

	SoundBank();
	~SoundBank();

	bool load(const Common::String &file);
	void unload();
	bool isLoaded() const { return _slots > 0; }

	/// The name the bank was loaded from, for logging.
	const Common::String &file() const { return _file; }

	/// How many instrument slots the module declares, empty ones included.
	uint slotCount() const { return _slots; }

	/// One sample by its 1-based instrument slot, or null for an empty slot.
	const Sample *sample(uint index) const;

private:
	Common::String _file;
	uint _slots;
	Sample _sample[kMaxSlots + 1];	///< index 0 unused; slots are 1-based
};

/**
 * The sound effects: the resident bank, three voices and the delay queue.
 *
 * A room's effects all come out of one bank, chosen by the selector table the
 * room number indexes (StaticTables::sfxBank). Only one bank is resident, and
 * entering a room whose selector names the bank already loaded changes nothing --
 * the original compares the two indices before reloading.
 *
 * Triggers come in two shapes, both from the room's own code:
 *
 * - `sound(n)` (INPUT:0x194) plays slot n straight away, centred, at full volume
 *   and 11000 Hz -- the rate is always passed explicitly and is never the C2SPD
 *   in the sample header.
 * - `play_sample(n, rate, volume, panning, delay)` (INPUT:0x55E) writes the
 *   trigger into a ten-slot queue with a countdown in ticks, which is how an
 *   effect is synchronised to a point inside an animation. The countdown steps on
 *   the same tick pair the animation slots do, and the slot fires the tick its
 *   counter reads zero.
 *
 * Playback is three voices used round-robin, so a fourth effect steals the
 * oldest: MIDAS is handed a channel number that increments and wraps at three.
 * Volume is MIDAS's 0..64 and panning its -64..+64.
 */
class SoundFX {
public:
	static const uint kVoiceCount = 3;
	static const uint kQueueSize = 10;

	/// What sound() passes: full volume, centred, 11000 Hz.
	static const byte kFullVolume = 64;
	static const uint32 kDefaultRate = 11000;

	SoundFX();

	/// Silences the voices before the bank they read from goes away: the engine's
	/// own members are destroyed ahead of the mixer.
	~SoundFX();

	void init(Audio::Mixer *mixer) { _mixer = mixer; }

	/**
	 * Make the bank the room needs resident. Returns true when a load happened,
	 * so the caller can log it; a room that names the loaded bank, or names none
	 * at all, keeps what is playing.
	 */
	bool enterRoom(const StaticTables &tables, int room);

	const SoundBank &bank() const { return _bank; }
	int bankIndex() const { return _bankIndex; }

	/** Play a sample now, on the next voice in the rotation. */
	void play(uint sample, uint32 rate, byte volume, int8 panning);

	/** Hand a trigger to the delay queue, `delay` ticks from now. */
	void queue(uint sample, uint32 rate, byte volume, int8 panning, uint16 delay);

	/**
	 * Service the queue. `pairTick` is the tick-pair gate the original tests
	 * ([0xa5fc]): the countdowns only step on those ticks, but a slot whose
	 * counter is already zero fires on any of them -- which is what makes a
	 * queued trigger with a delay of zero sound on the next service rather than
	 * never.
	 */
	void tick(bool pairTick);

	/** Drop every queued trigger and silence the voices: the room change. */
	void flush();

	/** How many queue slots are waiting, for the debug dump. */
	uint pending() const;

private:
	Audio::Mixer *_mixer;
	Audio::SoundHandle _voice[kVoiceCount];
	uint _nextVoice;

	SoundBank _bank;
	int _bankIndex;

	struct Trigger {
		bool busy;
		uint sample;
		uint32 rate;
		byte volume;
		int8 panning;
		uint16 delay;
	};

	Trigger _queue[kQueueSize];
	uint _write;			///< the queue is a ring with one cursor, as in the original
};

} // End of namespace Alien

#endif
