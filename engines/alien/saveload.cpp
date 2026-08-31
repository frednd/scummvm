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

#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/savefile.h"
#include "common/serializer.h"
#include "common/memstream.h"
#include "common/stream.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"

namespace Alien {

/**
 * The original's save file, and what of it this engine can read.
 *
 * The save routine in the unit overlay at stub segment 0x11ED is a run of block
 * writes, each dumping a fixed range of the data segment, and
 * tools/savestate.py recovered the call order: nine ranges, 5412 bytes. The big
 * one carries the whole puzzle state, the character's position and the mode
 * bytes; the small ones carry the item tables and the click queue.
 *
 * SAVEVARS.TMP and SAVEGAME.AUT are exactly that block. A numbered slot carries
 * the slot's preview thumbnail first -- u16 width 125, u16 height 30, then 3750
 * bytes of a 250 by 60 crop of the screen taken every second pixel, centred on
 * the character (docs/playthrough_findings.md finding #31) -- and the state block
 * after it, so the block is read as the tail of whichever file is given.
 */
struct DosRange {
	uint16 address;		///< where the range sits in the data segment
	uint16 length;
};

static const DosRange kDosLayout[] = {
	{ 0x99D6, 4966 },	// every game flag, the click counters, the mode bytes
	{ 0x7924, 1 },		// the sound side's own byte
	{ 0x2F88, 36 },		// the carried-item list, as far as a save keeps it
	{ 0x2FEC, 49 },		// item arity, with the cycle bit
	{ 0x301E, 196 },	// item outcome codes, four to an item
	{ 0x3370, 11 },		// the click outcome queue
	{ 0x3392, 104 },
	{ 0x2936, 9 },
	{ 0xBEDC, 40 }
};

static const uint32 kDosStateSize = 5412;

// The preview picture a numbered slot carries in front of its state: a 125 x 30
// crop of the screen taken every second pixel, so a 250 x 60 window, over the
// grey ramp OBJ:sub_0692c leaves the screen in before the menu is drawn. The
// window is clamped into the playfield rows, [0x98e4] and [0x98e8].
static const uint32 kDosThumbnailSize = 3754;
static const int kThumbWidth = 125;
static const int kThumbHeight = 30;
static const int kCropTop = 14;
static const int kCropBottom = 156;
static const byte kRampBase = 0xC0;

// How far from where the saved feet put it the window is looked for, and what
// counts as having found it. A plate the picture came from scores over 90% and
// every other plate under 45% (tools/check_save.py --room), so the floor sits
// clear of both.
static const int kThumbSearchY = 12;
static const int kThumbSample = 4;
static const int kThumbFloorPercent = 50;

/// Where the installed game keeps them.
static const char *const kDosSaveDir = "SAVEGAME";

// What the port reads out of the big range, by the address the disassembly uses.
static const uint16 kDosBase = 0x99D6;
static const uint16 kDosItemCounter = 0x99D7;	///< per item, the look counter
static const uint16 kDosCharacterX = 0x9F8E;	///< walk_from: where the character is
static const uint16 kDosCharacterY = 0x9F90;
static const uint16 kDosFlags = 0xA600;			///< the puzzle state block
static const uint16 kDosSubmode = 0xA87E;
static const uint16 kDosMode = 0xA880;			///< game_mode: the room
static const uint16 kDosOutcomeCounter = 0xAC26;	///< per object, the rotation

// The current save version. Bumping it invalidates nothing yet: version 1 is the
// first, and older saves do not exist.
static const byte kSaveVersion = 1;

bool AlienEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher ||
		   f == kSupportsLoadingDuringRuntime ||
		   f == kSupportsSavingDuringRuntime;
}

/**
 * The whole of what carries across a save: the room, the character, the puzzle
 * state, the inventory and the rotations.
 *
 * What is deliberately left out is everything a click is halfway through -- the
 * pending outcome, the armed exit, the dialog queue -- because the original does
 * not keep those either: its save is taken between clicks.
 */
