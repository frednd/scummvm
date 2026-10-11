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
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// The canned answers in TALKALL.TAL that the click and item-use paths fall back
// on when a room has nothing of its own to say.
static const byte kOutcomeNothingSpecial = 4;	///< "I can't see anything special about it."
static const byte kOutcomeNoCombination = 3;	///< "Using these things together doesn't seem to work."
static const byte kOutcomeOnFire = 5;	///< "I don't think I should set that on fire."
static const byte kOutcomeRepeatRefusal = 0x10;	///< the first of three, for the same use again
static const byte kUseRepeatMax = 3;
static const byte kMatches = 1;
// [0xa6d6], which gives room 15 its second item-use table.
static const uint16 kUseTableB = 0xa6d6;

// The status-line verb whose zero outcome falls back to a canned line. Any
// other verb with a zero code is the room's own business.
static const byte kVerbLookAt = 5;

void AlienEngine::updateHover(int x, int y) {
	// Hotspot boxes are room-space (an object past x=320 in a wide room keeps
	// its authored coordinates); the incoming x is screen-space, so the scroll
	// offset goes back in before testing them. The bar below the playfield is
	// never panned, so it keeps the raw screen x.
	// Nothing hovers while the cursor is gone. [0xa948] is the original's one
	// switch for that: the registration pass 1021:0x71b runs only while it is
	// set, so a machine that has taken the cursor away -- the opening, the
	// safe, the lab's and the sewer's, a clip -- neither lights a rectangle nor
	// writes the status line until it hands the cursor back. The port carries
	// the flag as the cursor's own visibility, which every one of those
	// machines already clears.
	if (_openingStep || !CursorMan.isVisible())
		return;

	_cursorX = x;
	_cursorY = y;

	// While a conversation is up the bar is not there: its rows hold the menu,
	// and the status line's entries 66 and 67 are the first option's colours
	// (chat.cpp). Hovering the rows where the item slots would be used to
	// write an item's name into the line, which relit those two entries and
	// then faded them over the option.
	if (_chat.isActive())
		return;

	const int roomX = x + _scrollX;

	// The original registers a room's rectangles one after another and each
	// registration overwrites the globals on a hit, so where two overlap the
	// one registered last is the one that answers -- hence the whole table is
	// scanned rather than stopping at the first match.
	int hit = -1;
	for (uint i = 0; i < _spots.size(); i++) {
		if (_spots[i].contains(roomX, y))
			hit = (int)i;
	}

	const int slot = _inventory.slotAt(x, y);
	const Inventory::Arrow arrow = _inventory.arrowHover(x, y);
	const bool menu = _inventory.menuButtonAt(x, y) && menuAllowed();

	if (hit != _hover || slot != _hoverSlot || arrow != _hoverArrow || menu != _hoverMenu) {
		_hover = hit;
		_hoverSlot = slot;
		_hoverArrow = arrow;
		_hoverMenu = menu;
		_labelSlot = hit >= 0 ? _spots[hit].label : 0;
		_dirty = true;
	}

	// The status line is rebuilt whether or not the hover changed: over bare
	// floor it reads the walk verb, and the point can cross onto a wall without
	// any rectangle being involved.
	refreshLabel(x, y);
}

/**
 * The markers ScummVM shows while the hotspot key is held: one per rectangle
 * the cursor could find right now, named the way the status line would name
 * it. Nothing is reported while the hover itself would answer nothing -- the
 * cursor taken away by a machine ([0xa948], see updateHover) or a
 * conversation holding the bar.
 */
void AlienEngine::getHotspotPositions(Common::Array<Graphics::HotspotInfo> &hotspots) {
	_hotspotStamp = hotspotStamp();
	if (_openingStep || !CursorMan.isVisible() || _chat.isActive())
		return;

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];

		// Where two rectangles overlap the one registered last answers the
		// cursor, so a marker on one that is wholly covered would point at
		// something no click can reach.
		bool covered = false;
		for (uint j = i + 1; j < _spots.size() && !covered; j++)
			covered = _spots[j].contains(spot.x1, spot.y1) && _spots[j].contains(spot.x2, spot.y2);
		if (covered)
			continue;

		// A room draws one object as several rectangles (room 10's table is
		// four); it gets one marker, on the largest of them.
		const int area = (spot.x2 - spot.x1 + 1) * (spot.y2 - spot.y1 + 1);
		bool smaller = false;
		for (uint j = 0; j < _spots.size() && !smaller; j++) {
			const Hotspot &other = _spots[j];
			if (j == i || other.obj != spot.obj || other.label != spot.label)
				continue;
			const int otherArea = (other.x2 - other.x1 + 1) * (other.y2 - other.y1 + 1);
			smaller = otherArea > area || (otherArea == area && j < i);
		}
		if (smaller)
			continue;

		const int roomX = (spot.x1 + spot.x2) / 2;
		const int y = (spot.y1 + spot.y2) / 2;
		const int x = roomX - _scrollX;
		if (x < 0 || x >= kScreenWidth || y < 0 || y > kPlayfieldBottom)
			continue;

		Common::U32String name;
		const TalFile::Entry &entry = _labels.entry(spot.label);
		if (!entry.lines.empty())
			name = Common::U32String(entry.lines[0], Common::kDos850);

		WalkTarget target;
		const bool exit = _script.walkTarget(roomX, y, spot.obj, target) && target.submode;
		hotspots.push_back(Graphics::HotspotInfo(Common::Point(x, y), name,
												 exit ? Graphics::kHotspotExit : Graphics::kHotspotObject));
	}
}

