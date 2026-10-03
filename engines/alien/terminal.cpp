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
#include "alien/font.h"
#include "alien/play.h"
#include "alien/resources.h"

namespace Alien {

// Room 57's network terminal: 15f3:sub_17373, opened by card 42 in the
// terminal's slot once the jail's LCD has given the password (corridor.cpp).
//
// It is the only thing in the game that takes the force field in the jail
// corridor down ([0xa7b0], jail.cpp). A scene with a frame loop of its own,
// like the elevator panel beside it in the same resident unit (elevator.cpp):
// NETSCRE5.PCX with its palette, NETOBJ.PCX over the object sheet -- the
// green copy of the font atlas, with the cursor's glow under it at row 35 --
// six button banks and NET_LTSC, the screen's own animations, in slots 0..6.
//
// What the screen shows is a network of fifty nodes, read out of NET_DATA:
// each is 200 bytes, a word count of rectangles, the rectangles as four signed
// words around the screen's centre, and five switch bytes at +0xb6; a table of
// 143 zoom factors follows at 0x27d8, 0x40 at index 0x47 being life size. Two
// nodes are in flight at once, [0xbf0c] at zoom [0xbf08] and [0xbf0d] at
// [0xbf0a], 0x47 apart: the arrows move both one step a pass until one of them
// reaches 0x47 and becomes the node shown, [0xbf0f], and a node that runs off
// either end of the table comes back at the other as the next one along. Each
// is drawn as glowing outlines added onto the plate (sub_16d24, sub_16dd5),
// fading in and out at the table's ends (sub_16c9f).
//
// The switches are only a picture: a click flips one in the copy of NET_DATA
// the scene loaded, which is read again the next time. What matters is the
// box under them: pressed on node 0x26 while [0xa086 + 0x26] still stands --
// MAIN sets all fifty at a new game and this is the only writer -- it clears
// both that byte and [0xa7b0] and plays the field going down; on any other
// node it shows "FORCE FIELD HOLDING". The port has no store for [0xa086]: its
// one byte that ever changes is cleared in the same breath as [0xa7b0], so it
// is read as that.

static const char *const kPlate = "NETSCRE5.PCX";
static const char *const kSheet = "NETOBJ.PCX";
static const char *const kNetData = "NET_DATA";
static const char *const kBanks[] = {
	"NET_BUT1.DL1", "NET_BUT2.DL1", "NET_BUT3.DL1", "NET_BUT4.DL1", "NET_BUT5.DL1",
	"NET_BUT6.DL1", "NET_LTSC.DL1"
};

static const uint kSlotLit = 5;			///< NET_BUT6: the node's own lamp, [0xa086 + node]
static const uint kSlotScreen = 6;		///< NET_LTSC
static const uint kSwitchCount = 5;		///< NET_BUT1..5, one a switch

static const int kForward = 1;			///< MIDAS:0xb85, anim_play_mode1

/// NET_DATA: 0x2bc0 bytes, block-read whole (0x153d).
static const uint32 kDataSize = 0x2bc0;
static const uint32 kNodeSize = 0xc8;
static const uint32 kNodeSwitches = 0xb6;
static const uint32 kZoomTable = 0x27d8;

static const int kFirstNode = 1, kLastNode = 0x31;
static const int kFieldNode = 0x26;

/// [0xbf08]/[0xbf0a]: 0..0x8e, the node in focus at 0x47.
static const int kZoomFocus = 0x47, kZoomLast = 0x8e;

/// The outlines are drawn about this point, and only inside this box.
static const int kCentreX = 0x88, kCentreY = 0x45;
static const int kViewLeft = 0x1a, kViewRight = 0xf8, kViewTop = 0x11, kViewBottom = 0x7b;

/// sub_170a6: where the cursor may go.
static const int kCursorLeft = 0x1a, kCursorRight = 0xef, kCursorTop = 0x11, kCursorBottom = 0xa0;

/// sub_1710c: the glow, 10 by 9 out of the sheet's row 35, added onto the
/// plate and held under 0x6f.
static const int kGlowRow = 35, kGlowWidth = 10, kGlowHeight = 9, kGlowLimit = 0x6f;

/// sub_17069: the node's mark under the buttons, 3 by 3 in 0x46.
static const int kMarkX = 0x9b, kMarkY = 0x98, kMarkSize = 3;
static const byte kMarkColor = 0x46;

/// The node's number and the refusal, both in the sheet's face (DIALOG:sub_0b15d).
static const int kNumberX = 0x1b, kNumberY = 0x12;
static const int kRefusalX = 0x50, kRefusalY = 0x12;
static const char *const kRefusal = "FORCE FIELD HOLDING";
static const uint kRefusalPasses = 0x32;

/// The click boxes, bounds exclusive as the original tests them.
struct NetBox {
	int x1, y1, x2, y2;
};
static const NetBox kSwitchBoxes[kSwitchCount] = {
	{ 0x1b, 0x7e, 0x57, 0x8a }, { 0x59, 0x7e, 0x95, 0x8a }, { 0x97, 0x7e, 0xd4, 0x8a },
	{ 0x1b, 0x89, 0x57, 0x95 }, { 0x59, 0x89, 0x95, 0x95 }
};
static const NetBox kExecuteBox = { 0x97, 0x89, 0xd4, 0x95 };
static const NetBox kExitBox = { 0x1c, 0x95, 0x94, 0x9f };
static const NetBox kUpBox = { 0xd9, 0x7e, 0xf0, 0x8e };
static const NetBox kDownBox = { 0xd9, 0x8f, 0xf0, 0x9f };

static bool inNetBox(int x, int y, const NetBox &box) {
	return x > box.x1 && y > box.y1 && x < box.x2 && y < box.y2;
}

/// The scene's [0xa49f] steps.
static const byte kStepIdle = 0;
static const byte kStepRefused = 0xa;
static const byte kStepFieldDown = 0x1e;
static const byte kStepFieldGone = 0x20;
static const byte kStepStarting = 0x28;

static const uint16 kFieldUp = 0xa7b0;
static const uint16 kTerminalReturn = 0xa7d4;
static const uint16 kScenePlayed = 0xa6d3;

static const uint kScriptedGiveUp = 3000;

namespace {

/// What the scene draws over the plate each pass, and the node data it reads.
struct NetScreen {
	Common::Array<byte> data;
	const Graphics::Surface *plate;
	Graphics::Surface *frame;

