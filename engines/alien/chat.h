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

#ifndef ALIEN_CHAT_H
#define ALIEN_CHAT_H

#include "common/scummsys.h"
#include "common/str.h"

#include "alien/tal.h"

namespace Graphics {
struct Surface;
}

namespace Alien {

class Font;

/**
 * The conversation menu: what the player may say, and the tree behind it.
 *
 * A room that has somebody to talk to opens this with a topic number
 * (OBJ:sub_0967e, seventeen callers). The topic is a row of up to four options
 * in the tree at the end of the room's TAL file (TalFile::ChatOption), each of
 * them a slice of one dialog entry; they are listed over the status bar, in the
 * face OBJ:sub_09094 draws with, and the player picks one with the left button.
 * Picking speaks that slice over the character and moves the tree to the option's
 * `next` topic, or -- when `next` is 0xff -- ends the conversation and leaves the
 * room's own machine to answer.
 *
 * Nothing about the menu is drawn as a panel. Each option owns two palette
 * entries, and everything the player sees happening -- the options arriving one
 * after another, the one under the cursor lighting up, the unpicked ones going
 * out again -- is those entries being reprogrammed while the same text stands
 * (OBJ:sub_0961b, which writes DAC entries 0x42, 0x43 and 0x4a..0x4f).
 *
 * The menu is stepped and drawn by AlienEngine; it owns no timing of its own.
 */
class ChatMenu {
public:
	static const uint kOptions = TalFile::kChatOptions;

	/// [0xa633], the phase the four colour pairs are in.
	enum Phase {
		kLive = 0,		///< listed and waiting: the cursor may highlight one
		kOpening = 1,	///< the options arriving, one colour step at a time
		kPicked = 2,	///< one is chosen; the others go out around it
		kSettled = 3,	///< all dark, the menu is standing still
		kFading = 4		///< the chosen one going out too
	};

	/// Where the four options sit: the first line at y 160, one every ten rows.
	static const int kFirstRow = 160;
	static const int kRowHeight = 10;
	static const int kBandTop = 0xa0;		///< nothing above this is the menu
	static const int kLeftMargin = 1;

	ChatMenu();

	bool isActive() const { return _active; }

	/// [0xa60e]: the option that was picked ended the conversation.
	bool isFinished() const { return _finished; }

	uint topic() const { return _topic; }

	/** Open on a topic of this file's tree; a topic with no options does not open. */
	bool open(const TalFile &tal, uint topic);

	/** Drop the menu, whatever phase it is in (a room change, a save load). */
	void close();

	/**
	 * One tick pair of the colour machine, with the cursor where it is now.
	 *
	 * `lineDone` is whether the line the last pick queued has finished; the
	 * original waits three ticks past the click that dismissed it ([0xa608],
	 * counted down beside the dialog countdown) before it moves the tree on,
	 * which comes to the same moment.
	 */
	void tick(int cursorY, bool lineDone);

	/** A left click: takes it, and answers whether it landed on an option. */
	bool click(int cursorY);

	/**
	 * The option just picked, if one was, so the engine can speak it. Answers
	 * once per pick.
	 */
	bool takePick(TalFile::ChatOption &option);

	void draw(const Font &font, Graphics::Surface &dest) const;

	/** Write the eight entries the options are drawn in into a 256*3 palette. */
	void applyPalette(byte *palette) const;

	/** Which palette entry the first of an option's two is (66, 74, 76, 78). */
	static byte inkOffset(uint slot);

private:
	struct Line {
		Common::String text;
		int row;			///< 0..7, ten pixels apart from kFirstRow
		uint slot;			///< which option's colour pair it is drawn in
	};

	void build();
	void stepColors(int cursorY);
	void advance();

	const TalFile *_tal;
	bool _active;
	bool _finished;
	uint _topic;
	uint _count;			///< [0xa632], how many options this topic offers

	Line _lines[kOptions * 2];
	uint _lineCount;

	/// [0xa620..0xa62f]: the y range a click on each option falls in. An option
	/// the topic does not offer is given a range no cursor can reach.
	int _bandTop[kOptions];
	int _bandBottom[kOptions];

	byte _colors[kOptions * 2];	///< [0xa610..0xa617]
	Phase _phase;
	uint _choice;			///< [0xa60c], 1..4 while one is picked
	bool _picked;			///< [0xa60d], a pick the engine has not taken yet
	TalFile::ChatOption _pick;
	uint _next;				///< the topic the pick moves to
	bool _advancePending;
};

} // End of namespace Alien

#endif