/// What the marker list is built from: the room, the scroll, the cursor's
/// visibility and the rectangles themselves.
uint32 AlienEngine::hotspotStamp() const {
	uint32 stamp = (uint32)_room * 31 + (uint32)_scrollX;
	stamp = stamp * 31 + (CursorMan.isVisible() ? 1 : 0) + (_chat.isActive() ? 2 : 0);
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		stamp = stamp * 31 + (uint32)(spot.x1 ^ (spot.y1 << 9) ^ (spot.x2 << 16) ^ (spot.y2 << 25));
		stamp = stamp * 31 + spot.label;
	}
	return stamp;
}

bool AlienEngine::hotspotDirty() const {
	return hotspotStamp() != _hotspotStamp;
}

bool AlienEngine::menuAllowed() const {
	// OBJ:sub_0883d skips the whole test when handler_code is 0x1e, so the
	// button is dead in room 30 and nowhere else.
	return _room > 0 && _room != 30;
}

bool AlienEngine::clickBar(int x, int y, bool rightButton) {
	// Everything below the playfield is the bar and the status line: the two
	// scroll arrows, the six item slots, and dead space. None of it walks.
	if (y <= kPlayfieldBottom)
		return false;

	// The save button answers to either button, and the click only raises the
	// request: OBJ:sub_0883d sets [0xa821] and the room's tick is what acts on
	// it a frame later, with the tick already finished.
	if (_inventory.menuButtonAt(x, y) && menuAllowed()) {
		_menuRequest = true;
		return true;
	}

	const Inventory::Arrow arrow = _inventory.arrowAt(x, y);
	if (arrow != Inventory::kArrowNone) {
		if (!rightButton && _inventory.arrowEnabled(arrow)) {
			const bool moved = arrow == Inventory::kArrowUp ? _inventory.pageUp()
														   : _inventory.pageDown();
			if (moved) {
				debugC(1, kDebugItems, "bar: page %u of %u", _inventory.page(),
					   _inventory.pageCount());
				updateHover(x, y);
				_dirty = true;
			}
		}
		return true;
	}

	// A right click in the bar looks at the slot it is over and puts down
	// whatever is in hand -- the original does the look first, then clears the
	// mode (1021:0x754 and 1021:0x8f6) -- and a left click takes the item into
	// the hand, which is what puts following clicks under "Use to".
	const int slot = _inventory.slotAt(x, y);

	if (rightButton) {
		if (!_heldItem && slot >= 0)
			lookAtItem(_inventory.slotItem((uint)slot));
		if (_heldItem)
			holdItem(Inventory::kNoItem);
		return true;
	}

	// A left click on a filled slot with something already in hand is not a
	// swap: 1021:0x949 hands it to 10c9:sub_10d65, the resident combine, and the
	// hand is only reloaded when it was empty.
	const byte clicked = slot >= 0 ? _inventory.slotItem((uint)slot) : 0;
	if (_heldItem && clicked) {
		combineItems(_heldItem, clicked);
		return true;
	}

	if (slot >= 0)
		holdItem(clicked);
	return true;
}

// The two items a combine is written against, and what it leaves behind.
// 10c9:sub_10d65 spells each pair out in both orders -- it compares the held
// item [0xa64e] against the first and the clicked one [0xa64f] against the
// second, so a pair written once would only answer one way round -- and so does
// this table, row for row.
struct CombineRule {
	byte held;			///< [0xa64e], the item in hand
	byte clicked;		///< [0xa64f], the slot clicked on
	byte drop[2];		///< inv_remove, in the original's order (0 = none)
	byte add;			///< inv_add (0 = none)
	byte replaceFrom;	///< inv_replace, the item looked for (0 = no replace)
	byte replaceTo;		///< inv_replace, the item left on the same square
	uint16 flag;		///< the flag the combine sets to 1 (0 = none)
	byte outcome;		///< the TALKALL record it speaks (0 = silent)
};

