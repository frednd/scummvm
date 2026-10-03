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

#include "common/events.h"
#include "common/system.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/font.h"
#include "alien/resources.h"

namespace Alien {

// The TV news studio: scene id 14, which rooms 31 and 40 raise as they open
// once the maze is done ([0xa77d] == 1, written in the crystal room).
//
// It is CUTSCENE:sub_0c6dd, written out by hand like the look through the
// front door's glass (hallway.cpp) rather than read from a record. Its files
// are string literals in the code segment (0c55:0130): STUDIO.PCX is the
// plate, NEWSMAN/KAMERA/STUDBARS go into slots 0-2, news_eka.tal is the
// dialog, OBJSTUD.PCX replaces the font page for the length of the scene and
// NEWS.PCX is the picture the studio's wall screen shows. Music is slot 8,
// UUTISET.S3M.
//
// **No slot ever plays.** The scene draws bank frames itself, through the
// three MIDAS routines a room never calls this way:
//
// | Routine          | What it does                                            |
// |------------------|---------------------------------------------------------|
// | MIDAS:0x04af     | draw frame N of a slot's bank into the frame buffer      |
// | MIDAS:0x0a10     | restore the clean plate under that slot's current frame |
// | MIDAS:0x0865     | copy the box of this frame and the last to the screen   |
//
// Everything is in three buffers -- the frame buffer the scene composes in,
// the clean plate it erases from, and the screen -- and several of its effects
// write the screen directly without ever touching the frame buffer. So this is
// played on three buffers too: what reaches the screen, and in what order, is
// then the original's own, including the camera's trail between two erases.
//
// **The timeline.** Each pass of the loop is one master tick: OBJ:obj_set_active
// spins on [0x7926]. `cutscene_pos` steps on the animation frame, every fourth
// pass, and the whole scene hangs off it:
//
// - Nineteen lines, queued by CUTSCENE:sub_0c5e7 at fixed positions (kLines).
//   The bottom band carries them in the studio's own face, over STUDBARS'
//   two caption bars when a line has two rows.
// - The anchor's six gestures, queued at fixed positions (kGestures) as runs
//   of the frame list at ds:0x343e, one frame every second animation frame.
// - The ceiling camera, swinging between targets one frame per tick pair.
// - The small monitor on the left desk, a live half-size copy of a 40x34
//   window of the frame buffer that pans one pixel a pass between targets:
//   it shows the anchor as the camera sees him (OBJ:obj_action_a).
// - From 0x49c, the wall screen: NEWS.PCX's crop circle scaled into the top
//   right corner (OBJ:obj_action_b), grown from 0x4b0 and shrunk from 0x6cc,
//   with palette entries 224-255 -- the picture's own -- faded up out of a
//   dark blue and back down (OBJ:obj_action_d).
//
// The scene ends at 0x730 or on both mouse buttons; then the room it
// interrupted is put back the way a record's is.

static const char *const kStudioPlate = "STUDIO.PCX";
static const char *const kStudioFontPage = "OBJSTUD.PCX";
static const char *const kStudioScreenPage = "NEWS.PCX";
static const char *const kStudioTal = "news_eka.tal";
static const char *const kStudioBanks[] = { "NEWSMAN.DL1", "KAMERA.DL1", "STUDBARS.DL1" };
static const uint kStudioMusic = 8;

static const uint kAnchorSlot = 0, kCameraSlot = 1, kBarsSlot = 2;

/// [0xa49c] at which the scene is over (0c55:09a2).
static const uint kStudioEnd = 0x730;

/// The bottom band the lines go up in and come down from: y 0x7b, 0x4c rows.
static const int kBandTop = 0x7b, kBandHeight = 0x4c;

/// How long a line stands, in animation frames, by its line count (0c55:03de).
static const int kOneLineTicks = 0x28, kTwoLineTicks = 0x50;

/// CUTSCENE:sub_0c5e7's nineteen call sites: [0xa49c], then the entry. Entry
/// 14 is an empty line and nothing queues it.
static const struct {
	uint16 pos;
	byte entry;
} kLines[] = {
	{ 0x050, 0 }, { 0x0b4, 1 }, { 0x0dc, 2 }, { 0x12c, 3 }, { 0x17c, 4 },
	{ 0x1cc, 5 }, { 0x230, 6 }, { 0x280, 7 }, { 0x2e4, 8 }, { 0x334, 9 },
	{ 0x384, 10 }, { 0x3e8, 11 }, { 0x438, 12 }, { 0x49c, 13 }, { 0x564, 15 },
	{ 0x5c8, 16 }, { 0x62c, 17 }, { 0x690, 18 }, { 0x6f4, 19 }
};

/// The anchor's frame list, ds:0x343e. Each entry is a frame plus one, so a
/// 1 is frame zero: nothing drawn, the anchor the plate itself holds.
static const byte kAnchorFrames[] = {
	1, 2, 3, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5, 3, 2, 1,
	1, 8, 9, 1, 8, 9, 8, 9, 1, 8, 1, 8, 9, 1, 8, 9, 1,
	1, 10, 13, 13, 13, 13, 13, 13, 13, 13, 10, 1,
	1, 10, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14, 13, 1,
	1, 10, 13, 11, 12, 13, 11, 12, 11, 13, 11, 12, 13, 11, 13, 11, 12, 13, 11, 12, 13, 10, 1,
	1, 10, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 10, 1
};

/// CUTSCENE:sub_0c608..sub_0c66c, the six runs of that list: where each one
/// starts and how long it is ([0xa5eb], [0xa5ec]). Named after the routine
/// that starts each one.
enum Gesture { kRun0608, kRun061c, kRun0630, kRun0644, kRun0658, kRun066c };
static const struct {
	byte first, count;
} kGestureRuns[] = {
	{ 0x00, 0x12 }, { 0x12, 0x11 }, { 0x23, 0x0c }, { 0x2f, 0x13 }, { 0x42, 0x17 }, { 0x59, 0x0e }
};

/// And the positions each one is started at (0c55:069e..0878).
static const struct {
	uint16 pos;
	Gesture gesture;
} kGestures[] = {
	{ 0x005, kRun0608 }, { 0x050, kRun061c }, { 0x073, kRun0630 }, { 0x087, kRun0644 },
	{ 0x0b4, kRun0658 }, { 0x0e0, kRun061c }, { 0x100, kRun0658 }, { 0x12c, kRun061c },
	{ 0x14c, kRun061c }, { 0x16c, kRun061c }, { 0x18c, kRun061c }, { 0x1ae, kRun0658 },
	{ 0x1da, kRun0658 }, { 0x206, kRun066c }, { 0x230, kRun061c }, { 0x250, kRun0658 },
	{ 0x27c, kRun061c }, { 0x29c, kRun066c }, { 0x2e4, kRun061c }, { 0x304, kRun061c },
	{ 0x324, kRun061c }, { 0x344, kRun0630 }, { 0x35c, kRun061c }, { 0x37c, kRun061c },
	{ 0x39c, kRun061c }, { 0x3bc, kRun066c }, { 0x3e8, kRun061c }, { 0x408, kRun061c },
	{ 0x428, kRun061c }, { 0x440, kRun0658 }, { 0x46c, kRun061c }, { 0x48c, kRun0630 },
	{ 0x49c, kRun061c }, { 0x4bc, kRun061c }, { 0x53c, kRun0608 }, { 0x564, kRun061c },
	{ 0x5c8, kRun0658 }, { 0x62c, kRun061c }, { 0x690, kRun061c }, { 0x6f4, kRun061c }
};

/// The camera's targets, by its own counter [bp-0xe], which wraps at 0x208.
static const struct {
	uint16 at;
	byte frame;
} kCameraTargets[] = {
	{ 0x014, 0x2d }, { 0x03c, 0x0f }, { 0x078, 0x2d }, { 0x0c8, 0x19 }, { 0x10e, 0x2d },
	{ 0x12c, 0x28 }, { 0x14a, 0x1e }, { 0x168, 0x0f }, { 0x1ae, 0x2d }, { 0x1f4, 0x23 }
};
static const uint16 kCameraPeriod = 0x208;

/// The monitor's window, by its counter [bp-0x10], which wraps at 0x190.
static const struct {
	uint16 at;
	int16 x, y;
} kMonitorTargets[] = {
	{ 0x028, 0x5a, 0x14 }, { 0x0aa, 0x64, 0x3c }, { 0x0d2, 0x5f, 0x3c }, { 0x0dc, 0x5a, 0x3c },
	{ 0x0e6, 0x55, 0x3c }, { 0x0ea, 0x58, 0x39 }, { 0x122, 0x82, 0x32 }, { 0x136, 0x64, 0x3c }
};
static const uint16 kMonitorPeriod = 0x190;

/// OBJ:obj_action_a: a 20x17 copy of every other pixel of the window, onto the
/// monitor at 21,32 (VGA offset 0x2815).
static const int kMonitorX = 21, kMonitorY = 32, kMonitorW = 0x14, kMonitorH = 0x11;

/// OBJ:obj_action_b: the wall screen's corner, the picture's full size, and
/// its scale in 8.8 -- 0x3e8 is the smallest the scene shows it at, 0x100 the
/// picture pixel for pixel.
static const int kScreenX = 0xcb, kScreenY = 0x0c;
static const int kScreenW = 0x70, kScreenH = 0x46;
static const int kScaleSmall = 0x3e8, kScaleFull = 0x100, kScaleStep = 0x14;

/// [0x7dde] and [0x7de2]: which way the screen is zooming and fading, or
/// neither.
static const int kGrow = 1, kShrink = 0, kZoomIdle = 2;
static const int kFadeDown = 1, kFadeUp = 0, kFadeIdle = -1;

/// OBJ:obj_action_d's range: the picture's own 32 entries.
static const uint kScreenColors = 0xe0, kScreenColorCount = 0x20;

/// The positions the screen's machine is switched at (0c55:0672..0697) and
/// the extra fade the scene makes at 0x64 (0c55:0407).
static const uint kScreenFrom = 0x49c, kZoomIn = 0x4b0, kZoomOut = 0x6cc, kFadeOut = 0x6db;
static const uint kFadeAgain = 0x64;

namespace {

/// One slot's bank as the scene draws it: the frame it last drew and the one
/// before that, the original's [-0x73a6] and [-0x7368] word arrays.
struct StudioSlot {
	int shown;
	int before;
	StudioSlot() : shown(0), before(0) {}
};

/// The box a frame's stored bounds give, empty for frame zero or a frame the
/// bank does not have.
Common::Rect frameBox(const DL1Sprite &bank, int frame) {
	if (frame < 1 || frame > (int)bank.frameCount())
		return Common::Rect();
	const DL1Sprite::Frame &f = bank.frame((uint)(frame - 1));
	if (!f.hasBbox)
		return Common::Rect();
	return Common::Rect(f.bbox[0], f.bbox[1], f.bbox[2] + 1, f.bbox[3] + 1);
}

void copyRect(const Graphics::Surface &from, Graphics::Surface &to, Common::Rect r) {
	r.clip(Common::Rect(0, 0, MIN<int>(from.w, to.w), MIN<int>(from.h, to.h)));
	if (r.isEmpty())
		return;
	for (int y = r.top; y < r.bottom; y++)
		memcpy(to.getBasePtr(r.left, y), from.getBasePtr(r.left, y), r.width());
}

/// The number of destination pixels the 8.8 scale makes of `size` source
/// pixels, counted the way obj_action_b counts them (0251:4cfc).
int scaledSize(int size, int scale) {
	int n = 0;
	while ((((n + 1) * scale) >> 8) <= size)
		n++;
	return n;
}

} // End of anonymous namespace

/**
 * CUTSCENE:sub_0c6dd, the TV news studio.
 *
 * See the top of this file for what is on screen. The loop below is the
 * original's in the original's order, one pass per master tick, so the
 * comments name the instruction each part comes from.
 */
void AlienEngine::playStudio() {
	Graphics::Surface plate, page, screenPage;
	byte palette[256 * 3];
	byte unused[256 * 3];
	Font font;
	if (!loadGamePCX(Common::Path(kStudioPlate), plate, palette) ||
		!loadGamePCX(Common::Path(kStudioScreenPage), screenPage, unused) ||
		!font.load(Font::kSpeech, kStudioFontPage)) {
		plate.free();
		screenPage.free();
		warning("studio: could not load the studio's pages");
		return;
	}

	const int room = _room;
	const int benX = _ben.walkX();
	const int benY = _ben.walkY();
	const int benFacing = _ben.facing();
	stopSpeech();

	CursorMan.showMouse(false);
	_cutscene = true;
	_background.free();
	_background.copyFrom(plate);
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	memcpy(_palette, palette, sizeof(_palette));

	_tal.load(Common::Path(kStudioTal), &_pack);
	_anims.loadBanks(kStudioBanks, ARRAYSIZE(kStudioBanks));
	playMusicSlot(kStudioMusic);

	// The frame buffer starts as the plate, and the screen as the frame buffer
	// (0c55:0266..02ba).
	page.copyFrom(plate);
	copyRect(page, _screen, Common::Rect(0, 0, kScreenWidth, kScreenHeight));
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);

