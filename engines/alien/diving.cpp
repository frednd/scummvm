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

#include "common/scummsys.h"

#include <math.h>
#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// Room 46, underwater: overlay 0x0ec3 and the resident segment 0x0ec7 beside it.
//
// The room is two views on [0xa785]. View 0 is WATER1, the shaft he comes down
// from room 48's pool, with the light falling through it (DIV_SADE on slot 0);
// view 1 is WATER2, the cave with the propeller on the left (DIV_PROP, slot 0)
// and the chest on the right (DIV_CHES, slot 1). View 1 is loaded as room 47:
// the overlay writes handler code 0x2f while the plates are read, so the
// background and the foreground sheet come out of room 47's row of the name
// table. MAIN:sub_00110 is what switches between them: the room's loop ending
// on submode 1 or 2 sets [0xa785] and dispatches room 46 again.
//
// There is no walk mask. The room zeroes the node count, so every click goes
// straight to OBJ:sub_07890, which in this room hands the leg to the swim's own
// plotter (OBJ:sub_0585d) and segment 0x0ec7 steps it (charanim.cpp,
// Walker::setSwimming). The same segment draws the two bubble pools and puts
// the whole playfield on the screen through a sine table, row by row, which
// is the water's wobble.
static const int kDivingRoom = 46;
static const int kShoreRoom = 41;

static const uint16 kView = 0xa785;			///< 0 the shaft, 1 the cave
static const uint16 kChestShut = 0xa786;
static const uint16 kKeyReady = 0xa788;		///< the key is still in the chest
static const uint16 kPropeller = 0xa789;		///< ships 1; room 50's valve stops it
static const uint16 kPassing = 0xa78a;		///< the propeller is drawn over him
static const uint16 kArrival = 0xa644;

static const byte kChestObj = 1;
static const byte kPicklock = 37;
static const byte kKeyObj = 2;
static const byte kOldKey = 38;
static const byte kNearPropeller = 4;
static const byte kThroughPropeller = 5;

static const uint kLightSlot = 0;
static const uint kPropellerSlot = 0;
static const uint kChestSlot = 1;
static const int kLightFrames = 0x21;
static const int kLightRate = 2;
static const int kChestFrames = 6;
static const int kChestRate = 3;
static const int kModeOnce = 1;

/// The chest's creak, sfx_play_delayed(1, 0, 0xfa0, 0x40, 0x14, 1) (0x0609).
static const uint kChestSample = 1;
static const uint32 kChestRateHz = 0xfa0;
static const byte kChestVolume = 0x40;
static const int8 kChestPanning = 0x14;
static const uint16 kChestDelay = 1;

static const byte kOutcomeOldKey = 2;
static const byte kOutcomeTooClose = 13;	///< the propeller is still turning

// The [0xa49f] machine.
static const byte kStepChestOpening = 3;
static const byte kStepChestOpen = 4;
static const byte kStepSwimOut = 0x14;
static const byte kStepSwimmingOut = 0x15;
static const byte kStepSwimmingIn = 0x1e;

/// The view-0 click tests at 0x00c3: a target this high takes him up into
/// room 48, one this low down into the cave; in the cave the same top edge
/// takes him back up the shaft (0x0186).
static const int kTopEdge = 0x18;
static const int kBottomEdge = 0x89;
static const int kOffTop = 0;
static const int kOffBottom = 0xbb;

static const byte kSubmodeUp = 1;			///< MAIN:sub_00110: view 0
static const byte kSubmodeDown = 2;			///< view 1
static const byte kSubmodePool = 3;			///< room 48
static const byte kSubmodeShore = 10;		///< room 41

/// Where the views place him on a switch (0x0336, 0x0350): his x is kept.
static const int kUpY = 0x6e;
static const int kDownY = 0;

/// Room 41 sends him in through the propeller's tunnel (0x03fa), and the
/// tunnel is where he leaves by (0x0642).
static const int kInX = 0x6b;
static const int kInY = 0x75;
static const int kOutX = 0;
static const int kOutY = 0x71;

/// OBJ:sub_079de: [0xa808] has to be five before a stop counts as standing.
static const int kStandSettle = 5;

// The bubbles, 0ec7:sub_0ec70..sub_0eff5. Pool 0 is the four pixel bubble, pool
// 1 the single pixel; both rise faster as they go (0ec7:0x15a, 0x0e9) and
// drift right at a speed that runs down by four each pass.
static const uint16 kBubbleDead = 0xffff;
static const uint kBubbleDeadRow = 0xf0;
static const uint kBubbleTopRow = 0xf;
static const uint16 kBubbleRise[2] = { 4, 1 };
static const int16 kBubbleDriftDecay = 4;
static const int kBubbleLastRow[2] = { 0x97, 0x9a };

