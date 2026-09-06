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
#include "common/util.h"

#include "alien/detection.h"
#include "alien/inventory.h"
#include "alien/resources.h"
#include "alien/tables.h"

namespace Alien {

// The icon grid and the plate the chrome is cut from. The original blits the
// first into EMS page 8 as the game starts and keeps the second resident.
static const char *const kIconPage = "KAMAT.PCX";
static const char *const kChromePage = "OBJFILE.PCX";
static const char *const kPanelPage = "INVENTOR.PCX";
static const char *const kItemNames = "INVENTOR.TAL";

// The panel itself. OBJ:obj_func_1973 loads INVENTOR.PCX and then copies
// 0x3200 bytes of it -- 40 rows of 320 -- to offset 0xc800 of both the main and
// the second framebuffer, which is row 160. So the plate's own top 40 rows are
// the bar, and they land at the bottom of the screen; the rest of the file is
// spare pieces the game never copies wholesale. The room plate underneath is a
// full 320x200 picture with no bar area reserved, so the panel is what covers
// its bottom quarter.
static const int kPanelY = 160;
static const int kPanelHeight = 40;

// The bar: six slots of 35 by 22, all on one row -- OBJ:sub_03c77.
static const int kSlotWidth = 35;
static const int kSlotHeight = 22;
static const int kSlotY = 173;
static const int kSlotX[Inventory::kSlotCount] = { 50, 87, 124, 161, 198, 235 };

// The highlight block the original swaps the palette in, per slot. These are
// hardcoded framebuffer offsets in OBJ:0x4011, and they do not sit at a constant
// offset from the slot itself -- the drift is the original's, kept as it is.
static const int kHighlightX[Inventory::kSlotCount] = { 50, 88, 124, 160, 196, 232 };
static const int kHighlightY = 174;
static const int kHighlightWidth = 33;
static const int kHighlightHeight = 20;
static const byte kHighlightFrom = 0x19;
static const byte kHighlightTo = 0x27;

// The scroll arrows. The click bounds are OBJ:sub_0883d's, the hover bounds
// OBJ:sub_06ac6's, and the two differ by a pixel at the edges; each is used for
// what the original uses it for.
static const int kArrowX = 6;
static const int kArrowWidth = 27;
static const int kArrowHeight = 15;
static const int kArrowUpY = 165;
static const int kArrowDownY = 181;

static const int kClickLeft = 5, kClickRight = 32;
static const int kClickUpTop = 164, kClickUpBottom = 180;
static const int kClickDownTop = 181, kClickDownBottom = 196;

static const int kHoverUpTop = 165, kHoverUpBottom = 179;
static const int kHoverDownTop = 182, kHoverDownBottom = 195;

// Where each arrow state is cut from the chrome plate: the blank plate that
// erases a disabled arrow, the idle arrow, and the one under the cursor.
static const int kArrowSrcX = 265, kArrowHoverSrcX = 293;
static const int kArrowUpIdleY = 34, kArrowUpBlankY = 66;
static const int kArrowDownIdleY = 50, kArrowDownBlankY = 82;
static const int kArrowUpHoverY = 81, kArrowDownHoverY = 96;
static const int kArrowHoverWidth = 26;

// The save button at the right end of the bar: the one HUD control that is not
// the inventory. Its click bounds are OBJ:sub_0883d's and its hover bounds
// OBJ:0x482f's, and unlike the arrows the two are the same box. The two disk
// plates sit side by side on the chrome page, and OBJ:sub_07f2f / sub_07f57 pick
// between them.
static const int kMenuLeft = 286, kMenuRight = 309;
static const int kMenuTop = 169, kMenuBottom = 189;
static const int kMenuX = 284, kMenuY = 169;
static const int kMenuWidth = 30, kMenuHeight = 23;
static const int kMenuSrcX = 196, kMenuHoverSrcX = 228, kMenuSrcY = 82;

// The scroll thumb: one strip, cut to the height of a page's share of the track
// and placed down it. OBJ:update_sala_state spells out every combination of page
// count and page; the heights it uses are 32 / pages, rounded about the track,
// and the tops follow from them.
static const int kThumbSrcX = 312, kThumbSrcY = 47;
static const int kThumbX = 34, kThumbTop = 164;
static const int kThumbWidth = 8, kThumbTrack = 32;

/// Height and top of the thumb per page count (1..6) and page, as the original
/// blits them. Zero marks a combination that cannot occur.
static const struct { byte height, top; } kThumb[7][7] = {
	{ { 0, 0 } },
	{ { 0, 0 }, { 32, 164 } },
	{ { 0, 0 }, { 16, 164 }, { 16, 180 } },
	{ { 0, 0 }, { 11, 164 }, { 11, 175 }, { 10, 186 } },
	{ { 0, 0 }, {  8, 164 }, {  8, 172 }, {  8, 180 }, { 8, 188 } },
	{ { 0, 0 }, {  6, 164 }, {  6, 170 }, {  6, 176 }, { 7, 182 }, { 7, 189 } },
	{ { 0, 0 }, {  5, 164 }, {  5, 169 }, {  5, 174 }, { 5, 179 }, { 6, 184 }, { 6, 190 } }
};

Inventory::Inventory() : _page(1) {
	reset();
}

Inventory::~Inventory() {
	_icons.free();
	_chrome.free();
	_panel.free();
}

bool Inventory::load() {
	byte palette[256 * 3];
	if (!loadGamePCX(Common::Path(kIconPage), _icons, palette)) {
		warning("Alien::Inventory: could not load %s, the bar will be empty", kIconPage);
		return false;
	}
	if (!loadGamePCX(Common::Path(kChromePage), _chrome, palette)) {
		warning("Alien::Inventory: could not load %s, the bar has no arrows", kChromePage);
		return false;
	}

	if (!loadGamePCX(Common::Path(kPanelPage), _panel, palette))
		warning("Alien::Inventory: could not load %s, the bar has no panel", kPanelPage);

	// The names are a NAMEROOM file like a room's labels, one entry per item id.
	if (!_names.load(Common::Path(kItemNames)))
		warning("Alien::Inventory: could not load %s, items will have no names", kItemNames);

	debugC(1, kDebugResource, "inventory: %dx%d icon page, %u item names",
		   _icons.w, _icons.h, _names.usedEntries());
	return true;
}

void Inventory::reset() {
	memset(_list, kNoItem, sizeof(_list));
	// MAIN sets every click counter to one before the first room, so the first
	// look at an item speaks the first of its codes rather than the byte in
	// front of its record.
	for (uint i = 0; i < kListSize; i++)
		_counter[i] = 1;
	_page = 1;
}

void Inventory::add(byte item) {
	if (!item)
		return;

	for (uint i = 1; i < kListSize; i++) {
		if (_list[i])
			continue;
		_list[i] = item;
		showNewest();
		debugC(1, kDebugItems, "item: %u (%s) picked up, list slot %u, page %u",
			   item, name(item).c_str(), i, _page);
		return;
	}

	warning("Alien::Inventory: the list is full, item %u dropped", item);
}

void Inventory::remove(byte item) {
	if (!item)
		return;

	for (uint i = 1; i < kListSize; i++) {
		if (_list[i] != item)
			continue;

		// Removal closes the gap, which is what keeps the list dense enough for
		// the bar to page through it.
		memmove(&_list[i], &_list[i + 1], kListSize - i - 1);
		_list[kListSize - 1] = kNoItem;
		debugC(1, kDebugItems, "item: %u (%s) given up", item, name(item).c_str());

		showNewest();
		return;
	}

	debugC(1, kDebugItems, "item: %u given up but not carried", item);
}

void Inventory::replace(byte oldItem, byte newItem) {
	if (!oldItem)
		return;

	for (uint i = 1; i < kListSize; i++) {
		if (_list[i] != oldItem)
			continue;

		// In place: the original overwrites the slot rather than closing the gap
		// and appending, so the new item is drawn where the old one was.
		_list[i] = newItem;
		showNewest();
		debugC(1, kDebugItems, "item: %u (%s) became %u (%s), list slot %u",
			   oldItem, name(oldItem).c_str(), newItem, name(newItem).c_str(), i);
		return;
	}

	debugC(1, kDebugItems, "item: %u swapped for %u but not carried", oldItem, newItem);
}

bool Inventory::has(byte item) const {
	if (!item)
		return false;
	for (uint i = 1; i < kListSize; i++) {
		if (_list[i] == item)
			return true;
	}
	return false;
}

uint Inventory::pageCount() const {
	// The original probes the first entry of each page in turn and stops at the
	// first empty one, so a page exists as soon as anything is on it.
	uint pages = 1;
	for (uint p = 2; p <= kSlotCount; p++) {
		if (!_list[(p - 1) * kSlotCount + 1])
			break;
		pages = p;
	}
	return pages;
}

void Inventory::showNewest() {
	// The original's [0xa7ec]. All three routines that change the list -- the
	// add and the swap at OBJ:sprite_add and OBJ:sub_08f38, and the removal that
	// closes the gap behind it -- raise the flag and then rebuild the bar, and
	// the rebuild OBJ:sub_03c77 opens by reading it: [0xa813] is first clamped
	// down to the page count, then, if the flag is up, raised to it. Both ways
	// round that lands on the last page, which is the page a new item is on.
	_page = pageCount();
}

bool Inventory::pageUp() {
	if (_page <= 1)
		return false;
	_page--;
	return true;
}

bool Inventory::pageDown() {
	if (_page >= pageCount())
		return false;
	_page++;
	return true;
}

byte Inventory::itemOn(uint page, uint slot) const {
	if (slot >= kSlotCount || page < 1)
		return kNoItem;
	const uint index = (page - 1) * kSlotCount + 1 + slot;
	return index < kListSize ? _list[index] : kNoItem;
}

int Inventory::slotAt(int x, int y) const {
	// The bounds are exclusive in the original, so a click on the border line
	// belongs to neither slot; and a slot with nothing in it is not there at all.
	for (uint slot = 0; slot < kSlotCount; slot++) {
		if (x <= kSlotX[slot] - 1 || x >= kSlotX[slot] + kSlotWidth)
			continue;
		if (y <= kSlotY - 1 || y >= kSlotY + kSlotHeight)
			continue;
		return slotItem(slot) ? (int)slot : -1;
	}
	return -1;
}

Inventory::Arrow Inventory::arrowAt(int x, int y) const {
	if (x < kClickLeft || x > kClickRight)
		return kArrowNone;
	if (y >= kClickUpTop && y <= kClickUpBottom)
		return kArrowUp;
	if (y >= kClickDownTop && y <= kClickDownBottom)
		return kArrowDown;
	return kArrowNone;
}

Inventory::Arrow Inventory::arrowHover(int x, int y) const {
	// The hover test is a second set of bounds a pixel inside the click's, and
	// only the arrow that leads somewhere lights up.
	if (x < kClickLeft || x > kClickRight)
		return kArrowNone;
	if (y >= kHoverUpTop && y <= kHoverUpBottom && arrowEnabled(kArrowUp))
		return kArrowUp;
	if (y >= kHoverDownTop && y <= kHoverDownBottom && arrowEnabled(kArrowDown))
		return kArrowDown;
	return kArrowNone;
}

bool Inventory::menuButtonAt(int x, int y) const {
	return x >= kMenuLeft && x <= kMenuRight &&
		   y >= kMenuTop && y <= kMenuBottom;
}

bool Inventory::arrowEnabled(Arrow arrow) const {
	// [0xa81a] and [0xa81b]: with one page neither arrow is there, and at either
	// end of the list only the one that leads somewhere is.
	const uint pages = pageCount();
	if (pages <= 1)
		return false;
	if (arrow == kArrowUp)
		return _page > 1;
	if (arrow == kArrowDown)
		return _page < pages;
	return false;
}

byte Inventory::lookOutcome(const StaticTables &tables, byte item) {
	if (!item || item >= kListSize)
		return 0;

	const byte code = tables.itemOutcome(item, _counter[item]);
	const byte arity = tables.itemArity(item);

	_counter[item]++;
	if (arity && _counter[item] > arity)
		_counter[item] = tables.itemCycles(item) ? 1 : arity;

	debugC(1, kDebugItems, "item: %u looked at -> outcome %u, counter now %u of %u%s",
		   item, code, _counter[item], arity,
		   tables.itemCycles(item) ? " (cycles)" : " (clamps)");
	return code;
}

Common::String Inventory::name(byte item) const {
	const TalFile::Entry &entry = _names.entry(item);
	return entry.lines.empty() ? Common::String() : entry.lines[0];
}

void Inventory::blit(Graphics::Surface &dest, const Graphics::Surface &src,
					 int srcX, int srcY, int w, int h, int dstX, int dstY,
					 bool transparent) const {
	if (!src.getPixels())
		return;

	for (int row = 0; row < h; row++) {
		const int sy = srcY + row, dy = dstY + row;
		if (sy < 0 || sy >= src.h || dy < 0 || dy >= dest.h)
			continue;
		for (int col = 0; col < w; col++) {
			const int sx = srcX + col, dx = dstX + col;
			if (sx < 0 || sx >= src.w || dx < 0 || dx >= dest.w)
				continue;
			const byte pixel = *((const byte *)src.getBasePtr(sx, sy));
			if (transparent && !pixel)
				continue;
			*((byte *)dest.getBasePtr(dx, dy)) = pixel;
		}
	}
}

void Inventory::drawIcon(const StaticTables &tables, Graphics::Surface &dest,
						 byte item, uint slot, bool hover) const {
	blit(dest, _icons, tables.itemIconX(item), tables.itemIconY(item),
		 kSlotWidth, kSlotHeight, kSlotX[slot], kSlotY, true);

	if (!hover)
		return;

	// The highlight is a palette swap over the drawn slot rather than a second
	// bitmap: OBJ:0x4011 walks the block and rewrites one index.
	uint swapped = 0;
	for (int row = 0; row < kHighlightHeight; row++) {
		const int y = kHighlightY + row;
		if (y < 0 || y >= dest.h)
			continue;
		for (int col = 0; col < kHighlightWidth; col++) {
			const int x = kHighlightX[slot] + col;
			if (x < 0 || x >= dest.w)
				continue;
			byte *pixel = (byte *)dest.getBasePtr(x, y);
			if (*pixel == kHighlightFrom) {
				*pixel = kHighlightTo;
				swapped++;
			}
		}
	}
	debugC(3, kDebugItems, "bar: slot %u lit, %u pixels swapped", slot, swapped);
}

void Inventory::drawPanel(Graphics::Surface &dest) const {
	blit(dest, _panel, 0, 0, dest.w, kPanelHeight, 0, kPanelY);
}

void Inventory::drawThumb(Graphics::Surface &dest) const {
	const uint pages = MIN<uint>(pageCount(), kSlotCount);
	const uint page = MIN<uint>(_page, pages);

	// The track is erased from the plate first, so a shorter thumb does not
	// leave the taller one it replaced behind.
	blit(dest, _chrome, kThumbX, kThumbTop, kThumbWidth, kThumbTrack,
		 kThumbX, kThumbTop);

	const byte height = kThumb[pages][page].height;
	if (!height)
		return;
	blit(dest, _chrome, kThumbSrcX, kThumbSrcY, kThumbWidth, height,
		 kThumbX, kThumb[pages][page].top);
}

void Inventory::draw(const StaticTables &tables, Graphics::Surface &dest,
					 int hoverSlot, Arrow hoverArrow, bool hoverMenu,
					 byte heldItem) const {
	// The plate first: everything below is cut into the recesses it draws, and
	// it is also what erases the previous frame's icons.
	drawPanel(dest);

	for (uint slot = 0; slot < kSlotCount; slot++) {
		const byte item = slotItem(slot);
		if (!item)
			continue;

		// Lit because the cursor is in it, or because it is the item in hand.
		// The original compares the slot's place in the list against [0xa859]
		// rather than the item itself; the list never carries the same item
		// twice, so matching on the item comes to the same thing and survives
		// the hand being filled by something other than a click on the bar.
		drawIcon(tables, dest, item, slot,
				 (int)slot == hoverSlot || (heldItem && item == heldItem));
	}

	const bool up = arrowEnabled(kArrowUp), down = arrowEnabled(kArrowDown);

	if (!up)
		blit(dest, _chrome, kArrowSrcX, kArrowUpBlankY, kArrowWidth, kArrowHeight,
			 kArrowX, kArrowUpY);
	else if (hoverArrow == kArrowUp)
		blit(dest, _chrome, kArrowHoverSrcX, kArrowUpHoverY, kArrowHoverWidth,
			 kArrowHeight, kArrowX, kArrowUpY);
	else
		blit(dest, _chrome, kArrowSrcX, kArrowUpIdleY, kArrowWidth, kArrowHeight,
			 kArrowX, kArrowUpY);

	if (!down)
		blit(dest, _chrome, kArrowSrcX, kArrowDownBlankY, kArrowWidth, kArrowHeight,
			 kArrowX, kArrowDownY);
	else if (hoverArrow == kArrowDown)
		blit(dest, _chrome, kArrowHoverSrcX, kArrowDownHoverY, kArrowHoverWidth,
			 kArrowHeight, kArrowX, kArrowDownY);
	else
		blit(dest, _chrome, kArrowSrcX, kArrowDownIdleY, kArrowWidth, kArrowHeight,
			 kArrowX, kArrowDownY);

	blit(dest, _chrome, hoverMenu ? kMenuHoverSrcX : kMenuSrcX, kMenuSrcY,
		 kMenuWidth, kMenuHeight, kMenuX, kMenuY);

	drawThumb(dest);
}

void Inventory::setCounters(const byte *counters, uint count) {
	for (uint i = 0; i < count && i < kListSize; i++)
		_counter[i] = counters[i];
}

void Inventory::syncGame(Common::Serializer &s) {
	s.syncBytes(_list, kListSize);
	s.syncBytes(_counter, kListSize);

	uint16 page = (uint16)_page;
	s.syncAsUint16LE(page);
	if (s.isLoading())
		_page = page;
}

} // End of namespace Alien
