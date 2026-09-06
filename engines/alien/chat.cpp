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
#include "common/file.h"
#include "common/system.h"
#include "common/util.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "alien/alien.h"
#include "alien/chat.h"
#include "alien/detection.h"
#include "alien/font.h"

namespace Alien {

// The colour an option settles at once it has arrived, and the brighter pair
// the one under the cursor is given (OBJ:sub_0a89c). Only the green channel is
// ever written: the entries are (0, level, 0).
static const byte kInkLevel = 0x1c;
static const byte kShadowLevel = 0x10;
static const byte kHoverInk = 0x30;
static const byte kHoverShadow = 0x1c;

// An option starts arriving once the one before it is this far along, which is
// what staggers the four of them.
static const byte kFollowLevel = 6;

// The fade out steps by two rather than one, and stops when nothing is left
// above this.
static const byte kFadeStep = 2;
static const byte kDarkLevel = 1;

// The gate the dimming of one option waits on: while the option before it is
// still brighter than this it stays where it is.
static const byte kDimGate = 0x16;

// The four y ranges a click is tested against, before the ones below a two-line
// option are pushed down (OBJ:sub_09469).
static const int kDefaultBands[ChatMenu::kOptions][2] = {
	{ 0xa1, 0xa8 }, { 0xa9, 0xb3 }, { 0xb4, 0xbd }, { 0xbe, 0xc8 }
};

/// What an option the topic does not offer gets instead: 1000, off the screen.
static const int kUnreachable = 0x3e8;

/// How far each option's colour pair sits past the face's own two indices.
static const byte kInkOffsets[ChatMenu::kOptions] = { 0, 8, 10, 12 };

ChatMenu::ChatMenu() : _tal(nullptr), _active(false), _finished(false), _topic(0), _count(0),
					   _lineCount(0), _phase(kSettled), _choice(0), _picked(false), _next(0),
					   _advancePending(false) {
	memset(_colors, 0, sizeof(_colors));
	for (uint i = 0; i < kOptions; i++) {
		_bandTop[i] = kUnreachable;
		_bandBottom[i] = kUnreachable;
	}
}

byte ChatMenu::inkOffset(uint slot) {
	return slot < kOptions ? kInkOffsets[slot] : 0;
}

bool ChatMenu::open(const TalFile &tal, uint topic) {
	const uint count = tal.chatOptionCount(topic);
	if (!count) {
		debugC(1, kDebugChat, "chat: topic %u has nothing to say", topic);
		return false;
	}

	_tal = &tal;
	_topic = topic;
	_count = count;
	_active = true;
	_finished = false;
	_choice = 0;
	_picked = false;
	_advancePending = false;
	memset(_colors, 0, sizeof(_colors));
	_phase = kOpening;
	build();

	// A topic with a single option is not a choice: OBJ:sub_0483e takes it on
	// the spot, without ever listing it.
	if (_count == 1) {
		_choice = 1;
		_picked = true;
		_phase = kPicked;
	}

	debugC(1, kDebugChat, "chat: topic %u opens with %u option%s", topic, _count,
		   _count == 1 ? "" : "s");
	return true;
}

void ChatMenu::close() {
	_active = false;
	_finished = false;
	_picked = false;
	_choice = 0;
	_lineCount = 0;
	_phase = kSettled;
	memset(_colors, 0, sizeof(_colors));
}

void ChatMenu::build() {
	_lineCount = 0;

	for (uint i = 0; i < kOptions; i++) {
		_bandTop[i] = kDefaultBands[i][0];
		_bandBottom[i] = kDefaultBands[i][1];
	}

	int row = 0;
	for (uint slot = 0; slot < kOptions; slot++) {
		const TalFile::ChatOption &option = _tal->chatOption(_topic, slot);
		if (!option.lines) {
			// Nothing to list here, and nothing below it moves.
			_bandTop[slot] = kUnreachable;
			continue;
		}

		const TalFile::Entry &entry = _tal->entry(option.entry);
		for (uint i = 0; i < option.lines; i++) {
			const uint index = option.line + i;
			if (index >= entry.lines.size())
				break;
			_lines[_lineCount].text = entry.lines[index];
			_lines[_lineCount].row = row + i;
			_lines[_lineCount].slot = slot;
			_lineCount++;
		}

		// A two-line option is twice as tall, so its own range and every
		// boundary under it come down by one row.
		if (option.lines > 1) {
			_bandBottom[slot] += kRowHeight;
			for (uint j = slot + 1; j < kOptions; j++) {
				_bandTop[j] += kRowHeight;
				_bandBottom[j] += kRowHeight;
			}
		}

		row += option.lines;
	}
}

void ChatMenu::stepColors(int cursorY) {
	switch (_phase) {
	case kLive:
		// Every option is put back to its resting pair each pass, and the one
		// the cursor is over is then brightened -- which is the whole of the
		// highlight: there is nothing to un-highlight.
		for (uint i = 0; i < kOptions; i++) {
			_colors[i * 2] = kInkLevel;
			_colors[i * 2 + 1] = kShadowLevel;
		}

		if (cursorY < kBandTop)
			break;

		for (uint i = 0; i < kOptions; i++) {
			if (cursorY < _bandTop[i] || cursorY > _bandBottom[i])
				continue;
			_colors[i * 2] = kHoverInk;
			_colors[i * 2 + 1] = kHoverShadow;
		}
		break;

	case kOpening:
		for (uint i = 0; i < kOptions; i++) {
			if (i && _colors[(i - 1) * 2] < kFollowLevel)
				break;
			if (_colors[i * 2] < kInkLevel)
				_colors[i * 2]++;
			if (_colors[i * 2 + 1] < kShadowLevel)
				_colors[i * 2 + 1]++;
		}

		if (_colors[(kOptions - 1) * 2] >= kInkLevel)
			_phase = kLive;
		break;

	case kPicked:
		// The picked option holds its brightness while its line is spoken and
		// the others go out from it outward. Each waits on the one before it,
		// and an option whose predecessor is the picked one -- which never
		// comes down -- waits on the one before that instead.
		for (uint i = 0; i < kOptions; i++) {
			if (_choice == i + 1)
				continue;

			int prev = (int)i - 1;
			if (prev >= 0 && _choice == (uint)prev + 1)
				prev--;
			if (prev >= 0 && _colors[prev * 2] > kDimGate)
				continue;

			if (_colors[i * 2] >= 1)
				_colors[i * 2]--;
			if (_colors[i * 2 + 1] >= 1)
				_colors[i * 2 + 1]--;
		}

		// Once the last of the others is dark the picked one follows.
		if ((_choice != kOptions && !_colors[(kOptions - 1) * 2]) ||
			!_colors[(kOptions - 2) * 2])
			_phase = kFading;
		break;

	case kFading:
		for (uint i = 0; i < kOptions * 2; i++)
			_colors[i] = _colors[i] > kFadeStep ? _colors[i] - kFadeStep : 0;

		if (_colors[0] <= kDarkLevel && _colors[2] <= kDarkLevel &&
			_colors[4] <= kDarkLevel && _colors[6] <= kDarkLevel)
			_phase = kSettled;
		break;

	case kSettled:
		memset(_colors, 0, sizeof(_colors));
		break;
	}
}

void ChatMenu::tick(int cursorY, bool lineDone) {
	if (!_active)
		return;

	stepColors(cursorY);

	if (_phase != kSettled)
		return;

	// Standing still with the tree already moved on: the next topic is listed
	// once the line the pick queued has been spoken.
	if (_finished) {
		// The room's own machine reads isFinished(), so the menu only has to
		// take itself off the screen.
		_lineCount = 0;
		_active = false;
		debugC(1, kDebugChat, "chat: the conversation ends");
		return;
	}

	if (_advancePending && lineDone)
		advance();
}

void ChatMenu::advance() {
	_advancePending = false;
	_choice = 0;

	if (_next >= TalFile::kChatTopics || !_tal->chatOptionCount(_next)) {
		// A topic with nothing in it is the end of the line as surely as 0xff.
		_finished = true;
		_active = false;
		debugC(1, kDebugChat, "chat: topic %u is empty, the conversation ends", _next);
		return;
	}

	_topic = _next;
	_count = _tal->chatOptionCount(_topic);
	memset(_colors, 0, sizeof(_colors));
	_phase = kOpening;
	build();

	if (_count == 1) {
		_choice = 1;
		_picked = true;
		_phase = kPicked;
	}

	debugC(1, kDebugChat, "chat: topic %u opens with %u option%s", _topic, _count,
		   _count == 1 ? "" : "s");
}

bool ChatMenu::click(int cursorY) {
	if (!_active || _phase != kLive || cursorY < kBandTop)
		return false;

	for (uint i = 0; i < kOptions; i++) {
		if (cursorY < _bandTop[i] || cursorY > _bandBottom[i])
			continue;

		_colors[i * 2] = kHoverInk;
		_colors[i * 2 + 1] = kHoverShadow;
		_choice = i + 1;
		_picked = true;
		_phase = kPicked;
		return true;
	}

	return false;
}

bool ChatMenu::takePick(TalFile::ChatOption &option) {
	if (!_picked)
		return false;

	_picked = false;
	_pick = _tal->chatOption(_topic, _choice - 1);
	option = _pick;

	_next = _pick.next;
	if (_pick.next == TalFile::kChatEnd) {
		_finished = true;
	} else if (_pick.next == TalFile::kChatRoom) {
		// 0xfe hands the conversation to the room's own entry 4 body, which is
		// not lifted for any of the four overlays that have one. Ending the
		// conversation is what the option does either way; what is missing is
		// whatever the room would have said next.
		_finished = true;
		warning("Alien::ChatMenu: topic %u option %u wants the room's own dialogue body",
				_topic, _choice);
	} else {
		_advancePending = true;
	}

	debugC(1, kDebugChat, "chat: option %u picked, entry %u lines %u..%u, next %u",
		   _choice, _pick.entry, _pick.line, _pick.line + _pick.lines - 1, _pick.next);
	return true;
}

void ChatMenu::draw(const Font &font, Graphics::Surface &dest) const {
	if (!_active)
		return;

	for (uint i = 0; i < _lineCount; i++) {
		const Line &line = _lines[i];
		font.drawStringOffset(dest, line.text, kLeftMargin,
							  kFirstRow + line.row * kRowHeight, inkOffset(line.slot));
	}
}

void ChatMenu::applyPalette(byte *palette) const {
	for (uint slot = 0; slot < kOptions; slot++) {
		for (uint i = 0; i < 2; i++) {
			const uint entry = Font::kChatInk + inkOffset(slot) + i;
			const byte level = _colors[slot * 2 + i] * 255 / 63;
			palette[entry * 3 + 0] = 0;
			palette[entry * 3 + 1] = level;
			palette[entry * 3 + 2] = 0;
		}
	}
}

// The two ends of the menu the engine owns: opening it for a room, and
// stepping it beside the room's own machine.

/// [0xad1e] on a pick: a fixed count rather than the length-derived one.
static const int kChatLineTicks = 0x3c;

/// The first of the eight palette entries the menu writes, and how many of them
/// have to be put back when it goes: 66 and 67 are the status line's as well.
static const uint kChatFirstEntry = 66;
static const uint kChatEntryCount = 14;

void AlienEngine::sweepChatTrees() {
	// Every room's tree, printed the way tools/check_chat.py prints it from the
	// files. Most rooms carry an all-zero table: only the ones with somebody to
	// talk to fill one in, so a room with nothing is named and skipped.
	TalFile tal;

	for (int room = 0; room < StaticTables::kRoomCount; room++) {
		RoomAssets assets;
		Common::Path script;
		if (_overlays.readRoom(room, assets) && !assets.script.empty())
			script = Common::Path(assets.script);
		else
			script = Common::Path(Common::String::format("ROOM%d.TAL", room));

		if (!Common::File::exists(script) || !tal.load(script))
			continue;

		uint topics = 0;
		for (uint topic = 0; topic < TalFile::kChatTopics; topic++)
			if (tal.chatOptionCount(topic))
				topics++;

		if (!topics)
			continue;

		debug("tree room %2d %s: %u topic%s", room, script.toString().c_str(), topics,
			  topics == 1 ? "" : "s");

		for (uint topic = 0; topic < TalFile::kChatTopics; topic++) {
			const uint count = tal.chatOptionCount(topic);
			for (uint i = 0; i < count; i++) {
				const TalFile::ChatOption &o = tal.chatOption(topic, i);
				debug("option room %2d topic %2u %u: entry %3u line %u lines %u next %3u",
					  room, topic, i + 1, o.entry, o.line, o.lines, o.next);
			}
		}
	}
}

void AlienEngine::openChat(uint topic) {
	if (!_chatColorsHeld) {
		memcpy(_chatPalette, _palette + kChatFirstEntry * 3, sizeof(_chatPalette));
		_chatColorsHeld = true;
	}

	if (!_chat.open(_tal, topic))
		return;

	// The cursor comes back for the menu even when the machine that opened it
	// had taken it away (OBJ:sub_09469's last instruction).
	CursorMan.showMouse(true);
	_dirty = true;
}

void AlienEngine::stepChat() {
	if (!_chat.isActive()) {
		// The bar is back, and with it the three entries the status line is
		// drawn in: the menu had two of them.
		if (_chatColorsHeld) {
			memcpy(_palette + kChatFirstEntry * 3, _chatPalette, sizeof(_chatPalette));
			g_system->getPaletteManager()->setPalette(_palette + kChatFirstEntry * 3,
													  kChatFirstEntry, kChatEntryCount);
			_chatColorsHeld = false;
			_dirty = true;
		}
		return;
	}

	_chat.tick(_cursorY, speechDone());

	// What the player just said, spoken over him: the option is a slice of one
	// of the file's entries rather than an entry of its own.
	TalFile::ChatOption pick;
	if (_chat.takePick(pick)) {
		const TalFile::Entry &entry = _tal.entry(pick.entry);
		TalFile::Entry line;
		for (uint i = 0; i < pick.lines && pick.line + i < entry.lines.size(); i++)
			line.lines.push_back(entry.lines[pick.line + i]);
		line.lineCount = (byte)line.lines.size();
		line.present = !line.lines.empty();

		int x = 0, y = 0;
		characterAnchor(x, y);
		if (line.present) {
			speakEntry(line, x, y, kChatLineTicks);
			debugC(1, kDebugChat, "chat: \"%s\"%s", line.lines[0].c_str(),
				   line.lines.size() > 1 ? " ..." : "");
		}
	}

	_chat.applyPalette(_palette);
	g_system->getPaletteManager()->setPalette(_palette + kChatFirstEntry * 3,
											  kChatFirstEntry, kChatEntryCount);
	_dirty = true;
}

} // End of namespace Alien
