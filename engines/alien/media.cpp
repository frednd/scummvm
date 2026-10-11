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

#include "common/config-manager.h"
#include "common/events.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

/**
 * Start a music slot, the way INPUT:music_play_slot does.
 *
 * The slot is not a module: two 14-byte tables turn it into a module index and
 * the order in that module's own list the track starts at, which is why a search
 * for module names over the overlays misses most of the music.
 */
void AlienEngine::playMusicSlot(uint slot) {
	if (slot >= (uint)StaticTables::kMusicSlotCount)
		return;

	stopMusic();
	traceEvent("music %u", slot);

	const byte module = _tables.musicSlotModule(slot);
	const byte order = _tables.musicSlotOrder(slot);
	const Common::String &name = _tables.musicName(module);
	if (name.empty() || !_music.load(name)) {
		warning("could not load the music module for slot %u", slot);
		return;
	}

	_musicSlot = (int)slot;
	debugC(1, kDebugMusic, "slot %u: module %d %s at order %d, %u channels", slot, module,
		   name.c_str(), order, _music.channelCount());

	// The player owns nothing but the module, which outlives it, so the mixer is
	// free to dispose of the stream when the track is stopped.
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_musicHandle,
					   new S3MPlayer(&_music, _mixer->getOutputRate(), order));
}

void AlienEngine::stopMusic() {
	if (_mixer->isSoundHandleActive(_musicHandle))
		_mixer->stopHandle(_musicHandle);
	_musicSlot = -1;
}

/**
 * Show a clip, from its first frame to its last, with everything else stopped.
 *
 * Both players in the original are the same shape: the screen belongs to the
 * clip, the game loop does not run, and a keypress or a click ends it early.
 */
void AlienEngine::playVideo(Video::VideoDecoder &video, CDA2Decoder *subtitles) {
	// A clip owns the whole screen, and the original clears cursor_visible
	// [0xa948] before one plays.
	CursorMan.showMouse(false);
	video.start();
	offerSkip();

	// A clip keeps time by the audio the mixer has played, which turbo has no
	// way to hurry, so there it goes a frame per pass instead: every frame is
	// still decoded and drawn, only the waiting between them is gone.
	bool skipped = false;
	// And the clip is over at its last frame rather than when the mixer has
	// played the last of its sound, which turbo would otherwise wait out in
	// real time.
	while (!shouldQuit() && !skipped && !video.endOfVideo() &&
		   !(turbo() && video.getCurFrame() + 1 >= (int)video.getFrameCount())) {
		if (turbo() || video.needsUpdate()) {
			const Graphics::Surface *frame = video.decodeNextFrame();
			if (frame) {
				const int w = MIN<int>(frame->w, _screen.w);
				const int h = MIN<int>(frame->h, _screen.h);
				for (int y = 0; y < h; y++)
					memcpy(_screen.getBasePtr(0, y), frame->getBasePtr(0, y), w);

				// getPalette() clears the dirty flag, so what it says has to be
				// asked before the palette is taken.
				const bool changed = video.hasDirtyPalette();
				const byte *palette = video.getPalette();
				if (changed)
					g_system->getPaletteManager()->setPalette(palette, 0, 256);

				if (subtitles && _subtitles)
					drawSubtitle(*subtitles);

				g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0,
										   _screen.w, _screen.h);
			}
			present();
		}

		// ANIMPLAY's keyboard handler keeps a bitmap of the keys held, and the
		// player stops only on Esc's bit ([0x34ec8] & 2); it never reads the
		// mouse buttons. A click here used to end the clip, so one stray click
		// in the ending lost the credits and "Thanks for playing" (manual
		// player).
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN &&
				event.kbd.keycode == Common::KEYCODE_ESCAPE)
				skipped = true;
		}

		sleep(10);
	}

	video.stop();
	video.close();

	// The room the clip interrupted owns the screen again, palette and all.
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
}

/**
 * Draw the line the clip is on, in the player's own face.
 *
 * ANIMPLAY:sub_11da1 sets subtitles in a font of its own, out of tables in the
 * player executable, and every pixel of it is palette index 255 -- the
 * near-white each CDA2 palette reserves for it. The record carries the place
 * as well as the text: a negative x centres each line on its own width, and a
 * negative y centres the block of them on eight rows a line.
 */