// 10c9:0x14e onwards. The first two pairs are written out inline, the last two
// go through 10c9:sub_10d21, the generic "replace one of them and speak"
// helper, which is why they carry a replace rather than a remove/add.
static const CombineRule kCombines[] = {
	// Battery into the remote control: the one combine that sets a flag, and
	// [0xa6f1] is what rooms 15 and 11 read as the living-room door (10c9:0x190).
	{ 26,  7, { 26,  7 },  7, 0, 0, 0xa6f1, 0x36 },
	{  7, 26, { 26,  7 },  7, 0, 0, 0xa6f1, 0x36 },
	// Gameson and the super radio become the teleport engine (10c9:0x1dd).
	{ 30, 36, { 30, 36 }, 35, 0, 0, 0,      0    },
	{ 36, 30, { 30, 36 }, 35, 0, 0, 0,      0    },
	// The pumpkin carved with the piece of glass or with the scissors
	// (10c9:0x201, four sub_10d21 calls).
	{ 23, 21, {  0,  0 },  0, 21, 22, 0,    0x34 },
	{ 21, 23, {  0,  0 },  0, 21, 22, 0,    0x34 },
	{ 13, 21, {  0,  0 },  0, 21, 22, 0,    0x34 },
	{ 21, 13, {  0,  0 },  0, 21, 22, 0,    0x34 },
};

/**
 * An item on an object that no body of the room's answered.
 *
 * The room's own item-use table comes first (10c9:sub_11aae, a record per item
 * and object, item 0 for any item, the line picked at random among its codes).
 * What it does not answer, 10c9:sub_119e2 refuses out of the shared file: the
 * matches get "I don't think I should set that on fire." (dialog branch 5),
 * anything else the next of eighteen rotating refusals -- or, when it is the
 * same item on the same object again, one of three firmer ones by how many
 * times in a row ([0xa653], 1021:0x07b9).
 */
void AlienEngine::refuseItemUse(byte obj, byte item, int anchorX, int anchorY) {
	const byte answer = _tables.useAnswer(_room, item, obj,
										  (byte)_script.flag(kUseTableB), _rnd);
	if (answer) {
		queueOutcome(_tal, answer, anchorX, anchorY);
		return;
	}

	if (item == kMatches) {
		queueOutcome(_talkall, kOutcomeOnFire, anchorX, anchorY);
		return;
	}

	byte code;
	if (_useRepeat)
		code = (byte)(kOutcomeRepeatRefusal + _useRepeat - 1);
	else
		code = _tables.refusal(_refusalNext++);
	if (_refusalNext >= StaticTables::kRefusalCount)
		_refusalNext = 0;
	queueOutcome(_talkall, code, anchorX, anchorY);
}

bool AlienEngine::combineItems(byte held, byte clicked) {
	// The tail of sub_10d65: a pair with no body of its own is refused, but only
	// when the two names really differ. Clicking the slot the hand came from
	// falls out of the routine at 0x28c with the item still held.
	if (held == clicked)
		return false;

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);

	for (uint i = 0; i < ARRAYSIZE(kCombines); ++i) {
		const CombineRule &rule = kCombines[i];
		if (rule.held != held || rule.clicked != clicked)
			continue;

		for (uint drop = 0; drop < ARRAYSIZE(rule.drop); ++drop) {
			if (rule.drop[drop])
				_inventory.remove(rule.drop[drop]);
		}
		if (rule.add)
			_inventory.add(rule.add);
		if (rule.replaceFrom)
			_inventory.replace(rule.replaceFrom, rule.replaceTo);
		if (rule.flag)
			_script.setFlag(rule.flag, 1);

		debugC(1, kDebugItems, "bar: combined item %u (%s) with item %u (%s)",
			   held, _inventory.name(held).c_str(), clicked,
			   _inventory.name(clicked).c_str());

		// The line comes out of the shared file, not the room's: sub_0b5cc is
		// queue_event with the other EMS handle mapped first (OBJ:sub_02638,
		// [0x993c], where queue_event's OBJ:sub_0262a maps [0x993a]) -- the
		// TALKALL image. TALKALL's 0x36 is "There. The battery is in."; the
		// room files' 0x36 are silent, which is why the combine said nothing.
		if (rule.outcome)
			queueOutcome(_talkall, rule.outcome, anchorX, anchorY);

		// [0xa6bb] = 0: the hand is spent either way.
		holdItem(Inventory::kNoItem);
		return true;
	}

	// 10c9:0x257 raises dialog branch 1, which DIALOG:sub_0b776 turns into
	// record 3 of the shared file -- the same refusal an item used on the wrong
	// object gets.
	queueOutcome(_talkall, kOutcomeNoCombination, anchorX, anchorY);
	holdItem(Inventory::kNoItem);
	return false;
}

void AlienEngine::holdItem(byte item) {
	_heldItem = item;
	_dirty = true;

	// Picking an item up rewrites the line as "USE <item> WITH ...", which is a
	// change the cursor never moved for.
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	refreshLabel(mouse.x, mouse.y);
	debugC(1, kDebugItems, "bar: holding item %u (%s)", item,
		   _inventory.name(item).c_str());
}

