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

#include "alien/pack.h"

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

	/// The conversation tree is the last 0x3fc bytes of the file, 51 topics of
	/// four options each. OBJ:sub_04225 reads exactly that much from the end of
	/// the file into [0x9a56], and the text zone the loader reads before it is
	/// sized `filesize - 0x898 - 0x3fc`, so the two never overlap.
	static const uint32 kChatTable = 0x3fc;
	static const uint kChatTopics = 51;
	static const uint kChatOptions = 4;
	static const uint kChatRecord = 5;

	/// The next-topic byte of an option that ends the conversation, and the one
	/// that hands it to the room instead ([0xa606], the entry 4 path).
	static const byte kChatEnd = 0xff;
	static const byte kChatRoom = 0xfe;

	struct Entry {
		byte lineCount;					///< 1..5, doubles as the layout selector
		Common::Array<Common::String> lines;
		bool present;

		Entry() : lineCount(0), present(false) {}
	};

	/**
	 * One line of the four a topic can offer.
	 *
	 * The player's own lines are not entries of their own: `entry` names a
	 * dialog entry and `line` and `lines` cut a window out of it, so one entry
	 * holds every option of a topic and each option is a slice. `next` is the
	 * topic the conversation moves to once the line has been spoken.
	 *
	 * `reply` is the outcome code the other party answers with, and it is read
	 * by the room rather than by the menu: room 21's entry 4 body picks it out
	 * at offset +3 and hands it to DIALOG:sub_0bcba in Yodle's own colour
	 * (ovr_15_0ea7:0x05e9). A zero means the room answers for itself, which is
	 * what room 8's owl does.
	 */
	struct ChatOption {
		byte entry;
		byte line;
		byte lines;
		byte reply;			///< the outcome the other party answers with, 0 for none
		byte next;

		ChatOption() : entry(0), line(0), lines(0), reply(0), next(0) {}
		bool present() const { return entry || line || lines || reply || next; }
	};

	struct Outcome {
		byte count;						///< how many ids are in use
		byte ids[kMaxOutcomeIds];

		Outcome() : count(0) { memset(ids, 0, sizeof(ids)); }
	};

	TalFile();

	/**
	 * Read one file, and lay the pack's overrides for it on top.
	 *
	 * The pack is keyed on the file's base name, so a caller that reads a
	 * stream rather than a path has to say which file it is holding. Without a
	 * pack the file is what the game shipped, which is what every caller that
	 * does not pass one wants.
	 */
	bool load(const Common::Path &path, const AlienPack *pack = nullptr);
	bool loadStream(Common::SeekableReadStream &stream, const AlienPack *pack = nullptr,
					const Common::String &name = Common::String());

	bool isLoaded() const { return _loaded; }

	/** Drop every entry, leaving the file empty. */
	void unload() { clear(); }

	/** The dialog entry for an id, or an empty one when the slot is unused. */
	const Entry &entry(uint id) const;

	/** The outcome record for a handler code; count is 0 when unpopulated. */
	const Outcome &outcome(uint code) const;

	uint usedEntries() const;

	/** One option of a topic; an absent one reads back all zero. */
	const ChatOption &chatOption(uint topic, uint option) const;

	/** How many of the four options a topic offers (OBJ:sub_0456e). */
	uint chatOptionCount(uint topic) const;

	/**
	 * What the pack says about how an entry is spoken, or null.
	 *
	 * Replacement lines are already in `entry`: only the things the file cannot
	 * carry -- the countdown, the colour, the anchor, the layout -- are read
	 * back from here, at the point the line goes up.
	 */
	const AlienPack::TextOverride *textOverride(uint id) const;

	/** The base name the pack's rows for this file are keyed on. */
	const Common::String &name() const { return _name; }

private:
	void clear();
	bool parseEntry(const byte *data, uint32 size, uint32 start, uint32 end, Entry &out) const;
	void applyPack(const AlienPack &pack);

	Common::String _name;
	const AlienPack::TextOverride *_overrides[kEntryCount];
	Entry _entries[kEntryCount];
	ChatOption _chat[kChatTopics][kChatOptions];
	ChatOption _emptyOption;
	Outcome _outcomes[kOutcomeCount];
	Entry _empty;
	Outcome _emptyOutcome;
	bool _loaded;
};

} // End of namespace Alien

#endif