/// Where the bubbles come from, as the random ranges sub_0eff5 adds its bases
/// to: in the shaft from below the bottom left, in the cave from the
/// propeller. The odds are one in `chance`.
struct BubbleSource {
	int chance[2];
	int x;
	int y;
	int drift;
};

static const BubbleSource kBubbleSources[2] = {
	{ { 7, 3 }, 0x1e, 0xa0, 0x9b },
	{ { 3, 3 }, 0x26, 0x4a, 0xff }
};

static const int kBubbleRiseRange[2] = { 0xf, 0x3 };

// The water, 0ec7:sub_0f0f1 and sub_0f1ca. Row r of the playfield shows row
// r + round(1.8) + round(1.8 sin a), a stepping by pi/64 from 3pi/2 down the
// table and the phase three entries a pass.
static const int kWaterTop = 13;
static const int kWaterRows = 0x8e;	///< the counter runs 0..0x8d inclusive
static const double kWaveAmplitude = 1.8;
static const double kWaveStart = 4.71238898038469;
static const double kWaveStep = 0.0490873852123405;
static const uint16 kWavePhaseStep = 3;
static const uint16 kWavePhaseWrap = 0x7f;

static int waveRound(double v) {
	return v < 0 ? -(int)(-v + 0.5) : (int)(v + 0.5);
}

/// The view picks slot 0's bank, and only the cave has the chest: the overlay
/// loads by [0xa785] (0x036a), where the table the port reads has all three.
void AlienEngine::loadDivingBanks(int room) {
	if (room != kDivingRoom)
		return;

	if (_script.flag(kView) == 0) {
		_anims.loadBank(kLightSlot, "DIV_SADE.DL1");
		_anims.loadBank(kChestSlot, nullptr);
	} else {
		_anims.loadBank(kPropellerSlot, "DIV_PROP.DL1");
		_anims.loadBank(kChestSlot, "DIV_CHES.DL1");
	}
}

/// The room has opened, in either view.
void AlienEngine::startDiving() {
	if (_room != kDivingRoom)
		return;

	_divingStep = 0;
	_divingExit = 0;
	_script.setFlag(kPassing, 0);

	for (uint p = 0; p < 2; p++) {
		for (uint i = 0; i < kDivingBubbles; i++) {
			DivingBubble &b = _divingBubbles[p][i];
			b.x = 0;
			b.y = kBubbleDead;
			b.vy = 0;
			b.drift = 0;
		}
	}

	double angle = kWaveStart;
	const int base = waveRound(kWaveAmplitude);
	for (uint i = 0; i < kDivingWaveRows; i++) {
		_divingWave[i] = (int16)(base + waveRound(sin(angle) * kWaveAmplitude));
		angle += kWaveStep;
	}
	_divingWavePhase = 0;

	// The opening plays as the table lifted them run every one of the
	// overlay's calls, whichever view: put back the one the view makes. The
	// chest's six frame play is the machine's (0x0620), not the opening's, and
	// an open chest is the plate stamp's to draw.
	const bool cave = _script.flag(kView) == 1;
	_anims.takeDown(kChestSlot);
	if (!cave)
		_anims.play(kLightSlot, 1, kLightFrames, kLightRate, kModeOnce);
	else if (_script.flag(kPropeller) != 1)
		_anims.takeDown(kPropellerSlot);

	// A switch between the views keeps his x and puts him at the edge he
	// swam out of (0x0336, 0x0350). A restored game is already where it was
	// saved.
	if (_restoring) {
		// Nothing: the save put him there.
	} else if (_mode == kDivingRoom) {
		_ben.placeSprite(_ben.spriteX(), _lastSubmode == kSubmodeUp ? kUpY : kDownY,
						 _ben.facing());
	} else if (_mode == kShoreRoom) {
		// In through the tunnel, the propeller drawn over him until he is
		// clear of it (0x03fa).
		_script.setFlag(kPassing, 1);
		_ben.swimTo(kInX, kInY, 1);
		_divingStep = kStepSwimmingIn;
		CursorMan.showMouse(false);
	}

	debugC(1, kDebugRooms, "diving: view %d, propeller %s", cave ? 1 : 0,
		   _script.flag(kPropeller) == 1 ? "turning" : "stopped");
}

