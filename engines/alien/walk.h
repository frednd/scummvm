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

#ifndef ALIEN_WALK_H
#define ALIEN_WALK_H

#include "common/scummsys.h"

namespace Common {
class Path;
}

namespace Alien {

struct RoomAssets;

/**
 * The walkability bitmap of one room, from its KIER*.PIC files.
 *
 * Despite the extension these are not images: the file is a run list,
 * u16 run count followed by (u16 length, u8 value) records, expanding to a
 * 320 byte per row map where 0 is floor and anything else is blocked. The
 * stored run count is one short of the runs actually present, so decoding
 * runs to the end of the file instead.
 *
 * A room wider than the screen ships two files, which the original mapped into
 * EMS pages 6 and 7 and indexed at x and at x - 319; column 319 is therefore
 * duplicated between the two pages. See docs/walk_system.md.
 */
class WalkMask {
public:
	enum {
		kWidth = 320,
		kHeight = 200,
		kPageSplit = 319		///< the second page starts at x - kPageSplit
	};

	WalkMask();
	~WalkMask();

	/** Load one page; index 0 is the left half of the room, 1 the right. */
	bool loadPage(uint page, const Common::Path &path);
	void unload();

	bool isLoaded() const { return _page[0] != nullptr; }

	/** True for blocked floor, and for anything outside the mask. */
	bool blocked(int x, int y) const;

private:
	byte *_page[2];
	uint32 _size[2];
};

/**
 * The walk nodes of one room, from KIERRA{N}.DAT.
 *
 *     u16 zoneCount   A, zero in every per-room file
 *     u16 nodeCount   B
 *     u16 zone[4][A]  four parallel tables, skipped when A is zero
 *     u16 x[B]        room coordinates, above 319 in the wide rooms
 *     u16 y[B]        always inside the floor band, 80..153
 *
 * The X and Y arrays are stored separately, not interleaved, and the node
 * connectivity is not stored at all: the nodes form a ring in file order.
 */
class WalkNodes {
public:
	enum {
		kMaxNodes = 51			///< the engine's own array size; the data peaks at 33
	};

	WalkNodes();

	bool load(const Common::Path &path);
	void unload() { _count = 0; }

	uint count() const { return _count; }
	int x(uint node) const { return _x[node]; }
	int y(uint node) const { return _y[node]; }

private:
	int16 _x[kMaxNodes];
	int16 _y[kMaxNodes];
	uint _count;
};

/**
 * A plotted path. Slot 0 is where the walk starts, the rest are waypoints.
 */
struct WalkRoute {
	enum {
		kMaxWaypoints = 59,		///< the original's cap, walk_route_idx > 0x3b
		kMaxPoints = kMaxWaypoints + 2
	};

	struct Point {
		int16 x;
		int16 y;
	};

	Point points[kMaxPoints];
	uint count;

	WalkRoute() : count(0) {}
};

/**
 * The walk system of one room: mask, nodes and the router over them.
 *
 * The router is a port of walk_plot_route (OBJ 0x7c52) by way of
 * tools/kierra.py in the reverse-engineering repository, and keeps the
 * original's integer arithmetic so that it picks the same waypoints the DOS
 * build did.
 */
class Walk {
public:
	Walk();

	/**
	 * Load the mask and node files a room's overlay manifest names, falling
	 * back to the naming convention when the room has no overlay.
	 */
	bool load(int room, const RoomAssets &assets);
	void unload();

	const WalkMask &mask() const { return _mask; }
	const WalkNodes &nodes() const { return _nodes; }

	/** Plot a path from one point to another; false when there is no mask. */
	bool plotRoute(int fromX, int fromY, int toX, int toY, WalkRoute &route) const;

	/** Line of sight across the mask, walk_los_blocked (OBJ 0x76df). */
	bool losBlocked(int x0, int y0, int x1, int y1) const;

private:
	/** walk_nearest_node (OBJ 0x7816); returns 0xFF when nothing is visible. */
	uint nearestVisibleNode(int fromX, int fromY, int toX, int toY) const;
	/** walk_build_route (OBJ 0x7a36) for one direction around the ring. */
	void buildRoute(int fromX, int fromY, int toX, int toY, bool forward,
					WalkRoute &route) const;

	WalkMask _mask;
	WalkNodes _nodes;
};

} // End of namespace Alien

#endif
