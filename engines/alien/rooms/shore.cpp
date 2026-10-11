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

#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/rooms/shore.h"

namespace Alien {

// Room 41, the shore, and the two ledges high up in the cave behind it.
//
// The room is one plate with three standing places, and [0xa77f] is which of
// them he is at: 0 is the beach itself, the only one with walk nodes and a mask
// (0x087d loads both, and 0x0878 clears the node count for the other two), 1
// is the left ledge, with maze B's doorway (object 5) and a fence (object 7),
// and 2 is the far ledge, at the middle doorway (object 6) into maze A. The
// room's init stands him on either ledge with a char_place of its own -- both
// already lifted into roominit.cpp under their [0xa77f] guard. The hover names
// (NAMEROOM R41.TAL) are what the objects are: 5 doorway, 6 doorway, 7 fence,
// 8 ledge, and 9 the rope, registered only once [0xa780] says it is there.
//
// What that flag gates is which door is a door. walkgeom.cpp's room 41 rows
// carry one `kWalkSubmode` per view: object 2 arms submode 1 at the beach, and
// only while [0xa781] is 0; object 5 arms submode 2 on the left ledge; object
// 6 arms submode 3 on the far one. Those are the three ways into the two mazes
// (transitions.cpp: 41/1 and 41/2 -> room 44, 41/3 -> room 43), and none of
// them can be armed until something writes the flag. Three things do, and only
// one of them was ported before this: the two mazes, on the way out
// (maze.cpp), and this room's
// own tick, which is what walking along the rope does.
//
// So the chain the game intends, and the reason the pick-axe is where it is:
//
//   beach          object 2, opened  ([0xa781] := 0, an ordinary script row)
//                  then walked to    -> submode 1 -> maze B, cell 0
//   maze B         the pick-axe, item 39, and back out to the beach or to the
//                  left ledge (maze.cpp)
//   left ledge     the pick-axe used on the fence (objects 6, 7 or 8):
//                  [0xa49f] 0xa..0xd, MAE_THRO -- the axe tied to the rope
//                  and thrown across -- which spends it and sets [0xa780]
//   left ledge     walked to object 6, 8 or 9, which with [0xa780] set is
//                  the walk along the rope (MAE_SLIP the first time, MAE_RIGH
//                  after) -> [0xa77f] := 2, stood at the middle doorway
//   far ledge      walked to object 6 -> submode 3 -> maze A
//
// [0xa644] is the object the last left click was on (`HOTSPOT:sub_13605`
// latches the hovered object into it, 1336:02ba), and both ledges branch on it
// once the walk it started has finished. The port latches it the same way, in
// clickAt.
//
// Every view also refuses the objects that are not a way anywhere from where he
// stands, once he has stopped (OBJ:sub_07961, [0xa808] >= 5): on the beach the
// ledges' four (5-8) are "I can't reach it from here" (event 3, 0x097f), and on
// either ledge the beach's own three are events 5, 6 and 9, with the far ledge's
// door and the gap before the rope is across event 8 (0x09e2-0x0a2f and
// 0x0ad3-0x0b05). A ledge drops [0xa644] for every click it has answered.
//
// The diving suit on the water (object 3, at the beach) is the way back down
// to room 46: a line, MAE_DIVE, and then submode 10 with [0xa785] set, so the
// swim opens in the cave, out of the propeller's tunnel (diving.cpp).
static const int kShoreRoom = 41;

static const uint16 kView = 0xa77f;		///< 0 the beach, 1 and 2 the two ledges
static const uint16 kRockOpen = 0xa780;	///< the rope is across
static const uint16 kSeenMouth = 0xa783;	///< the first climb up has been made

static const byte kBeach = 0;
static const byte kFirstMouth = 1;
static const byte kSecondMouth = 2;

/// [0xa644], the object the last left click was on.
static const uint16 kClickedObj = 0xa644;

static const byte kAxe = 39;			///< item 0x27
static const byte kSuit = 0x1c;
static const byte kWaterObj = 3;
static const uint16 kSuitTried = 0xa782;	///< the second dive says so
static const uint16 kDivingView = 0xa785;	///< room 46's: 1 is the cave
static const byte kRockObjA = 6, kRockObjB = 7, kRockObjC = 8;

// The pick-axe machine, [0xa49f] 0xa..0xd (0x0bd0-0x0c41).
static const byte kStepAxeLine = 0x0a;
static const byte kStepAxeSwing = 0x0b;
static const byte kStepAxeDone = 0x0c;
static const byte kStepAxeLast = 0x0d;
// And the two climbs, 0x32/0x33 and 0x46, each of which is one animation and
// then the cursor back (0x0c43-0x0c95).
static const byte kStepClimbUp = 0x32;
static const byte kStepClimbFirst = 0x33;
static const byte kStepClimbBack = 0x46;
// And the dive, 3..5 (0x0b54-0x0bc8).
static const byte kStepSuitStart = 3;
static const byte kStepSuitLine = 4;
static const byte kStepSuitDive = 5;
static const byte kOutcomeSuitFirst = 0x16;	///< an empty entry: nothing is said
static const byte kOutcomeSuitAgain = 0x17;	///< "...try out my diving suit again."
static const uint kDiveSlot = 10;
static const int kDiveFrames = 0x40;
static const byte kDiveSubmode = 10;

// The refusals (0x09b3, 0x09e2-0x0a1e, 0x0ad3-0x0af4).
static const byte kOutcomeTooHigh = 3;		///< "I can't reach it from here."
static const byte kOutcomeHurt = 5;		///< obj 1: "I better not. I might hurt myself."
static const byte kOutcomeCaverns = 6;		///< obj 2: "That door leads to the caverns."
static const byte kOutcomeTooFar = 8;		///< obj 6, 8: "...too far away for me to jump."
static const byte kOutcomeShallow = 9;		///< obj 3: "...might not be deep enough..."
/// OBJ:sub_07961: [0xa808] has to be five before a stop counts as arrived.
static const int kStandSettle = 5;

static const byte kOutcomeSwing = 0x18;	///< "..." as the axe goes in
static const byte kOutcomeOpen = 0x19;	///< and once the rope is across

static const uint kAxeSlot = 5;
static const int kAxeFrames = 0x28;
static const uint kClimbSlot = 6;		///< every climb after the first
static const int kClimbFrames = 0x1e;
static const uint kFirstClimbSlot = 9;	///< and the longer one it gets once
static const int kFirstClimbFrames = 0x3a;
static const uint kBackSlot = 7;
static const int kAnimRate = 4;
static const int kAnimMode = 1;

/// Where the rope across stands him, at the far ledge's door:
/// char_place(0, 0x80, -1, -0xd, 2) at 0x0a91, as a sprite origin.
static const int16 kAcrossX = 0x80, kAcrossY = -0x0d;
static const byte kAcrossFacing = 2;
/// And the way back stands him on the first ledge again:
/// char_place(0, 0x2c, -1, -0xf, 4) at 0x0b3b.
static const int16 kBackX = 0x2c, kBackY = -0x0f;
static const byte kBackFacing = 4;

ShoreRoom::ShoreRoom(AlienEngine *vm) : Room(vm),
		_shoreStep(0) {
}

/**
 * A walk on either ledge. The room's init zeroes the node count before it
 * looks at [0xa77f] (0x0878) and loads KIERRA41 only for the beach, so up on a
 * ledge OBJ:sub_07890 has nothing to route through and goes straight to the
 * target. The beach's mask and nodes would stop him well short of where the pick-axe throw
 * starts.
 */
bool ShoreRoom::walk(int x, int y, int arrivalFacing) {
	if (_vm->_room != kShoreRoom || _vm->_script.flag(kView) == kBeach)
		return false;

	_vm->straightWalkTo(x, y, arrivalFacing);
	return true;
}

/// Every arrival resets the machine; the view itself is a flag and outlives it.
void ShoreRoom::start() {
	if (_vm->_room != kShoreRoom)
		return;

	_shoreStep = 0;
}

/**
 * The pick-axe on the fence, on the left ledge: entry 3's one item branch
 * (0x0043-0x0077).
 */
bool ShoreRoom::armShoreAxe(int obj, byte item) {
	if (_vm->_room != kShoreRoom || item != kAxe)
		return false;
	if (obj != kRockObjA && obj != kRockObjB && obj != kRockObjC)
		return false;
	if (_vm->_script.flag(kView) != kFirstMouth)
		return false;

	int anchorX, anchorY;
	_vm->characterAnchor(anchorX, anchorY);
	_vm->queueOutcome(_vm->_tal, kOutcomeSwing, anchorX, anchorY);
	CursorMan.showMouse(false);
	_shoreStep = kStepAxeLine;
	debugC(1, kDebugRooms, "shore: the pick-axe and the rope go over");
	return true;
}

/// The diving suit on the water, entry 3's other item branch (0x007c).
bool ShoreRoom::armShoreSuit(int obj, byte item) {
	if (_vm->_room != kShoreRoom || item != kSuit || obj != kWaterObj)
		return false;
	if (_vm->_script.flag(kView) != kBeach)
		return false;

	_shoreStep = kStepSuitStart;
	debugC(1, kDebugRooms, "shore: into the water again");
	return true;
}

/**
 * A walk finished at one of the two ledges (0x09c4 and 0x0ab5).
 *
 * Both blocks read the object the click was on and, for the three that are the
 * way on, swap the view. The original swaps it before the climb animation
 * rather than after, so the rectangles and the armed submode belong to the new
 * ledge from that frame on -- which is why the table is rebuilt here too.
 */
/**
 * Whether a ledge has a click it will answer once the walk is done, for a
 * scripted settle: a walk that ends a pixel short takes a step, and the settle
 * would otherwise end on the tick between the step and the room's own test.
 */
bool ShoreRoom::busy() const {
	if (_vm->_room != kShoreRoom || _shoreStep)
		return false;
	const byte view = _vm->_script.flag(kView);
	const byte obj = _vm->_script.flag(kClickedObj);
	if (view == kBeach)
		return obj >= 5 && obj <= 8;
	return obj != 0;
}

/// What the view says for an object it cannot reach, or 0 (0x0992, 0x09e2, 0x0ad3).
byte ShoreRoom::shoreRefusal(byte view, byte obj) const {
	if (view == kBeach)
		return obj >= 5 && obj <= 8 ? kOutcomeTooHigh : 0;

	byte line = 0;
	if (obj == 1)
		line = kOutcomeHurt;
	if (obj == 2)
		line = kOutcomeCaverns;
	if (obj == 3)
		line = kOutcomeShallow;
	if (view == kFirstMouth && _vm->_script.flag(kRockOpen) == 0 && (obj == 6 || obj == 8))
		line = kOutcomeTooFar;
	return line;
}

void ShoreRoom::shoreArrival() {
	const byte view = _vm->_script.flag(kView);
	if (_shoreStep || _vm->_ben.isWalking() || _vm->_ben.isTurning())
		return;

	const byte obj = _vm->_script.flag(kClickedObj);
	if (!obj)
		return;

	// The refusals, and the ledges' drop of a click that is no way on. These
	// speak over whatever the click itself said: the original's test does not
	// wait for the line, only for him to have stood five frames.
	const bool crossing = view == kFirstMouth
						  ? _vm->_script.flag(kRockOpen) == 1 && (obj == 6 || obj == 8 || obj == 9)
						  : view == kSecondMouth && (obj == 5 || obj == 7 || obj == 9);
	if (!crossing) {
		if (_vm->_ben.idleCount() < kStandSettle)
			return;
		const byte line = shoreRefusal(view, obj);
		if (view == kBeach && !line)
			return;
		_vm->_script.setFlag(kClickedObj, 0);
		if (line) {
			int anchorX, anchorY;
			_vm->characterAnchor(anchorX, anchorY);
			_vm->queueOutcome(_vm->_tal, line, anchorX, anchorY);
			debugC(1, kDebugRooms, "shore: object %u is out of reach, event %u", obj, line);
		}
		return;
	}

	if (!_vm->speechDone())
		return;

	if (view == kFirstMouth) {
		// 0x0a34: and only once the rope is across.

		_vm->_script.setFlag(kClickedObj, 0);
		_vm->_script.setFlag(kView, kSecondMouth);
		CursorMan.showMouse(false);

		// 0x0a5a: the walk along the rope is the animation's, MAE_RIGH, and
		// the first one is MAE_SLIP, a longer one with a wobble in it, which
		// [0xa783] remembers has been seen.
		if (_vm->_script.flag(kSeenMouth) == 1) {
			_shoreStep = kStepClimbUp;
			_vm->playCharacterAnim(kClimbSlot, 1, kClimbFrames, kAnimRate, kAnimMode);
		} else {
			_vm->_script.setFlag(kSeenMouth, 1);
			_shoreStep = kStepClimbFirst;
			_vm->playCharacterAnim(kFirstClimbSlot, 1, kFirstClimbFrames, kAnimRate, kAnimMode);
		}

		// 0x0a91: and he is put down at the far door at once, out of sight
		// until the crossing has played.
		_vm->_ben.placeSprite(kAcrossX, kAcrossY, kAcrossFacing);

		debugC(1, kDebugRooms, "shore: across the rope to the far ledge");
	} else {
		// 0x0b0a: and back down again.
		_vm->_script.setFlag(kClickedObj, 0);
		_vm->_script.setFlag(kView, kFirstMouth);
		_shoreStep = kStepClimbBack;
		_vm->playCharacterAnim(kBackSlot, 1, kClimbFrames, kAnimRate, kAnimMode);
		CursorMan.showMouse(false);
		_vm->_ben.placeSprite(kBackX, kBackY, kBackFacing);

		debugC(1, kDebugRooms, "shore: back along the rope to the left ledge");
	}

	_vm->rebuildHotspots();
}

/// The room's [0xa49f] machines: the pick-axe, and the two climbs.
void ShoreRoom::tickPair() {
	if (_vm->_room != kShoreRoom)
		return;

	switch (_shoreStep) {
	case 0:
		shoreArrival();
		break;

	case kStepSuitStart: {
		// 0x0b54: the line is the machine's, a pass after the click, so it is
		// what is left standing over whatever the click itself answered.
		int anchorX, anchorY;
		_vm->characterAnchor(anchorX, anchorY);
		_vm->queueOutcome(_vm->_tal, _vm->_script.flag(kSuitTried) == 1 ? kOutcomeSuitAgain : kOutcomeSuitFirst,
					 anchorX, anchorY);
		_vm->_script.setFlag(kSuitTried, 1);
		CursorMan.showMouse(false);
		_shoreStep = kStepSuitLine;
		break;
	}

	case kStepSuitLine:
		// 0x0b8d: the line, then the dive.
		if (!_vm->speechDone())
			break;
		_vm->playCharacterAnim(kDiveSlot, 1, kDiveFrames, kAnimRate, kAnimMode);
		_shoreStep = kStepSuitDive;
		break;

	case kStepSuitDive:
		// 0x0bb2: and under, into the cave.
		if (_vm->_anims.isBusy(kDiveSlot))
			break;
		_shoreStep = 0;
		_vm->_script.setFlag(kDivingView, 1);
		_vm->takeExit(kDiveSubmode);
		break;

	case kStepAxeLine:
		// 0x0bd4: the line comes down before the swing.
		if (!_vm->speechDone())
			break;
		_vm->_drawCharacter = false;
		_vm->playCharacterAnim(kAxeSlot, 1, kAxeFrames, kAnimRate, kAnimMode);
		_shoreStep = kStepAxeSwing;
		break;

	case kStepAxeSwing: {
		// 0x0bf9: the axe goes over with the rope, and the way across opens.
		if (_vm->_anims.isBusy(kAxeSlot))
			break;
		_vm->_inventory.remove(kAxe);
		_vm->_script.setFlag(kRockOpen, 1);
		int anchorX, anchorY;
		_vm->characterAnchor(anchorX, anchorY);
		_vm->queueOutcome(_vm->_tal, kOutcomeOpen, anchorX, anchorY);
		_vm->_drawCharacter = true;
		_shoreStep = kStepAxeDone;
		debugC(1, kDebugItems, "shore: item %u (%s) goes over with the rope",
			   kAxe, _vm->_inventory.name(kAxe).c_str());
		break;
	}

	case kStepAxeDone:
		_shoreStep = kStepAxeLast;
		break;

	case kStepAxeLast:
		// 0x0c30: and the cursor comes back once the second line is down.
		if (!_vm->speechDone())
			break;
		_shoreStep = 0;
		CursorMan.showMouse(true);
		_vm->rebuildHotspots();
		break;

	case kStepClimbUp:
	case kStepClimbFirst:
	case kStepClimbBack:
		// 0x0c43, 0x0c5f and 0x0c7b are one shape: wait for the climb's own
		// animation, then give him and the cursor back -- and take the
		// animation's last frame, a drawn-on Ben, off the ledge with it.
		if (_vm->_anims.isBusy(kStepClimbUp == _shoreStep ? kClimbSlot
						  : (_shoreStep == kStepClimbFirst ? kFirstClimbSlot : kBackSlot)))
			break;
		_shoreStep = 0;
		_vm->showCharacter();
		CursorMan.showMouse(true);
		break;

	default:
		break;
	}
}

/// The pick-axe on the rocks, which is what opens the way into the second
/// maze. Entry 3 answers it and raises [0xa602] (0x0077), so the table's own
/// row for it, the same line, never runs.
Room::Body ShoreRoom::body(int obj, byte verb, byte item, int anchorX, int anchorY) {
	return armShoreAxe(obj, item) ? kBodyReplaced : kBodyNone;
}

void ShoreRoom::afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) {
	// The diving suit on the water, the way back down to room 46.
	armShoreSuit(obj, item);
}

} // End of namespace Alien