void AlienEngine::syncGame(Common::Serializer &s) {
	byte version = kSaveVersion;
	s.syncAsByte(version);

	int16 room = (int16)_room;
	s.syncAsSint16LE(room);
	s.syncAsByte(_secondPlate);
	s.syncAsByte(_mode);
	s.syncAsByte(_liftPlayed);

	int16 x = (int16)_ben.walkX();
	int16 y = (int16)_ben.walkY();
	byte facing = (byte)_ben.facing();
	s.syncAsSint16LE(x);
	s.syncAsSint16LE(y);
	s.syncAsByte(facing);

	s.syncAsByte(_heldItem);
	_inventory.syncGame(s);
	_script.syncGame(s);
	s.syncBytes(_outcomeCounter, kObjectCount);

	if (s.isLoading()) {
		// MAIN clears [0x7dc4] on the branch that has just restored a save, so a
		// loaded game never opens with the monologue -- and never hides the
		// cursor for it either.
		cancelOpening();

		// The room is reloaded from scratch, which is what puts its script,
		// its hotspots and its banks back the way the state says they are.
		if (!loadRoom(room, _secondPlate != 0))
			loadRoom(room);
		_ben.place(x, y, facing);
		_ben.stop();
		stopSpeech();
		_pending = -1;
		_pendingItem = Inventory::kNoItem;
		_armed = 0;
		_dirty = true;
	}
}

Common::Error AlienEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	Common::Serializer s(nullptr, stream);
	syncGame(s);
	return Common::kNoError;
}

Common::Error AlienEngine::loadGameStream(Common::SeekableReadStream *stream) {
	Common::Serializer s(stream, nullptr);
	syncGame(s);
	return Common::kNoError;
}

bool AlienEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	// Between clicks only, the way the original's own save is taken: with a line
	// on screen or a walk owing an outcome there is state the file has no room
	// for.
	return _room > 0 && !_speech && _pending < 0 && !_ben.isWalking();
}

bool AlienEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return _room > 0;
}

/**
 * Read one of the original's saves.
 *
 * Only the fields whose addresses are established are taken: the room, the
 * character's feet, the puzzle state, the two rotation counters and as much of
 * the carried-item list as the file keeps. The rest of the block is left alone
 * rather than guessed at, and the unidentified 3754-byte tail of a numbered slot
 * is not read at all.
 */
byte *AlienEngine::readDosSave(const Common::String &file) {
	// The saves live in a subdirectory of the game directory, which is not one of
	// the trees SearchMan indexes, so the file is reached through the directory
	// itself rather than by name.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	Common::FSNode node = gameDataDir.getChild(kDosSaveDir).getChild(file);
	Common::SeekableReadStream *save = node.createReadStream();
	if (!save) {
		debugC(1, kDebugSave, "could not open %s", file.c_str());
		return nullptr;
	}

	if (save->size() < (int64)kDosStateSize) {
		warning("%s is %d bytes, too short for a save", file.c_str(), (int)save->size());
		delete save;
		return nullptr;
	}

	// A numbered slot carries its 3754-byte preview thumbnail first, so the state
	// block is always the tail of the file.
	save->seek(save->size() - (int64)kDosStateSize);

	byte *block = (byte *)malloc(kDosStateSize);
	if (!block) {
		delete save;
		return nullptr;
	}
	const bool complete = save->read(block, kDosStateSize) == kDosStateSize;
	delete save;
	if (!complete) {
		free(block);
		return nullptr;
	}
	return block;
}

/**
 * Where each of the nine ranges starts in the block.
 *
 * The file is the ranges written back to back, so the mapping from a data
 * segment address to a byte of the file is the writes' own order.
 */
static void dosOffsets(uint32 *offset) {
	uint32 at = 0;
	for (uint i = 0; i < ARRAYSIZE(kDosLayout); i++) {
		offset[i] = at;
		at += kDosLayout[i].length;
	}
}

/** The carried list and the per-item look counters, as an import restores them. */
void AlienEngine::applyDosItems(const byte *block) {
	uint32 offset[ARRAYSIZE(kDosLayout)];
	dosOffsets(offset);
	const byte *list = block + offset[2];

	_inventory.reset();
	for (uint i = 0; i < kDosLayout[2].length && list[i]; i++)
		_inventory.add(list[i]);
	_inventory.setCounters(block + offset[0] + (kDosItemCounter - kDosBase),
						   Inventory::kListSize);
}