/// Entry 3: the picklock on the chest, and the key out of it.
bool AlienEngine::armDiving(int obj, byte item) {
	if (_room != kDivingRoom)
		return false;

	// The line is the lifted row's (outcome 9, block 0); what the row cannot
	// carry is the machine that waits for it.
	if (obj == kChestObj && item == kPicklock) {
		_divingStep = kStepChestOpening;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "diving: the picklock finds the catch");
		return true;
	}

	if (obj == kKeyObj && item == Inventory::kNoItem && _script.flag(kKeyReady) == 1) {
		_script.setFlag(kKeyReady, 0);
		_inventory.add(kOldKey);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeOldKey, anchorX, anchorY);
		debugC(1, kDebugItems, "diving: the chest holds item %u (%s)", kOldKey,
			   _inventory.name(kOldKey).c_str());
		return true;
	}

	return false;
}

/**
 * Entry 0's own half of a click, after the walk geometry: the edges that leave
 * the view, which only arm while he is not turning (0x00ca, 0x018d), and then
 * the swim itself. Every click re-arms from nothing, the way 1021:sub_105fa
 * clears walk_submode first.
 */
bool AlienEngine::divingSwimTo(int x, int y, int arrivalFacing) {
	if (_room != kDivingRoom)
		return false;

	_divingExit = 0;
	if (!_ben.isTurning()) {
		if (_script.flag(kView) == 0) {
			if (y < kTopEdge) {
				_divingExit = kSubmodePool;
				y = kOffTop;
			}
			if (y > kBottomEdge) {
				_divingExit = kSubmodeDown;
				y = kOffBottom;
			}
		} else if (y < kTopEdge) {
			_divingExit = kSubmodeUp;
			y = kOffTop;
		}
	}

	debugC(1, kDebugGraphics, "swim %d,%d -> %d,%d facing %d%s",
		   _ben.walkX(), _ben.walkY(), x, y, arrivalFacing,
		   _divingExit ? Common::String::format(", arms submode %u", _divingExit).c_str() : "");
	_ben.swimTo(x, y, arrivalFacing);
	_dirty = true;
	return true;
}

/// The view switch MAIN:sub_00110 makes, or the chain for the two real exits.
bool AlienEngine::divingTakeExit(byte submode) {
	if (submode != kSubmodeUp && submode != kSubmodeDown)
		return takeExit(submode);

	_script.setFlag(kView, submode == kSubmodeUp ? 0 : 1);
	_mode = (byte)kDivingRoom;
	_lastSubmode = submode;
	debugC(1, kDebugRooms, "diving: up to view %d", submode == kSubmodeUp ? 0 : 1);
	return loadRoom(kDivingRoom, false, true);
}

/// The room's tick: the [0xa49f] machine, the arrivals, and the exits.
void AlienEngine::stepDiving() {
	if (_room != kDivingRoom)
		return;

	const bool standing = !_ben.isWalking() && !_ben.isTurning() &&
						  _ben.idleCount() >= kStandSettle;

	switch (_divingStep) {
	case kStepChestOpening:
		if (speechDone())
			_divingStep = kStepChestOpen;
		break;

	case kStepChestOpen:
		_sound.queue(kChestSample, kChestRateHz, kChestVolume, kChestPanning, kChestDelay);
		_script.setFlag(kChestShut, 0);
		_anims.play(kChestSlot, 1, kChestFrames, kChestRate, kModeOnce);
		rebuildHotspots(_room);
		CursorMan.showMouse(true);
		_divingStep = 0;
		break;

	case kStepSwimOut:
		// Through the stopped propeller and out to the shore (0x063d).
		_script.setFlag(kPassing, 1);
		_ben.swimTo(kOutX, kOutY, 4);
		_divingStep = kStepSwimmingOut;
		playMusicSlot(1);
		break;

	case kStepSwimmingOut:
		if (standing) {
			_divingStep = 0;
			takeExit(kSubmodeShore);
			return;
		}
		break;

	case kStepSwimmingIn:
		if (standing) {
			_divingStep = 0;
			CursorMan.showMouse(true);
			_script.setFlag(kPassing, 0);
		}
		break;

	default:
		break;
	}

	// The two arrivals the room answers itself (0x069b, 0x06bd): the tunnel,
	// which only registers once the propeller has stopped, and the propeller
	// while it is still turning.
	if (standing && _script.flag(kArrival) == kThroughPropeller) {
		_script.setFlag(kArrival, 0);
		_divingStep = kStepSwimOut;
		CursorMan.showMouse(false);
		debugC(1, kDebugRooms, "diving: out through the tunnel");
	}

	if (standing && _script.flag(kArrival) == kNearPropeller) {
		_script.setFlag(kArrival, 0);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, kOutcomeTooClose, anchorX, anchorY);
	}

	// And the edge a click armed, once he has stopped (0x06e6).
	if (_divingExit && standing) {
		const byte submode = _divingExit;
		_divingExit = 0;
		divingTakeExit(submode);
	}
}

