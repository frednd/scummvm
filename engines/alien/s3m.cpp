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
#include "common/textconsole.h"
#include "common/util.h"

#include "alien/detection.h"
#include "alien/s3m.h"

namespace Alien {

// The periods of the twelve semitones of octave zero, the table every ST3-family
// replayer works from. An octave up halves the period.
static const int kPeriodTable[12] = {
	1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907
};

// The Amiga clock ST3 divides by a period to get a playback rate: 8363 * 1712,
// the rate and period of C-4.
static const int kAmigaClock = 14317056;

// A quarter of a sine, mirrored and negated the way trackers do it, for vibrato.
static const byte kSineTable[32] = {
	  0,  24,  49,  74,  97, 120, 141, 161,
	180, 197, 212, 224, 235, 244, 250, 253,
	255, 253, 250, 244, 235, 224, 212, 197,
	180, 161, 141, 120,  97,  74,  49,  24
};

// Effect commands, stored as 1 = A. Only the fourteen the game's modules use are
// acted on; see docs/sound_reference.md for the scan that measured that.
enum {
	kFxSpeed = 1,			// A
	kFxJump = 2,			// B
	kFxBreak = 3,			// C
	kFxVolumeSlide = 4,		// D
	kFxPortaDown = 5,		// E
	kFxPortaUp = 6,			// F
	kFxTonePorta = 7,		// G
	kFxVibrato = 8,			// H
	kFxTremor = 9,			// I
	kFxArpeggio = 10,		// J
	kFxOffset = 15,			// O
	kFxRetrigger = 17,		// Q
	kFxSpecial = 19,		// S
	kFxTempo = 20			// T
};

// The only Sxy sub-command any module uses.
static const byte kSpecialNoteDelay = 0xD;

static const int kMinPeriod = 64;
static const int kMaxPeriod = 32767;

// -- the module --------------------------------------------------------------

S3MModule::S3MModule()
	: _channels(0), _patternCount(0), _speed(6), _tempo(125), _globalVolume(64),
	  _masterVolume(48), _loaded(false) {
}

S3MModule::~S3MModule() {
	unload();
}

void S3MModule::unload() {
	for (uint i = 0; i < _samples.size(); i++)
		free(_samples[i].data);
	_samples.clear();
	_patterns.clear();
	_orders.clear();
	_channels = 0;
	_patternCount = 0;
	_loaded = false;
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
	Common::Array<byte> data;
	data.resize(size);
	if (!size || s3m.read(&data[0], size) != size)
		return false;
	const byte *d = &data[0];

	if (size < 0x60 || memcmp(d + 0x2C, "SCRM", 4) != 0) {
		warning("Alien::S3MModule: %s is not a Scream Tracker 3 module", file.c_str());
		return false;
	}

	_file = file;
	_title = Common::String((const char *)d, 28);
	while (!_title.empty() && (_title.lastChar() == ' ' || _title.lastChar() == '\0'))
		_title.deleteLastChar();

	const uint16 orderCount = READ_LE_UINT16(d + 0x20);
	const uint16 insCount = READ_LE_UINT16(d + 0x22);
	_patternCount = READ_LE_UINT16(d + 0x24);
	_globalVolume = d[0x30];
	// Bit 7 of the master volume is the stereo flag; every module here is mono.
	_masterVolume = d[0x33] & 0x7F;
	_speed = d[0x31];
	_tempo = d[0x32];
	const byte defaultPan = d[0x35];

	// A channel setting under 16 is a channel that plays; 16 to 31 are muted and
	// 255 is a channel the module does not have at all.
	_channels = 0;
	for (uint i = 0; i < kMaxChannels; i++) {
		if (d[0x40 + i] < 16)
			_channels = i + 1;
	}

	uint32 at = 0x60;
	if (at + orderCount + insCount * 2 + _patternCount * 2 > size)
		return false;

	_orders.resize(orderCount);
	for (uint i = 0; i < orderCount; i++)
		_orders[i] = d[at + i];
	at += orderCount;

	Common::Array<uint32> insPtr;
	insPtr.resize(insCount);
	for (uint i = 0; i < insCount; i++)
		insPtr[i] = READ_LE_UINT16(d + at + i * 2) * 16;
	at += insCount * 2;

	Common::Array<uint32> patPtr;
	patPtr.resize(_patternCount);
	for (uint i = 0; i < _patternCount; i++)
		patPtr[i] = READ_LE_UINT16(d + at + i * 2) * 16;
	at += _patternCount * 2;

	(void)defaultPan;		// the modules are mono, so the pan table is ignored

	// Slot 0 is never addressed -- instrument numbers in a cell are 1-based --
	// so it is filled to keep the indexing literal.
	_samples.resize(insCount + 1);
	for (uint i = 0; i < insCount; i++) {
		const uint32 off = insPtr[i];
		if (!off || off + 0x50 > size)
			continue;
		if (d[off] != 1)	// 0 is an empty slot, 2 and up are Adlib, and none ship
			continue;

		Sample &sample = _samples[i + 1];
		sample.length = READ_LE_UINT32(d + off + 0x10);
		sample.loopStart = READ_LE_UINT32(d + off + 0x14);
		sample.loopEnd = READ_LE_UINT32(d + off + 0x18);
		sample.volume = d[off + 0x1C];
		const byte flags = d[off + 0x1F];
		sample.loops = (flags & 1) != 0;
		sample.c2spd = READ_LE_UINT32(d + off + 0x20);
		sample.name = Common::String((const char *)d + off + 0x30, 28);
		while (!sample.name.empty() &&
			   (sample.name.lastChar() == ' ' || sample.name.lastChar() == '\0'))
			sample.name.deleteLastChar();

		const uint32 pcm = ((uint32)d[off + 0x0D] << 16 | READ_LE_UINT16(d + off + 0x0E)) * 16;
		if (!sample.length || pcm + sample.length > size) {
			sample.length = 0;
			continue;
		}

		// Every sample in every module the game ships is 8-bit unsigned, mono
		// and unpacked; they are stored signed here so mixing is a plain add.
		sample.data = (byte *)malloc(sample.length);
		if (!sample.data) {
			sample.length = 0;
			continue;
		}
		for (uint32 j = 0; j < sample.length; j++)
			sample.data[j] = (byte)(d[pcm + j] ^ 0x80);

		if (sample.loopEnd > sample.length)
			sample.loopEnd = sample.length;
		if (sample.loopStart >= sample.loopEnd)
			sample.loops = false;
	}

	_patterns.resize(_patternCount * kRowsPerPattern * kMaxChannels);
	for (uint i = 0; i < _patternCount; i++) {
		const uint32 off = patPtr[i];
		if (!off || off + 2 > size)
			continue;
		// The length word counts itself, so the body is two bytes shorter.
		const uint16 packed = READ_LE_UINT16(d + off);
		if (packed < 2 || off + packed > size)
			continue;
		readPattern(d + off + 2, packed - 2, i);
	}

	_loaded = true;
	return true;
}

bool S3MModule::readPattern(const byte *body, uint32 size, uint pattern) {
	uint32 at = 0;
	uint row = 0;
	Cell *cells = &_patterns[pattern * kRowsPerPattern * kMaxChannels];

	while (at < size && row < kRowsPerPattern) {
		const byte what = body[at++];
		if (!what) {
			row++;
			continue;
		}

		const uint channel = what & 0x1F;
		Cell &cell = cells[row * kMaxChannels + channel];

		if (what & 0x20) {
			if (at + 2 > size)
				return false;
			cell.note = body[at];
			cell.instrument = body[at + 1];
			at += 2;
		}
		if (what & 0x40) {
			if (at >= size)
				return false;
			cell.volume = body[at++];
		}
		if (what & 0x80) {
			if (at + 2 > size)
				return false;
			cell.command = body[at];
			cell.info = body[at + 1];
			at += 2;
		}
	}
	return row == kRowsPerPattern;
}

byte S3MModule::order(uint index) const {
	return index < _orders.size() ? _orders[index] : 255;
}

const S3MModule::Cell &S3MModule::cell(uint pattern, uint row, uint channel) const {
	if (pattern >= _patternCount || row >= (uint)kRowsPerPattern ||
		channel >= (uint)kMaxChannels)
		return _empty;
	return _patterns[(pattern * kRowsPerPattern + row) * kMaxChannels + channel];
}

const S3MModule::Sample *S3MModule::sample(uint index) const {
	if (index >= _samples.size() || !_samples[index].data)
		return nullptr;
	return &_samples[index];
}

// -- the player --------------------------------------------------------------

void S3MPlayer::Channel::reset() {
	sample = nullptr;
	instrument = 0;
	period = 0;
	targetPeriod = 0;
	position = 0;
	increment = 0;
	volume = 0;
	note = S3MModule::kNoteEmpty;
	portaSpeed = 0;
	volSlide = 0;
	tonePorta = 0;
	vibratoSpeed = 0;
	vibratoDepth = 0;
	vibratoPos = 0;
	tremorMem = 0;
	tremorCount = 0;
	retrigMem = 0;
	offsetMem = 0;
	arpeggio = 0;
	noteDelay = 0;
	delayed = S3MModule::Cell();
}

S3MPlayer::S3MPlayer(const S3MModule *module, int rate, uint startOrder)
	: _module(module), _rate(rate), _startOrder(0), _order(startOrder), _pattern(0), _row(0),
	  _tick(0), _nextOrder(-1), _nextRow(-1), _samplesLeft(0), _ended(false),
	  _looped(false) {
	_speed = module ? module->speed() : 6;
	_tempo = module ? module->tempo() : 125;
	_globalVolume = module ? module->globalVolume() : 64;
	_masterVolume = module ? module->masterVolume() : 48;

	// An order past the end, or one holding the end marker, means the song plays
	// from the top: MIDAS does the same when a slot names an order a shortened
	// module no longer has.
	if (!module || _order >= module->orderCount())
		_order = 0;
	while (module && _order < module->orderCount() && module->order(_order) >= 254)
		_order++;
	if (module && _order >= module->orderCount())
		_order = 0;

	_startOrder = _order;
	_pattern = module ? module->order(_order) : 0;
}

S3MPlayer::~S3MPlayer() {
}

int S3MPlayer::periodFor(byte note, uint32 c2spd) const {
	if (note >= 254 || !c2spd)
		return 0;
	const int octave = note >> 4;
	const int semitone = note & 0x0F;
	if (semitone > 11)
		return 0;
	return (int)((8363L * 16 * kPeriodTable[semitone] >> octave) / (long)c2spd);
}

void S3MPlayer::updateIncrement(Channel &channel) {
	if (channel.period < kMinPeriod) {
		channel.increment = 0;
		return;
	}
	const uint32 frequency = kAmigaClock / channel.period;
	channel.increment = (uint32)(((uint64)frequency << 16) / (uint32)_rate);
}

void S3MPlayer::triggerNote(Channel &channel, const S3MModule::Cell &cell) {
	if (cell.instrument) {
		const S3MModule::Sample *sample = _module->sample(cell.instrument);
		channel.instrument = cell.instrument;
		channel.sample = sample;
		// An instrument on its own resets the volume to the sample's default,
		// with or without a note beside it.
		channel.volume = sample ? sample->volume : 0;
	}

	if (cell.note < 254) {
		const int period = periodFor(cell.note, channel.sample ? channel.sample->c2spd : 8363);
		channel.note = cell.note;

		// A tone portamento takes the note as a destination and leaves the
		// sample where it is; anything else restarts it.
		if (cell.command == kFxTonePorta) {
			channel.targetPeriod = period;
		} else {
			channel.period = period;
			channel.targetPeriod = period;
			channel.position = (cell.command == kFxOffset)
							   ? ((uint32)channel.offsetMem << 8 << 16) : 0;
			channel.vibratoPos = 0;
			channel.tremorCount = 0;
		}
	} else if (cell.note == S3MModule::kNoteCut) {
		channel.volume = 0;
		channel.sample = nullptr;
	}

	if (cell.volume != S3MModule::kVolumeEmpty)
		channel.volume = MIN<byte>(cell.volume, 64);

	updateIncrement(channel);
}

void S3MPlayer::applyCell(Channel &channel, const S3MModule::Cell &cell) {
	// The effect memories are filled before the note is triggered, because Oxx
	// and Gxx both change what triggering it does.
	switch (cell.command) {
	case kFxOffset:
		if (cell.info)
			channel.offsetMem = cell.info;
		break;
	case kFxTonePorta:
		if (cell.info)
			channel.tonePorta = cell.info;
		break;
	case kFxVolumeSlide:
		if (cell.info)
			channel.volSlide = cell.info;
		break;
	case kFxPortaUp:
	case kFxPortaDown:
		if (cell.info)
			channel.portaSpeed = cell.info;
		break;
	case kFxVibrato:
		if (cell.info >> 4)
			channel.vibratoSpeed = cell.info >> 4;
		if (cell.info & 0x0F)
			channel.vibratoDepth = cell.info & 0x0F;
		break;
	case kFxTremor:
		if (cell.info)
			channel.tremorMem = cell.info;
		break;
	case kFxRetrigger:
		if (cell.info)
			channel.retrigMem = cell.info;
		break;
	case kFxArpeggio:
		if (cell.info)
			channel.arpeggio = cell.info;
		break;
	default:
		break;
	}

	triggerNote(channel, cell);

	// The effects that act on tick zero and then not again.
	switch (cell.command) {
	case kFxSpeed:
		if (cell.info)
			_speed = cell.info;
		break;
	case kFxTempo:
		if (cell.info >= 32)
			_tempo = cell.info;
		break;
	case kFxJump:
		_nextOrder = cell.info;
		break;
	case kFxBreak:
		// The parameter is two decimal digits, as the tracker shows it.
		_nextRow = (cell.info >> 4) * 10 + (cell.info & 0x0F);
		break;
	case kFxVolumeSlide:
		// DxF and DFx are the fine slides: one step, on tick zero only.
		if ((channel.volSlide & 0x0F) == 0x0F && (channel.volSlide >> 4))
			channel.volume = MIN(64, channel.volume + (channel.volSlide >> 4));
		else if ((channel.volSlide >> 4) == 0x0F && (channel.volSlide & 0x0F))
			channel.volume = (byte)MAX(0, channel.volume - (channel.volSlide & 0x0F));
		break;
	case kFxPortaUp:
	case kFxPortaDown: {
		const byte info = channel.portaSpeed;
		int step = 0;
		if ((info >> 4) == 0x0F)
			step = (info & 0x0F) * 4;		// Fine: one step of four units
		else if ((info >> 4) == 0x0E)
			step = info & 0x0F;				// Extra fine: one unit a step
		if (step) {
			channel.period += (cell.command == kFxPortaUp) ? -step : step;
			channel.period = CLIP(channel.period, kMinPeriod, kMaxPeriod);
			updateIncrement(channel);
		}
		break;
	}
	default:
		break;
	}
}

void S3MPlayer::startRow() {
	if (!_module)
		return;

	_pattern = _module->order(_order);

	for (uint i = 0; i < _module->channelCount(); i++) {
		Channel &channel = _channels[i];
		const S3MModule::Cell &cell = _module->cell(_pattern, _row, i);

		// SDx holds the note back by x ticks; the cell is kept and played then.
		if (cell.command == kFxSpecial && (cell.info >> 4) == kSpecialNoteDelay &&
			(cell.info & 0x0F)) {
			channel.noteDelay = cell.info & 0x0F;
			channel.delayed = cell;
			channel.delayed.command = 0;
			continue;
		}

		channel.noteDelay = 0;
		applyCell(channel, cell);
	}
}

void S3MPlayer::tickEffects() {
	if (!_module)
		return;

	for (uint i = 0; i < _module->channelCount(); i++) {
		Channel &channel = _channels[i];

		if (channel.noteDelay) {
			channel.noteDelay--;
			if (!channel.noteDelay)
				triggerNote(channel, channel.delayed);
			continue;
		}

		const S3MModule::Cell &cell = _module->cell(_pattern, _row, i);
		switch (cell.command) {
		case kFxVolumeSlide: {
			const byte up = channel.volSlide >> 4;
			const byte down = channel.volSlide & 0x0F;
			if (up && down != 0x0F)
				channel.volume = MIN(64, channel.volume + up);
			else if (down && up != 0x0F)
				channel.volume = (byte)MAX(0, channel.volume - down);
			break;
		}
		case kFxPortaUp:
		case kFxPortaDown: {
			const byte info = channel.portaSpeed;
			if ((info >> 4) >= 0x0E)
				break;			// the fine forms already moved, on tick zero
			const int step = info * 4;
			channel.period += (cell.command == kFxPortaUp) ? -step : step;
			channel.period = CLIP(channel.period, kMinPeriod, kMaxPeriod);
			updateIncrement(channel);
			break;
		}
		case kFxTonePorta: {
			const int step = channel.tonePorta * 4;
			if (!channel.targetPeriod || !step)
				break;
			if (channel.period < channel.targetPeriod)
				channel.period = MIN(channel.period + step, channel.targetPeriod);
			else
				channel.period = MAX(channel.period - step, channel.targetPeriod);
			updateIncrement(channel);
			break;
		}
		case kFxVibrato: {
			channel.vibratoPos = (channel.vibratoPos + channel.vibratoSpeed) & 0x3F;
			const int sine = kSineTable[channel.vibratoPos & 0x1F];
			const int delta = sine * channel.vibratoDepth / 128;
			const int period = channel.vibratoPos < 32 ? channel.period + delta
													   : channel.period - delta;
			const int saved = channel.period;
			channel.period = CLIP(period, kMinPeriod, kMaxPeriod);
			updateIncrement(channel);
			channel.period = saved;		// the swing is around the note, not from it
			break;
		}
		case kFxTremor: {
			const byte on = (channel.tremorMem >> 4) + 1;
			const byte off = (channel.tremorMem & 0x0F) + 1;
			channel.tremorCount++;
			if (channel.tremorCount >= on + off)
				channel.tremorCount = 0;
			break;
		}
		case kFxArpeggio: {
			if (channel.note >= 254 || !channel.sample)
				break;
			const int which = _tick % 3;
			int note = channel.note;
			if (which == 1)
				note += channel.arpeggio >> 4;
			else if (which == 2)
				note += channel.arpeggio & 0x0F;
			// Notes are octave * 16 + semitone, so an arpeggio step can carry
			// past the twelfth semitone and has to be renormalised.
			int semitone = (note & 0x0F) + ((note >> 4) * 12);
			const int period = periodFor((byte)(((semitone / 12) << 4) | (semitone % 12)),
										 channel.sample->c2spd);
			const int saved = channel.period;
			if (period) {
				channel.period = period;
				updateIncrement(channel);
			}
			channel.period = saved;
			break;
		}
		case kFxRetrigger: {
			const byte every = channel.retrigMem & 0x0F;
			if (!every || (_tick % every))
				break;
			channel.position = 0;
			switch (channel.retrigMem >> 4) {
			case 1: case 2: case 3: case 4: case 5:
				channel.volume = (byte)MAX(0, channel.volume - (1 << ((channel.retrigMem >> 4) - 1)));
				break;
			case 6:
				channel.volume = channel.volume * 2 / 3;
				break;
			case 7:
				channel.volume /= 2;
				break;
			case 9: case 0xA: case 0xB: case 0xC: case 0xD:
				channel.volume = MIN(64, channel.volume + (1 << ((channel.retrigMem >> 4) - 9)));
				break;
			case 0xE:
				channel.volume = MIN(64, channel.volume * 3 / 2);
				break;
			case 0xF:
				channel.volume = MIN(64, channel.volume * 2);
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
	}
}

void S3MPlayer::advanceRow() {
	if (!_module)
		return;

	if (_nextOrder >= 0) {
		_order = (uint)_nextOrder;
		_row = (_nextRow >= 0) ? (uint)_nextRow : 0;
		_nextOrder = _nextRow = -1;
	} else if (_nextRow >= 0) {
		_order++;
		_row = (uint)_nextRow;
		_nextRow = -1;
	} else {
		_row++;
		if (_row >= (uint)S3MModule::kRowsPerPattern) {
			_row = 0;
			_order++;
		}
	}

	// 254 is the skip marker and 255 the end of the list. No module the game
	// ships uses the skip marker, but stepping over it costs one branch.
	while (_order < _module->orderCount() && _module->order(_order) == 254)
		_order++;

	// 255 ends a song. Three of the modules are banks of several songs with a
	// marker between them, and a slot names the order in front of the one it
	// wants, so the end of a track is the marker after it: looping back to where
	// the slot started keeps a track playing itself rather than falling into its
	// neighbour. Which of the two MIDAS does is not recorded anywhere the
	// disassembly shows.
	if (_order >= _module->orderCount() || _module->order(_order) == 255) {
		_order = _startOrder;
		_row = 0;
		_looped = true;
	}

	_pattern = _module->order(_order);
	if (_row >= (uint)S3MModule::kRowsPerPattern)
		_row = 0;
}

bool S3MPlayer::traceRow(RowTrace &trace) {
	if (!_module || !_module->isLoaded())
		return false;

	trace.order = _order;
	trace.pattern = _module->order(_order);
	trace.row = _row;

	startRow();
	for (_tick = 1; _tick < _speed; _tick++)
		tickEffects();
	advanceRow();
	_tick = 0;
	return true;
}

void S3MPlayer::renderTick(int16 *buffer, int samples) {
	if (!_module)
		return;

	for (uint i = 0; i < _module->channelCount(); i++) {
		Channel &channel = _channels[i];
		if (!channel.sample || !channel.increment || !channel.volume)
			continue;

		// Tremor silences the channel for the off half of its cycle.
		const S3MModule::Cell &cell = _module->cell(_pattern, _row, i);
		if (cell.command == kFxTremor) {
			const byte on = (channel.tremorMem >> 4) + 1;
			if (channel.tremorCount >= on)
				continue;
		}

		const S3MModule::Sample &sample = *channel.sample;
		for (int s = 0; s < samples; s++) {
			while ((channel.position >> 16) >= sample.length) {
				if (!sample.loops) {
					channel.sample = nullptr;
					break;
				}
				channel.position -= (sample.loopEnd - sample.loopStart) << 16;
			}
			if (!channel.sample)
				break;

			// The 8-bit sample scaled up to the mixer's 16, then through the
			// three volumes MIDAS stacks: the channel's own, the module's global
			// volume and its master volume, each 0 to 64.
			int value = (int)(int8)sample.data[channel.position >> 16] * 256;
			value = value * channel.volume / 64;
			value = value * _globalVolume / 64;
			value = value * _masterVolume / 64;
			buffer[s] = (int16)CLIP<int>(buffer[s] + value, -32768, 32767);
			channel.position += channel.increment;
		}
	}
}

int S3MPlayer::readBuffer(int16 *buffer, const int numSamples) {
	if (!_module || !_module->isLoaded()) {
		_ended = true;
		return 0;
	}

	memset(buffer, 0, numSamples * sizeof(int16));

	int written = 0;
	while (written < numSamples) {
		if (_samplesLeft <= 0) {
			// Tick zero of a row is the row itself; the ticks after it are the
			// effects running on. A tick lasts 2.5 seconds over the tempo, which
			// is the tracker's own definition of BPM.
			if (_tick == 0)
				startRow();
			else
				tickEffects();
			_samplesLeft = (int)((uint32)_rate * 5 / (2 * (uint32)_tempo));
		}

		const int chunk = MIN(numSamples - written, _samplesLeft);
		renderTick(buffer + written, chunk);
		written += chunk;
		_samplesLeft -= chunk;

		if (_samplesLeft <= 0) {
			_tick++;
			if (_tick >= _speed) {
				advanceRow();
				_tick = 0;
			}
		}
	}

	return numSamples;
}

} // End of namespace Alien