/**
 * The preview picture at the head of a numbered slot, or null when there is none.
 *
 * `RESTART.GAM` and the autosave carry only the state block; a slot the player
 * made carries the picture in front of it.
 */
byte *AlienEngine::readDosThumbnail(const Common::String &file, int &width, int &height) {
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	Common::FSNode node = gameDataDir.getChild(kDosSaveDir).getChild(file);
	Common::SeekableReadStream *save = node.createReadStream();
	if (!save)
		return nullptr;

	if (save->size() < (int64)(kDosStateSize + kDosThumbnailSize)) {
		delete save;
		return nullptr;
	}

	width = save->readUint16LE();
	height = save->readUint16LE();
	if (width != kThumbWidth || height != kThumbHeight) {
		delete save;
		return nullptr;
	}

	const uint32 pixels = (uint32)width * height;
	byte *thumb = (byte *)malloc(pixels);
	if (!thumb) {
		delete save;
		return nullptr;
	}
	const bool complete = save->read(thumb, pixels) == pixels;
	delete save;
	if (!complete) {
		free(thumb);
		return nullptr;
	}
	return thumb;
}

/** One plate turned into the grey the save menu leaves the screen in. */
static void rampOf(const byte *palette, byte *ramp) {
	for (uint i = 0; i < 256; i++) {
		const int grey = ((palette[i * 3] >> 2) + (palette[i * 3 + 1] >> 2)
						  + (palette[i * 3 + 2] >> 2)) / 3;
		ramp[i] = (byte)(kRampBase + grey);
	}
}

/**
 * How well the picture sits on one plate at one offset, as a percentage.
 *
 * `stride` samples the picture rather than reading all of it, which is what
 * makes sweeping every column affordable; the right plate at the right offset
 * is exact, so a quarter of its pixels separate it from the rest just as well.
 */
static int scoreWindow(const byte *thumb, const Graphics::Surface &plate,
					   const byte *ramp, int originX, int originY, int stride) {
	int matched = 0, total = 0;
	for (int y = 0; y < kThumbHeight; y += stride) {
		const byte *row = (const byte *)plate.getBasePtr(originX, originY + y * 2);
		const byte *want = thumb + y * kThumbWidth;
		for (int x = 0; x < kThumbWidth; x += stride) {
			total++;
			if (ramp[row[x * 2]] == want[x])
				matched++;
		}
	}
	return total ? matched * 100 / total : 0;
}

/**
 * Which room a save's thumbnail was taken in.
 *
 * The state block does not carry the room: game_mode is the room being *left*,
 * and the room itself is erased before the write (docs/playthrough_findings.md
 * finding #31). What does carry it is the picture, which is an exact crop of a
 * plate once the grey ramp is reproduced -- so every room's plates are scored
 * against it and the best one wins.
 *
 * The saved feet position narrows the search: the crop is taken around the
 * character, and a room's vertical extent is not scrolled, so the rows worth
 * trying are the ones near where his feet were. The columns are all swept,
 * because a wide room's crop is in screen space and the scroll it was taken at
 * is not saved.
 */
int AlienEngine::thumbnailRoom(const byte *thumb, int feetX, int feetY, int &score) {
	int best = 0;
	score = 0;

	for (int room = 1; room <= StaticTables::kRoomCount; room++) {
		for (uint which = 0; which < 2; which++) {
			const Common::String name = which ? _tables.secondPlate(room) : roomPlate(room);
			if (name.empty())
				continue;

			Graphics::Surface plate;
			byte palette[256 * 3];
			if (!loadGamePCX(Common::Path(name), plate, palette)) {
				plate.free();
				continue;
			}

			byte ramp[256];
			rampOf(palette, ramp);

			const int lastX = plate.w - kThumbWidth * 2;
			const int lastY = MIN<int>(plate.h, kCropBottom) - kThumbHeight * 2;
			if (lastX >= 0 && lastY >= kCropTop) {
				// The original takes the window at `(box.y1 + box.y2) / 2 - 30`,
				// the character's vertical middle (docs/playthrough_findings.md
				// finding #31). The save keeps his feet rather than his box, and
				// he is about 56 rows tall, so `feetY - 60` lands within a few
				// rows of it -- near enough to search around.
				const int centred = CLIP(feetY - kThumbHeight * 2, kCropTop, lastY);
				const int fromY = feetY > 0 ? MAX(kCropTop, centred - kThumbSearchY) : kCropTop;
				const int toY = feetY > 0 ? MIN(lastY, centred + kThumbSearchY) : lastY;

				for (int y = fromY; y <= toY; y++) {
					for (int x = 0; x <= lastX; x++) {
						const int sampled = scoreWindow(thumb, plate, ramp, x, y,
														kThumbSample);
						if (sampled <= score)
							continue;
						score = sampled;
						best = room;
					}
				}
			}
			plate.free();
		}
	}

	debugC(1, kDebugSave, "thumbnail: feet %d,%d matches room %d at %d%%",
		   feetX, feetY, best, score);
	return score >= kThumbFloorPercent ? best : 0;
}

