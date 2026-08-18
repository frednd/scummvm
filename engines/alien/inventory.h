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

#ifndef ALIEN_INVENTORY_H
#define ALIEN_INVENTORY_H

#include "common/scummsys.h"
#include "common/serializer.h"
#include "common/str.h"
#include "graphics/surface.h"

#include "alien/tal.h"

namespace Alien {

class StaticTables;

/**
 * What the player is carrying, and the bar it is shown in.
 *
 * The list itself is a hundred bytes at DS:0x2F87 holding item ids, kept dense:
 * giving something up memmoves the tail down over it, which is what lets the bar
 * page through the list six at a time. The port keeps the same shape, so a save
 * game maps onto it one for one. See docs/inventory_system.md.
 *
 * The bar is six 35 by 22 slots along the bottom of the screen with a scroll
 * arrow above and below a proportional thumb at its left end. The icons come
 * from KAMAT.PCX -- an 8 by 8 grid of cells indexed by item id, which the
 * original keeps in EMS page 8 -- and the arrows and the thumb from OBJFILE.PCX,
 * the same plate the font and the cursor are cut from.
 *
 * Each item also has a click record of its own, parallel to the one world
 * objects have: an arity with a wrap bit and four outcome codes in the
 * executable (StaticTables::itemOutcome), plus a counter here that a right-click
 * on the slot rotates. The counters start at one, as MAIN leaves them.
 */
class Inventory {
public:
	/// Indices 1..100; index 0 is unused, as in the original.
	static const uint kListSize = 101;

	/// The bar shows six items at a time, and the list pages six at a time.
	static const uint kSlotCount = 6;

	static const byte kNoItem = 0;

	/// What arrowAt() answers with.
	enum Arrow {
		kArrowNone = 0,
		kArrowUp,		///< towards page 1
		kArrowDown
	};

	Inventory();
	~Inventory();

	/** The icon page, the chrome plate and the item names. */
	bool load();

	/** A new game: nothing carried, every item's click counter back to one. */
	void reset();

	void add(byte item);
	void remove(byte item);
	bool has(byte item) const;

	/** How many pages the list fills, 1..6 -- OBJ:sub_03617. */
	uint pageCount() const;
	uint page() const { return _page; }

	/// True when the page changed, so the caller knows to redraw.
	bool pageUp();
	bool pageDown();

	/** The item shown in a slot of the bar, or kNoItem for an empty slot. */
	byte slotItem(uint slot) const { return itemOn(_page, slot); }

	/** The item a given page would show in a given slot. */
	byte itemOn(uint page, uint slot) const;

	/** Which slot a point is in, or -1. Empty slots do not answer. */
	int slotAt(int x, int y) const;

	/** Which scroll arrow a point is in, ignoring whether it is usable. */
	Arrow arrowAt(int x, int y) const;

	/** Which arrow lights up under the cursor: tighter bounds, and usable only. */
	Arrow arrowHover(int x, int y) const;

	/** Whether that arrow can be taken from the page on show. */
	bool arrowEnabled(Arrow arrow) const;

	/**
	 * The outcome code a look at this item speaks, rotating the item's own
	 * counter the way 10c9:0x4A0 does: the code is read first, then the counter
	 * steps and either wraps to one or stops on the last, per the item's flag.
	 */
	byte lookOutcome(const StaticTables &tables, byte item);

	/** The item's name, out of NAMEROOM/<lang>/INVENTOR.TAL. */
	Common::String name(byte item) const;

	/**
	 * Draw the bar: the icons of the page on show, the two arrows in the state
	 * the page leaves them, and the thumb. `hoverSlot` and `hoverArrow` are what
	 * the cursor is over, or -1 and kArrowNone.
	 */
	void draw(const StaticTables &tables, Graphics::Surface &dest,
			  int hoverSlot, Arrow hoverArrow) const;

	/** For the debug dump: the raw list, index 1..100. */
	byte at(uint index) const { return index < kListSize ? _list[index] : kNoItem; }

	/// Overwrite the per-item look counters, as an imported save does.
	void setCounters(const byte *counters, uint count);

	/// The list, the counters and which page of the bar is showing.
	void syncGame(Common::Serializer &s);

private:
	void blit(Graphics::Surface &dest, const Graphics::Surface &src,
			  int srcX, int srcY, int w, int h, int dstX, int dstY) const;
	void drawIcon(const StaticTables &tables, Graphics::Surface &dest,
				  byte item, uint slot, bool hover) const;
	void drawThumb(Graphics::Surface &dest) const;

	byte _list[kListSize];
	byte _counter[kListSize];	///< per item, the click counter at DS:0x99D7
	uint _page;

	Graphics::Surface _icons;	///< KAMAT.PCX, the icon grid
	Graphics::Surface _chrome;	///< OBJFILE.PCX, the arrows and the thumb
	TalFile _names;
};

} // End of namespace Alien

#endif
