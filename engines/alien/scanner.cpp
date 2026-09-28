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
#include "common/file.h"
#include "common/system.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

// Room 52, the security scanner between the ship's corridor and the
// transporter chamber.
//
// The room has no dispatch of its own: MAIN:sub_001eb stands in front of its
// overlay, and it is that routine, not anything clicked in the room, which is
// the scan. Every entry runs it:
//
//   [0xa79a] == 1              scanned already: the room, as it is
//   [0xa79a] == 0, no mask     [0xa7ad] < 2: the scan without the mask
//                              (CUTSCENE:sub_0e2a0(0)), [0x33e3] := 1, then
//                              the room -- whose init and tick are the arrest
//                              [0xa7ad] >= 2: straight to the jail, room 58,
//                              with [0xa7ae] and [0xa7b1] raised
//   [0xa79a] == 0, mask on     the scan with the mask (sub_0e2a0(1)),
//                              [0xa79a] := 1, [0x33e1] := 1, then the room
//
// [0xa79b] is the mask-is-on flag room 56 writes (alien.cpp), [0xa7ad] the
// arrest count room 58 raises (jail.cpp, job 13).
//
// The scan itself is SEC_SCR1.DL2 or SEC_SCR2.DL2 over SEC_SCAN.PCX or
// SEC_SCA2.PCX, music slot 12: one frame every animation tick pair
// (CUTSCENE:sub_0e253 with [0xa4fa] = 0), each painted over what the last one
// left -- the frames are deltas, not cels -- until the file's frame count is
// reached ([0x8cd6], which the DL2 loader leaves) or both joystick buttons are
// held ([0xa900]/[0xa901], CUTSCENE:sub_0c550). Either way it leaves
// [0x33e2] := 1, which the room's init reads to put Ben at the scanner's far
// door when the robot is not waiting for him.
//
// With [0x33e3] standing the room's init (roominit.cpp) plays the robot --
// SEC_NORO frame 1, SEC_ROBO's forty frames -- and the overlay raises its own
// [0xa49f] machine with the cursor gone (ovr_34_0f96:0x05ee):
//
//   3     SEC_ROBO has run out ([0xa4f0]): outcome 0x1e at (0xa0, 0x32),
//         "You have been classified as an illegal alien..."
//   0xa   the line is down ([0xad1c]): [0xa7ae] := 1, [0xa7b1] := 1,
//         game_submode 0x6f -- transitions.cpp's room 52, submode 111 ->
//         room 58, the jail
static const int kScannerRoom = 52;
static const int kJailRoom = 58;

static const uint16 kScanned = 0xa79a;		///< the scan has passed him
static const uint16 kMaskOn = 0xa79b;
static const uint16 kArrests = 0xa7ad;		///< times he has been jailed
static const uint16 kJailArrival = 0xa7ae;
static const uint16 kJailGate = 0xa7b1;
static const uint16 kPassedLatch = 0x33e1;
static const uint16 kScanPlayed = 0x33e2;
static const uint16 kArrestLatch = 0x33e3;
static const uint16 kMachine = 0xa49f;

static const uint kRobotSlot = 6;			///< SEC_ROBO, [0xa4f0]
static const byte kArrestLine = 0x1e;
static const int kArrestX = 0xa0;
static const int kArrestY = 0x32;
static const byte kJailSubmode = 111;		///< transitions.cpp: room 52, submode 111 -> room 58

static const byte kStepArrest = 3;
static const byte kStepLine = 0xa;

static const uint kScanMusic = 12;
static const uint kDl2RecordSize = 20;

int AlienEngine::scannerGate(int room) {
	if (room != kScannerRoom || _script.flag(kScanned) == 1)
		return room;

	if (_script.flag(kMaskOn) == 1) {
		playScannerScan(true);
		_script.setFlag(kScanned, 1);
		_script.setFlag(kPassedLatch, 1);
		debugC(1, kDebugRooms, "scanner: the mask passes the scan");
		return room;
	}

	if (_script.flag(kArrests) < 2) {
		playScannerScan(false);
		_script.setFlag(kArrestLatch, 1);
		_scannerArrest = true;
		debugC(1, kDebugRooms, "scanner: no mask, the robot is waiting");
		return room;
	}

	// 0x0227: jailed twice already, and the scan is not played a third time.
	_script.setFlag(kJailArrival, 1);
	_script.setFlag(kJailGate, 1);
	debugC(1, kDebugRooms, "scanner: arrest %u, straight to the jail",
		   _script.flag(kArrests) + 1);
	return kJailRoom;
}

/**
 * CUTSCENE:sub_0e2a0: the scan, a DL2 played frame by frame over its plate.
 *
 * Blocks, the way every scene out of the CUTSCENE segment does. Nothing of the
 * room is loaded yet -- the scan runs in front of the room's own dispatch -- so
 * there is nothing to put back afterwards: loadRoom() goes on to load the room.
 */