bool AlienEngine::importDosSave(const Common::String &file, bool apply) {
	byte *block = readDosSave(file);
	if (!block)
		return false;

	uint32 offset[ARRAYSIZE(kDosLayout)];
	dosOffsets(offset);

	const byte *big = block + offset[0];
	// Range 2 is the carried-item list, which starts one byte in front of the
	// range at 0x2F87; a save keeps the 36 entries after that head byte.
	const byte *list = block + offset[2];

	const byte mode = big[kDosMode - kDosBase];
	const byte submode = big[kDosSubmode - kDosBase];
	const int x = READ_LE_UINT16(big + (kDosCharacterX - kDosBase));
	const int y = READ_LE_UINT16(big + (kDosCharacterY - kDosBase));

	// The room the save was taken in is only in the picture, so a slot that has
	// one is matched against the plates; game_mode -- the room being left -- is
	// what is left when there is no picture to match.
	byte room = mode;
	int width = 0, height = 0, score = 0;
	byte *thumb = readDosThumbnail(file, width, height);
	if (thumb) {
		const int matched = thumbnailRoom(thumb, x, y, score);
		free(thumb);
		if (matched)
			room = (byte)matched;
	}

	// The mirrored line keeps naming what the block itself holds; which room the
	// picture was taken in is a separate line, because it is a separate fact.
	debugC(1, kDebugSave, "dos save %s: room %d submode %d, character at %d,%d",
		   file.c_str(), mode, submode, x, y);

	uint carried = 0;
	for (uint i = 0; i < kDosLayout[2].length; i++) {
		if (!list[i])
			break;
		carried++;
		debugC(1, kDebugSave, "dos item %2u %3u", i + 1, list[i]);
	}

	uint set = 0;
	for (uint i = 0; i < RoomScript::kFlagCount; i++) {
		const byte value = big[(kDosFlags - kDosBase) + i];
		if (!value)
			continue;
		set++;
		debugC(2, kDebugSave, "dos flag %04x %3u", kDosFlags + i, value);
	}

	uint rotated = 0;
	for (uint i = 0; i < kObjectCount; i++) {
		const byte value = big[(kDosOutcomeCounter - kDosBase) + i];
		if (!value)
			continue;
		rotated++;
		debugC(2, kDebugSave, "dos outcome obj %3u %3u", i, value);
	}

	debugC(1, kDebugSave, "dos save %s: %u items, %u flags set, %u objects rotated",
		   file.c_str(), carried, set, rotated);

	if (apply) {
		for (uint i = 0; i < RoomScript::kFlagCount; i++)
			_script.setFlag(RoomScript::kFlagBase + i, big[(kDosFlags - kDosBase) + i]);

		applyDosItems(block);

		memcpy(_outcomeCounter, big + (kDosOutcomeCounter - kDosBase), kObjectCount);

		_mode = mode;
		_heldItem = Inventory::kNoItem;
		cancelOpening();
		if (loadRoom(room)) {
			_ben.place(x, y);
			_ben.stop();
		}
	}

	free(block);
	return true;
}

/**
 * The save channel: read every original save the install has, then take the
 * engine's own state through a round trip.
 */
void AlienEngine::dumpSaves() {
	static const char *const kDosSaves[] = {
		"SAVEGAME.0", "SAVEGAME.AUT", "SAVEVARS.TMP", "RESTART.GAM"
	};

	for (uint i = 0; i < ARRAYSIZE(kDosSaves); i++)
		importDosSave(kDosSaves[i], false);
}

