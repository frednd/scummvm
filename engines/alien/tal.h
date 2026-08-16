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

#ifndef ALIEN_TAL_H
#define ALIEN_TAL_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Common {
class Path;
class SeekableReadStream;
}

namespace Alien {

/**
 * One room's script and dialog text.
 *
 * TALFILES/<lang>/ROOMn.TAL carries the whole thing; NAMEROOM/<lang>/Rn.TAL is
 * the same layout with only the text zone populated and holds the hover labels
 * for that room's objects.
 *
 *   zone 1  0x000..0x898  200 outcome records of 11 bytes: a click count and
 *                         up to 10 dialog ids, rotated through on repeat use
 *   zone 2  0x898         100 u16 offsets, relative to the zone 2 base
 *           0x960         the text blob the offsets point into
 *
 * A dialog entry starts with a line count of 1..5 (which is also the render
 * mode downstream) followed by that many Pascal strings. Entry length is not
 * terminated: an entry runs up to the next offset in use, so the lines are
 * read until the byte range is consumed.
 *
 * Text is CP850, as in Font.
 */
class TalFile {
public:
	static const uint kOutcomeCount = 200;
	static const uint kOutcomeSize = 11;
	static const uint kMaxOutcomeIds = kOutcomeSize - 1;
	static const uint kEntryCount = 100;
	static const uint kMaxLines = 5;
	static const uint32 kZone2Base = 0x898;

	struct Entry {
		byte lineCount;					///< 1..5, doubles as the layout selector
		Common::Array<Common::String> lines;
		bool present;

		Entry() : lineCount(0), present(false) {}
	};

	struct Outcome {
		byte count;						///< how many ids are in use
		byte ids[kMaxOutcomeIds];

		Outcome() : count(0) { memset(ids, 0, sizeof(ids)); }
	};

	TalFile();

	bool load(const Common::Path &path);
	bool loadStream(Common::SeekableReadStream &stream);

	bool isLoaded() const { return _loaded; }

	/** Drop every entry, leaving the file empty. */
	void unload() { clear(); }

	/** The dialog entry for an id, or an empty one when the slot is unused. */
	const Entry &entry(uint id) const;

	/** The outcome record for a handler code; count is 0 when unpopulated. */
	const Outcome &outcome(uint code) const;

	uint usedEntries() const;

private:
	void clear();
	bool parseEntry(const byte *data, uint32 size, uint32 start, uint32 end, Entry &out) const;

	Entry _entries[kEntryCount];
	Outcome _outcomes[kOutcomeCount];
	Entry _empty;
	Outcome _emptyOutcome;
	bool _loaded;
};

} // End of namespace Alien

#endif