void AlienEngine::lookAtItem(byte item) {
	if (!item)
		return;

	// The click reports itself the way a click on a room object does: 10c9's
	// look routine sets [0xa825] to 5 at 0x4ae before it speaks, and the mode
	// byte is what 1021:0x85e turns into a status line, indexing the verb table
	// at DS:0x3221 by it -- entry 5 is "Look at". The name it appends is
	// [0xabb8], which the hover pass has already filled with the item's own.
	setClickLabel(_tables.verb(kVerbLookAt), kLabelHoldVerb);

	// The answers to a look at a carried item are in the shared file, not the
	// room's own, and Ben is the one saying them.
	const byte code = _inventory.lookOutcome(_tables, item);
	if (!code)
		return;

	int anchorX, anchorY;
	characterAnchor(anchorX, anchorY);
	queueOutcome(_talkall, code, anchorX, anchorY);

	// 10c9:0x4f7 - item right-click script.
	if (item == 2 && _script.flag(0xa7e5) == 1) {
		_inventory.add(6);
		_script.setFlag(0xa7e5, 0);
	} else if (item == 10 && _script.flag(0xa7e6) == 1) {
		_inventory.add(11);
		_script.setFlag(0xa7e6, 0);
	} else if (item == 7 && _script.flag(0xa6f1) == 1) {
		_inventory.add(26);
		queueOutcome(_talkall, 0x37, anchorX, anchorY);
		_script.setFlag(0xa6f1, 0);
	} else if (item == 35) { // 0x23: Teleport engine
		_inventory.remove(35);
		_inventory.add(36); // super radio
		_inventory.add(30); // gameson
	} else if (item == 22) { // 0x16: the pumpkin mask, and the only way to put it on
		// 10c9:0x5fe..0x68c. The longest arm of the script by far, and the one
		// the port had stubbed out as a no-op: three refusals and, in the one
		// room that allows it, the wear itself.
		//
		// [0xa79b] is the mask-is-on flag -- the byte charPaletteFile() reads to
		// give rooms 51..59 PUMP_PAL (or room 52 SEC_BPAL) over their own --
		// and nothing in the tree wrote it before this, so the alien ship
		// could only ever be walked with Ben's own palette.
		_script.setFlag(0xa602, 0);

		if (_script.flag(0xa79b) == 1) {
			// Already wearing it.
			queueOutcome(_talkall, 0x4d, anchorX, anchorY);
		} else if (_room == 58 && _script.flag(0xa7b1) == 1) {
			// In the jail, once [0xa7b1] stands (the escape gate, job 13).
			queueOutcome(_talkall, 0x4e, anchorX, anchorY);
		} else if (_room != 56 && _room != 58) {
			// Anywhere but the transporter chamber and the jail: no reason to.
			queueOutcome(_talkall, 0x4f, anchorX, anchorY);
		}

		// Room 56 only, and only once -- [0xa602] is the room's own
		// answered-this-click byte, cleared at the top of the arm, so the
		// guard here can only fail if something else in the same click set it.
		if (_room == 56 && _script.flag(0xa79b) == 0 && _script.flag(0xa602) == 0) {
			_script.setFlag(0xa602, 1);
			_script.setFlag(0xa79b, 1);

			// [0xa881] = 1 and game_submode = 10, then the walk target and the
			// owed facing copied over the live ones so the reload puts him back
			// where he stood. transitions.cpp's { 56, 10, 56 } is a self-link,
			// and takeExit() keeps the position for exactly that case, so the
			// copy is what keepPosition already does.
			takeExit(10);
		}
	} else if (item == 18) { // 0x12: the note, read once
		// 10c9:0x68d. Reading it is all there is to it, but the mailbox in
		// room 25 counts the trips it has made since ([0xa774], up to 2) and
		// answers a look with label 0x12 + that count instead of 0x11
		// (ovr_19_0e8f:0x1a7 and 0x327).
		_script.setFlag(0xa7e7, 1);
	}
}

byte AlienEngine::rotateOutcome(const Hotspot &spot) {
	// LOGIC:sub_120d4: a per-object counter walks the hotspot's outcome codes
	// and wraps at its arity, which is what makes clicking the same thing twice
	// give a different line. Only a hotspot with two to four codes (arity
	// marker 0x20..0x22 in [0xa645]) touches the counter; a single-code one
	// leaves it at zero, which guards read.
	const byte arity = spot.outcomeCount;
	if (arity < 2)
		return spot.outcomes[0];
	byte &counter = _outcomeCounter[spot.obj];
	if (counter >= arity)
		counter = 0;

	const byte code = spot.outcomes[counter];
	counter++;
	return code;
}

