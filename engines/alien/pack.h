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

#ifndef ALIEN_PACK_H
#define ALIEN_PACK_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "alien/cutscenes.h"

namespace Common {
class Path;
class SeekableReadStream;
}

namespace Alien {

/**
 * ALIEN.DAT: the data the port reads instead of carrying it as code.
 *
 * The tables the port lifts out of GAME.EXE and the scene overlays used to be
 * generated as C++ and compiled in, so changing one meant rebuilding the
 * engine. They are moving into one file the engine reads at startup, written by
 * tools/gen_pack.py out of two JSON layers: the lift, which is the DOS ground
 * truth, and the edits, which are deliberate changes laid over it.
 *
 * The dialog layer is first. The game's own TALFILES are still what the text
 * comes out of -- the pack carries only what the files cannot say: how long a
 * line stands, what colour it is set in, where it is anchored, and, when
 * somebody means to rewrite a line, the replacement.
 *
 * The file is optional. Without it every override is simply absent, which is
 * the DOS behaviour, so a checkout that has never run the generator plays the
 * same as one that has.
 *
 * Layout is documented in tools/packlib.py; everything is little-endian and
 * strings live once in a shared table the other sections index.
 */
class AlienPack {
public:
	/// Which fields of an override are set; the rest leave the DOS path alone.
	enum TextFlags {
		kTextDuration = 1 << 0,
		kTextTicksPerChar = 1 << 1,
		kTextColor = 1 << 2,
		kTextAnchor = 1 << 3,
		kTextBand = 1 << 4,
		kTextLines = 1 << 5
	};

	struct TextOverride {
		uint16 flags;
		int16 duration;			///< half ticks the line stands for
		int8 ticksPerChar;		///< the multiplier, when the duration is left derived
		byte color[3];			///< the text DAC entry, six bits a gun
		int16 anchorX;			///< where the line is centred, -1 for the speaker's box
		int16 anchorY;
		byte band;				///< 1 = the bottom band instead of over the speaker
		Common::Array<Common::String> lines;

		/// The shipped text this was written against, so an override that has
		/// drifted off its entry can be reported rather than applied blind.
		uint32 hash;
		bool hasHash;

		TextOverride() : flags(0), duration(-1), ticksPerChar(-1), anchorX(-1),
						 anchorY(-1), band(0), hash(0), hasHash(false) {
			color[0] = color[1] = color[2] = 0;
		}

		bool has(TextFlags f) const { return (flags & f) != 0; }
	};

	AlienPack();

	/**
	 * Read the pack, from the path ConfMan's `alienpack` names or, failing
	 * that, ALIEN.DAT in the game's own tree. False when there is none, which
	 * is not an error.
	 */
	bool load();
	bool loadStream(Common::SeekableReadStream &stream);

	bool isLoaded() const { return _loaded; }

	/** The override for one dialog entry, or null where there is none. */
	const TextOverride *text(const Common::String &talName, byte entry) const;

	/** A replacement chain for an outcome code, or null. */
	const Common::Array<byte> *outcome(const Common::String &talName, byte code) const;

	/** A named scalar, or the caller's own constant when the pack sets none. */
	int tunable(const char *name, int fallback) const;

	/**
	 * The cutscene tables, which cutscenes.cpp hands to the rest of the engine.
	 *
	 * These are the whole of what used to be generated C++: the scene records
	 * and their step streams, the lifted procedures, the scene-id dispatch and
	 * the per-room call sites. A record's name fields point into the pack's own
	 * string table, which outlives every read of them.
	 */
	bool hasCutscenes() const { return !_records.empty(); }
	const Common::Array<ScriptEffect> &effects() const { return _effects; }
	const Common::Array<CutsceneProc> &procs() const { return _procs; }
	const Common::Array<CutsceneRecord> &records() const { return _records; }
	const Common::Array<CutsceneStep> &steps() const { return _steps; }
	const Common::Array<CutsceneArm> &arms() const { return _arms; }
	const Common::Array<CutsceneTrigger> &triggers() const { return _triggers; }

	/**
	 * The room scripts: what a click on an object does, and under which flags.
	 *
	 * `rooms` names each room's run of blocks, a block names its slice of
	 * `roomEffects`, and `flags` is the state a new game starts in. This is the
	 * gating layer -- when an item can be picked up, when a door opens -- and
	 * like the cutscenes it used to be generated C++.
	 */
	bool hasRoomScripts() const { return !_rooms.empty(); }
	const Common::Array<ScriptEffect> &roomEffects() const { return _roomEffects; }
	const Common::Array<ScriptBlock> &blocks() const { return _blocks; }
	const Common::Array<ScriptFlagInit> &flags() const { return _flags; }

	/** One room's blocks, in overlay order; count is zero for a room with none. */
	const ScriptBlock *blocksForRoom(int room, uint &count) const;

	/** The key an override is matched on: the bare upper-case base name. */
	static Common::String talKey(const Common::Path &path);
	static Common::String talKey(const Common::String &name);

	/**
	 * FNV-1a over an entry's lines, joined by a NUL.
	 *
	 * tools/packlib.py computes the same hash over the same bytes; it is how an
	 * override says which text it was written for.
	 */
	static uint32 textHash(const Common::Array<Common::String> &lines);

private:
	Common::String key(const Common::String &talName, byte index) const;
	static void readEffects(const byte *body, uint32 size, Common::Array<ScriptEffect> &out);

	Common::HashMap<Common::String, TextOverride> _text;
	Common::HashMap<Common::String, Common::Array<byte> > _outcomes;
	Common::HashMap<Common::String, int> _tune;

	/// The string table, kept because the records point into it. Nothing may
	/// append to it once the records are built, or those pointers move.
	Common::Array<Common::String> _strings;

	Common::Array<ScriptEffect> _effects;
	Common::Array<CutsceneProc> _procs;
	Common::Array<CutsceneRecord> _records;
	Common::Array<CutsceneStep> _steps;
	Common::Array<CutsceneArm> _arms;
	Common::Array<CutsceneTrigger> _triggers;

	/// One room's entry in the block index: which run of blocks is its own.
	struct RoomBlocks {
		byte room;
		uint16 first;
		uint16 count;
	};

	Common::Array<ScriptEffect> _roomEffects;
	Common::Array<ScriptBlock> _blocks;
	Common::Array<RoomBlocks> _rooms;
	Common::Array<ScriptFlagInit> _flags;

	bool _loaded;
};

} // End of namespace Alien

#endif