/// The bubbles, a pass of the room loop each.
void AlienEngine::stepDivingWater() {
	if (_room != kDivingRoom)
		return;

	// 0ec7:sub_0efd9: every bubble rises and drifts; one that reaches the top
	// of the playfield, or wraps past it, is dead.
	for (uint p = 0; p < 2; p++) {
		for (uint i = 0; i < kDivingBubbles; i++) {
			DivingBubble &b = _divingBubbles[p][i];
			uint row = b.y >> 7;
			if (row <= kBubbleTopRow) {
				b.y = kBubbleDead;
				row = kBubbleDeadRow;
			}
			if (row >= kBubbleDeadRow) {
				b.vy = 0;
				continue;
			}

			b.vy += kBubbleRise[p];
			b.y -= b.vy;
			b.x += (uint16)b.drift;
			if (b.drift >= kBubbleDriftDecay)
				b.drift -= kBubbleDriftDecay;
		}
	}

	// 0ec7:sub_0eff5: new ones, only while the propeller turns -- in the shaft
	// as well as beside it.
	if (_script.flag(kPropeller) == 1) {
		const BubbleSource &src = kBubbleSources[_script.flag(kView) == 1 ? 1 : 0];
		for (uint p = 0; p < 2; p++) {
			if (_rnd.getRandomNumber(src.chance[p] - 1) != 0)
				continue;

			const int x = (int)_rnd.getRandomNumber(6) + src.x;
			const int y = (int)_rnd.getRandomNumber(0x1e) + src.y;
			const int drift = (int)_rnd.getRandomNumber(0xfe) + src.drift;
			const int vy = (int)_rnd.getRandomNumber(kBubbleRiseRange[p] - 1);

			// sub_0ec70: the first dead slot, if there is one.
			for (uint i = 0; i < kDivingBubbles; i++) {
				DivingBubble &b = _divingBubbles[p][i];
				if ((b.y >> 7) <= kBubbleDeadRow)
					continue;
				b.x = (uint16)(x << 8);
				b.y = (uint16)(y << 7);
				b.vy = (uint16)vy;
				b.drift = (int16)drift;
				break;
			}
		}
	}

	_dirty = true;
}

/// The water's phase. The original steps it once a pass of the room loop
/// like the bubbles, but that loop outruns the tick pair the port gives
/// them; stepped every tick, the ripple keeps the original's pace.
void AlienEngine::stepDivingWave() {
	if (_room != kDivingRoom)
		return;

	_divingWavePhase += kWavePhaseStep;
	if (_divingWavePhase > kWavePhaseWrap)
		_divingWavePhase = 0;

	_dirty = true;
}

/// Over the composed room: the bubbles, then the water that moves all of it.
void AlienEngine::drawDivingWater(Graphics::Surface &dest) const {
	if (_room != kDivingRoom)
		return;

	// 0ec7:sub_0edee, the four pixel bubble in its three blues, and sub_0ee63,
	// the single pixel one.
	static const int8 kShape[][3] = {
		{ 1, 0, (int8)0x92 }, { 2, 0, (int8)0x92 },
		{ 0, 1, (int8)0x92 }, { 1, 1, (int8)0x90 }, { 2, 1, (int8)0x90 }, { 3, 1, (int8)0x92 },
		{ 0, 2, (int8)0x92 }, { 1, 2, (int8)0x90 }, { 2, 2, (int8)0x91 }, { 3, 2, (int8)0x92 },
		{ 1, 3, (int8)0x92 }, { 2, 3, (int8)0x92 }
	};

	for (uint p = 0; p < 2; p++) {
		for (uint i = 0; i < kDivingBubbles; i++) {
			const DivingBubble &b = _divingBubbles[p][i];
			const int row = b.y >> 7;
			if (row > kBubbleLastRow[p])
				continue;
			const int col = b.x >> 8;

			if (p == 1) {
				if (col < dest.w && row < dest.h)
					*(byte *)dest.getBasePtr(col, row) = 0x90;
				continue;
			}

			for (uint s = 0; s < ARRAYSIZE(kShape); s++) {
				const int px = col + kShape[s][0];
				const int py = row + kShape[s][1];
				if (px < dest.w && py < dest.h)
					*(byte *)dest.getBasePtr(px, py) = (byte)kShape[s][2];
			}
		}
	}

	// 0ec7:sub_0f1ca: the playfield goes out row by row, each from a row a
	// little further down. Every offset is down, so the copy runs in place.
	for (int r = 0; r < kWaterRows; r++) {
		const int y = kWaterTop + r;
		const int from = y + _divingWave[_divingWavePhase + r];
		if (from == y || from >= dest.h)
			continue;
		memcpy(dest.getBasePtr(0, y), dest.getBasePtr(0, from), dest.w);
	}
}

} // End of namespace Alien