	debugC(1, kDebugCutscene, "studio: %s, %s, music %u", kStudioPlate, kStudioTal, kStudioMusic);

	StudioSlot slots[ARRAYSIZE(kStudioBanks)];

	// MIDAS:0x04af: a frame into the frame buffer, remembering the last.
	auto drawFrame = [&](uint slot, int frame) {
		slots[slot].before = slots[slot].shown;
		slots[slot].shown = frame;
		_anims.drawBankFrame(slot, frame, page);
	};
	// MIDAS:0x0a10: the plate back under the frame a slot is showing.
	auto eraseFrame = [&](uint slot) {
		copyRect(plate, page, frameBox(_anims.bank(slot), slots[slot].shown));
	};
	// MIDAS:0x0865: both frames' boxes onto the screen, so the one the slot
	// just left is taken off it as well.
	auto flipFrame = [&](uint slot, int frame) {
		Common::Rect box = frameBox(_anims.bank(slot), frame);
		const Common::Rect last = frameBox(_anims.bank(slot), slots[slot].before);
		if (box.isEmpty())
			box = last;
		else if (!last.isEmpty())
			box.extend(last);
		copyRect(page, _screen, box);
	};

	const Common::Rect band(0, kBandTop, kScreenWidth, kBandTop + kBandHeight);
	byte base[kScreenColorCount * 3];
	memcpy(base, _palette + kScreenColors * 3, sizeof(base));