	uint16 word(uint32 at) const {
		return at + 1 < data.size() ? READ_LE_UINT16(&data[at]) : 0;
	}

	/// sub_16c9f: the zoom factor and the brightness of one zoom step.
	void zoom(int step, int &scale, int &bright) const {
		scale = word(kZoomTable + (uint32)step * 2);
		bright = 0x40;
		if (step < 0x48)
			bright = (step << 6) / kZoomFocus;
		if (step > 0x6b)
			bright = (int16)((0x20 - (step - 0x6c)) << 6) >> 5;
		if (step > 0x8c)
			bright = 0;
	}

	byte at(int x, int y) const {
		return *(const byte *)plate->getBasePtr(x, y);
	}
	void put(int x, int y, byte value) {
		*(byte *)frame->getBasePtr(x, y) = value;
	}

	/// sub_16d24: three rows over the plate, the middle one bright.
	void hline(int x1, int y, int x2, byte dim, byte bright) {
		if (x2 > kViewRight)
			x2 = kViewRight;
		if (x1 < kViewLeft)
			x1 = kViewLeft;
		if (y < kViewTop || y > kViewBottom || x2 - x1 + 1 < 1)
			return;
		for (int x = x1; x <= x2; x++) {
			// The two upper rows are one word add, carry and all.
			const uint16 pair = (uint16)(at(x, y - 1) | at(x, y) << 8) + (dim | bright << 8);
			put(x, y - 1, (byte)pair);
			put(x, y, (byte)(pair >> 8));
			put(x, y + 1, (byte)(at(x, y + 1) + dim));
		}
	}

	/// sub_16dd5: the same down a column.
	void vline(int x, int y1, int y2, byte dim, byte bright) {
		if (y2 > kViewBottom)
			y2 = kViewBottom;
		if (y1 < kViewTop)
			y1 = kViewTop;
		if (x < kViewLeft || x > kViewRight || y2 - y1 + 1 < 1)
			return;
		for (int y = y1; y <= y2; y++) {
			const uint16 pair = (uint16)(at(x - 1, y) | at(x, y) << 8) + (dim | bright << 8);
			put(x - 1, y, (byte)pair);
			put(x, y, (byte)(pair >> 8));
			put(x + 1, y, (byte)(at(x + 1, y) + dim));
		}
	}

