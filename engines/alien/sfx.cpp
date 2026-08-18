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
#include "audio/decoders/raw.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/path.h"

#include "alien/detection.h"
#include "alien/sfx.h"
#include "alien/tables.h"

namespace Alien {

// The S3M header. The tag is what tells a module from anything else with the
// same extension, and the two counts locate the parapointer arrays that follow
// the order list.
static const uint32 kTagOffset = 0x2C;
static const uint32 kOrderCount = 0x20;
static const uint32 kInstrumentCount = 0x22;
static const uint32 kOrderList = 0x60;

// One instrument header, 0x50 bytes at its parapointer. Type 1 is a PCM sample;
// anything else is an Adlib instrument or an empty slot, and neither occurs in
// the SFX banks.
static const uint kInstrumentHeader = 0x50;
static const uint kInsType = 0x00;
static const uint kInsMemSegHigh = 0x0D;
static const uint kInsMemSegLow = 0x0E;
static const uint kInsLength = 0x10;
static const uint kInsFlags = 0x1F;
static const uint kInsC2Spd = 0x20;
static const uint kInsName = 0x30;
static const uint kInsNameLength = 28;
static const byte kInsTypePCM = 1;
static const byte kInsFlagLoop = 1;

// Parapointers and the sample pointer are both in 16-byte paragraphs.
static const uint kParagraph = 16;

SoundBank::SoundBank() : _slots(0) {
	for (uint i = 0; i <= kMaxSlots; i++) {
		_sample[i].data = nullptr;
		_sample[i].length = 0;
		_sample[i].c2spd = 0;
	}
}

SoundBank::~SoundBank() {
	unload();
}

void SoundBank::unload() {
	for (uint i = 0; i <= kMaxSlots; i++) {
		delete[] _sample[i].data;
		_sample[i].data = nullptr;
		_sample[i].length = 0;
		_sample[i].c2spd = 0;
		_sample[i].name.clear();
	}
	_slots = 0;
	_file.clear();
}

bool SoundBank::load(const Common::String &file) {
	unload();

	// The names in the table carry the DOS separator, so the path is split on it
	// rather than handed over as one component.
	Common::File module;
	if (!module.open(Common::Path(file, '\\'))) {
		warning("Alien::SoundBank: could not open %s", file.c_str());
		return false;
	}

	const uint32 size = (uint32)module.size();
	byte *raw = new byte[size];
	if (module.read(raw, size) != size) {
		delete[] raw;
		return false;
	}

	if (size < kOrderList || memcmp(raw + kTagOffset, "SCRM", 4)) {
		warning("Alien::SoundBank: %s is not a module", file.c_str());
		delete[] raw;
		return false;
	}

	const uint orders = READ_LE_UINT16(raw + kOrderCount);
	const uint instruments = READ_LE_UINT16(raw + kInstrumentCount);
	const uint32 pointers = kOrderList + orders;
	if (instruments > kMaxSlots || pointers + instruments * 2 > size) {
		warning("Alien::SoundBank: %s declares %u instruments", file.c_str(), instruments);
		delete[] raw;
		return false;
	}

	uint loaded = 0;
	for (uint slot = 1; slot <= instruments; slot++) {
		const uint32 header = READ_LE_UINT16(raw + pointers + (slot - 1) * 2) * kParagraph;
		if (!header || header + kInstrumentHeader > size)
			continue;
		if (raw[header + kInsType] != kInsTypePCM)
			continue;

		const uint32 length = READ_LE_UINT32(raw + header + kInsLength);
		const uint32 offset = ((READ_LE_UINT16(raw + header + kInsMemSegLow)
								| (raw[header + kInsMemSegHigh] << 16)) * kParagraph);
		if (!length || offset + length > size) {
			warning("Alien::SoundBank: %s slot %u runs past the end of the file",
					file.c_str(), slot);
			continue;
		}

		// No SFX sample is flagged looping -- all 18 banks were measured -- so a
		// loop here would mean the reader lost alignment rather than that a
		// door creak sustains.
		if (raw[header + kInsFlags] & kInsFlagLoop)
			warning("Alien::SoundBank: %s slot %u is flagged looping", file.c_str(), slot);

		Sample &sample = _sample[slot];
		sample.data = new byte[length];
		memcpy(sample.data, raw + offset, length);
		sample.length = length;
		sample.c2spd = READ_LE_UINT32(raw + header + kInsC2Spd);

		char label[kInsNameLength + 1];
		memcpy(label, raw + header + kInsName, kInsNameLength);
		label[kInsNameLength] = '\0';
		sample.name = Common::String(label);
		sample.name.trim();
		loaded++;
	}

	delete[] raw;
	_slots = instruments;
	_file = file;
	return loaded > 0;
}

const SoundBank::Sample *SoundBank::sample(uint index) const {
	if (index < 1 || index > kMaxSlots || !_sample[index].data)
		return nullptr;
	return &_sample[index];
}

SoundFX::SoundFX() : _mixer(nullptr), _nextVoice(0), _bankIndex(-1), _write(0) {
	memset(_queue, 0, sizeof(_queue));
}

SoundFX::~SoundFX() {
	flush();
}

bool SoundFX::enterRoom(const StaticTables &tables, int room) {
	// Whatever was queued belonged to the room being left; the original clears
	// all ten slots on the way out (INPUT:0x67B).
	flush();

	const byte bank = tables.sfxBank(room);
	if (bank == StaticTables::kSfxBankNone || (int)bank == _bankIndex)
		return false;

	const Common::String &name = tables.sfxName(bank);
	if (name.empty())
		return false;

	_bank.unload();
	_bankIndex = _bank.load(name) ? (int)bank : -1;
	return _bankIndex >= 0;
}

void SoundFX::play(uint sample, uint32 rate, byte volume, int8 panning) {
	if (!_mixer)
		return;

	const SoundBank::Sample *pcm = _bank.sample(sample);
	if (!pcm) {
		debugC(1, kDebugSound, "sfx: bank %s has no sample %u",
			   _bank.file().c_str(), sample);
		return;
	}

	// The voice is taken whether or not it is still sounding, so a fourth effect
	// cuts the oldest of the three off, as it does in the original.
	Audio::SoundHandle &voice = _voice[_nextVoice];
	_mixer->stopHandle(voice);

	// The sample data stays with the bank: the stream must not free it, and the
	// bank outlives every voice because a room change silences them first.
	Common::SeekableReadStream *stream =
		new Common::MemoryReadStream(pcm->data, pcm->length, DisposeAfterUse::NO);
	Audio::AudioStream *audio = Audio::makeRawStream(stream, rate, Audio::FLAG_UNSIGNED,
													 DisposeAfterUse::YES);

	// MIDAS takes 0..64 and -64..+64; the mixer takes 0..255 and -127..+127.
	const byte mixerVolume = (byte)(volume * Audio::Mixer::kMaxChannelVolume / kFullVolume);
	const int8 balance = (int8)(panning * 127 / (int)kFullVolume);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &voice, audio, -1,
					   mixerVolume, balance);