void AlienEngine::clickAt(int x, int y, bool rightButton) {
	// While the conversation menu is listed it owns the bottom of the screen:
	// a click there picks an option and never reaches the room (chat.cpp),
	// and it is tested before the speech-skip gate below. The port's
	// speech-skip convenience does not exist in the original (see the
	// comment on that gate), so it must never eat the click a player makes
	// to answer a line the room or another party is still speaking under an
	// open menu -- the greeting on first opening a topic, and the option's
	// own line plus any reply the room adds after each pick, are exactly
	// when this matters. isListed() covers kLive and kOpening (a click while
	// the options arrive is swallowed, ChatMenu::click); a menu that
	// is kPicked/kSettled/kFading has no option to land on and falls through
	// to the ordinary gates below.
	if (_chat.isListed()) {
		if (!rightButton)
			_chat.click(y);
		return;
	}

	// A click while someone is talking cuts the line short, the same as the
	// countdown running out -- and this is tested before the input gate below,
	// so it works under a machine that has taken the cursor away as well. The
	// opening monologue is the case that needs it: it holds the screen for the
	// whole of outcome 0x50 and there was no way to hurry it along.
	// The original has no click-to-skip for dialogue: a line is dismissed
	// only when the countdown timer reaches 0. Allowing click-to-skip is an
	// engine convenience that advances the dialogue queue.
	if (_speech) {
		nextSpeech();
		return;
	}

	// The opening monologue owns the screen: the original takes the cursor away
	// for it ([0xa948] = 0, ovr_03_0e57:0xb04) and gives it back on the step
	// that ends the machine, so nothing the player does lands until then.
	//
	// And every other machine that takes the cursor is the same switch: while
	// [0xa948] is clear the click dispatch does not run at all, so a click
	// landing mid-animation is not held for later -- it never happened. Without
	// this the safe could be emptied while it was still being cracked, and the
	// hand holding the stethoscope would take the click instead.
	if (_openingStep || !CursorMan.isVisible())
		return;

	// The menu may still be active but past its listed phases (kPicked and
	// onward): the pick has been taken, and the room path below must not run
	// while the tree is still settling, the same as before.
	if (_chat.isActive())
		return;

	// 1021:0x6c1, the first thing the dispatch does: the line the last click
	// left standing goes, and the hover is free to answer again.
	_labelHold = 0;
	_walkReported = false;

	updateHover(x, y);

	// LOGIC's dispatch zeroes the idle count on the way past, so any click at
	// all settles the character back to standing rather than only one that
	// sends him somewhere.
	_ben.resetIdle();

	// The bar answers for itself, and a click there is never a walk.
	if (clickBar(x, y, rightButton))
		return;

	// The two buttons do different jobs. The **left** button only ever walks: the room's own loop calls the
	// overlay's entry 0 with the click point when [0x8d0e] is set
	// (ovr_03_0e57:0xd7a is the plain shape of it) and the route starts from
	// walk_pos on the next tick, with no object, no verb and no outcome
	// involved. The **right** button is the one that acts: 1021:0x71b runs the
	// registration pass and then LOGIC:sub_12198, which is what walks *and*
	// arms the action. A right click over nothing has no object to act on and
	// does nothing at all.
	//
	// 1021:0x6f7 is the exception: a right click while an item is in hand is
	// rewritten into a left click at the same point, so with a full hand both
	// buttons perform the item use.
	const bool act = rightButton || _heldItem;
	if (rightButton && !_heldItem && _hover < 0)
		return;

	// A click on nothing with an item in hand puts the item down, and that is
	// all it does. Entry 0 still aims the walk, but 1021:0x902 clears the
	// hand ([0xa6bb], [0xa825]) and sets [0xa601], and OBJ:0x558c starts a
	// walk only while both [0xa825] and [0xa601] are clear -- so he stays
	// where he is. The right button is a left one by then (1021:0x6f7).
	if (_heldItem && _hover < 0) {
		if (!rightButton) {
			_pending = -1;
			_pendingItem = Inventory::kNoItem;
		}
		_armed = 0;
		holdItem(Inventory::kNoItem);
		return;
	}

	// ... and the click counts as a left one -- which is what arms an exit --
	// unless it is the plain right-button action (1021:sub_10210 tests both
	// buttons before it writes walk_submode).
	const bool asLeft = !rightButton || _heldItem;

	// A left click also cancels an action already waiting on an arrival
	// (1021:0x6dc clears [0xa956]), so walking away abandons it.
	if (!act) {
		_pending = -1;
		_pendingItem = Inventory::kNoItem;
	}

	// The room's own walk geometry decides where the click sends him: an object
	// has an approach point and a facing, a floor rectangle snaps the point onto
	// the room's floor line. Without it he walks onto whatever he was sent to
	// use. See walkgeom.h.
	// The room's own walk geometry and a plain floor click both work in
	// room-space, same as the hotspot boxes updateHover just tested.
	const int roomX = x + _scrollX;

	const byte obj = _hover >= 0 ? _spots[_hover].obj : 0;

	// HOTSPOT:sub_13605 (1336:02ba): a left click latches the object under the
	// cursor, a right one clears it, and rooms whose tick answers an arrival
	// read it back to find out what the walk was for -- the shore's two cave
	// mouths (shore.cpp), the cemetery's statue, the sewer's ladder.
	// An item use leaves it alone: on the original the pick-axe thrown over
	// the shore's fence kept the latch of the click before it, so the walk
	// along the rope waits for a plain click on the fence.
	if (!_heldItem)
		_script.setFlag(0xa644, asLeft ? obj : 0);
	// Room 50 writes its own value over it for the side the steam blocks
	// (steam.cpp).
	if (asLeft)
		for (Room *r : _rooms)
			r->leftClick(roomX, y);

	WalkTarget target;
	// Every click rearms from scratch, as the original rewrites walk_submode
	// [0xa87d] each time entry 0 runs -- and 1021:sub_105fa, the first thing
	// entry 0 calls, lets him turn round again ([0xa958]).
	_armed = 0;
	_ben.setTurnBlocked(false);
	if (_script.walkTarget(roomX, y, obj, target)) {
		if (target.submode && asLeft) {
			_armed = target.submode;
			_armedX = target.x;
			_armedY = target.y;
			_armedFacing = target.facing;
			debugC(1, kDebugRooms, "exit: object %u arms submode %u at %d,%d facing %u",
				   obj, target.submode, target.x, target.y, target.facing);
		}
		// And room 40's hole, approached from whichever side he is on.
		caveWalkTarget(obj, target);
		// And the rooms' own (rooms/): room 31, which answers a click off the
		// ledge he is standing on with a climb (cliff.cpp), room 8's open safe, where the port
		// stands him aside to take what is on its shelf (library.cpp), and
		// room 56's chamber, which has two approach points, chosen by the
		// button rather than by a flag (teleport.cpp).
		for (Room *r : _rooms)
			r->walkTarget(obj, roomX, y, rightButton || _heldItem, target);

		// OBJ:sub_07a0a: feet within a pixel of the approach point and already
		// facing its way, a plain left click starts no walk at all -- the
		// armed exit then fires where he stands. The port walks to the spot he
		// is on, which plots nothing; walking the last pixel was a leg the
		// original never takes. A right click
		// or one with an item in hand walks it on the original all the same
		// (rooms 3, 6, 18, 22 and 54), so the test is the plain click's.
		if (!rightButton && !_heldItem &&
			ABS(target.x - _ben.walkX()) <= 1 && ABS(target.y - _ben.walkY()) <= 1 &&
			_ben.facing() == (int)target.facing) {
			target.x = _ben.walkX();
			target.y = _ben.walkY();
		}

		walkTo(target.x, target.y, target.facing);
	} else {
		walkTo(roomX, y);
	}

	// And a click in a room with walk nodes plots him a route -- a
	// zero-length one if he is already there -- and that keeps him facing the
	// way he stands (LOGIC's four dispatch sites, [0xa958] = 1, behind
	// `cmp [0x9e5e], 0`): talking to someone leaves him turned to them, not
	// to the player.
	if (_walk.nodes().count())
		_ben.setTurnBlocked(true);

	if (!act) {
		// 1021:0x998: the line now reports the walk, not the object. The
		// underwater room swims instead, and either way the line stands until
		// he gets there ([0xac1e] = 0xbb8, cleared on arrival by OBJ:sub_07a0a).
		setClickLabel(_room == kSwimRoom ? _tables.swimVerb() : _tables.walkVerb(),
					  kLabelHoldWalk);
		_walkReported = true;
		return;
	}

	_pending = _hover;
	if (_pending < 0) {
		// Not reached: both ways to act on nothing returned above.
		return;
	}

	const Hotspot &spot = _spots[_pending];

	// With an item in hand the click is an item use: no outcome rotation, because
	// the answer comes from the room's own combination body or from the shared
	// refusal. LOGIC:sub_12336 sets the pair and walks; the body runs on arrival.
	_pendingItem = _heldItem;
	_pendingOutcome = _pendingItem ? 0 : rotateOutcome(spot);

	if (_pendingItem) {
		// LOGIC:sub_12336 builds no line of its own: the "USE <item> WITH
		// <object>" the hover pass already put up is what stands, held for the
		// walk the same way a plain walk's line is.
		_labelHold = kLabelHoldWalk;
		_walkReported = true;
	} else {
		// 1021:0x85e, once sub_12198 has copied the verb into [0xa825]: the
		// verb word appears now, on the click, and times out on its own.
		setClickLabel(_tables.verb(spot.verb), kLabelHoldVerb);
	}

	if (_pendingItem)
		debugC(1, kDebugItems, "click: item %u (%s) on object %u", _pendingItem,
			   _inventory.name(_pendingItem).c_str(), spot.obj);
	else
		debugC(1, kDebugGraphics, "click: object %u, verb %u (%s), outcome %u",
			   spot.obj, spot.verb, _tables.verb(spot.verb).c_str(), _pendingOutcome);

	// Rooms with no walk mask never start a route, so the action is due at once.
	if (!_ben.isWalking() && !_ben.isTurning())
		finishAction();
}

