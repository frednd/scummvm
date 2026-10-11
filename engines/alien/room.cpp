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

#include "common/file.h"
#include "graphics/cursorman.h"

#include "alien/alien.h"
#include "alien/detection.h"
#include "alien/resources.h"
#include "alien/occlusion.h"

namespace Alien {

// The rooms the alien ship's lift serves: the lobby and the two hallways whose
// overlays open with the 0FAE:sub_101a6 call that plays the clip.
static const int kLiftRooms[] = { 51, 53, 57 };

/**
 * Room's total pixel width, `[0xa0c0]` as set by that room's own overlay init
 * code (default 0x140 = 320). Twelve rooms are wider than the 320px screen
 * and pan with `_scrollX` (sub_13bce); the rest are a no-op override (<= 320)
 * and stay fixed. Of the ship's rooms only the hallways (53 and 57, one
 * overlay) are wide: 54, 55 and 56 have overlays of their own that never set
 * `[0xa0c0]` or `[0x7924]`, and 320-wide plates.
 */
int AlienEngine::roomWidth(int room) {
	switch (room) {
	case 3:  return 0x18e;
	case 8:  return 0x17c;
	case 15: return 0x260;
	case 21: return 0x268;
	case 22: return 0x27e;
	case 27: return 0x1f0;
	case 32: return 0x1e0;
	case 50: return 0x190;
	case 52: return 0x27e;
	case 53:
	case 57: return 0x1b8;
	case 58: return 0x27e;
	default: return kScreenWidth;
	}
}

const char *AlienEngine::charPaletteFile(int room) const {
	// Ben's own colours (palette indices 1..24) are not part of the room's
	// background plate -- the original reloads them from a separate PCX after
	// every room's palette fade (via load_char_palette, OBJ:sub_073bf), which
	// is why a background PCX is free to put anything unrelated in that range.
	// VAKIPAL.PCX is the default (set at boot) and most rooms re-load exactly
	// that; the rooms
	// below are every overlay whose own call to load_char_palette names
	// something else, read out of the manifest string each call points at.
	//
	// While the pump suit is worn the alien-ship rooms re-apply the suit's own
	// palette after their room palette (CHARANIM:sub_14679, called from all of
	// rooms 51..59, and from room 60's init, LOGIC:sub_125ec), so the suit wins
	// there. Room 52 is the exception: the
	// scanner overwrites the suit again with its own SEC_BPAL, and only while
	// the suit is on (ovr_34_0f96:0x62b guards the call on the same flag).
	if (_script.flag(0xa79b) == 1 && room >= 51 && room <= 60)
		return room == 52 ? "SEC_BPAL.PCX" : "PUMP_PAL.PCX";

	switch (room) {
	case 14: return "MANPAL0.PCX";		// ovr_0e_0e83:0x505
	case 21: return "GAME21A.PCX";		// ovr_15_0ea7:0xb2e
	case 26: return "GAME26.PCX";		// ovr_1a_0eaf:0x637
	case 28: return "GAME28.PCX";		// ovr_1c_0eb7:0x66c
	case 35: return "MANPAL1.PCX";		// ovr_23_0e7b:0x537
	case 46: return "WATER1.PCX";		// ovr_2e_0ec3:0x313
	// Room 49 loads MANPAL2 first, stashes the 24 triples it produced, then
	// loads VAKIPAL over them (ovr_31_0f85:0x374..0x399), so VAKIPAL is what
	// the room opens with. The stash is the far half of the engine room: its
	// tick swaps to it whenever Ben walks past x=0xcd (OBJ:sub_0a74c), which
	// is a per-frame effect this function cannot express.
	default: return "VAKIPAL.PCX";
	}
}

void AlienEngine::applyCharPalette(int room) {
	Graphics::Surface dummy;
	byte charPalette[256 * 3];
	const char *name = charPaletteFile(room);
	if (!loadGamePCX(Common::Path(name), dummy, charPalette)) {
		debugC(1, kDebugResource, "room %d: no character palette %s", room, name);
		dummy.free();
		return;
	}
	debugC(1, kDebugResource, "room %d: character palette %s", room, name);
	dummy.free();

	// Indices 1..24 only -- everything else in this file is unused filler.
	memcpy(_palette + 1 * 3, charPalette + 1 * 3, 24 * 3);

	// And the same block unscaled, which is what the room's light map scales
	// from once Ben moves (lighting.cpp).
	keepCharPalette(room, charPalette);
}

bool AlienEngine::loadRoom(int room, bool secondPlate, bool keepPosition) {
	// A self-reload (takeExit's submode 111, "this room again") carries no
	// char_place of its own unless one happens to be guarded on the room just
	// left, which on a self-reload it never is -- so without keepPosition the
	// walk-node fallback below moves Ben to whatever the room's first KIERRA
	// node happens to be. Captured before anything else runs, since nothing
	// between here and the placement below touches _ben.
	const int keptX = _ben.walkX();
	const int keptY = _ben.walkY();
	const int keptFacing = _ben.facing();

	// Room 52 is dispatched through MAIN:sub_001eb, which plays the scan and
	// may send him to the jail instead (scanner.cpp). A self-reload is the
	// room coming back to itself, not an entry through the door.
	if (!keepPosition)
		for (Room *r : _rooms)
			room = r->redirect(room);

	// And room 57's alarm, which its own entry plays in front of the room the
	// first time it is entered with the force field down (corridor.cpp).
	if (!keepPosition)
		hallwayScene(room);

	// Before anything is read: the two mazes take their position from the way
	// in, and their plate and their rectangles are both read from it
	// (maze.cpp).
	mazeEnter(room);

	const int width = roomWidth(room);
	const bool wide = width > kScreenWidth;

	// Wide rooms show both plates stitched side by side and panned by the
	// scroll offset; the manual A/B toggle (the 'b' debug key) only makes
	// sense for the narrow rooms whose B plate is a genuine alternate view,
	// not a second half.
	const Common::String plate = (secondPlate && !wide) ? _tables.secondPlate(room)
														 : roomPlate(room);
	if (plate.empty()) {
		debugC(1, kDebugResource, "room %d has no %s plate", room,
			   secondPlate ? "second" : "background");
		return false;
	}

	// Eleven of the names in the table belong to cut rooms whose art never
	// shipped, so a failure here is expected and must leave the current room
	// standing.
	Graphics::Surface loaded;
	byte palette[256 * 3];
	// The two mazes have no plate of their own: the screen is composed piece by
	// piece from the cell, out of the three MAZBLK plates (maze.cpp). Failing
	// that -- missing art -- roomPlate() answers the FADE43.PCX both inits open
	// on, so the room still loads.
	if (!mazeBackground(room, loaded, palette) &&
		!loadGamePCX(Common::Path(plate), loaded, palette)) {
		loaded.free();
		return false;
	}

	if (wide) {
		// Plate A supplies the left 320 columns, plate B the remainder,
		// mirroring the two EMS pages the original pans between. A room
		// missing its B plate just shows padding past 320.
		Graphics::Surface stitched;
		stitched.create(width, loaded.h, loaded.format);
		stitched.fillRect(Common::Rect(0, 0, width, loaded.h), 0);
		for (int y = 0; y < loaded.h; y++)
			memcpy(stitched.getBasePtr(0, y), loaded.getBasePtr(0, y), loaded.w);

		const Common::String &secondName = _tables.secondPlate(room);
		if (!secondName.empty()) {
			Graphics::Surface second;
			byte palette2[256 * 3];
			if (loadGamePCX(Common::Path(secondName), second, palette2)) {
				const int copyW = MIN<int>(second.w, width - kScreenWidth);
				const int copyH = MIN<int>(second.h, loaded.h);
				for (int y = 0; y < copyH; y++)
					memcpy(stitched.getBasePtr(kScreenWidth, y), second.getBasePtr(0, y), copyW);
			}
			second.free();
		}

		loaded.free();
		loaded = stitched;
	}

	_background.free();
	_background = loaded;
	_baked.clear();
	_roomWidth = wide ? width : kScreenWidth;
	_scrollX = 0;
	_scrollHold = -1;
	_roomEnterTick = _tick;
	// Every wide room's init zeroes [0xa0c4]/[0xa0c6] and sets [0xa0ce] to
	// 0xff; room 8 clears the speed too, and nothing else carries it over.
	_scrollPos = 0;
	_scrollVel = 0;
	_scrollState = 0xff;
	// The screen still shows the room being left, and _palette is still its
	// palette, so this is the moment the original fades it away: OBJ:sub_0879a
	// takes a copy as the old room's loop ends and OBJ:sub_07db3 fades that copy
	// out before it uploads the new room's pixels (fade.cpp).
	fadeOut();

	memcpy(_palette, palette, sizeof(_palette));
	applyCharPalette(room);
	loadOccluder(room);
	loadLightMap(room);
	_lightArmed = false;

	// The sprite banks and the dialog file are named by the room's own scene
	// overlay. Rooms driven from a resident segment have no overlay, and those
	// keep an empty manifest until their code is understood.
	_overlays.readRoom(room, _assets);
	loadSpriteBank(0);

	// The walk mask and the node ring, so a click can be routed. Rooms with no
	// KIERRA files have no free movement at all, and those keep an empty mask.
	_walk.load(room, _assets);
	_route.count = 0;

	_spots.clear();
	_hover = -1;
	_hoverSlot = -1;
	_hoverArrow = Inventory::kArrowNone;
	_hoverMenu = false;
	_pending = -1;
	_pendingItem = Inventory::kNoItem;
	_armed = 0;
	stopSpeech();

	// The room's sample bank, and with it the queue flush the original does on
	// the way out of a room. Loaded before the script is entered, in case the
	// room's opening setup triggers an effect.
	if (_sound.enterRoom(_tables, room))
		debugC(1, kDebugSound, "room %d: bank %d %s, %u slots", room, _sound.bankIndex(),
			   _sound.bank().file().c_str(), _sound.bank().slotCount());

	// The banks the room's animation slots play, from the overlay's own load
	// calls. Loaded before the script is entered, because entering it runs the
	// room's opening plays against these slots.
	// Rooms 8, 14 and 18 zero their clock as the overlay opens, so a room
	// re-entered starts counting again (roomtick.cpp).
	resetRoomClock(room);
	_labStep = 0;
	_bedroomLiftStep = 0;
	_mansionStep = 0;
	_labPos = 0;
	_labNearHole = false;
	_sewerStep = 0;
	_basementStep = 0;
	_basementClimbing = false;
	_basementWalkOff = false;
	_livingStep = 0;
	_cliffStep = 0;
	_cliffClimb = 0;
	_cliffPos = 0;
	_mailboxStep = 0;
	_mailboxPos = 0;
	_townStep = 0;
	_sluggsStep = 0;
	_sluggsPos = 0;
	_sluggsLeft = 0;
	_sluggsSpeaking = false;
	_sluggsTalking = false;
	for (Room *r : _rooms)
		r->reset();
	_chatPickNew = false;
	_chat.close();
	_barHidden = false;
	_inventory.setBarAway(false);
	_chatOwnsBar = false;

	// OBJ:0x6300, the room's teardown: he may turn round again in the next.
	_ben.setTurnBlocked(false);

	// Room 46 swims him, in a character set of its own (diving.cpp). Before
	// the placement below, which picks his first frame out of that set.
	_ben.setSwimming(room == kSwimRoom);

	// And the pumpkin mask: PUMPWALK while [0xa79b]
	// stands, which the ship rooms load as they open and a restore reloads
	// (CHARANIM:sub_14679, sub_14612). Room 22 takes it off on every way in
	// (ovr_16_0ea3:0x0594), the teleporter's way back to Earth included.
	if (room == 22)
		_script.setFlag(0xa79b, 0);
	_ben.setPumpkin(_script.flag(0xa79b) == 1);

	// MAIN sets [0xa636] for a new game and the first character placement
	// (LOGIC:sub_132e4) clears it as it runs the opening fade; nothing in the
	// port reads it, but the savegame carries it.
	_script.setFlag(0xa636, 0);

	// [0xa94d] is put back by the shared room open, so a room left mid-sequence
	// does not carry the character's absence into the next one. The slots go
	// with it: the room's banks are about to be dropped anyway, and a machine
	// that was still holding him keeps no claim on a slot of the next room.
	_characterAnimSlots = 0;
	_drawCharacter = true;

	// And [0xa948] with it: OBJ:sub_08567 writes the pair together (0251:6096
	// and 0251:60a5). Every machine that leaves a room takes the cursor away
	// for its animation and leaves it away -- the ladder up out of the basement
	// and the one up out of the sewer among them -- and relies on the next
	// room's open to hand it back. Without this the game arrives in the next
	// room with no pointer and no way to click anything.
	// It runs here, before the per-room arrivals below, because those are the
	// original's overlay init, which follows sub_08567 and is entitled to take
	// the cursor away again for the way in.
	CursorMan.showMouse(true);

	// OBJ:sub_08567, the shared room open every overlay's entry 1 calls, puts
	// the DL1 clip line back at the bottom of the playfield (0251:605f). Only
	// the sewer ever moves it, and it moves it every frame it is flooded.
	_clipBottom = kPlayfieldBottom;

	// It also takes room 31's ledge flag down everywhere but room 31 itself
	// (0251:60d3), so the half-size depth the chimney's init sets ([0xa73a],
	// ovr_0e_0e83:0x052f, after this call) lasts only as long as the visit
	// (mailbox).
	if (room != 31)
		_script.setFlag(0xa73a, 0);
	// Room 41 raises it again straight after (ovr_29_0f89:0x07e1), from an
	// init that sits inline in entry 2 and so is not lifted: what OBJ:sub_06466
	// reads to offset the character's frames on the shore's far ledges.
	if (room == 41)
		_script.setFlag(0xa73a, 1);

	// Room 56's view, before anything that draws or registers by it.
	advanceChamberView(room);

	_anims.loadRoom(room, _script);
	loadDivingBanks(room);

	// What the room does with a click on its own account, lifted out of its
	// overlay, plus the opening frame of every slot. The state block is not
	// touched here: puzzle
	// flags outlive the room they were set in.
	_script.enterRoom(room, _sceneHandBack);

	// And then the plate patch-up: the room's own routine in the resident 10c9
	// unit, which stamps into the background page every frame the puzzle state
	// says belongs there -- the door left open, the shelf pushed aside, the item
	// already taken -- and starts the slots the room opens with running
	// (roomplate.cpp). The original calls it last, so it runs last here too.
	// And the rooms' own, which their overlays write rather than the 10c9
	// unit -- room 8's safe, cracked, the shelf pushed off it, the padlock
	// (library.cpp). They run before the 10c9 pass, as the enter routine does.
	for (Room *r : _rooms)
		r->restorePlate(room);

	openRoomPlate(room);

	// And the one room whose open carries more than the lift can express: the
	// sewer's ladder, and the water it may still be full of (sewer.cpp).
	enterSewer(room);
	enterMansion(room);

	// And room 13's, which is the climb down when he came in over it
	// (basement.cpp).
	enterBasement(room);

	// And room 33's, which is him landing in the road after the mailbox went
	// up -- the far end of [0x33bc] (mailbox.cpp).
	enterTown(room);

	// And room 34's, which puts Sluggs out on the road (sluggs.cpp).
	enterSluggs(room);

	// The rectangles the room registers, by running entry 1 of its overlay as
	// Which ones exist depends on the puzzle
	// state, so this runs after the script has the room bound and again
	// whenever a click has moved the state.
	rebuildHotspots(room);

	// With the anim channel on, the room opens with everything in it moving
	// rather than in its opening state -- a way to see every bank a room holds
	// without hunting for the click that plays it. The 'a' key repeats it.
	if (debugChannelSet(-1, kDebugAnim))
		_anims.playAll(kDebugAnimRate);

	// Where the character enters is the room's own business: its opening effects
	// carry a CHARANIM:0x4e call, often one per way in under a guard on the room
	// that was left. A room that says nothing leaves him on the first walk node,
	// which is at least a place the room itself calls floor.
	int placeX = 0, placeY = 0, placeFacing = 0;
	if (_script.placeRequest(placeX, placeY, placeFacing)) {
		// What the call passes is the sprite's own origin, [0xa8ec] and [0xa8ee],
		// not the point the walk system routes -- those differ by (10, 64).
		_ben.placeSprite(placeX, placeY, placeFacing);
		debugC(1, kDebugRooms, "room %d: opens with the character at %d,%d facing %d",
			   room, _ben.walkX(), _ben.walkY(), placeFacing);
	} else if (keepPosition) {
		// The original never moves him on a reload that names no char_place of
		// its own -- see keptX/keptY/keptFacing above.
		_ben.place(keptX, keptY, keptFacing);
	} else if (_walk.nodes().count()) {
		// The last two are the port's own: the original makes no char_place,
		// so they are not traced (room 28 handed back to itself, #178).
		_ben.place(_walk.nodes().x(0), _walk.nodes().y(0), 3, false);
	} else {
		_ben.place(kScreenWidth / 2, 140, 3, false);
	}

	// And the one room that walks him on from there (bedroom.cpp).
	enterLanding(room);

	// Most rooms name a room<n>.tal, but several speak through a shared file,
	// and the ones without an overlay fall back to the naming convention.
	Common::Path script(_assets.script);
	if (_assets.script.empty())
		script = Common::Path(Common::String::format("ROOM%d.TAL", room));
	if (Common::File::exists(script))
		_tal.load(script, &_pack);
	else
		_tal.unload();
	// Room 40's pool names no file and there is no ROOM40.TAL: its init takes
	// one of LOGIC's ten shared files instead, LOGIC:sub_124a7(1) at
	// ovr_28_0ebb:0x0399, which is MUMMY0.TAL, and [0x33db] remembers which.
	enterCave(room);

	// Every OBJ file loader drops [0xa742] (0251:1e25 and its three siblings),
	// the byte LOGIC:sub_12418 keeps for "ROOM21.TAL is the one in"; room 21's
	// own arrival puts it back (yodle.cpp).
	_script.setFlag(0xa742, 0);

	// The hover names live in a file of their own, laid out like the script but
	// with only the text zone filled in. Rooms whose objects were never given
	// names have no file at all.
	// Room 53's are room 57's: both of the hallways' entries load R57 (the
	// overlay's 0x0509 and 0x0b7f).
	const Common::Path labels(Common::String::format("R%d.TAL", room == 53 ? 57 : room));
	if (Common::File::exists(labels))
		_labels.load(labels, &_pack);
	else
		_labels.unload();

	// White is what the dialog unit resets the text entry to before every
	// line; the per-speaker colours are set by the room code that triggers
	// the line, and none of that exists yet.
	setTextColor(0x3F, 0x3F, 0x3F);

	// The status line's own three entries are the game's, not the plate's, so
	// they go back over whatever the room's palette put there. The line itself
	// starts empty: the new room has not been hovered yet.
	_labelText.clear();
	_labelShown.clear();
	_labelFading = false;
	_labelHold = 0;
	_walkReported = false;
	resetLabelColors();

	// And then black, rather than the palette all of that just settled: the new
	// room's first frame is composed under a dead palette and the fade in
	// raises it once the frame is up, which is the order the tail of the
	// original's room loop works in (fade.cpp). Last, so that the entries
	// resetLabelColors uploads for itself go down with everything else.
	uploadPalette(_palette, 0);
	_fadePending = true;

	debugC(1, kDebugResource,
		   "room %d %c: %s, %dx%d plate, %u sprite frames, %u dialog entries, "
		   "%u labels, %u hotspots",
		   room, secondPlate ? 'B' : 'A', plate.c_str(), _background.w, _background.h,
		   _sprite.frameCount(), _tal.usedEntries(), _labels.usedEntries(), _spots.size());

	_room = room;
	// OBJ:sub_08567, the shared open: the room, the way in ([0xa880], the room
	// left), and whether a scene is handing it back.
	traceEvent("room %d from %d%s", room, _mode, _sceneHandBack ? " again" : "");

	// What the room says as it opens, if its opening effects queued an outcome:
	// room 3's two-line monologue is how the game itself starts. It is spoken
	// here rather than where the effect ran, because the effects run before the
	// room's dialog file is loaded and before the character has been placed.
	if (_script.queuedEvent() != RoomScript::kNoEvent) {
		const byte code = _script.queuedEvent();
		_script.clearQueuedEvent();
		debugC(1, kDebugRooms, "room %d: opens by speaking outcome %d", room, code);
		int anchorX, anchorY;
		characterAnchor(anchorX, anchorY);
		queueOutcome(_tal, code, anchorX, anchorY);
	}

	// HOTSPOT:sub_13605's latch, the object the last left click was on, is
	// written by every click in the game, so the exit click that brought him
	// here is still in it. The rooms that read it back (cemetery.cpp,
	// shore.cpp, corridor.cpp) only do so on the pass a route runs out
	// ([0x9908]), which a placement never is; the port tests "not walking",
	// which he already is on entry. With the cliff's object 1 still latched,
	// the cemetery said "I can't walk through the beams" the moment he
	// arrived. Any walk in the new room starts with a
	// click that writes it again, so clearing it here loses nothing.
	_script.setFlag(0xa644, 0);

	// The escape pod runs its own sequence as it opens, once the pod is ready
	// to leave; every other room, and the pod before then, does nothing here.
	startEnding();

	// And the lab does the same at the other end of the game: a new game opens
	// with the character talking to himself before the player gets the cursor.
	startOpening();

	// And the two mazes, whose dialog file the shared overlay's manifest names
	// wrongly and whose torch the cell may not have (maze.cpp).
	startMaze();

	// And room 46's swim, the same way (diving.cpp).
	startDiving();

	// And room 41's machines (shore.cpp), and room 32's way back from the
	// crystal (cemetery.cpp).
	startShore();
	startCemetery();

	// And the ladder between rooms 49 and 50, and room 56's return trip
	// (steam.cpp, teleport.cpp).
	startSteam();
	startTeleport();

	// And room 22's arrival scene, which is the room's own machine rather than
	// a lifted cutscene (park.cpp).
	startPark();
	startYodle();
	startForest();
	startObservatory();

	// And the ship's four elevator floors (corridor.cpp), the scanner's
	// robot (scanner.cpp).
	startCorridor();

	// And the rooms' own (rooms/), in the order rooms.cpp gives.
	for (Room *r : _rooms)
		r->start();

	// The shared room open clears [0xa6d3] once the room has placed Ben.
	// Done last so hand-written room opening handlers above still see it.
	_script.setFlag(0xa6d3, 0);

	// Update character scale for the initial position so the first frame
	// draws at the correct size.
	stepCharScale();

	// And the scenes the room raises on entry, which the loop plays once the
	// room's first frame is up: the original reaches them from entry 2, which
	// the transition does not call until the room is on the screen. Raising
	// them here would play them over the room being left, and the reload a
	// scene ends with would raise them a second time.
	_pendingCutscenes = true;

	// When the walk debug channel is enabled, print the resolved walk target
	// for every hotspot in the room. The 'g' key repeats it.
	if (debugChannelSet(-1, kDebugWalk))
		sweepWalkGeometry();
	if (debugChannelSet(-1, kDebugHotspots))
		dumpHotspots();
	if (debugChannelSet(-1, kDebugRooms))
		dumpExits();
	_secondPlate = secondPlate;
	_spriteFrame = 0;
	_dialogId = 1;
	_labelSlot = 0;
	_dirty = true;

	// And the room's light map, sampled once before anything is composed. The
	// original's sampler (OBJ:sub_069fd) is the first thing every overlay's
	// entry 2 does, so its answer is already in the palette when the room's
	// first frame reaches the screen; sampling only from the tick would leave
	// one frame of the character in whatever brightness the previous room had.
	stepLighting();

	// The elevator clip, on the way into one of the rooms the lift serves. The
	// original plays it from the top of those rooms' overlay entry 2, which is
	// the room's own opening code, so it runs after everything else is in place.
	for (uint i = 0; i < ARRAYSIZE(kLiftRooms); i++) {
		if (room != kLiftRooms[i])
			continue;
		// A debug run skips the clip but not its latch.
		if (gDebugLevel <= 0)
			playLift();
		else
			_script.setFlag(0x33de, 1);
		break;
	}

	updateScroll(true);
	// The rooms that hold the camera do it at the top of every pass of their
	// loop, the first one included, so the hold is in place before anything
	// asks whether the camera is still.
	stepParkHold();
	stepYodleHold();

	return true;
}

/**
 * The camera. Two routines in the original write [0xa0c4]:
 *
 * `CHARANIM:sub_13bce` *places* the character, and snaps the camera with him:
 * `[0xa0c4] = clamp([0xa8ec] - 0xa0, 0, [0xa0c0] - 0x140)`, measured from the
 * sprite origin (the walk point is ten pixels further on). Every place() does
 * that here, spotted through Walker::placements(), and so does `snap`.
 *
 * `LOGIC:sub_13015`, which every wide room's loop calls once a pass with no
 * gate, *pans* it. The x it makes for, [0xa0c2], only follows the character
 * while his sprite is within 0x41 of the left edge or past 0xeb; in between
 * it stays where it was, so he walks across the middle of the screen with the
 * camera still. It pans right while that x is more than 0xaa into the screen
 * and left while it is under 0x96, on a speed [0xa0cc] in 1/1024 px that grows
 * by 0x28 a pass, and coasts to a stop on the same 0x28 once the scroll has
 * moved ([0xa0ce]). A scene's hold, [0xa8e0]/[0xa8e2], just replaces the x.
 *
 * The loop never sleeps, so "a pass" is whatever the machine managed; the port
 * takes one per master tick. Narrow rooms always resolve to zero.
 */
/**
 * The rooms whose loop holds the camera every pass, just before the pan reads
 * [0xa8e0]/[0xa8e2]. Room 27's hold is
 * never off: the mansion is two views, the left edge while the character is
 * left of 0xf1 and the right edge past it (ovr_1b_0e7f:0x062a). Room 53's
 * holds only past x 0xc8, to the right edge (ovr_35_0f9e:0x1055, in the
 * overlay's entry 3, which is room 53's tick; room 57's has no hold).
 */
void AlienEngine::stepCameraHold() {
	const int x = _ben.spriteX();
	// A placement sets [0xa8f0] with [0xa8ec] (CHARANIM:sub_13bce): no crossing.
	if (_ben.placements() != _cameraPlacements) {
		_cameraPlacements = _ben.placements();
		_cameraLastX = x;
	}
	if (_room == 27)
		_scrollHold = x < 0xf1 ? 0 : 0x1f0;
	else if (_room == 53)
		_scrollHold = x > 0xc8 ? 0x1f4 : -1;
	else if (_room == 50 && _cameraLastX < 0xcd && x > 0xcc)
		// Room 50 does not hold; crossing x 0xcd to the right moves the x the
		// pan makes for to 0xdc (ovr_32_0f81:0x0711, [0xa8f0] against [0xa8ec]).
		_scrollFocus = 0xdc;
	_cameraLastX = x;
}

/**
 * Whether updateScroll's next pass would pan, which the original shows as
 * [0xa0cc] going non-zero on the pass itself: the moment the camera has been
 * placed (a room entered) it has not moved yet, but it is not at rest either.
 * A camera at the right cap does not count, since nothing moves there.
 */
bool AlienEngine::scrollWouldPan() const {
	if (_roomWidth <= kScreenWidth)
		return false;
	const int x = _ben.spriteX();
	int focus = _scrollFocus;
	if (x - _scrollX > 0xeb || x - _scrollX < 0x41)
		focus = x;
	if (_scrollHold >= 0)
		focus = _scrollHold;
	const int maxScroll = _roomWidth - kScreenWidth;
	return (focus - _scrollX > 0xaa && _scrollX < _roomWidth / 2 && _scrollX < maxScroll)
		|| (focus - _scrollX < 0x96 && _scrollX > 0);
}

void AlienEngine::updateScroll(bool snap) {
	if (_roomWidth <= kScreenWidth) {
		if (_scrollX != 0) {
			_scrollX = 0;
			_dirty = true;
		}
		_scrollPos = 0;
		_scrollPlacements = _ben.placements();
		return;
	}

	const int maxScroll = _roomWidth - kScreenWidth;
	const int32 maxPos = (int32)maxScroll << 10;
	const int x = _ben.spriteX();
	const int prev = _scrollX;

	if (snap || _ben.placements() != _scrollPlacements) {
		_scrollPlacements = _ben.placements();
		_scrollX = CLIP(x - kScreenWidth / 2, 0, maxScroll);
		_scrollPos = (int32)_scrollX << 10;
		_scrollFocus = x;
		if (_scrollX != prev)
			_dirty = true;
		return;
	}

	// A scene that puts the camera somewhere itself (hallway.cpp, lift.cpp...)
	// writes _scrollX alone; carry on from there rather than jump back.
	if ((_scrollPos >> 10) != _scrollX)
		_scrollPos = (int32)_scrollX << 10;
	if (debugChannelSet(2, kDebugTrace))
		traceEvent("cam %d %d %d %d %d %d", x, _scrollFocus, _scrollX, _scrollVel, _scrollPos, _scrollState);

	if (x - _scrollX > 0xeb || x - _scrollX < 0x41)
		_scrollFocus = x;
	if (_scrollHold >= 0)
		_scrollFocus = _scrollHold;

	// Right, but only from the left half of the room's width.
	if (_scrollFocus - _scrollX > 0xaa && _scrollX < _roomWidth / 2) {
		_scrollPos += _scrollVel;
		_scrollVel += 0x28;
		if (_scrollPos < 0)
			_scrollPos = 0;
		if (_scrollPos > maxPos) {
			_scrollPos = maxPos;
			_scrollVel = 0;
		}
		_scrollX = _scrollPos >> 10;
		if (_scrollVel > 0x400)
			_scrollVel = 0x400;
		// Whether or not the cap was hit: the jle over the clamp (0x11ea)
		// lands on this store, not past it. A pan pass always leaves the
		// state at 0xff, so the coast below only runs on a pass that did not
		// pan.
		_scrollState = 0xff;
	}

	// Left. A speed still going right is braked twice over.
	if (_scrollFocus - _scrollX < 0x96 && _scrollX > 0) {
		_scrollPos += _scrollVel;
		if (_scrollPos < 0)
			_scrollPos = 0;
		_scrollX = _scrollPos >> 10;
		_scrollVel -= 0x28;
		if (_scrollVel > 0)
			_scrollVel -= 0x28;
		if (_scrollVel < -0x400)
			_scrollVel = -0x400;
		// 0x1268's jge lands on the store too. Leaving the state at 0 here
		// let the coast undo each pass's -0x28 the moment the camera had
		// moved a pixel, so a pan left from rest stopped one pixel in
		_scrollState = 0xff;
	}

	// Coasting after a move right...
	if (_scrollState == 1) {
		_scrollVel -= 0x28;
		if (_scrollVel < 0x100)
			_scrollVel = 0;
		if (_scrollVel > 0x104) {
			_scrollPos += _scrollVel;
			if (_scrollPos > maxPos)
				_scrollPos = maxPos;
			_scrollX = _scrollPos >> 10;
		}
	}

	// ...or after one left.
	if (_scrollState == 0) {
		_scrollVel += 0x28;
		if (_scrollVel > -0x100)
			_scrollVel = 0;
		if (_scrollVel < -0x104) {
			_scrollPos += _scrollVel;
			if (_scrollPos < 0)
				_scrollPos = 0;
			_scrollX = _scrollPos >> 10;
		}
	}

	if (_scrollX > prev)
		_scrollState = 1;
	else if (_scrollX < prev)
		_scrollState = 0;

	if (_scrollX != prev)
		_dirty = true;
}

void AlienEngine::loadSpriteBank(uint bank) {
	// One bank at a time: which of a room's banks are on screen, and at which
	// frame, is what the room's own code decides, and none of that is ported
	// yet, so drawing them all at once would only pile doors on top of doors.
	_sprite.unload();
	_spriteBank = bank;
	_spriteFrame = 0;
	_dirty = true;

	if (bank >= _assets.spriteCount)
		return;

	if (!_sprite.load(Common::Path(_assets.sprites[bank])))
		return;

	debugC(1, kDebugResource, "sprite bank %u/%u: %s, %u frames", bank + 1,
		   _assets.spriteCount, _assets.sprites[bank].c_str(), _sprite.frameCount());
}

void AlienEngine::stepSpriteBank(int delta) {
	if (!_assets.spriteCount)
		return;

	const int count = (int)_assets.spriteCount;
	int bank = ((int)_spriteBank + delta) % count;
	if (bank < 0)
		bank += count;
	loadSpriteBank((uint)bank);
}

void AlienEngine::hideCharacter(uint slot) {
	if (slot != kNoCharacterSlot && slot < AnimSlots::kSlotCount)
		_characterAnimSlots |= (uint16)(1 << slot);

	_drawCharacter = false;
	_dirty = true;
}

void AlienEngine::playCharacterAnim(uint slot, int first, int count, int rate, int mode) {
	hideCharacter(slot);
	_anims.play(slot, first, count, rate, mode);
	debugC(1, kDebugGraphics, "character anim: slot %u has him for %d frames", slot, count);
}

void AlienEngine::showCharacter() {
	for (uint i = 0; i < AnimSlots::kSlotCount; i++) {
		if (_characterAnimSlots & (1 << i))
			_anims.takeDown(i);
	}
	_characterAnimSlots = 0;

	_drawCharacter = true;
	_dirty = true;
}

void AlienEngine::walkTo(int x, int y, int arrivalFacing) {
	// Room 46 has no mask and no nodes: every walk is a swim in a straight
	// line (diving.cpp).
	if (divingSwimTo(x, y, arrivalFacing))
		return;
	// Room 41's two ledges have no nodes and no mask of their own (shore.cpp).
	if (shoreWalkTo(x, y, arrivalFacing))
		return;

	if (!_walk.plotRoute(_ben.walkX(), _ben.walkY(), x, y, _route)) {
		// The two mazes have no mask to route through and move him anyway, the
		// way the original's forced routes do (maze.cpp), and room 60's entry 0
		// walks him straight to the click (boss.cpp), as the two hallways' and
		// the jail's walk geometry do with no mask behind it (corridor.cpp,
		// jail.cpp).
		for (Room *r : _rooms)
			if (r->walkUnrouted(x, y, arrivalFacing))
				return;
		if (mazeWalkTo(x, y, arrivalFacing) || hallwayWalkTo(x, y, arrivalFacing))
			return;

		debugC(1, kDebugGraphics, "walk to %d,%d: room %d has no walk mask", x, y, _room);
		return;
	}

	debugC(1, kDebugGraphics, "walk %d,%d -> %d,%d: %u waypoints, target %s, facing %d",
		   _ben.walkX(), _ben.walkY(), x, y, _route.count,
		   _walk.mask().blocked(x, y) ? "blocked" : "walkable", arrivalFacing);

	_ben.setLedgePace(_script.flag(0xa73a) == 1);
	_ben.follow(_route, x, y, arrivalFacing);
	_dirty = true;
}

void AlienEngine::straightWalkTo(int x, int y, int arrivalFacing) {
	_route.count = 0;
	_route.points[_route.count].x = (int16)_ben.walkX();
	_route.points[_route.count].y = (int16)_ben.walkY();
	_route.count++;
	_route.points[_route.count].x = (int16)x;
	_route.points[_route.count].y = (int16)y;
	_route.count++;

	debugC(1, kDebugGraphics, "straight walk %d,%d -> %d,%d facing %d",
		   _route.points[0].x, _route.points[0].y, x, y, arrivalFacing);
	_ben.setLedgePace(_script.flag(0xa73a) == 1);
	_ben.follow(_route, x, y, arrivalFacing);
	_dirty = true;
}

/**
 * 1021:sub_1023c, the hand-built route.
 *
 * The antique store has no walk mask, so nothing can be routed through it: the original
 * writes two points into the walk arrays itself and lets the walker run them.
 * The port's walker takes a route the same way, with the character's own
 * position as the first point and the target appended by `follow`.
 */
void AlienEngine::walkHandRoute(int viaX, int viaY, int x, int y, int facing) {
	WalkRoute route;
	route.points[0].x = (int16)_ben.walkX();
	route.points[0].y = (int16)_ben.walkY();
	route.points[1].x = (int16)viaX;
	route.points[1].y = (int16)viaY;
	route.count = 2;
	_ben.follow(route, x, y, facing);
	_dirty = true;

	debugC(1, kDebugRooms, "store: walks %d,%d -> %d,%d -> %d,%d facing %d",
		   _ben.walkX(), _ben.walkY(), viaX, viaY, x, y, facing);
}

/**
 * The room's background plate.
 *
 * Room 7 is the one room that swaps its whole set on [0xa6fa]: its loader
 * (ovr_07_0e63:0x3f) takes the X names -- GAME7X/MSCR7X/FADE7X, the bedroom
 * with the light off -- while the flag is clear, and the plain ones once the
 * light switch sets it (bedroom.cpp). The room is entered in the dark, so the
 * table's own name is the exception here rather than the rule. Every other room
 * takes the table entry.
 */
void AlienEngine::rebuildHotspots(int room) {
	// The rectangles the room registers, by running entry 1 of its overlay as
	// Which ones exist depends on the puzzle
	// state, so this runs on the way in and again whenever a click has moved the
	// state.
	_script.buildHotspots(room < 0 ? _room : room, _spots);

	// The two mazes are the exception: theirs are per cell rather than per flag,
	// which is a guard the lift cannot carry, so the maze replaces the table
	// outright (maze.cpp).
	buildMazeHotspots(room < 0 ? _room : room);

	// And the rooms' own, which the lift cannot carry either: room 58's two
	// prisoners, behind a computed guard and in a rectangle that moves
	// (jailguard.cpp).
	for (Room *r : _rooms)
		r->buildHotspots(room < 0 ? _room : room);

	// The table can be shorter than it was -- an item taken out of it, or a
	// maze cell with fewer ways out than the last -- and what was hovered is an
	// index into it. updateHover recomputes it on the next mouse move, but not
	// while a machine has the cursor, so drop it here rather than leave it
	// pointing past the end.
	if (_hover >= (int)_spots.size()) {
		_hover = -1;
		_labelSlot = 0;
	}
}

Common::String AlienEngine::roomPlate(int room) const {
	if (room == 7 && !_script.flag(0xa6fa))
		return "GAME7X.PCX";

	// Room 46's cave is loaded as room 47: the overlay writes handler code
	// 0x2f while the plates are read (diving.cpp).
	if (room == kSwimRoom && _script.flag(0xa785) == 1)
		return _tables.background(47);

	// Rooms 43 and 44, the two mazes: MAIN's own dispatch sends each to a
	// resident helper rather than to the overlay directly (roommap.py:
	// "MAIN:sub_00000" and "MAIN:sub_0008c"), which is why the table this reads
	// has nothing for either -- whatever built it walks each room's own overlay
	// entry point. Neither has a plate at all in the original either: the screen
	// is composed piece by piece from the cell, which mazeBackground() does
	// (maze.cpp). FADE43.PCX, what both entry points open on before their first
	// composition (the literal pools at 0x570 and 0x84e), is the answer here so
	// that a missing MAZBLK still leaves a room that loads -- without one,
	// booting into either hangs the headless run in the engine's own "could not
	// load the starting room" error path.
	if (room == 43 || room == 44)
		return "FADE43.PCX";

	return _tables.background(room);
}

/**
 * The room's foreground sheet: the page the original keeps at [0xd136].
 *
 * A narrow room's sheet is the second-plate table entry -- which is what the
 * table holds for it, mscr<n>.pcx -- while a wide room's entry is its plate B,
 * and the sheet is named by the room's own overlay instead. Room 7's sheet
 * follows [0xa6fa] the same way its plate does, MSCR7X.PCX while the light is
 * off and MSCR7.PCX once it is on.
 */
Common::String AlienEngine::occluderPlate(int room) const {
	if (room == 7)
		return _script.flag(0xa6fa) ? "MSCR7.PCX" : "MSCR7X.PCX";

	// And its foreground sheet with it, the row for room 47.
	if (room == kSwimRoom && _script.flag(0xa785) == 1)
		return _tables.secondPlate(47);

	const Common::String &second = _tables.secondPlate(room);
	if (second.hasPrefixIgnoreCase("mscr"))
		return second;

	return Common::String::format("MSCR%d.PCX", room);
}

void AlienEngine::loadOccluder(int room) {
	_occluder.free();

	uint count = 0;
	if (!occlusionRects(room, count))
		return;			// nothing in this room the character can walk behind

	Graphics::Surface sheet;
	byte palette[256 * 3];
	const Common::String name = occluderPlate(room);
	if (!loadGamePCX(Common::Path(name), sheet, palette)) {
		sheet.free();
		warning("room %d: could not load the foreground sheet %s", room, name.c_str());
		return;
	}

	// The sheet's own palette is not loaded: the original reads it as indices
	// into the room's, which is why the pieces match the plate they came from.
	_occluder = sheet;
	debugC(1, kDebugOcclusion, "room %d: foreground sheet %s, %dx%d, %u rectangles",
		   room, name.c_str(), _occluder.w, _occluder.h, count);
}

/**
 * Stamp the room's foreground back over the character, as OBJ:sub_03400 does.
 *
 * Called with the character already drawn, once per rectangle the room's tick
 * lists. Only the part of a rectangle the character reaches into is copied --
 * the original tests his bounding box first and clips to it -- so an animation
 * slot playing under the same foreground keeps whatever it drew.
 */
void AlienEngine::applyOcclusion() {
	uint count = 0;
	const OcclusionRect *rects = occlusionRects(_room, count);
	if (!count || !_occluder.getPixels())
		return;

	Common::Rect ben;
	if (!_ben.bounds(ben))
		return;

	for (uint i = 0; i < count; i++) {
		const OcclusionRect &rect = rects[i];

		if (rect.benYBelow >= 0 && _ben.spriteY() >= rect.benYBelow)
			continue;

		uint guardCount = 0;
		const ScriptCond *guards = occlusionGuards(rect, guardCount);
		bool guarded = false;
		for (uint g = 0; g < guardCount; g++)
			guarded |= (_script.flag(guards[g].addr) == guards[g].value) == guards[g].negate;
		if (guarded)
			continue;

		Common::Rect area(rect.dstX, rect.dstY,
						  rect.dstX + rect.width, rect.dstY + rect.height);
		area.clip(ben);
		if (area.isEmpty())
			continue;

		// The blit moves whole words, so an odd overlap loses its last column
		// rather than rounding up. Keeping that is the difference between the
		// port's edges and the original's.
		const int width = area.width() & ~1;
		if (width <= 0)
			continue;

		for (int y = 0; y < area.height(); y++) {
			const int srcY = rect.srcY + (area.top - rect.dstY) + y;
			const int dstY = area.top + y;
			if (srcY < 0 || srcY >= _occluder.h || dstY < 0 || dstY >= _screen.h)
				continue;

			const byte *in = (const byte *)_occluder.getBasePtr(0, srcY);
			byte *out = (byte *)_screen.getBasePtr(0, dstY);
			for (int x = 0; x < width; x++) {
				const int srcX = rect.srcX + (area.left - rect.dstX) + x;
				const int dstX = area.left + x - _scrollX;
				if (srcX < 0 || srcX >= _occluder.w || dstX < 0 || dstX >= _screen.w)
					continue;
				if (in[srcX])
					out[dstX] = in[srcX];
			}
		}

		debugC(2, kDebugOcclusion, "room %d: rect %u covers %d,%d..%d,%d of the character",
			   _room, i, area.left, area.top, area.right, area.bottom);
	}
}

void AlienEngine::checkExit() {
	// OBJ:sub_078dd, the half of the exit mechanism the walk geometry does not
	// hold: an armed submode fires only once the route has run out, the arrival
	// turn has played, and the feet are within two pixels of the point the
	// geometry named (`walk_pos - 3 < feet < walk_pos + 3`, 0251:53fc-0x5442). Anything else -- he was interrupted, or the click sent him
	// somewhere the router could not reach exactly -- leaves the exit armed.
	if (!_armed || _speech)
		return;

	// Two overlays have no call to it at all -- room 13 and room 46 -- and both
	// answer an arrival with an animation of their own and write game_submode
	// out of how far it has got instead (basement.cpp). Firing the shared test
	// there would take the room before its climb had played.
	if (!roomHasSharedExit(_room))
		return;

	if (ABS(_ben.walkX() - _armedX) > 2 || ABS(_ben.walkY() - _armedY) > 2)
		return;

	// The facing test is a plain comparison in the original, against a global
	// that holds 10 when no turn was owed -- and the character's own facing is
	// only ever 1..4, so an exit whose approach point names no facing can never
	// fire. Two rooms arm one of those; the original cannot take them either.
	if (_ben.facing() != (int)_armedFacing)
		return;

	const byte submode = _armed;
	_armed = 0;

	// Room 7's closet answers its arrival with a line and a walk of its
	// own before the exit is written (bedroom.cpp).
	if (hijackBedroomLift(submode))
		return;

	// And room 19's stairs, which play the climb before the room is left
	// (observatory.cpp).
	if (hijackObservatoryStairs(submode))
		return;

	// And the rooms' own (rooms/): room 54's tick tail sees the exit armed but
	// not yet taken, and can drop it for a machine of its own (waiting.cpp).
	for (Room *r : _rooms)
		if (r->hijackExit(submode))
			return;

	takeExit(submode);
}

bool AlienEngine::takeExit(byte submode) {
	// Except in the two mazes, where an armed submode is a turn rather than a
	// door: the game answers it out of the cell table and re-enters the
	// same room, and only four of them ever reach the chain at all (maze.cpp).
	if (mazeExit(submode))
		return true;

	// The room's tick loop ends here, and ending it is what sets game_mode to
	// the room being left (OBJ:sub_0879a). The main loop then walks its chain of
	// check_event() calls with that pair.
	_mode = (byte)_room;
	_lastSubmode = submode;

	// sub_0879a also drops the door edges every ship room keeps in [0xa7a2]-
	// [0xa7a5] (0251:62ec), so a door left open behind him is not shut again
	// by the next room's tick; the room's init raises the one he comes
	// through.
	for (uint16 addr = 0xa7a2; addr <= 0xa7a5; addr++)
		_script.setFlag(addr, 0);

	const int room = _script.nextRoom(_mode, submode);
	if (!room) {
		debugC(1, kDebugRooms, "exit: room %d submode %u has no link in the chain",
			   _mode, submode);
		return false;
	}

	debugC(1, kDebugRooms, "exit: room %d submode %u -> room %d", _mode, submode, room);

	// Submode 111 names the room itself: a close-up or a cutscene that comes
	// back to where it started, and the original reloads it too. The only
	// thing to get right on a self-reload is the character: it names no
	// char_place of its own, so without keepPosition he would otherwise land
	// on the room's first walk node (for instance room 28's telescope and
	// monitor -- telescope.cpp claims that pair before this ever runs, but the
	// game's other bare submode-111 rows, rooms 3/15/22/26/52 among them,
	// still fall through to here).
	if (!loadRoom(room, false, room == _mode)) {
		debugC(1, kDebugRooms, "exit: room %d would not load, staying in %d",
			   room, _mode);
		return false;
	}

	return true;
}

} // End of namespace Alien