void AlienEngine::drawSubtitle(const CDA2Decoder &video) {
	if (!_videoFont.isLoaded())
		return;

	const CDA2Decoder::Subtitle record = video.subtitleAt();
	if (record.empty())
		return;

	// A line break is '@', already a newline by the time the record is read.
	Common::Array<Common::String> lines;
	uint start = 0;
	while (start <= record.line.size()) {
		uint end = start;
		while (end < record.line.size() && record.line[end] != '\n')
			end++;
		lines.push_back(Common::String(record.line.c_str() + start, end - start));
		if (end >= record.line.size())
			break;
		start = end + 1;
	}

	const int step = _videoFont.lineHeight();
	int y = record.y;
	if (record.y < 0)
		y = (kScreenHeight - (int)lines.size() * step) / 2;

	for (uint i = 0; i < lines.size(); i++, y += step) {
		if (lines[i].empty())
			continue;
		const int x = record.x < 0 ? (kScreenWidth - _videoFont.measure(lines[i])) / 2 : record.x;
		_videoFont.drawString(_screen, lines[i], x, y);
	}
}

/**
 * The elevator clip, on the first entry into a room the lift serves.
 *
 * 0FAE:sub_101a6 guards it with [0x33de], so it plays once a session however
 * often the player rides the lift afterwards.
 */
bool AlienEngine::playLift() {
	if (_liftPlayed || _script.flag(0x33de))
		return false;
	// The latch goes up whether or not the clip can be played, as 0FAE:0x06d8
	// sets it after the call; the savegame carries it.
	_script.setFlag(0x33de, 1);

	MA1Decoder video;
	if (!video.loadFile(Common::Path(kLiftClip))) {
		debugC(1, kDebugVideo, "could not open %s", kLiftClip);
		return false;
	}

	_liftPlayed = true;
	debugC(1, kDebugVideo, "%s: %d frames", kLiftClip, video.getFrameCount());
	traceEvent("video %s", kLiftClip);
	playVideo(video);
	return true;
}

bool AlienEngine::playCutscene(const char *file) {
	CDA2Decoder video;
	if (!video.loadFile(Common::Path(file))) {
		debugC(1, kDebugVideo, "could not open %s", file);
		return false;
	}

	// The subtitle face lives in the player executable and nothing outside a
	// clip is set in it, so it is read the first time one plays.
	if (!_videoFont.isLoaded())
		_videoFont.load();

	video.setLanguage(subtitleLanguage());
	traceEvent("video %s", file);
	debugC(1, kDebugVideo, "%s: %u frames, %u Hz, %u languages, showing %u", file,
		   video.frameCount(), video.sampleRate(), video.languageCount(), video.language());

	playVideo(video, &video);
	return true;
}

void AlienEngine::syncSoundSettings() {
	Engine::syncSoundSettings();
	_subtitles = ConfMan.getBool("subtitles");
}

/// Which of the file's four subtitle tracks this run's language asks for.
uint AlienEngine::subtitleLanguage() const {
	switch (getLanguage()) {
	case Common::DE_DEU:
		return CDA2Decoder::kGerman;
	case Common::FR_FRA:
		return CDA2Decoder::kFrench;
	case Common::FI_FIN:
		return CDA2Decoder::kFinnish;
	default:
		return CDA2Decoder::kEnglish;
	}
}

/**
 * The three LOGIC routines a room's tick calls just before it restarts one of
 * its loops: each looks at that slot's frames left ([0xa4ea + slot]) and, on
 * the last one, queues a pair of samples, so the animation makes its sound
 * once a cycle. Room 26's squirrel (LOGIC:0x086d), room 56's beam
 * (LOGIC:0x089d) and room 48's slot 2 (LOGIC:0x08cd).
 */
struct LoopSound {
	int room;
	uint slot;
	struct {
		byte sample;
		uint16 rate;
		byte volume;
		int8 panning;
		uint16 delay;
	} plays[2];
};

static const LoopSound kLoopSounds[] = {
	{ 26, 3, { { 3, 0xd6d8, 0x1e,  0x32, 0x0a }, { 3, 0xcb20, 0x14,  0x32, 0x16 } } },
	{ 48, 2, { { 1, 0x30d4, 0x26, -0x14, 0x2b }, { 1, 0x30d4, 0x26,  0x14, 0xd0 } } },
	{ 56, 0, { { 5, 0x3e80, 0x2d, -0x1e, 0x03 }, { 5, 0x7d00, 0x28, -0x1e, 0x26 } } },
};

void AlienEngine::stepLoopSounds() {
	for (uint i = 0; i < ARRAYSIZE(kLoopSounds); i++) {
		const LoopSound &l = kLoopSounds[i];
		if (l.room != _room || _anims.remaining(l.slot) != 1)
			continue;
		for (uint p = 0; p < 2; p++)
			_sound.queue(l.plays[p].sample, l.plays[p].rate, l.plays[p].volume,
						 l.plays[p].panning, l.plays[p].delay);
	}
}

} // End of namespace Alien
