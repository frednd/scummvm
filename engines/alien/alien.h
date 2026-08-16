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

#include "alien/charanim.h"
#include "alien/dl1.h"
#include "alien/font.h"
#include "alien/overlay.h"
#include "alien/tables.h"
#include "alien/tal.h"
#include "alien/walk.h"

struct ADGameDescription;

namespace Alien {

class AlienEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;

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

	void walkTo(int x, int y);
	void stepAnimation();
	void drawWalkOverlay();

	void setTextColor(byte r, byte g, byte b);
	void showLabel(uint slot);
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

	Walk _walk;
	WalkRoute _route;
	bool _showWalk;					///< draw the mask, the node ring and the route

	Walker _ben;					///< the player character walking that route
	uint32 _lastTick;				///< when the animation clock last advanced

	Font _font;
	Font _labelFont;				///< the shorter face the status line is set in
	TalFile _tal;
	TalFile _labels;				///< NAMEROOM/<lang>/R<n>.TAL, the hover names
	uint _labelSlot;
	uint _dialogId;
	bool _dialogBand;				///< bottom band layout instead of over the speaker

	bool _dirty;
	bool _quit;
};

} // End of namespace Alien

#endif