	// OBJ:obj_action_d: the picture's colours at `level` of 255, the rest of
	// the way made up in blue. The original works in the DAC's six bits.
	auto fadeScreen = [&](int level) {
		for (uint i = 0; i < kScreenColorCount; i++) {
			const int r = (base[i * 3 + 0] >> 2) * level >> 8;
			const int g = (base[i * 3 + 1] >> 2) * level >> 8;
			const int b = ((base[i * 3 + 2] >> 2) * level + 0x20 * (0xff - level)) >> 8;
			byte *dst = _palette + (kScreenColors + i) * 3;
			dst[0] = (byte)((r << 2) | (r >> 4));
			dst[1] = (byte)((g << 2) | (g >> 4));
			dst[2] = (byte)((b << 2) | (b >> 4));
		}
		g_system->getPaletteManager()->setPalette(_palette + kScreenColors * 3, kScreenColors,
												  kScreenColorCount);
	};

	uint pos = 0;				// [0xa49c]
	int cameraCount = 0;		// [bp-0xe]
	int monitorCount = 0;		// [bp-0x10]
	int retrigger = 0;			// [0xa5ee]
	int anchorFrame = 0;		// [bp-3]
	int cameraFrame = 1;		// [bp-1]
	int cameraTarget = 0x0f;	// [bp-2]
	int monX = 0x64, monY = 0x3c;			// [bp-6], [bp-8]
	int monTargetX = 0x64, monTargetY = 0x3c;	// [bp-0xa], [bp-0xc]
	int runNext = 0, runLeft = 0;			// [0xa5eb], [0xa5ec]
	int zoom = kZoomIdle, scale = kScaleSmall;	// [0x7dde], [0x7de0]
	int fade = kFadeDown, level = 0;			// [0x7de2], [0x7de4]
	int screenRight = 0, screenBottom = 0;		// [0x7de6], [0x7de8]
	int dialogTicks = 0;		// [0xad1e]
	bool lineQueued = false;	// [0xacff]: a line is to go up this pass
	bool lineUp = false;		// [0xacfd]: a line is up
	bool lineDown = false;		// [0xad1d]: the line is coming down this pass
	uint entry = 0;				// [0xad02]
	bool frameDue = false;		// [0xa5f8]
	bool skipped = false;
	uint dumped = kStudioEnd;