void AlienEngine::finishAction() {
	const int index = _pending;
	const byte item = _pendingItem;
	_pending = -1;
	_pendingItem = Inventory::kNoItem;
	if (index < 0 || index >= (int)_spots.size())
		return;

	// The speech is anchored on the horizontal midpoint of the clicked
	// object's box, at its top edge. Taken by value because the script below
	// may rebuild the table this points into.
	const Hotspot spot = _spots[index];
	const int anchorX = (spot.x1 + spot.x2) / 2;
	const int anchorY = spot.y1;

	// Every overlay calls the generic dispatch (10c9:sub_11c10) before running
	// its own bodies, so the outcome speaks first. A code of zero means the room
	// answers for itself -- except under "Look at", where the shared script
	// supplies the canned line.
	//
	// **An item use takes neither.** The generic dispatch is called only from
	// the plain-verb half of an overlay's click body -- the half behind
	// `cmp [0xa956], 0x4e25` -- while an item use goes down the 0x4e22 half,
	// which calls 10c9:sub_11aae instead (ovr_03_0e57:0x00b0 against 0x0223 is
	// the plain shape of it). The port already zeroed the outcome for an item
	// use, but the "Look at" fallback below it did not ask, so using an item on
	// something whose rectangle carries the "Look at" verb spoke the canned
	// "nothing special" line over the room's own animation.
	if (_pendingOutcome != 0 && _pendingOutcome != 0xff)
		queueOutcome(_tal, _pendingOutcome, anchorX, anchorY);
	// The "Look at" fallback is not said here, though: sub_11c10 only raises
	// dialog branch 3 ([0xad00], [0xad02]) and LOGIC:sub_11f79 says it on the
	// next tick, after the body (below).
	const bool lookFallback = !item && _pendingOutcome == 0 && spot.verb == kVerbLookAt;

	// Then the room's own reaction. A body that queues an event of its own
	// speaks over whatever the generic path put up, as it does in the original:
	// both write the one queue, and the later call is the one that stands. An
	// item use runs under verb 2 with the item as the third part of the key.
	const byte verb = item ? kVerbUseTo : spot.verb;

	// Every item use counts toward the repeat the refusals read, answered or
	// not (1021:0x07b9).
	if (item) {
		if (item == _useLastItem && spot.obj == _useLastObj) {
			if (_useRepeat < kUseRepeatMax)
				_useRepeat++;
		} else {
			_useRepeat = 0;
		}
		_useLastItem = item;
		_useLastObj = spot.obj;
	}

	// One room's arms have to be read *before* its body, because one of them is
	// guarded on a flag the body sets (lab.cpp).
	armLab(spot.obj, item != Inventory::kNoItem);

	// And the rooms' own (rooms/): room 8's safe, which is armed the same way
	// and for the same reason.
	for (Room *r : _rooms)
		r->preAction(spot.obj, verb, item);

	// And room 19's fuse-panel cover, for the same reason: the lifted pair for
	// it has the branch that shuts the cover and not the one that opens it, so
	// the table on its own could only ever shut a cover nothing opened
	// (observatory.cpp).
	if (!item && armObservatoryPanel(spot.obj, verb)) {
		rebuildHotspots();
		_hover = -1;
		const Common::Point dome = g_system->getEventManager()->getMousePos();
		updateHover(dome.x, dome.y);
		return;
	}

	// The look at the same cover *is* a lifted row -- it raises event 10 -- and
	// what the lift has none of is the arrival that answers the line. Armed
	// before the row so that the row is what speaks.
	if (!item)
		armObservatoryLook(spot.obj, verb);

	// And the rooms' own (rooms/): a click a room answers with a machine of
	// its own -- the owl, Sluggs, the hippie, the maintenance man, the phone,
	// Yodle -- must not reach the body. The lifted rows for those keep their
	// guards but drop the calls that are the real bodies.
	for (Room *r : _rooms) {
		if (r->takeAction(spot.obj, verb, item, anchorX, anchorY)) {
			rebuildHotspots();
			_hover = -1;
			const Common::Point mouse = g_system->getEventManager()->getMousePos();
			updateHover(mouse.x, mouse.y);
			return;
		}
	}

	// And the rooms' own (rooms/): room 58's LCD reads [0xa7ab] before the
	// ring's row raises it (jail.cpp).
	for (Room *r : _rooms)
		r->beforeScript(spot.obj, verb, item);

	// The rooms' own bodies (rooms/): four of room 8's pick their animation
	// slot from a scratch byte the lift could not follow, and two of them it
	// dropped altogether, so the safe's door and the two things on its shelf
	// are run by the room instead (library.cpp) -- and the table really does
	// not see them: the lift now recovers the two shelf bodies it used to
	// drop, so leaving the row to run as well handed the key and the disks
	// out twice. And room 11's remote control, whose branches are the room's
	// click dispatch and not script rows (living.cpp), and the pick-axe on the
	// rocks at the shore (shore.cpp).
	bool roomReplaced = false, roomHandled = false;
	for (Room *r : _rooms) {
		const Room::Body b = r->body(spot.obj, verb, item, anchorX, anchorY);
		roomReplaced = roomReplaced || b == Room::kBodyReplaced;
		roomHandled = roomHandled || b != Room::kBodyNone;
	}

	// Room 40's item uses are the room's own dispatch too, and the table's
	// three rows for them lost their guards (cave.cpp).
	const bool caveHandled = armCave(spot.obj, item, anchorX, anchorY);
	const bool ranScript = !roomReplaced && !caveHandled && _script.run(spot.obj, verb, item);
	const bool handled = roomHandled || caveHandled || ranScript;

	// One object in the game is answered by a hook of its room's own rather
	// than by a script body: room 7's light switch (bedroom.cpp).
	if (!item && isBedroomSwitch(spot.obj))
		bedroomSwitch(anchorX, anchorY);

	// And room 19's circuit breaker, whose hand on slot 5 draws Ben himself
	// and whose line waits for it (observatory.cpp).
	if (!item)
		armObservatoryBreaker(spot.obj, verb);

	// And three bodies carry state the lift does not: the sewer's ladder, its
	// valve and its hatch all start that room's [0xa49f] machine (sewer.cpp).
	if (!item)
		armSewer(spot.obj, verb);

	// And the rooms' own (rooms/): room 26's parrot, room 50's lever, room
	// 57's terminal, and room 58's prisoners, shackle and card.
	for (Room *r : _rooms)
		r->afterScript(spot.obj, verb, item, anchorX, anchorY);
	if (_script.queuedEvent() != RoomScript::kNoEvent)
		queueOutcome(_tal, _script.queuedEvent(), anchorX, anchorY);

	// Room 3's computer is answered by a hook of its own for the same reason
	// room 7's light switch is: the original's body for it is overlay code and
	// not a script row, and it speaks its own line, so it runs past the table's
	// queue rather than before it (lift.cpp).
	if (_script.queuedEvent() == RoomScript::kNoEvent)
		armLiftCall(spot.obj, anchorX, anchorY);

	// A combination no room owns gets the shared refusal, and either way the hand
	// is empty once the click is spent.
	if (item) {
		if (!handled && _script.queuedEvent() == RoomScript::kNoEvent)
			refuseItemUse(spot.obj, item, anchorX, anchorY);
		holdItem(Inventory::kNoItem);
	}

	// The original re-registers every rectangle on the next frame, so a body
	// that opened a door has already changed what is clickable by the time the
	// player can click again.
	rebuildHotspots();
	_hover = -1;
	const Common::Point mouse = g_system->getEventManager()->getMousePos();
	updateHover(mouse.x, mouse.y);

	debugC(1, kDebugGraphics, "action: object %u verb %u item %u -> outcome %u, script %s",
		   spot.obj, verb, item, _pendingOutcome, handled ? "handled it" : "passed");

	// A body may end the scene on its own account rather than by arming an
	// approach point -- set_game_submode in the table -- and that leaves the room
	// as soon as the click is over, with no arrival to wait for.
	// The tick that says the "Look at" fallback never comes for a click that
	// leaves the room through a submode: the room load (OBJ:sub_0879a) clears
	// [0xad00] first, which is why room 28's monitor opens without the line.
	if (lookFallback && _script.submodeRequest() == RoomScript::kNoSubmode)
		queueOutcome(_talkall, kOutcomeNothingSpecial, anchorX, anchorY);

	if (_script.submodeRequest() != RoomScript::kNoSubmode) {
		_armed = 0;

		// Room 28's telescope and its computer both end in the table as
		// nothing but this submode: the real bodies are calls the lift cannot
		// carry, and this is the arrival test they run under
		// (ovr_1c_0eb7:0x0128, telescope.cpp).
		// And room 26's note, the same way (forest.cpp).
		bool taken = false;
		for (Room *r : _rooms)
			if (!taken)
				taken = r->takeSubmode(spot.obj, verb, _script.submodeRequest());
		if (!taken)
			takeExit(_script.submodeRequest());
	}
}

void AlienEngine::queueOutcome(const TalFile &tal, byte code, int anchorX, int anchorY,
							   bool benSpeaks) {
	// An outcome code does not name one line: it names a chain of up to ten
	// dialog ids in zone 1 of the room's TAL, played one after another.
	const TalFile::Outcome &chain = tal.outcome(code);
	traceEvent("say %s %u", tal.name().c_str(), code);
	_speechTal = &tal;
	_speechBen = benSpeaks;

	// [0xacf6], which queue_event writes before it does anything else: the id,
	// not any of the dialog ids it stands for. Room 15 is the one room that
	// reads it back, once the chain has finished speaking (hallway.cpp).
	_lastEvent = code;

	_queueCount = MIN<uint>(chain.count, TalFile::kMaxOutcomeIds);
	_queueNext = 0;
	for (uint i = 0; i < _queueCount; i++)
		_queue[i] = chain.ids[i];

	_speechX = anchorX;
	_speechY = anchorY;

	debugC(1, kDebugGraphics, "outcome %u: %u dialog ids", code, _queueCount);
	nextSpeech();
}

} // End of namespace Alien