/**
 * Import a carried list, which no save the install holds has.
 *
 * All four of the original's saves were taken with an empty inventory, so the
 * half of `importDosSave` that fills the list has never run against real data.
 * This gives it data: a real block off disk with a list patched into it, at the
 * offset the layout puts the list at, carrying more items than the bar shows at
 * once and counters that are not all one -- so a list read short, a counter
 * range not copied, or a list that does not page would all show. The list then
 * goes through a save and back, which is the other thing the empty saves never
 * exercised.
 */
void AlienEngine::checkDosItemImport() {
	static const char *const kSource = "SAVEVARS.TMP";
	// 13 items is three pages of the six-slot bar, and the counters are the
	// item's own id folded into the 1..5 a look rotates through.
	const uint kItems = 13;

	byte *block = readDosSave(kSource);
	if (!block)
		return;

	uint32 offset[ARRAYSIZE(kDosLayout)];
	dosOffsets(offset);

	byte *list = block + offset[2];
	byte *counters = block + offset[0] + (kDosItemCounter - kDosBase);
	for (uint i = 0; i < kDosLayout[2].length; i++)
		list[i] = i < kItems ? (byte)(i + 1) : 0;
	for (uint i = 0; i < kItems; i++)
		counters[i + 1] = (byte)(i % 5 + 1);

	applyDosItems(block);
	free(block);

	uint carried = 0;
	for (uint i = 1; i < Inventory::kListSize; i++)
		if (_inventory.at(i))
			carried++;

	debugC(4, kDebugSave, "import %s: %u items, %u pages", kSource, carried,
		   _inventory.pageCount());
	for (uint i = 1; i <= carried; i++)
		debugC(4, kDebugSave, "import item %2u %3u counter %3u", i, _inventory.at(i),
			   _inventory.lookCounter(_inventory.at(i)));

	// And back out through the engine's own save, since an imported list is a
	// list like any other once it is in.
	Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
	{
		Common::Serializer s(nullptr, &out);
		syncGame(s);
	}
	_inventory.reset();
	Common::MemoryReadStream in(out.getData(), out.size());
	{
		Common::Serializer s(&in, nullptr);
		syncGame(s);
	}

	bool same = _inventory.pageCount() == (carried + Inventory::kSlotCount - 1) /
											  Inventory::kSlotCount;
	for (uint i = 1; i <= carried && same; i++)
		same = _inventory.at(i) == (byte)i &&
			   _inventory.lookCounter((byte)i) == (byte)((i - 1) % 5 + 1);

	debugC(4, kDebugSave, "import roundtrip %s", same ? "identical" : "DIFFERS");
}

/**
 * Save the state, change it, load it back, and report whether it came back.
 *
 * A save file is only worth anything if what comes out of it is what went in, and
 * that is a property the engine can check on itself without a mirror.
 */
void AlienEngine::checkSaveRoundTrip() {
	Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
	{
		Common::Serializer s(nullptr, &out);
		syncGame(s);
	}

	const int room = _room;
	const byte mode = _mode;
	const int x = _ben.walkX();
	const int y = _ben.walkY();

	// Move everything the save carries, so a field that is not written shows up
	// as a field that does not come back.
	_script.setFlag(RoomScript::kFlagBase + 0x40, 0x5A);
	_inventory.add(7);
	_outcomeCounter[42] = 3;
	_mode = (byte)(mode + 1);
	_ben.place(x + 8, y + 8, 2);

	Common::MemoryReadStream in(out.getData(), out.size());
	{
		Common::Serializer s(&in, nullptr);
		syncGame(s);
	}

	const bool same = _room == room && _mode == mode && _ben.walkX() == x &&
					  _ben.walkY() == y && _script.flag(RoomScript::kFlagBase + 0x40) == 0 &&
					  !_inventory.has(7) && _outcomeCounter[42] == 0;

	debugC(3, kDebugSave, "roundtrip %s: room %d mode %d at %d,%d, %u bytes",
		   same ? "identical" : "DIFFERS", _room, _mode, _ben.walkX(), _ben.walkY(),
		   (uint)out.size());
}

} // End of namespace Alien