	/// sub_16eb5: one node's rectangles at one zoom step.
	void drawNode(int node, int step) {
		if (node < 0 || (uint32)(node + 1) * kNodeSize > data.size())
			return;

		int scale, level;
		zoom(step, scale, level);
		const byte bright = (byte)((level * 0x25) >> 6);
		const byte dim = (byte)((level * 0x0c) >> 6);

		const uint32 base = (uint32)node * kNodeSize;
		const int count = word(base);
		for (int i = 0; i < count; i++) {
			const uint32 rect = base + 2 + (uint32)i * 8;
			if (rect + 8 > data.size())
				break;
			// GFX:sub_262d5 is a longint multiply; the divide is signed.
			const int x1 = (int)(int16)word(rect + 0) * scale / 0x40 + kCentreX;
			const int y1 = (int)(int16)word(rect + 2) * scale / 0x40 + kCentreY;
			const int x2 = (int)(int16)word(rect + 4) * scale / 0x40 + kCentreX;
			const int y2 = (int)(int16)word(rect + 6) * scale / 0x40 + kCentreY;
			// sub_16e76
			hline(x1, y1, x2, dim, bright);
			hline(x1, y2, x2, dim, bright);
			vline(x1, y1 + 1, y2 - 1, dim, bright);
			vline(x2, y1 + 1, y2 - 1, dim, bright);
		}
	}
};

} // End of anonymous namespace

/**
 * The terminal, as 15f3:sub_17373 runs it. Blocks like playElevatorPanel(),
 * and leaves [0xa7d4] for room 57 to put Ben back at it; the room's exit is
 * the caller's (corridor.cpp).
 */
void AlienEngine::playNetTerminal() {
	NetScreen screen;
	{
		Common::File f;
		if (!f.open(Common::Path(kNetData)) || f.size() < (int64)kDataSize) {
			warning("terminal: cannot read %s", kNetData);
			return;
		}
		screen.data.resize(kDataSize);
		f.read(screen.data.data(), kDataSize);
	}

	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kPlate), plate, palette)) {
		plate.free();
		warning("terminal: could not load %s", kPlate);
		return;
	}

	Graphics::Surface sheet;
	byte sheetPalette[256 * 3];
	if (!loadGamePCX(Common::Path(kSheet), sheet, sheetPalette)) {
		plate.free();
		sheet.free();
		warning("terminal: could not load %s", kSheet);
		return;
	}

	Font face;
	if (!face.load(Font::kSpeech, kSheet))
		warning("terminal: could not load the face out of %s", kSheet);

	stopSpeech();
	_cutscene = true;
	_background.free();
	_background.copyFrom(plate);
	_roomWidth = kScreenWidth;
	_scrollX = 0;

	const int clip = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	// The glow is the cursor: OBJ draws no pointer while the scene runs.
	CursorMan.showMouse(false);

	_anims.loadBanks(kBanks, ARRAYSIZE(kBanks));
	// 0x14fb: the screen coming on.
	_anims.play(kSlotScreen, 1, 0x0c, 2, kForward);

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	screen.plate = &plate;
	screen.frame = &_background;

	int zoomA = 0, zoomB = kZoomFocus;			// [0xbf08], [0xbf0a]
	int nodeA = 0x30, nodeB = kLastNode;		// [0xbf0c], [0xbf0d]
	int shown = kLastNode;						// [0xbf0f]
	int moving = 0;								// [0xbf0e]: 1 down, 2 up
	byte step = kStepStarting;					// [0xa49f]
	uint16 pos = 0;								// [0xa49c]
	bool refusal = false;						// [0x6786] == 3
	uint16 refusalPasses = 0;					// [0x6788]
	bool glow = true;							// [0xa948]
	bool done = false;							// [bp - 1]
	int cursorX = kCursorLeft, cursorY = kCursorTop;

	const auto nodeLit = [&](int node) -> byte {
		return node == kFieldNode ? (byte)_script.flag(kFieldUp) : 1;
	};

	// sub_171fa: the five switches and the lamp of the node shown.
	const auto showButtons = [&]() {
		const uint32 at = (uint32)shown * kNodeSize + kNodeSwitches;
		for (uint i = 0; i < kSwitchCount; i++)
			_anims.play(i, screen.data[at + i] + 1, 1, 0, kForward);
		_anims.play(kSlotLit, nodeLit(shown) + 1, 1, 0, kForward);
	};

	const auto clampCursor = [&](int &x, int &y) {
		x = CLIP(x, kCursorLeft, kCursorRight);
		y = CLIP(y, kCursorTop, kCursorBottom);
	};

	const auto handleClick = [&](int x, int y, bool left) {
		clampCursor(x, y);

		if (left) {
			const uint32 at = (uint32)shown * kNodeSize + kNodeSwitches;
			for (uint i = 0; i < kSwitchCount; i++)
				if (inNetBox(x, y, kSwitchBoxes[i]))
					screen.data[at + i] ^= 1;

			if (inNetBox(x, y, kExecuteBox)) {
				if (shown == kFieldNode) {
					if (nodeLit(shown) == 1) {
						// 0x18f9: the field goes down.
						_anims.play(kSlotScreen, 0x10, 7, 2, kForward);
						glow = false;
						step = kStepFieldDown;
						pos = 0;
						_script.setFlag(kFieldUp, 0);
						debugC(1, kDebugRooms, "terminal: node 0x%02x, the force field is down",
							   shown);
					}
				} else {
					// 0x1921
					_anims.play(kSlotScreen, 0x4d, 2, 9, kForward);
					step = kStepRefused;
					pos = 0;
					refusal = true;
					refusalPasses = 0;
					debugC(1, kDebugRooms, "terminal: node 0x%02x, force field holding", shown);
				}
			}

			if (inNetBox(x, y, kExitBox))
				done = true;

			showButtons();
		}

		// 0x1968: the arrows take either button.
		if (inNetBox(x, y, kUpBox) && !(shown == kLastNode && moving == 0))
			moving = 2;
		if (inNetBox(x, y, kDownBox) && !(shown == kFirstNode && moving == 0))
			moving = 1;
	};

	debugC(1, kDebugRooms, "terminal: the screen opens on node 0x%02x, the field %s", shown,
		   _script.flag(kFieldUp) ? "up" : "down");

	static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;
	uint32 last = millis();
	uint32 tick = 0;
	uint scripted = 0;
	uint32 playWaitUntil = 0;
	bool leaving = false;

	while (!shouldQuit() && !_quit && !leaving) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_MOUSEMOVE) {
				cursorX = event.mouse.x;
				cursorY = event.mouse.y;
			} else if (event.type == Common::EVENT_LBUTTONDOWN ||
					   event.type == Common::EVENT_RBUTTONDOWN) {
				cursorX = event.mouse.x;
				cursorY = event.mouse.y;
				handleClick(event.mouse.x, event.mouse.y, event.type == Common::EVENT_LBUTTONDOWN);
			}
		}

		// The elevator panel's scripted reader: waits are held for real, and a
		// click or a right click lands on the screen.
		if (_playActive && playWaitUntil != 0 && millis() >= playWaitUntil)
			playWaitUntil = 0;

		if (_playActive && playWaitUntil == 0) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					_playIndex++;
					scripted = 0;
					playWaitUntil = millis() + (uint32)MAX(cmd.a, 0);
					break;
				}
				if (cmd.type != PlayCommand::kClick && cmd.type != PlayCommand::kRightClick)
					break;

				_playIndex++;
				scripted = 0;
				debugC(1, kDebugRooms, "terminal: play: %u: click %d,%d", cmd.sourceLine,
					   cmd.a, cmd.b);
				cursorX = cmd.a;
				cursorY = cmd.b;
				handleClick(cmd.a, cmd.b, cmd.type == PlayCommand::kClick);
				break;
			}

			if (++scripted > kScriptedGiveUp && step == kStepIdle && moving == 0) {
				// A port-only guard, as the elevator panel has: a script that
				// never presses EXIT would otherwise hold the run here.
				warning("terminal: the script left the screen open; leaving it");
				playLeftOpen("the network terminal");
				done = true;
			}
		}

		const uint32 now = millis();
		while (now - last >= kTickMillis && !leaving) {
			last += kTickMillis;
			tick++;

			if ((tick & 3) != 0)
				continue;

			pos++;
			refusalPasses++;
			_anims.tick();

			// 0x15c0..0x165d: both nodes one step on, the one that reaches
			// the focus is the node shown, and one that runs off the table
			// comes back at the other end as the next node along.
			if (moving == 1) {
				zoomA++;
				zoomB++;
			} else if (moving == 2) {
				zoomA--;
				zoomB--;
			}
			if (zoomA == kZoomFocus) {
				moving = 0;
				shown = nodeA;
				showButtons();
			}
			if (zoomB == kZoomFocus) {
				moving = 0;
				shown = nodeB;
				showButtons();
			}
			if (zoomB > kZoomLast) {
				zoomB = 0;
				nodeB = nodeA - 1;
			}
			if (zoomA > kZoomLast) {
				zoomA = 0;
				nodeA = nodeB - 1;
			}
			if (zoomB < 0) {
				zoomB = kZoomLast;
				nodeB = nodeA + 1;
			}
			if (zoomA < 0) {
				zoomA = kZoomLast;
				nodeA = nodeB + 1;
			}

			// sub_19df8: the screen is the plate again before anything is drawn.
			_background.copyFrom(plate);

			// 0x1660: the dimmer of the two first.
			int scale, brightA, brightB;
			screen.zoom(zoomA, scale, brightA);
			screen.zoom(zoomB, scale, brightB);
			if (brightB < brightA) {
				screen.drawNode(nodeB, zoomB);
				screen.drawNode(nodeA, zoomA);
			} else {
				screen.drawNode(nodeA, zoomA);
				screen.drawNode(nodeB, zoomB);
			}

			// sub_17069
			for (int y = 0; y < kMarkSize; y++)
				for (int x = 0; x < kMarkSize; x++)
					screen.put(kMarkX + shown + x, kMarkY + y, kMarkColor);

			// 0x1a10: the node's number, and the refusal while it stands.
			if (face.isLoaded()) {
				face.drawString(_background, Common::String::format("%d", shown), kNumberX,
								kNumberY);
				if (refusal)
					face.drawString(_background, kRefusal, kRefusalX, kRefusalY);
			}
			if (refusal && refusalPasses > kRefusalPasses)
				refusal = false;

			// sub_1710c: the glow under the cursor.
			if (glow) {
				int gx = cursorX, gy = cursorY;
				clampCursor(gx, gy);
				const int rows = gy > 0x98 ? 0xa1 - gy : kGlowHeight;
				for (int r = 0; r < rows; r++) {
					for (int c = 0; c < kGlowWidth; c += 2) {
						const uint16 pair = (uint16)(screen.at(gx + c, gy + r) |
													 screen.at(gx + c + 1, gy + r) << 8) +
											(uint16)(*(const byte *)sheet.getBasePtr(c, kGlowRow + r) |
													 *(const byte *)sheet.getBasePtr(c + 1, kGlowRow + r) << 8);
						screen.put(gx + c, gy + r, (byte)MIN<int>(pair & 0xff, kGlowLimit));
						screen.put(gx + c + 1, gy + r, (byte)MIN<int>(pair >> 8, kGlowLimit));
					}
				}
			}

			// 0x1a80: the scene's own steps.
			switch (step) {
			case kStepRefused:
				if (pos > 0x1e) {
					_anims.play(kSlotScreen, 0x4f, 6, 0x1e, kForward);
					step = kStepIdle;
				}
				break;
			case kStepFieldDown:
				if (pos > 0x50) {
					_anims.play(kSlotScreen, 0x17, 0x36, 4, kForward);
					step = kStepFieldGone;
					pos = 0;
				}
				break;
			case kStepFieldGone:
				if (pos > 0x13b) {
					step = kStepIdle;
					done = true;
				}
				break;
			case kStepStarting:
				if (pos > 0x6e) {
					_anims.play(kSlotScreen, 0x0d, 3, 3, kForward);
					step = kStepIdle;
				}
				break;
			default:
				break;
			}

			if (done)
				leaving = true;
			_dirty = true;
		}

		if (_dirty)
			redraw();

		if (debugChannelSet(3, kDebugTelescope) && (tick & 0x3f) == 0)
			dumpScreen(Common::String::format("terminal-%u.png", tick));

		present();
		sleep(10);
	}

	plate.free();
	sheet.free();

	// 0x1b03: CUTSCENE:sub_0c5c9's teardown, and [0xa7d4] for the room.
	_cutscene = false;
	_clipBottom = clip;
	_script.setFlag(kScenePlayed, 1);
	_script.setFlag(kTerminalReturn, 1);
	CursorMan.showMouse(true);
	debugC(1, kDebugRooms, "terminal: the screen closes on node 0x%02x", shown);
}

} // End of namespace Alien