	debugC(2, kDebugSound, "sfx: voice %u sample %u (%s) %u Hz vol %u pan %d",
		   _nextVoice, sample, pcm->name.c_str(), rate, volume, panning);

	_nextVoice = (_nextVoice + 1) % kVoiceCount;
}

void SoundFX::queue(uint sample, uint32 rate, byte volume, int8 panning, uint16 delay) {
	// The cursor advances whether the slot it landed on was free or not, so a
	// busy slot is overwritten -- the original does not look before it writes.
	Trigger &slot = _queue[_write];
	slot.busy = true;
	slot.sample = sample;
	slot.rate = rate;
	slot.volume = volume;
	slot.panning = panning;
	slot.delay = delay;
	_write = (_write + 1) % kQueueSize;

	debugC(2, kDebugSound, "sfx: queued sample %u in %u ticks, %u Hz vol %u pan %d",
		   sample, delay, rate, volume, panning);
}

void SoundFX::tick(bool pairTick) {
	for (uint i = 0; i < kQueueSize; i++) {
		Trigger &slot = _queue[i];
		if (!slot.busy)
			continue;

		// Two steps, the way INPUT:0x5D9 has them: the countdown only moves on
		// the tick pair, and the test for zero runs on every tick. A trigger
		// queued with no delay therefore sounds on the next service.
		if (pairTick && slot.delay > 0)
			slot.delay--;

		if (slot.delay > 0)
			continue;

		slot.busy = false;
		play(slot.sample, slot.rate, slot.volume, slot.panning);
	}
}

void SoundFX::flush() {
	memset(_queue, 0, sizeof(_queue));
	_write = 0;

	if (!_mixer)
		return;
	for (uint i = 0; i < kVoiceCount; i++)
		_mixer->stopHandle(_voice[i]);
}

uint SoundFX::pending() const {
	uint waiting = 0;
	for (uint i = 0; i < kQueueSize; i++) {
		if (_queue[i].busy)
			waiting++;
	}
	return waiting;
}

} // End of namespace Alien
