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
#include "alien/play.h"
#include "alien/resources.h"

namespace Alien {

// The ship's elevator: 15f3:sub_16895, the panel rooms 51, 53, 55 and 57 all
// open from object 2 (corridor.cpp).
//
// It is a scene with a frame loop of its own, like the observatory computer
// next to it in the same resident unit (telescope.cpp): SHP_ELEV.PCX with
// seven banks over it, the cursor on, and nothing of the room behind it. The
// player puts one of the three security cards in the slot and presses a floor;
// the floor is left in [0xa79e], and LOGIC:sub_1265c, which every one of the
// four rooms calls as its loop ends, turns it into game_submode 101..104 --
// the four rows transitions.cpp already had in each room and that nothing in
// the port ever armed.
//
// Which floors a card may press is a table in the data segment, 271a:0x6776,
// four bytes a card, read with `cmp byte ptr [di + 0x6776], 1` and never
// written. With no card at all the panel still opens, sits for 0x78 ticks and
// sends Ben to room 55 with [0xa7a0] raised, which room 55 answers with its
// line 5 (corridor.cpp).

/// The seven banks, 15f3:0x090a..0x0958, in the slots the loads put them in.
static const uint kSlotFloors = 0;		///< ELV_FLOO: which floors the card lights
static const uint kSlotCard1 = 1;		///< ELV_CAR1..3: the three cards on the tray
static const uint kSlotSlot = 4;		///< ELV_SLOT: the card slot
static const uint kSlotPress = 5;		///< ELV_PRES: the doors / the press
static const uint kSlotRead = 6;		///< ELV_READ: the readout

static const char *const kPlate = "SHP_ELEV.PCX";
static const char *const kBanks[] = {
	"ELV_FLOO.DL1", "ELV_CAR1.DL1", "ELV_CAR2.DL1", "ELV_CAR3.DL1",
	"ELV_SLOT.DL1", "ELV_PRES.DL1", "ELV_READ.DL1"
};

static const int kForward = 1;		///< MIDAS:0xb85, anim_play_mode1
static const int kReturn = 2;		///< MIDAS:0xc2e, sub_186be

/// The three cards, items 0x29..0x2b, as sprite_find_slot asks for them.
static const byte kCardItems[] = { 41, 42, 43 };

/// 271a:0x6776, one row a card (row 0 is never read: the floors are only
/// tested with a card in the slot), one byte a floor.
static const byte kFloorAllowed[4][4] = {
	{ 0, 0, 0, 0 },
	{ 0, 1, 1, 1 },	// item 41
	{ 1, 1, 1, 1 },	// item 42
	{ 1, 1, 0, 0 }	// item 43
};

/// OBJ:sub_08996's rectangles, bounds exclusive as telescope.cpp has them.
struct PanelBox {
	int x1, y1, x2, y2;
};
static const PanelBox kFloorBoxes[4] = {
	{ 0x40, 0x7c, 0x50, 0x80 },		// 0x0ac4, floor 1 -> submode 101, room 53
	{ 0x8d, 0x7c, 0x9d, 0x80 },		// 0x0af4, floor 2 -> submode 102, room 57
	{ 0xb3, 0x8f, 0xc3, 0x93 },		// 0x0b26, floor 3 -> submode 103, room 51
	{ 0xd7, 0x73, 0xe7, 0x77 }		// 0x0b59, floor 4 -> submode 104, room 55
};
static const PanelBox kEjectBox = { 0x106, 0x1b, 0x117, 0x55 };	// 0x0b79
static const PanelBox kCardBoxes[3] = {
	{ 0x62, 0xa2, 0x87, 0xbc },		// 0x0bda
	{ 0x88, 0xa3, 0xae, 0xbc },		// 0x0c21
	{ 0xae, 0xa3, 0xd4, 0xbb }		// 0x0c69
};

static bool inPanelBox(int x, int y, const PanelBox &box) {
	return x > box.x1 && y > box.y1 && x < box.x2 && y < box.y2;
}

/// [bp - 1]: what a click may do.
enum PanelClicks {
	kClicksNone = 0,	///< a floor is pressed, the doors are closing
	kClicksCard = 1,	///< the slot is empty: a card may go in
	kClicksFloor = 2	///< a card is in: a floor, or the eject
};

/// The panel's own [0xa49f] steps.
static const byte kStepIdle = 0;
static const byte kStepCardIn = 3;
static const byte kStepCardRead = 4;
static const byte kStepEjected = 0x14;
static const byte kStepFloor = 0x32;
static const byte kStepClosing = 0x33;
static const byte kStepNoCard = 0x64;

static const uint16 kNoCardWait = 0x78;

/// [0xa79e]: the floor the panel was left on, 1..4, or 0x64 with no card.
static const uint16 kFloorPicked = 0xa79e;
static const byte kFloorNoCard = 0x64;
/// [0xa7a0]: room 55 owes the no-card line.
static const uint16 kNoCardLine = 0xa7a0;
/// [0xa7a1]: the next room opens with Ben stepping out of its elevator.
static const uint16 kElevatorArrival = 0xa7a1;
/// [0xa6d3]: a scene has just played (CUTSCENE:sub_0c5c9, telescope.cpp).
static const uint16 kScenePlayed = 0xa6d3;

static const byte kSubmodeFirstFloor = 101;		///< LOGIC:sub_1265c: 0x65 + floor - 1
static const byte kSubmodeNoCard = 104;			///< and 0x68 for no card

static const uint kScriptedGiveUp = 3000;

/**
 * The panel, as 15f3:sub_16895 runs it. Blocks like playObservatoryScreen(),
 * and ends by taking the exit LOGIC:sub_1265c would have armed.
 */
void AlienEngine::playElevatorPanel() {
	Graphics::Surface plate;
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kPlate), plate, palette)) {
		plate.free();
		warning("elevator: could not load %s", kPlate);
		return;
	}

	stopSpeech();
	_cutscene = true;
	_background.free();
	_background = plate;
	_roomWidth = kScreenWidth;
	_scrollX = 0;

	const int clip = _clipBottom;
	_clipBottom = _screen.h - 1;
	memcpy(_palette, palette, sizeof(_palette));
	// 0x0977: [0xa948] and [0xa949], the cursor and the clicks both on.
	CursorMan.showMouse(true);

	_anims.loadBanks(kBanks, ARRAYSIZE(kBanks));
	// 0x09ee: the readout, relaunched every pass below.
	_anims.play(kSlotRead, 1, 3, 6, kForward);

	// 0x09fb..0x0a2e: sprite_find_slot for each card.
	bool held[3];
	bool noCard = true;
	for (uint i = 0; i < 3; i++) {
		held[i] = _inventory.has(kCardItems[i]);
		noCard = noCard && !held[i];
	}

	uint card = 0;							// [bp - 2]
	PanelClicks clicks = kClicksCard;		// [bp - 1]
	byte step = noCard ? kStepNoCard : kStepIdle;
	uint16 pos = 0;							// [0xa49c]
	bool loopPress = false;					// [0xa53f]
	byte floor = 0;							// [0xa79e]
	bool done = false;

	// sub_16814: the floor lights for the card in the slot, one held frame.
	const auto showFloors = [&](uint c) {
		_anims.play(kSlotFloors, (int)c + 1, 1, 0, kReturn);
	};
	// sub_167d3: the cards still on the tray.
	const auto showCards = [&]() {
		for (uint i = 0; i < 3; i++)
			if (held[i])
				_anims.play(kSlotCard1 + i, 1, 1, 0, kForward);
	};
	// sub_167a7: all three lifted off the tray.
	const auto hideCards = [&]() {
		for (uint i = 0; i < 3; i++)
			_anims.play(kSlotCard1 + i, 2, 1, 0, kForward);
	};

	showFloors(0);
	showCards();

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	_dirty = true;

	debugC(1, kDebugRooms, "elevator: the panel opens in room %d, cards %d%d%d", _room,
		   held[0], held[1], held[2]);

	const auto handleClick = [&](int x, int y) {
		if (clicks == kClicksFloor) {
			for (uint f = 0; f < 4; f++) {
				if (!kFloorAllowed[card][f] || !inPanelBox(x, y, kFloorBoxes[f]))
					continue;
				floor = (byte)(f + 1);
				step = kStepFloor;
				debugC(1, kDebugRooms, "elevator: floor %u pressed", floor);
				return;
			}
			if (inPanelBox(x, y, kEjectBox)) {
				// 0x0b8f
				showCards();
				_anims.play(kSlotSlot, 1, 1, 0, kForward);
				clicks = kClicksCard;
				card = 0;
				showFloors(0);
				loopPress = false;
				_anims.play(kSlotPress, 0x0b, 8, 1, kForward);
				step = kStepEjected;
				debugC(1, kDebugRooms, "elevator: card ejected");
			}
			return;
		}

		if (clicks != kClicksCard)
			return;

		for (uint i = 0; i < 3; i++) {
			if (!held[i] || !inPanelBox(x, y, kCardBoxes[i]))
				continue;
			// 0x0bf1 / 0x0c39 / 0x0c81
			_anims.play(kSlotSlot, (int)i + 2, 1, 0, kForward);
			hideCards();
			clicks = kClicksFloor;
			card = i + 1;
			showFloors(card);
			step = kStepCardIn;
			debugC(1, kDebugRooms, "elevator: card %u (item %u) in the slot", card,
				   kCardItems[i]);
			return;
		}
	};

	static const uint32 kTickMillis = AlienEngine::kMasterTickMillis;
	uint32 last = millis();
	uint32 tick = 0;
	uint scripted = 0;
	uint32 playWaitUntil = 0;

	while (!shouldQuit() && !_quit && !done) {
		Common::Event event;
		while (g_system->getEventManager()->pollEvent(event)) {
			if (event.type == Common::EVENT_LBUTTONDOWN ||
				event.type == Common::EVENT_RBUTTONDOWN)
				handleClick(event.mouse.x, event.mouse.y);
		}

		// The same scripted reader as the observatory computer's: waits are
		// held for real, a click of either kind is a click on the panel.
		if (_playActive && playWaitUntil != 0 && millis() >= playWaitUntil)
			playWaitUntil = 0;

		if (_playActive && playWaitUntil == 0) {
			while (_playIndex < _play.commands().size()) {
				const PlayCommand &cmd = _play.commands()[_playIndex];
				if (cmd.type == PlayCommand::kWait || cmd.type == PlayCommand::kSettle) {
					_playIndex++;
					scripted = 0;
					// A wait is in master ticks, as the outer driver and the
					// original's runner count it; a settle has nothing to watch
					// here and holds for its timeout in milliseconds.
					playWaitUntil = millis() + (uint32)MAX(cmd.a, 0) *
						(cmd.type == PlayCommand::kWait ? kMasterTickMillis : 1);
					break;
				}
				if (cmd.type != PlayCommand::kClick && cmd.type != PlayCommand::kRightClick)
					break;

				_playIndex++;
				scripted = 0;
				debugC(1, kDebugRooms, "elevator: play: %u: click %d,%d", cmd.sourceLine,
					   cmd.a, cmd.b);
				handleClick(cmd.a, cmd.b);
				break;
			}

			if (++scripted > kScriptedGiveUp && step != kStepNoCard &&
				step != kStepClosing && step != kStepFloor) {
				// A port-only guard, as the observatory computer has one: the
				// original panel has no way out but a floor.
				warning("elevator: the script left the panel open; taking the first floor a card allows");
				playLeftOpen("the elevator panel");
				for (uint i = 0; i < 3 && !card; i++)
					if (held[i])
						card = i + 1;
				for (uint f = 0; f < 4 && !floor; f++)
					if (kFloorAllowed[card][f])
						floor = (byte)(f + 1);
				step = kStepFloor;
			}
		}

		const uint32 now = millis();
		while (now - last >= kTickMillis && !done) {
			last += kTickMillis;
			tick++;
			sceneClockTick();

			if ((tick & 3) != 0)
				continue;

			pos++;
			_anims.tick();

			// 0x0a95..0x0aa5: the readout always loops, the press when told.
			_anims.relaunch(kSlotRead);
			if (loopPress)
				_anims.relaunch(kSlotPress);

			switch (step) {
			case kStepCardIn:
				// 0x0cac: the readout is cut, the doors start to open. The
				// original zeroes [0xa4f0] and never draws slot 6 again, so
				// what the press draws next covers its last frame; the port
				// recomposites every slot every pass, and a stopped mode-1
				// slot would keep "PLEASE INSERT YOUR SECURITY CARD" on top of
				// the card's own text. Taking it down is the same picture.
				_anims.takeDown(kSlotRead);
				_anims.play(kSlotPress, 1, 0x0a, 2, kForward);
				step = kStepCardRead;
				break;
			case kStepCardRead:
				// 0x0cca: [0xa4ef] == 2
				if (_anims.remaining(kSlotPress) == 2) {
					loopPress = true;
					_anims.play(kSlotPress, 9, 3, 6, kForward);
					step = kStepIdle;
				}
				break;
			case kStepEjected:
				// 0x0cee: [0xa4ef] == 0
				if (_anims.remaining(kSlotPress) == 0) {
					_anims.play(kSlotRead, 1, 3, 6, kForward);
					step = kStepIdle;
				}
				break;
			case kStepFloor:
				// 0x0d0d
				loopPress = false;
				_anims.play(kSlotPress, 0x0b, 8, 1, kForward);
				step = kStepClosing;
				clicks = kClicksNone;
				break;
			case kStepClosing:
				// 0x0d2e
				if (_anims.remaining(kSlotPress) == 0)
					done = true;
				break;
			case kStepNoCard:
				// 0x0d40
				if (pos > kNoCardWait) {
					floor = kFloorNoCard;
					done = true;
				}
				break;
			default:
				break;
			}

			_dirty = true;
		}

		if (_dirty)
			redraw();

		if (debugChannelSet(3, kDebugTelescope) && (tick & 0x3f) == 0)
			dumpScreen(Common::String::format("elevator-%u.png", tick));

		present();
		sleep(10);
	}

	// 0x0d5e: CUTSCENE:sub_0c5c9, the shared teardown, then [0xa7a1].
	_cutscene = false;
	_clipBottom = clip;
	_script.setFlag(kScenePlayed, 1);
	_script.setFlag(kElevatorArrival, 1);
	_script.setFlag(kFloorPicked, floor);

	// LOGIC:sub_1265c, reached through [0xa79f] and the room's loop ending.
	byte submode;
	if (floor == kFloorNoCard) {
		_script.setFlag(kNoCardLine, 1);
		submode = kSubmodeNoCard;
	} else {
		submode = (byte)(kSubmodeFirstFloor + floor - 1);
	}

	debugC(1, kDebugRooms, "elevator: the panel closes on floor %u, submode %u", floor,
		   submode);

	const int room = _room;
	if (!takeExit(submode) && room > 0)
		loadRoom(room, false, true);

	g_system->getPaletteManager()->setPalette(_palette, 0, 256);
	CursorMan.showMouse(true);
	_dirty = true;
}

} // End of namespace Alien
