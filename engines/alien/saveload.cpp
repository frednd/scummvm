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
 * bytes of a 2:1 downsample of the screen -- and the state block after it, so the
 * block is read as the tail of whichever file is given.
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
bool AlienEngine::importDosSave(const Common::String &file, bool apply) {
	// The saves live in a subdirectory of the game directory, which is not one of
	// the trees SearchMan indexes, so the file is reached through the directory
	// itself rather than by name.
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	Common::FSNode node = gameDataDir.getChild(kDosSaveDir).getChild(file);
	Common::SeekableReadStream *save = node.createReadStream();
	if (!save) {
		debugC(1, kDebugSave, "could not open %s", file.c_str());
		return false;
	}

	if (save->size() < (int64)kDosStateSize) {
		warning("%s is %d bytes, too short for a save", file.c_str(), (int)save->size());
		delete save;
		return false;
	}

	// A numbered slot carries its 3754-byte preview thumbnail first, so the state
	// block is always the tail of the file.
	save->seek(save->size() - (int64)kDosStateSize);

	byte *block = (byte *)malloc(kDosStateSize);
	if (!block) {
		delete save;
		return false;
	}
	const bool complete = save->read(block, kDosStateSize) == kDosStateSize;
	delete save;
	if (!complete) {
		free(block);
		return false;
	}

	// Rebuild the address-to-offset mapping the writes imply, so the fields can
	// be named by the addresses the disassembly uses.
	uint32 offset[ARRAYSIZE(kDosLayout)];
	uint32 at = 0;
	for (uint i = 0; i < ARRAYSIZE(kDosLayout); i++) {
		offset[i] = at;
		at += kDosLayout[i].length;
	}

	const byte *big = block + offset[0];
	// Range 2 is the carried-item list, which starts one byte in front of the
	// range at 0x2F87; a save keeps the 36 entries after that head byte.
	const byte *list = block + offset[2];

	const byte room = big[kDosMode - kDosBase];
	const byte submode = big[kDosSubmode - kDosBase];
	const int x = READ_LE_UINT16(big + (kDosCharacterX - kDosBase));
	const int y = READ_LE_UINT16(big + (kDosCharacterY - kDosBase));

	debugC(1, kDebugSave, "dos save %s: room %d submode %d, character at %d,%d",
		   file.c_str(), room, submode, x, y);

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

		_inventory.reset();
		for (uint i = 0; i < kDosLayout[2].length && list[i]; i++)
			_inventory.add(list[i]);
		_inventory.setCounters(big + (kDosItemCounter - kDosBase), Inventory::kListSize);

		memcpy(_outcomeCounter, big + (kDosOutcomeCounter - kDosBase), kObjectCount);

		_mode = room;
		_heldItem = Inventory::kNoItem;
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
