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
#include "common/textconsole.h"

#include "alien/detection.h"
#include "alien/overlay.h"

namespace Alien {

// Room -> overlay stub segment. Taken from the room init chain in MAIN, with
// the click chain standing in for the five rooms whose init lives in a resident
// segment; those overlays carry the same literal pool. Room 60 is driven from
// the cutscene unit and has no scene overlay at all, so it is absent here.
// Rooms 43/44/45 share one overlay, and so do 53/57: the manifest of a shared
// overlay is the union of what its rooms load.
struct RoomOverlay {
	byte room;
	uint16 stub;
};

static const RoomOverlay kRoomOverlays[] = {
	{  3, 0x0E57 }, {  6, 0x0E5F }, {  7, 0x0E63 }, {  8, 0x0E67 },
	{ 10, 0x0E73 }, { 11, 0x0E77 }, { 13, 0x0E6F }, { 14, 0x0E83 },
	{ 15, 0x0E5B }, { 17, 0x0EAB }, { 18, 0x0E6B }, { 19, 0x0EB3 },
	{ 21, 0x0EA7 }, { 22, 0x0EA3 }, { 23, 0x0E9F }, { 25, 0x0E8F },
	{ 26, 0x0EAF }, { 27, 0x0E7F }, { 28, 0x0EB7 }, { 30, 0x0E9B },
	{ 31, 0x0E87 }, { 32, 0x0E8B }, { 33, 0x0E97 }, { 34, 0x0E93 },
	{ 35, 0x0E7B }, { 40, 0x0EBB }, { 41, 0x0F89 }, { 43, 0x0F8D },
	{ 44, 0x0F8D }, { 45, 0x0F8D }, { 46, 0x0EC3 }, { 48, 0x0EBF },
	{ 49, 0x0F85 }, { 50, 0x0F81 }, { 51, 0x0FAA }, { 52, 0x0F96 },
	{ 53, 0x0F9E }, { 54, 0x0FA6 }, { 55, 0x0F9A }, { 56, 0x0F92 },
	{ 57, 0x0F9E }, { 58, 0x101D }, { 59, 0x0FA2 }
};

// Overlay stub layout, as laid down by the Turbo Pascal 7 overlay manager. The
// stub sits in the load image and the code it stands for is in GAME.OVR.
static const uint32 kStubOverlayOffset = 4;		///< dword, position in GAME.OVR
static const uint32 kStubCodeSize = 8;			///< word
static const uint32 kStubFixupSize = 10;		///< word, table of word offsets

// A name is at most 8.3 plus the dot; anything longer is not one.
static const byte kMinNameLength = 3;
static const byte kMaxNameLength = 13;

void RoomAssets::clear() {
	for (uint i = 0; i < kMaxSprites; i++)
		sprites[i].clear();
	spriteCount = 0;

	for (uint i = 0; i < kMaxMasks; i++)
		masks[i].clear();
	maskCount = 0;

	walkData.clear();
	script.clear();
}

OverlayIndex::OverlayIndex() : _imageBase(0), _loaded(false) {
}

uint16 OverlayIndex::stubForRoom(int room) {
	for (uint i = 0; i < ARRAYSIZE(kRoomOverlays); i++) {
		if (kRoomOverlays[i].room == room)
			return kRoomOverlays[i].stub;
	}
	return 0;
}

bool OverlayIndex::load() {
	Common::File exe;
	if (!exe.open(Common::Path("GAME.EXE"))) {
		warning("Alien::OverlayIndex: could not open GAME.EXE");
		return false;
	}

	// MZ header word at 0x08 is the header size in paragraphs, and the load
	// image the overlay stubs live in starts right after it.
	exe.seek(8);
	_imageBase = (uint32)exe.readUint16LE() * 16;
	if (exe.err() || !_imageBase) {
		warning("Alien::OverlayIndex: GAME.EXE has no usable MZ header");
		return false;
	}

	_loaded = true;
	return true;
}

/**
 * True when the byte run looks like a file name literal: printable, one dot,
 * and something on either side of it.
 */
static bool isNameLiteral(const byte *text, byte length) {
	int dot = -1;
	for (byte i = 0; i < length; i++) {
		if (text[i] < 32 || text[i] > 126)
			return false;
		if (text[i] == '.')
			dot = (dot < 0) ? i : -2;
	}
	// Room 7 stores one name as a stem plus a bare ".DL1", because it appends
	// a digit at runtime; a literal with nothing before the dot is that tail.
	return dot > 0 && dot < length - 1;
}

/**
 * Names are stored padded in a couple of rooms (ROPE__.DL1 in the basement).
 * DOS drops the blanks when it parses the name, so the engine has to as well.
 */
static Common::String readableName(const byte *text, byte length) {
	char buffer[kMaxNameLength + 1];
	uint out = 0;
	for (byte i = 0; i < length; i++) {
		if (text[i] != ' ')
			buffer[out++] = (char)text[i];
	}
	buffer[out] = '\0';
	return Common::String(buffer);
}

static bool hasExtension(const Common::String &name, const char *ext) {
	const uint32 dot = name.findLastOf('.');
	if (dot == Common::String::npos)
		return false;
	return name.substr(dot + 1).equalsIgnoreCase(ext);
}

bool OverlayIndex::readRoom(int room, RoomAssets &assets) const {
	assets.clear();

	const uint16 stub = stubForRoom(room);
	if (!_loaded || !stub)
		return false;

	Common::File exe;
	if (!exe.open(Common::Path("GAME.EXE")))
		return false;

	const uint32 base = _imageBase + (uint32)stub * 16;
	exe.seek(base);
	if (exe.readUint16BE() != 0xCD3F) {
		warning("Alien::OverlayIndex: no overlay stub at paragraph 0x%04x", stub);
		return false;
	}

	exe.seek(base + kStubOverlayOffset);
	const uint32 blobOffset = exe.readUint32LE();
	exe.seek(base + kStubCodeSize);
	const uint16 codeSize = exe.readUint16LE();
	exe.seek(base + kStubFixupSize);
	const uint16 fixupSize = exe.readUint16LE();
	if (exe.err() || !codeSize)
		return false;

	// The literal pool sits between the code and the fixup table, but reading
	// both together costs nothing and keeps the bounds trivial.
	const uint32 blobSize = (uint32)codeSize + fixupSize;
	byte *blob = (byte *)malloc(blobSize);
	if (!blob)
		return false;

	Common::File ovr;
	if (!ovr.open(Common::Path("GAME.OVR"))) {
		warning("Alien::OverlayIndex: could not open GAME.OVR");
		free(blob);
		return false;
	}
	ovr.seek(blobOffset);
	if (ovr.read(blob, blobSize) != blobSize) {
		warning("Alien::OverlayIndex: overlay at 0x%08x is truncated", blobOffset);
		free(blob);
		return false;
	}

	for (uint32 i = 0; i + 1 < blobSize;) {
		const byte length = blob[i];
		if (length < kMinNameLength || length > kMaxNameLength ||
			i + 1 + length > blobSize || !isNameLiteral(blob + i + 1, length)) {
			i++;
			continue;
		}

		const Common::String name = readableName(blob + i + 1, length);
		i += 1 + length;

		if (hasExtension(name, "DL1")) {
			if (assets.spriteCount < RoomAssets::kMaxSprites)
				assets.sprites[assets.spriteCount++] = name;
			else
				warning("Alien::OverlayIndex: room %d lists more than %d sprite banks",
						room, RoomAssets::kMaxSprites);
		} else if (hasExtension(name, "PIC")) {
			if (assets.maskCount < RoomAssets::kMaxMasks)
				assets.masks[assets.maskCount++] = name;
		} else if (hasExtension(name, "DAT")) {
			assets.walkData = name;
		} else if (hasExtension(name, "TAL")) {
			assets.script = name;
		}
	}

	free(blob);

	debugC(1, kDebugResource, "room %d overlay 0x%04x: %u sprite banks, walk %s, script %s",
		   room, stub, assets.spriteCount,
		   assets.walkData.empty() ? "-" : assets.walkData.c_str(),
		   assets.script.empty() ? "-" : assets.script.c_str());

	return true;
}

} // End of namespace Alien
