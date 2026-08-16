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

#ifndef ALIEN_OVERLAY_H
#define ALIEN_OVERLAY_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Alien {

/**
 * The asset names one scene overlay pulls in: its sprite banks, its walk data
 * and the dialog file it speaks from.
 */
struct RoomAssets {
	enum {
		kMaxSprites = 20,		///< the fullest room, the two hallways, needs 15
		kMaxMasks = 2			///< a wide room has one mask per half
	};

	Common::String sprites[kMaxSprites];
	uint spriteCount;

	Common::String masks[kMaxMasks];	///< KIER*.PIC walkability masks
	uint maskCount;

	Common::String walkData;			///< KIERRA*.DAT node ring
	Common::String script;				///< room<n>.tal, or a shared file

	RoomAssets() { clear(); }
	void clear();
};

/**
 * Reader for the asset manifests carried by the scene overlays.
 *
 * A room's plate comes from the tables in the data segment (see StaticTables),
 * but everything else it loads is named by string literals compiled into the
 * room's own overlay in GAME.OVR. Turbo Pascal pools those literals in one
 * contiguous run past the code, each a length byte followed by the characters,
 * so the manifest can be read without interpreting a single instruction.
 *
 * Finding a room's overlay is the one part that cannot be read out of the data:
 * the game selects it through an if-chain on the room number, so the resulting
 * map is tabulated below from tools/roommap.py in the reverse-engineering
 * repository. The detection entry pins the executable by MD5, so the stub
 * segments are stable.
 */
class OverlayIndex {
public:
	OverlayIndex();

	/** Read the MZ header of GAME.EXE, which is where the stubs are indexed from. */
	bool load();
	bool isLoaded() const { return _loaded; }

	/** Fill in the manifest for a room; false when the room has no overlay. */
	bool readRoom(int room, RoomAssets &assets) const;

	/** Load-image paragraph of a room's overlay stub, 0 when it has none. */
	static uint16 stubForRoom(int room);

private:
	uint32 _imageBase;			///< where the load image starts inside GAME.EXE
	bool _loaded;
};

} // End of namespace Alien

#endif