void AlienEngine::playScannerScan(bool mask) {
	const char *const clip = mask ? "SEC_SCR2.DL2" : "SEC_SCR1.DL2";
	const char *const plateName = mask ? "SEC_SCA2.PCX" : "SEC_SCAN.PCX";

	Common::File f;
	if (!f.open(Common::Path(clip))) {
		warning("scanner: cannot open %s", clip);
		_script.setFlag(kScanPlayed, 1);
		return;
	}
	const uint32 size = (uint32)f.size();
	Common::Array<byte> data;
	data.resize(size);
	if (size < 3 || f.read(data.data(), size) != size) {
		warning("scanner: short read on %s", clip);
		_script.setFlag(kScanPlayed, 1);
		return;
	}

	// u8 version, u16 count, count 20-byte records (+5: u16 size), count 8-byte
	// bboxes, then the frames back to back (docs/file_formats.md, section 4).
	const uint count = READ_LE_UINT16(&data[1]);
	uint32 payload = 3 + count * (kDl2RecordSize + 8);
	Common::Array<uint32> offsets, sizes;
	for (uint i = 0; i < count && payload <= size; i++) {
		const uint16 frameSize = READ_LE_UINT16(&data[3 + i * kDl2RecordSize + 5]);
		offsets.push_back(payload);
		sizes.push_back(frameSize);
		payload += frameSize;
	}
	if (payload > size) {
		warning("scanner: %s overruns its payload", clip);
		_script.setFlag(kScanPlayed, 1);
		return;
	}

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(plateName), plate, palette)) {
		plate.free();
		warning("scanner: could not load %s", plateName);
		_script.setFlag(kScanPlayed, 1);
		return;
	}

	stopSpeech();
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;
	const int clipBottom = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	CursorMan.showMouse(false);

	const char *none[] = { nullptr };
	_anims.loadBanks(none, ARRAYSIZE(none));

	playMusicSlot(kScanMusic);
	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugCutscene, "scanner: %s over %s, %u frames", clip, plateName, count);

	static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;
	uint32 last = g_system->getMillis();
	uint32 tick = 0;
	uint frame = 0;
	bool skipped = false;

	while (!shouldQuit() && !_quit && !skipped && frame < offsets.size()) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			// Both joystick buttons at once end it early in the original; the
			// port takes Escape or the right button, as every other scene does.
			if ((event.type == Common::EVENT_KEYDOWN &&
				 event.kbd.keycode == Common::KEYCODE_ESCAPE) ||
				event.type == Common::EVENT_RBUTTONDOWN)
				skipped = true;
		}

		// A scripted run steps the frames as fast as they can be painted, the
		// way the cutscene sweep does; it has nothing to click here.
		const uint32 now = g_system->getMillis();
		bool due = _playActive || _cutsceneFast;
		if (!due && now - last >= kTickMillis) {
			last += kTickMillis;
			due = (++tick & 1) == 0;	// [0xa5fc], the animation tick pair
		}

		if (due) {
			const byte *d = &data[offsets[frame]];
			const uint32 end = sizes[frame] >= 2 ? sizes[frame] - 2 : 0;
			uint32 off = 0;
			while (off + 4 <= end) {
				const uint16 addr = READ_LE_UINT16(d + off);
				const uint32 n = READ_LE_UINT16(d + off + 2) * 2;
				off += 4;
				if (off + n > end)
					break;
				for (uint32 i = 0; i < n; i++) {
					const uint32 at = addr + i;
					if (d[off + i] && at < (uint32)(kScreenWidth * _background.h))
						*(byte *)_background.getBasePtr(at % kScreenWidth, at / kScreenWidth) = d[off + i];
				}
				off += n;
			}
			frame++;
			_dirty = true;

			if (debugChannelSet(3, kDebugCutscene) && (frame & 63) == 0) {
				redraw();
				dumpScreen(Common::String::format("scan-%u-%03u.png", mask ? 2 : 1, frame));
			}
		}

		if (_dirty)
			redraw();
		g_system->updateScreen();
		if (!_playActive && !_cutsceneFast)
			g_system->delayMillis(5);
	}

	stopMusic();
	_cutscene = false;
	_clipBottom = clipBottom;
	_script.setFlag(kScanPlayed, 1);
	debugC(1, kDebugCutscene, "scanner: the scan ends at frame %u of %u%s", frame, count,
		   skipped ? ", skipped" : "");
}

/// The overlay's own arm for [0x33e3], ovr_34_0f96:0x05ee; the init effects
/// before it already cleared the latch and put the robot up.
void AlienEngine::startScanner() {
	_scannerStep = 0;
	if (_room != kScannerRoom || !_scannerArrest)
		return;

	_scannerArrest = false;
	_scannerStep = kStepArrest;
	_script.setFlag(kMachine, kStepArrest);
	CursorMan.showMouse(false);
	debugC(1, kDebugRooms, "scanner: the robot comes for him");
}

/// ovr_34_0f96:0x0853, the room's [0xa49f] machine.
void AlienEngine::stepScanner() {
	if (_room != kScannerRoom || !_scannerStep)
		return;

	switch (_scannerStep) {
	case kStepArrest:
		if (_anims.remaining(kRobotSlot) != 0)
			break;
		queueOutcome(_tal, kArrestLine, kArrestX, kArrestY);
		_scannerStep = kStepLine;
		_script.setFlag(kMachine, kStepLine);
		break;

	case kStepLine:
		if (_speech || _queueNext < _queueCount)
			break;
		_scannerStep = 0;
		_script.setFlag(kMachine, 0);
		_script.setFlag(kJailArrival, 1);
		_script.setFlag(kJailGate, 1);
		debugC(1, kDebugRooms, "scanner: arrested, to the jail");
		takeExit(kJailSubmode);
		// The jail's own machine is what gives the cursor back in the
		// original; until that is ported (job 13) the port does it here.
		CursorMan.showMouse(true);
		break;

	default:
		break;
	}
}

} // End of namespace Alien
