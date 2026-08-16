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
#include "common/path.h"
#include "common/str.h"

#include "alien/detection.h"
#include "alien/overlay.h"
#include "alien/walk.h"

namespace Alien {

static const uint kNoNode = 0xFF;

// --------------------------------------------------------------------------
// KIER*.PIC, the walk mask
// --------------------------------------------------------------------------

WalkMask::WalkMask() {
	for (uint i = 0; i < ARRAYSIZE(_page); i++) {
		_page[i] = nullptr;
		_size[i] = 0;
	}
}

WalkMask::~WalkMask() {
	unload();
}

void WalkMask::unload() {
	for (uint i = 0; i < ARRAYSIZE(_page); i++) {
		delete[] _page[i];
		_page[i] = nullptr;
		_size[i] = 0;
	}
}

bool WalkMask::loadPage(uint page, const Common::Path &path) {
	if (page >= ARRAYSIZE(_page))
		return false;

	Common::File f;
	if (!f.open(path)) {
		warning("Alien::WalkMask: cannot open %s", path.toString().c_str());
		return false;
	}

	delete[] _page[page];
	_page[page] = nullptr;
	_size[page] = 0;

	const uint32 pixels = kWidth * kHeight;
	byte *map = new byte[pixels];
	memset(map, 1, pixels);

	// The run count in the header is one short of the runs present, so the
	// file is expanded to its end rather than to that count.
	f.skip(2);
	uint32 out = 0;
	while (out < pixels && f.pos() + 3 <= f.size()) {
		uint16 length = f.readUint16LE();
		byte value = f.readByte();
		if (length > pixels - out)
			length = (uint16)(pixels - out);
		memset(map + out, value, length);
		out += length;
	}

	if (out < pixels)
		debugC(1, kDebugResource, "walk mask %s: %u of %u bytes covered",
			   path.toString().c_str(), out, pixels);

	_page[page] = map;
	_size[page] = out;
	return true;
}

bool WalkMask::blocked(int x, int y) const {
	if (x < 0 || y < 0 || y >= kHeight)
		return true;

	uint page = 0;
	if (x > kPageSplit) {
		page = 1;
		x -= kPageSplit;
	}

	if (!_page[page] || x >= kWidth)
		return true;

	const uint32 index = (uint32)y * kWidth + x;
	if (index >= _size[page])
		return true;

	return _page[page][index] != 0;
}

// --------------------------------------------------------------------------
// KIERRA{N}.DAT, the walk nodes
// --------------------------------------------------------------------------

WalkNodes::WalkNodes() : _count(0) {
	memset(_x, 0, sizeof(_x));
	memset(_y, 0, sizeof(_y));
}

bool WalkNodes::load(const Common::Path &path) {
	unload();

	Common::File f;
	if (!f.open(path)) {
		warning("Alien::WalkNodes: cannot open %s", path.toString().c_str());
		return false;
	}

	const uint16 zoneCount = f.readUint16LE();
	const uint16 nodeCount = f.readUint16LE();

	const int64 expected = 4 + (int64)zoneCount * 8 + (int64)nodeCount * 4;
	if (f.size() != expected) {
		warning("Alien::WalkNodes: %s is %d bytes, expected %d for %u zones and %u nodes",
				path.toString().c_str(), (int)f.size(), (int)expected, zoneCount, nodeCount);
		return false;
	}

	if (nodeCount > kMaxNodes) {
		warning("Alien::WalkNodes: %s has %u nodes, more than the engine's %d",
				path.toString().c_str(), nodeCount, kMaxNodes);
		return false;
	}

	// The four zone columns are dead in the shipped data: only the global
	// KIERRA.DAT has any, and nothing ever reads them back.
	f.skip((uint32)zoneCount * 8);

	// X and Y are two separate arrays rather than one array of pairs.
	for (uint16 i = 0; i < nodeCount; i++)
		_x[i] = (int16)f.readUint16LE();
	for (uint16 i = 0; i < nodeCount; i++)
		_y[i] = (int16)f.readUint16LE();

	if (f.eos() || f.err())
		return false;

	_count = nodeCount;
	return true;
}

// --------------------------------------------------------------------------
// The room's walk system
// --------------------------------------------------------------------------

Walk::Walk() {
}

void Walk::unload() {
	_mask.unload();
	_nodes.unload();
}

bool Walk::load(int room, const RoomAssets &assets) {
	unload();

	// Which page a mask belongs to is not something the manifest says, and the
	// order the names appear in is no guide either: room 3 lists NOKIERRA.PIC,
	// the walk-nowhere default, where its right half belongs. The names
	// themselves carry it -- KIER{N}B is the right half of a scrolling room,
	// anything else naming the room is the left -- so the manifest is used for
	// what it does settle, and the shipped naming fills in the rest.
	const Common::String secondName = Common::String::format("KIER%dB.", room);
	Common::String pageA, pageB;
	for (uint i = 0; i < assets.maskCount && i < RoomAssets::kMaxMasks; i++) {
		const Common::String &name = assets.masks[i];
		if (!scumm_strnicmp(name.c_str(), "NOKIERRA", 8))
			continue;
		if (!scumm_strnicmp(name.c_str(), secondName.c_str(), secondName.size()))
			pageB = name;
		else if (pageA.empty())
			pageA = name;
	}

	if (pageA.empty()) {
		static const char *const kPatterns[] = { "KIER%dA.PIC", "KIER%d.PIC", "KIERRA%d.PIC" };
		for (uint i = 0; i < ARRAYSIZE(kPatterns); i++) {
			Common::String name = Common::String::format(kPatterns[i], room);
			if (Common::File::exists(Common::Path(name))) {
				pageA = name;
				break;
			}
		}
	}

	if (pageB.empty()) {
		Common::String name = secondName + "PIC";
		if (Common::File::exists(Common::Path(name)))
			pageB = name;
	}

	if (!pageA.empty())
		_mask.loadPage(0, Common::Path(pageA));
	if (!pageB.empty())
		_mask.loadPage(1, Common::Path(pageB));

	Common::Path nodes(assets.walkData);
	if (assets.walkData.empty())
		nodes = Common::Path(Common::String::format("KIERRA%d.DAT", room));
	if (Common::File::exists(nodes))
		_nodes.load(nodes);

	debugC(1, kDebugResource, "room %d walk: mask %s, %u nodes", room,
		   _mask.isLoaded() ? "loaded" : "missing", _nodes.count());

	return _mask.isLoaded();
}

bool Walk::losBlocked(int x0, int y0, int x1, int y1) const {
	// walk_los_blocked (OBJ 0x76df). The line is marched in 6.6 fixed point
	// from (x1, y1) toward (x0, y0), sampling only every fourth pixel of the
	// major axis, and a zero delta is forced to 1 rather than special-cased,
	// which tilts a perfectly straight ray by one unit. Both quirks are what
	// make a port pick the waypoints the original picked.
	int dx = x0 - x1;
	int dy = y0 - y1;
	if (!dx)
		dx = 1;
	if (!dy)
		dy = 1;

	int major, stepX, stepY;
	if (ABS(dy) > ABS(dx)) {
		major = ABS(dy);
		stepX = (dx << 6) / major;		// truncating division, as x86 idiv does
		stepY = dy > 0 ? 64 : -64;
	} else {
		major = ABS(dx);
		stepX = dx > 0 ? 64 : -64;
		stepY = (dy << 6) / major;
	}
	stepX <<= 2;
	stepY <<= 2;

	int fx = x1 << 6;
	int fy = y1 << 6;
	for (int i = (major >> 2) + 1; i > 0; i--) {
		if (_mask.blocked(fx >> 6, fy >> 6))
			return true;
		fx += stepX;
		fy += stepY;
	}
	return false;
}

uint Walk::nearestVisibleNode(int fromX, int fromY, int toX, int toY) const {
	// walk_nearest_node (OBJ 0x7816). Distance halves each axis before
	// squaring, which is the original's guard against overflowing a 16 bit
	// multiply rather than a perspective correction. The seeding is quirky and
	// is reproduced as it stands: node 0 always sets the running distance
	// whether or not it is visible, and a separate branch adopts the first
	// visible node while none has been chosen.
	uint best = kNoNode;
	int bestDist = 0;

	for (uint i = 0; i < _nodes.count(); i++) {
		const int nx = _nodes.x(i);
		const int ny = _nodes.y(i);
		const int ddx = ABS(toX - nx) >> 1;
		const int ddy = ABS(toY - ny) >> 1;
		const int dist = ddx * ddx + ddy * ddy;
		const bool blocked = losBlocked(fromX, fromY, nx, ny);

		if (!blocked && dist < bestDist) {
			best = i;
			bestDist = dist;
		}
		if (!blocked && best == kNoNode) {
			best = i;
			bestDist = dist;
		}
		if (i == 0)
			bestDist = dist;
	}

	return best;
}

void Walk::buildRoute(int fromX, int fromY, int toX, int toY, bool forward,
					  WalkRoute &route) const {
	route.count = 0;
	route.points[route.count].x = (int16)fromX;
	route.points[route.count].y = (int16)fromY;
	route.count++;

	if (!_nodes.count())
		return;

	// Slot 0 is where the walk starts. When the target is already in sight no
	// node is used at all, which is the common case in an open room.
	if (!losBlocked(toX, toY, fromX, fromY)) {
		route.points[route.count] = route.points[0];
		route.count++;
		return;
	}

	uint cur = nearestVisibleNode(fromX, fromY, toX, toY);
	if (cur == kNoNode)
		cur = 0;

	// The original has no ring completion guard and relies on some node seeing
	// the target; when none can, it laps until the waypoint cap trips. Bounding
	// the sweep at one lap plus the cap ends it in the same place without
	// spinning.
	const uint laps = _nodes.count() + WalkRoute::kMaxWaypoints;
	for (uint step = 0; step < laps; step++) {
		if (route.count > WalkRoute::kMaxWaypoints)
			break;

		route.points[route.count].x = (int16)_nodes.x(cur);
		route.points[route.count].y = (int16)_nodes.y(cur);
		route.count++;

		const bool done = !losBlocked(_nodes.x(cur), _nodes.y(cur), toX, toY);

		// Shortcut: when the waypoint two back can see the new one, the one in
		// between is dropped. This is what turns the raw ring walk into a path
		// that hugs the corners.
		if (route.count >= 3) {
			const WalkRoute::Point &back = route.points[route.count - 3];
			const WalkRoute::Point &head = route.points[route.count - 1];
			if (!losBlocked(head.x, head.y, back.x, back.y)) {
				route.points[route.count - 2] = head;
				route.count--;
			}
		}

		if (done)
			break;

		if (forward)
			cur = (cur + 1) % _nodes.count();
		else
			cur = (cur + _nodes.count() - 1) % _nodes.count();
	}
}

bool Walk::plotRoute(int fromX, int fromY, int toX, int toY, WalkRoute &route) const {
	if (!_mask.isLoaded())
		return false;

	// walk_plot_route: both directions around the ring, keep the shorter.
	WalkRoute back;
	buildRoute(fromX, fromY, toX, toY, true, route);
	buildRoute(fromX, fromY, toX, toY, false, back);
	if (back.count < route.count)
		route = back;

	return true;
}

} // End of namespace Alien
