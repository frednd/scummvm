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

#ifndef ALIEN_ALIEN_H
#define ALIEN_ALIEN_H

#include "common/language.h"
#include "engines/engine.h"
#include "graphics/surface.h"

#include "alien/anim.h"
#include "alien/charanim.h"
#include "alien/dl1.h"
#include "alien/font.h"
#include "alien/hotspots.h"
#include "alien/inventory.h"
#include "alien/overlay.h"
#include "alien/script.h"
#include "alien/tables.h"
#include "alien/tal.h"
#include "alien/walk.h"

struct ADGameDescription;

namespace Alien {

class AlienEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;

	/// Object ids are bytes, and the outcome rotation is kept per object.
	static const uint kObjectCount = 256;

	AlienEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~AlienEngine() override;

	Common::Error run() override;

	const ADGameDescription *_gameDescription;
	const char *getGameId() const;
	Common::Language getLanguage() const;

private:
	bool loadRoom(int room, bool secondPlate = false);
	void stepRoom(int delta);
	void loadSpriteBank(uint bank);
	void stepSpriteBank(int delta);
	void redraw();
	void handleEvents();
	void dumpScreen();

	void walkTo(int x, int y, int arrivalFacing = Walker::kFacingKeep);
	void sweepWalkGeometry();
	void dumpHotspots();
	void stepClock();
	void drawWalkOverlay();

	void updateHover(int x, int y);
	bool clickBar(int x, int y, bool rightButton);
	void holdItem(byte item);
	void lookAtItem(byte item);
	void dumpItems();
	void sweepItemLooks();
	void dumpItemUses();
	void sweepClicks();
	void checkExit();
	bool takeExit(byte submode);
	void dumpExits();
	bool takeFirstExit();
	bool takeAnyExit(const int *avoid, uint avoidCount);
	bool arriveAt(const WalkTarget &target);
	void tourRooms();
	void clickAt(int x, int y, bool rightButton = false);
	byte rotateOutcome(const Hotspot &spot);
	void finishAction();
	void queueOutcome(const TalFile &tal, byte code, int anchorX, int anchorY);
	void nextSpeech();
	void stopSpeech();

	void setTextColor(byte r, byte g, byte b);
	void drawLabel();
	void drawSpeech(const TalFile::Entry &entry, int anchorX, int anchorY);
	void drawBand(const TalFile::Entry &entry);
	void showDialog(uint id);

	Graphics::Surface _screen;		///< 320x200 staging buffer, 8bpp
	Graphics::Surface _background;	///< the room plate as decoded
	byte _palette[256 * 3];

	DL1Sprite _sprite;
	uint _spriteFrame;
	uint _spriteBank;				///< index into the room's manifest

	StaticTables _tables;
	OverlayIndex _overlays;
	RoomAssets _assets;
	int _room;
	bool _secondPlate;				///< showing the room's B plate rather than A

	RoomScript _script;			///< the room's own reaction to a click
	AnimSlots _anims;			///< the room's DL1 banks and what is playing on them

	Walk _walk;
	WalkRoute _route;
	bool _showWalk;					///< draw the mask, the node ring and the route

	Walker _ben;					///< the player character walking that route
	uint32 _lastTick;				///< when the master clock last advanced
	uint32 _tick;					///< master ticks since the engine started

	/// The room's registered rectangles as its overlay's entry 1 last built
	/// them, and which one the cursor is over.
	Common::Array<Hotspot> _spots;
	int _hover;

	/// Per object, how far its outcomes have been rotated through. The original
	/// keeps this inside the saved state, indexed by object id across rooms.
	byte _outcomeCounter[kObjectCount];

	/// The hotspot whose outcome fires once the character has walked over, and
	/// the code the rotation picked when it was clicked.
	int _pending;
	byte _pendingOutcome;

	/// The exit the last click armed, and the point and facing it fires at.
	/// OBJ:sub_078dd tests all three every tick: the route has to have run out,
	/// the arrival turn to have played, and the feet to be within three pixels.
	byte _armed;
	int _armedX;
	int _armedY;
	byte _armedFacing;

	/// game_mode [0xa880]: the room the character came *from*. The original sets
	/// it as a room's tick loop ends, so the pair the transition chain matches
	/// on is (the room being left, the submode its exit armed).
	byte _mode;

	/// What the player carries, the bar it is shown in, and which part of that
	/// bar the cursor is over.
	Inventory _inventory;
	int _hoverSlot;
	Inventory::Arrow _hoverArrow;

	/// [0xa6bb]: the item picked out of the bar and not yet used on anything.
	/// While it is set the status line reads "USE <item> WITH <object>" and a
	/// click on an object is an item use rather than the object's own verb.
	byte _heldItem;

	/// The item the click being resolved is using, kept apart from _heldItem so
	/// the hand is empty again as soon as the click is spent.
	byte _pendingItem;

	/// The chain of dialog ids an outcome expanded into, played one at a time.
	byte _queue[TalFile::kMaxOutcomeIds];
	uint _queueCount;
	uint _queueNext;

	Font _font;
	Font _labelFont;				///< the shorter face the status line is set in
	TalFile _tal;
	TalFile _labels;				///< NAMEROOM/<lang>/R<n>.TAL, the hover names
	TalFile _talkall;				///< TALKALL.TAL, the answers no room owns
	const TalFile *_speechTal;		///< which of the two the queue came out of
	uint _labelSlot;
	uint _dialogId;
	bool _dialogBand;				///< bottom band layout instead of over the speaker
	bool _speech;					///< a line is on screen
	int _speechTicks;				///< half ticks left before it clears, 0 = no limit
	int _speechX;					///< anchor: the speaking object's bbox midpoint
	int _speechY;

	bool _dirty;
	bool _quit;
};

} // End of namespace Alien

#endif
