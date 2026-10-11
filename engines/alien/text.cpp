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

#include "graphics/paletteman.h"

#include "alien/alien.h"
#include "alien/detection.h"

namespace Alien {

// The status line the hover label is set in. The text is
// centred on x = 150 and its glyphs are blitted with their top row at y = 163.
static const int kLabelCenterX = 150;
static const int kLabelY = 163;
// The margins the status line is cropped at, OBJ:sub_08177's 0x2e and 0x109: a
// glyph starting at or outside either is not drawn, and the pen steps over it.
static const int kLabelLeft = 46;
static const int kLabelRight = 265;

// The label font draws in three palette entries of its own, 66..68, which the
// original programs directly rather than taking from the room's plate: OBJ's
// sub_0a6c4 writes them out of [0xa80d]..[0xa812] whenever the line changes.
static const int kLabelInkFirst = 66;

// Line advance of the in-room speech block, DIALOG:sub_0b27b.
static const int kSpeechAdvance = 9;

void AlienEngine::nextSpeech() {
	// Ids whose slot holds no text are stepped over rather than shown as an
	// empty pause; the shipped files do carry a few of those.
	while (_queueNext < _queueCount) {
		const uint id = _queue[_queueNext++];
		const TalFile::Entry &entry = _speechTal->entry(id);
		if (entry.lines.empty())
			continue;

		_dialogId = id;
		_speechCustom = false;
		_speech = true;
		_speechTicks = speechTicksFor(*_speechTal, id);
		applyTextOverride(_speechTal->textOverride(id));
		_dirty = true;

		debugC(1, kDebugGraphics, "speech %u: %u lines, %d ticks",
			   id, entry.lines.size(), _speechTicks);
		return;
	}

	stopSpeech();
}

/**
 * How long one line stands, the pack's configuration included.
 *
 * The original's countdown is three per character of text, floored at 0x46.
 * Both halves of that are tunable and either can be replaced outright by a
 * duration configured against this entry.
 */
int AlienEngine::speechTicksFor(const TalFile &tal, uint id) const {
	const AlienPack::TextOverride *ov = tal.textOverride(id);
	if (ov && ov->has(AlienPack::kTextDuration))
		return ov->duration;

	const TalFile::Entry &entry = tal.entry(id);
	uint length = 0;
	for (uint i = 0; i < entry.lines.size(); i++)
		length += entry.lines[i].size();

	const int perChar = ov && ov->has(AlienPack::kTextTicksPerChar)
		? ov->ticksPerChar
		: tunable("speech.ticksPerChar", kTicksPerCharacter);
	return MAX<int>((int)length * perChar, tunable("speech.minTicks", kMinSpeechTicks));
}

/**
 * The rest of an override: how the line is set rather than how long it stands.
 *
 * The colour is the one DAC entry the original reprograms per speaker, so
 * writing it here is the same move the room code makes -- and it lasts as long,
 * which is until the next line or the next room sets it again.
 */
void AlienEngine::applyTextOverride(const AlienPack::TextOverride *ov) {
	if (!ov)
		return;

	if (ov->has(AlienPack::kTextColor)) {
		setTextColor(ov->color[0], ov->color[1], ov->color[2]);
		uploadTextColor();
	}
	if (ov->has(AlienPack::kTextAnchor)) {
		// -1 in either half keeps the speaker's own box for that axis, so a row
		// can nudge a line sideways without pinning its height.
		if (ov->anchorX >= 0)
			_speechX = ov->anchorX;
		if (ov->anchorY >= 0)
			_speechY = ov->anchorY;
	}
	if (ov->has(AlienPack::kTextBand))
		_dialogBand = ov->band != 0;
}

void AlienEngine::speakEntry(const TalFile::Entry &entry, int anchorX, int anchorY, int ticks) {
	// A line that is not an entry of its own: the slice of one a conversation
	// option is. The original hands it to the same renderer through the five
	// line buffers, and gives it a fixed countdown rather than the
	// length-derived one ([0xad1e] = 0x3c on the pick).
	_speechEntry = entry;
	_speechCustom = true;
	_speechTal = &_tal;
	_speech = true;
	_speechTicks = ticks;
	_speechX = anchorX;
	_speechY = anchorY;
	_queueCount = 0;
	_queueNext = 0;
	_dirty = true;
}

void AlienEngine::stopSpeech() {
	_speech = false;
	_speechCustom = false;
	_speechTicks = 0;
	_queueCount = 0;
	_queueNext = 0;
	_dirty = true;
}

/**
 * Push the speaker colour to the DAC, which makes it visible.
 *
 * `setTextColor` updates the palette array; the room path uploads that copy
 * shortly after, but a scene sets its speaker colours from inside its own loop,
 * long after it uploaded the record's plate. The original programs the entry
 * directly every frame a line is displayed.
 */
/**
 * The rooms that colour their own dialogue.
 *
 * The engine resets dialogue colour to white before every line, and a room
 * that wants another colour writes the triple back in its tick. Two rooms do it
 * as an ambient colour of their own: the bedroom, where it follows the lamp,
 * and the underwater half of room 46.
 */
static const struct {
	int room;
	uint16 flag;		///< the state byte the room switches on, or 0 for always
	byte value;
	byte r, g, b;		///< the VGA 6-bit triple the room stores
} kRoomTextColors[] = {
	// ovr_07_0e63:0x0c51 -- the bedroom in the dark speaks in blue, and lights
	// its lines white again with the lamp ([0xa6fa], see bedroom.cpp).
	{  7, 0xa6fa, 0, 0x14, 0x14, 0x3F },
	{  7, 0xa6fa, 1, 0x3F, 0x3F, 0x3F },
	// ovr_2e_0ec3:0x0456 -- under the water, unguarded.
	{ 46,      0, 0, 0x1E, 0x23, 0x3F }
};

void AlienEngine::stepTextColor() {
	for (uint i = 0; i < ARRAYSIZE(kRoomTextColors); i++) {
		if (kRoomTextColors[i].room != _room)
			continue;
		if (kRoomTextColors[i].flag &&
			_script.flag(kRoomTextColors[i].flag) != kRoomTextColors[i].value)
			continue;

		const byte *entry = _palette + Font::kInkColor * 3;
		const byte r = kRoomTextColors[i].r * 255 / 63;
		if (entry[0] == r && entry[1] == kRoomTextColors[i].g * 255 / 63 &&
			entry[2] == kRoomTextColors[i].b * 255 / 63)
			return;

		setTextColor(kRoomTextColors[i].r, kRoomTextColors[i].g, kRoomTextColors[i].b);
		uploadTextColor();
		debugC(1, kDebugGraphics, "text: room %d speaks in %d,%d,%d", _room,
			   kRoomTextColors[i].r, kRoomTextColors[i].g, kRoomTextColors[i].b);
		return;
	}
}

void AlienEngine::uploadTextColor() {
	g_system->getPaletteManager()->setPalette(_palette + Font::kInkColor * 3,
											  Font::kInkColor, 1);
	_dirty = true;
}

void AlienEngine::setTextColor(byte r, byte g, byte b) {
	// The original reprograms a single DAC entry per speaker with VGA 6-bit
	// color values, so they are widened to 8-bit here.
	_palette[Font::kInkColor * 3 + 0] = r * 255 / 63;
	_palette[Font::kInkColor * 3 + 1] = g * 255 / 63;
	_palette[Font::kInkColor * 3 + 2] = b * 255 / 63;
}

Common::String AlienEngine::labelText(int x, int y) const {
	// The line names the thing under the cursor and nothing else. HOTSPOT's
	// registration pass writes the hovered object's name into [0xabb8] and
	// sub_13605, at the tail of entry 1, copies that buffer straight into the
	// line [0xaaec] -- no verb is prepended, because the verb word is what a
	// *click* reports afterwards (see clickLabel). sub_135e0 empties both
	// buffers at the top of every frame, so over anything unregistered -- the
	// bar, the walls, the sky -- the line really is empty, which is what the
	// fade below is for.
	(void)x;
	(void)y;

	if (_heldItem) {
		// The one case that does build a phrase: with an item in hand
		// HOTSPOT:sub_13605 detects the "use" mode -- [0xa825] == 2, which
		// 10c9:sub_11001 sets when a click lands in a filled slot of the bar --
		// and concatenates six pieces into [0xaaec]: the USE word at DS:0x3218,
		// a space, the item's name, a space, the WITH word at DS:0x3223, a
		// space, and the hovered object's name [0xabb8]. Both of its branches
		// append the first five: the strcmp at 1336:0x2f2 only decides whether
		// the object half is added, and over bare floor [0xabb8] is empty, so
		// the line really does read "USE KEY WITH " with nothing after it.
		Common::String text = _tables.useVerb() + " " + _inventory.name(_heldItem) +
							  " " + _tables.withVerb() + " ";
		if (_hover >= 0) {
			const TalFile::Entry &entry = _labels.entry(_spots[_hover].label);
			if (!entry.lines.empty())
				text += entry.lines[0];
		}
		return text;
	}

	// Which is [0xabb8] as the hover pass left it, the bar included.
	return hoverName();
}

Common::String AlienEngine::hoverName() const {
	// [0xabb8] on its own: what the click lines append their verb word to. It is
	// empty over bare floor, and the original appends it regardless, so a walk
	// onto nothing really does read as the bare verb.

	// The bar fills the same buffer: HOTSPOT:sub_1353e walks the six slots of
	// the page on show and, over a filled one, calls DIALOG:sub_0b930 for the
	// item's name and copies it into [0xabb8] (1336:0x1b2). So a carried item
	// names itself under the cursor exactly as a room object does.
	if (_hoverSlot >= 0) {
		const byte item = _inventory.slotItem((uint)_hoverSlot);
		if (item)
			return _inventory.name(item);
	}

	if (_hover >= 0) {
		const TalFile::Entry &entry = _labels.entry(_spots[_hover].label);
		if (!entry.lines.empty())
			return entry.lines[0];
	}
	return Common::String();
}

void AlienEngine::setClickLabel(const Common::String &verb, uint hold) {
	// 1021:0x85e and 1021:0x998 both overwrite the line the hover pass just
	// built and then set the suppression counter [0xac1e]. While that counter is
	// non-zero the room's entry 1 returns before registering anything, so
	// sub_13605 never runs and the line stands: the status line reports what was
	// just done rather than previewing what a click would do.
	const Common::String name = hoverName();
	_labelText = name.empty() ? verb : verb + " " + name;
	debugC(2, kDebugItems, "line: \"%s\" (click)", _labelText.c_str());
	_labelShown = _labelText;
	_labelFading = false;
	_labelHold = hold;
	resetLabelColors();
	_dirty = true;
}

void AlienEngine::refreshLabel(int x, int y) {
	// [0xac1e]: while a click's line is still up the room's entry 1 does not
	// register, so nothing rebuilds the line from the hover.
	if (_labelHold)
		return;

	const Common::String text = labelText(x, y);
	if (text == _labelText)
		return;

	_labelText = text;
	_dirty = true;
	debugC(2, kDebugItems, "line: \"%s\"", _labelText.c_str());

	// OBJ:sub_086f4 compares this frame's line against the last one: a new
	// non-empty line is redrawn at full brightness, and a line that has just
	// gone empty starts the fade instead of vanishing.
	if (_labelText.empty()) {
		// The glyphs stay where they are; only their colours go.
		_labelFading = !_labelShown.empty();
	} else {
		_labelShown = _labelText;
		_labelFading = false;
		resetLabelColors();
	}
}

void AlienEngine::resetLabelColors() {
	// OBJ:sub_082fd. Three shades the label font is drawn in, each stored as a
	// grey plus a blue: palette entries 66, 67 and 68.
	static const byte kBright[6] = { 0x0e, 0x13, 0x20, 0x27, 0x27, 0x2d };
	memcpy(_labelColors, kBright, sizeof(_labelColors));
	applyLabelColors();
}

void AlienEngine::stepLabelFade() {
	// Entry 1 decrements [0xac1e] once per frame and returns; the line only
	// starts answering the cursor again when it runs out (or when the walk it
	// was reporting ends -- see clearClickLabel).
	if (_labelHold) {
		_labelHold--;
		return;
	}

	if (!_labelFading)
		return;

	// And a fade the conversation opened over waits for it: the menu owns the
	// entries until stepChat puts them back.
	if (_chat.isActive())
		return;

	// OBJ:sub_06d66, one step per frame: the darkest shade loses one level a
	// tick and the other two lose two, and the line is done once they are all
	// black.
	static const byte kStep[6] = { 1, 1, 2, 2, 2, 2 };
	bool lit = false;
	for (uint i = 0; i < ARRAYSIZE(_labelColors); i++) {
		_labelColors[i] = _labelColors[i] > kStep[i] ? _labelColors[i] - kStep[i] : 0;
		if (_labelColors[i])
			lit = true;
	}

	if (!lit) {
		// Faded out for good: now the line can go.
		_labelFading = false;
		_labelShown.clear();
	}

	applyLabelColors();
	_dirty = true;
}

void AlienEngine::applyLabelColors() {
	// The pairs are (grey, blue): entry 66 is the shadow, 68 the highlight.
	for (uint i = 0; i < 3; i++) {
		const byte grey = _labelColors[i * 2];
		const byte blue = _labelColors[i * 2 + 1];
		_palette[(kLabelInkFirst + i) * 3 + 0] = grey * 255 / 63;
		_palette[(kLabelInkFirst + i) * 3 + 1] = grey * 255 / 63;
		_palette[(kLabelInkFirst + i) * 3 + 2] = blue * 255 / 63;
	}
	g_system->getPaletteManager()->setPalette(_palette + kLabelInkFirst * 3,
											  kLabelInkFirst, 3);
}

void AlienEngine::drawLabel() {
	// The line stays on screen while it fades: what goes dark is the palette,
	// not the text, so the last line drawn is still there until the fade has
	// run out.
	if (_labelShown.empty())
		return;

	// Centred on the line and never moved off centre: OBJ:sub_080c5 measures the
	// string and leaves 0x88 minus half of it in [0xa896], with no clamp of any
	// kind. A line wider than the space it has is cropped at both margins by the
	// draw instead -- OBJ:sub_08177 steps the pen over any glyph that starts at
	// or outside 0x2e and 0x109 -- so it stays centred and loses its ends.
	const int x = kLabelCenterX - _labelFont.measure(_labelShown) / 2;
	debugC(3, kDebugItems, "line: \"%s\" %d wide at x %d", _labelShown.c_str(),
		   _labelFont.measure(_labelShown), x);

	_labelFont.drawString(_screen, _labelShown, x, kLabelY, kLabelLeft, kLabelRight);
}

void AlienEngine::drawSpeech(const TalFile::Entry &entry, int anchorX, int anchorY) {
	// Lines are individually centred on the anchor, and the block is clamped
	// to keep it inside the playfield.
	const int lines = (int)entry.lines.size();
	if (!lines)
		return;

	int top = anchorY - lines * 10;
	if (top < 14)
		top = 14;
	if (top + lines * 11 > 155)
		top = 155 - lines * 11;

	// The anchor is room space, the way the original keeps it: it clamps the
	// block against [0xa0c4] + 2 and [0xa0c4] + 0x13b, the panned window's own
	// edges. Working in screen space here comes to the same thing.
	int anchor = anchorX - _scrollX;

	// One clamp for the whole block, driven by its widest line ([0x9904]), so
	// the lines stay centred on each other rather than each hitting the margin
	// on its own.
	int widest = 0;
	for (int i = 0; i < lines; i++)
		widest = MAX(widest, _font.measure(entry.lines[i]));

	if (anchor - widest / 2 < 2)
		anchor = widest / 2 + 2;
	if (anchor + widest / 2 > 0x13b)
		anchor = 0x13b - widest / 2;

	// DIALOG:sub_0b27b draws the lines at [0xa9b0] + 0, 9, 0x12, 0x1b, 0x24:
	// a 9 px advance, one row under the glyph height. The 10 and 11 above are
	// only the clamp's estimates of the block, and it keeps them.
	for (int i = 0; i < lines; i++) {
		const Common::String &line = entry.lines[i];
		const int x = anchor - _font.measure(line) / 2;
		_font.drawString(_screen, line, x, top + i * kSpeechAdvance);
	}
}

void AlienEngine::characterAnchor(int &x, int &y) const {
	// A line the room queues rather than a click raises is anchored on the
	// midpoint of the character's sprite, at its top edge. The feet are the
	// wrong end: anchoring there puts the text over him.
	Common::Rect box;
	if (_ben.bounds(box)) {
		x = box.left + (box.right - box.left) / 2;
		y = box.top;
		return;
	}

	x = _ben.walkX();
	y = _ben.walkY() - Walker::kWalkPointY;
}

void AlienEngine::drawBand(const TalFile::Entry &entry) {
	// The narration layout: left margin 20, four lines at most, 14 px apart.
	// The block grows downwards, starting at y = 126 for multiple lines (or
	// y = 140 for a single line).
	const int lines = MIN<int>(entry.lines.size(), 4);
	if (!lines)
		return;

	const int firstY = lines == 1 ? 140 : 126;
	for (int i = 0; i < lines; i++)
		_font.drawString(_screen, entry.lines[i], 20, firstY + i * 14);
}

void AlienEngine::showDialog(uint id) {
	// Stepping through the file by hand, for checking the text layer against the
	// reference renders: no countdown, so the line stays up until the next key.
	_dialogId = id;
	_speechTal = &_tal;
	_speech = true;
	_speechTicks = 0;
	_speechX = kAnchorX;
	_speechY = kAnchorY;
	_dirty = true;

	const TalFile::Entry &e = _tal.entry(id);
	debugC(1, kDebugGraphics, "dialog %u: %u lines, count byte %d",
		   id, e.lines.size(), e.lineCount);
}

} // End of namespace Alien