	uint32 iteration = 0;
	uint32 last = g_system->getMillis();

	while (!shouldQuit() && !_quit && !skipped && pos != kStudioEnd) {
		// Both buttons at once end it (CUTSCENE:sub_0c550 and 0c55:0994); a
		// headless run has neither, so a key or the right button does here,
		// as in the hallway's scene.
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_KEYDOWN || event.type == Common::EVENT_RBUTTONDOWN)
				skipped = true;
		}

		// 0c55:02ea: the countdown runs on the previous pass's frame flag.
		if (frameDue && dialogTicks > 0)
			dialogTicks--;

		// OBJ:obj_load: one tick pair every other pass, one animation frame
		// every other tick pair.
		iteration++;
		const bool tickPair = (iteration & 1) == 0;
		frameDue = (iteration & 3) == 0;

		if (frameDue) {
			pos++;
			_script.setCutscenePos((uint16)pos);
			cameraCount++;
			monitorCount++;
			// [0xa5ed] is 1 for the whole scene, so the anchor and the camera
			// are wiped every second frame and drawn over themselves between.
			if (++retrigger > 1) {
				retrigger = 0;
				eraseFrame(kAnchorSlot);
				eraseFrame(kCameraSlot);
			}
		}

		if (dialogTicks == 0 && lineUp)
			lineDown = true;
		if (lineDown)
			copyRect(plate, page, band);

		drawFrame(kAnchorSlot, anchorFrame);
		drawFrame(kCameraSlot, cameraFrame);

		if (lineQueued) {
			// 0c55:038f: the entry's first byte is its line count. Two lines
			// go up on the caption bars, one on the bare band.
			const TalFile::Entry &text = _tal.entry(entry);
			if (text.lineCount >= 2) {
				drawFrame(kBarsSlot, 1);
				drawFrame(kBarsSlot, 2);
			}
			// DIALOG:render_dialog, the bottom-band layout drawBand() has.
			const int lines = MIN<int>(text.lines.size(), 4);
			const int firstY = lines == 1 ? 140 : 126;
			for (int i = 0; i < lines; i++)
				font.drawString(page, text.lines[i], 20, firstY + i * 14);
			if (text.lineCount == 1)
				dialogTicks = kOneLineTicks;
			if (text.lineCount >= 2)
				dialogTicks = kTwoLineTicks;
			lineDown = false;
		}

		// OBJ:obj_set_active: the pass waits for the master tick here.
		if (!_cutsceneFast) {
			while (!shouldQuit() && g_system->getMillis() - last < kMasterTickMillis)
				g_system->delayMillis(1);
			last = g_system->getMillis();
		}

		if (fade != kFadeIdle)
			fadeScreen(level);
		if (pos == kFadeAgain)
			fadeScreen(level);

		flipFrame(kAnchorSlot, anchorFrame);
		flipFrame(kCameraSlot, cameraFrame);

		bool lineShown = false;
		if (lineQueued) {
			copyRect(page, _screen, band);
			lineQueued = false;
			lineShown = true;
		}
		if (lineDown) {
			copyRect(page, _screen, band);
			lineDown = false;
			lineUp = false;
		}

		// OBJ:obj_action_a: the monitor, straight onto the screen.
		for (int y = 0; y < kMonitorH; y++) {
			for (int x = 0; x < kMonitorW; x++) {
				const int sx = monX + x * 2, sy = monY + y * 2;
				if (sx < page.w && sy < page.h)
					*(byte *)_screen.getBasePtr(kMonitorX + x, kMonitorY + y) =
						*(const byte *)page.getBasePtr(sx, sy);
			}
		}

		if (pos > kScreenFrom) {
			// OBJ:obj_action_b: the picture scaled onto the screen.
			const int w = scaledSize(kScreenW, scale);
			const int h = scaledSize(kScreenH, scale);
			for (int y = 0; y < h && kScreenY + y < _screen.h; y++) {
				const int sy = (y * scale) >> 8;
				for (int x = 0; x < w && kScreenX + x < _screen.w; x++) {
					const int sx = (x * scale) >> 8;
					*(byte *)_screen.getBasePtr(kScreenX + x, kScreenY + y) =
						*(const byte *)screenPage.getBasePtr(sx, sy);
				}
			}
			screenRight = kScreenX + w + 1;
			screenBottom = kScreenY + h;

			// OBJ:obj_action_c twice: while it shrinks, the strips it has just
			// left go back to the plate.
			if (zoom == kShrink) {
				copyRect(plate, _screen, Common::Rect(screenRight, kScreenY, screenRight + 7,
													  screenBottom));
				copyRect(plate, _screen, Common::Rect(kScreenX, screenBottom, screenRight + 7,
													  screenBottom + 5));
			}
		}

		// CUTSCENE:sub_0c5e7: a line due at this position. It is tested on
		// every pass, so it is queued again for each pass the position lasts.
		for (uint i = 0; i < ARRAYSIZE(kLines); i++) {
			if (pos == kLines[i].pos) {
				lineQueued = true;
				entry = kLines[i].entry;
				lineUp = true;
			}
		}

		for (uint i = 0; i < ARRAYSIZE(kCameraTargets); i++)
			if (cameraCount == kCameraTargets[i].at)
				cameraTarget = kCameraTargets[i].frame;
		if (cameraCount == kCameraPeriod)
			cameraCount = 0;

		for (uint i = 0; i < ARRAYSIZE(kMonitorTargets); i++) {
			if (monitorCount == kMonitorTargets[i].at) {
				monTargetX = kMonitorTargets[i].x;
				monTargetY = kMonitorTargets[i].y;
			}
		}
		if (monitorCount == kMonitorPeriod)
			monitorCount = 0;

		if (pos == kZoomIn) {
			zoom = kGrow;
			fade = kFadeUp;
		}
		if (pos == kZoomOut)
			zoom = kShrink;
		if (pos == kFadeOut)
			fade = kFadeDown;

		for (uint i = 0; i < ARRAYSIZE(kGestures); i++) {
			if (pos == kGestures[i].pos) {
				runNext = kGestureRuns[kGestures[i].gesture].first;
				runLeft = kGestureRuns[kGestures[i].gesture].count;
			}
		}

		// 0c55:087b: the next frame of the run, on the frame the wipe is due.
		if (runLeft > 0 && frameDue && retrigger == 0) {
			anchorFrame = kAnchorFrames[runNext] - 1;
			runNext++;
			runLeft--;
		}

		if (cameraFrame < cameraTarget && tickPair)
			cameraFrame++;
		if (cameraFrame > cameraTarget && tickPair)
			cameraFrame--;

		if (monTargetX < monX)
			monX--;
		if (monTargetY < monY)
			monY--;
		if (monTargetX > monX)
			monX++;
		if (monTargetY > monY)
			monY++;

		if (zoom == kGrow)
			scale -= kScaleStep;
		if (zoom == kShrink)
			scale += kScaleStep;
		if (scale < kScaleFull) {
			scale = kScaleFull;
			zoom = kZoomIdle;
		}
		if (scale > kScaleSmall) {
			scale = kScaleSmall;
			zoom = kZoomIdle;
		}

		if (fade == kFadeDown)
			level -= 8;
		if (fade == kFadeUp)
			level += 8;
		if (level > 0xff) {
			level = 0xff;
			fade = kFadeIdle;
		}
		if (level < 0) {
			level = 0;
			fade = kFadeIdle;
		}

		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
		g_system->updateScreen();

		// Level 3 writes the screen as each line goes up and at each step of
		// the wall screen's machine, which is how a headless run sees it.
		if (debugChannelSet(3, kDebugCutscene) && pos != dumped &&
			(lineShown || pos == kZoomIn + 40 || pos == kZoomOut - 1 || pos == kFadeOut + 40)) {
			dumped = pos;
			dumpScreen(Common::String::format("studio-%04x.png", pos));
		}
	}

	debugC(1, kDebugCutscene, "studio: ended at 0x%x%s", pos, skipped ? " (skipped)" : "");

	page.free();
	screenPage.free();
	plate.free();

	// 0c55:09ad: the font page goes back to OBJFILE.PCX -- nothing to do here,
	// the studio's face was this scene's own -- and the room is handed back
	// the way every scene's is.
	endCutscene(room, benX, benY, benFacing);
}

} // End of namespace Alien
