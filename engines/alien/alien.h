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
#include "common/serializer.h"
#include "engines/engine.h"
#include "graphics/surface.h"

#include "alien/anim.h"
#include "alien/charanim.h"
#include "alien/dl1.h"
#include "alien/font.h"
#include "alien/hotspots.h"
#include "alien/inventory.h"
#include "alien/overlay.h"
#include "alien/play.h"
#include "alien/s3m.h"
#include "alien/script.h"
#include "alien/sfx.h"
#include "alien/tables.h"
#include "alien/tal.h"
#include "alien/video.h"
#include "alien/walk.h"

struct ADGameDescription;

namespace Alien {

class AlienEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;

	/// Object ids are bytes, and the outcome rotation is kept per object.
	static const uint kObjectCount = 256;

	// docs/dialog_system.md 2: the auto-dismiss countdown is three per character
	// of text, floored at 0x46. It is decremented under the tick pair gate rather
	// than the animation one, so it runs at half the master rate, not a quarter
	// of it. A cutscene's lines are timed the same way, so both files need them.
	static const int kTicksPerCharacter = 3;
	static const int kMinSpeechTicks = 0x46;

	AlienEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~AlienEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;

	const ADGameDescription *_gameDescription;
	const char *getGameId() const;
	Common::Language getLanguage() const;

	/**
	 * Start a music slot, as INPUT:music_play_slot does.
	 *
	 * Public because a room's own script starts its theme: the interpreter runs
	 * a kOpMusic effect straight out of the room's lifted table (see script.h).
	 */
	void playMusicSlot(uint slot);

private:
	bool loadRoom(int room, bool secondPlate = false);
	void stepRoom(int delta);
	static int roomWidth(int room);
	static const char *charPaletteFile(int room);
	void applyCharPalette(int room);
	void updateScroll();
	void loadSpriteBank(uint bank);
	void stepSpriteBank(int delta);
	void redraw();
	void handleEvents();
	void dumpScreen(const Common::String &name = Common::String());

	bool loadPlayScript(const Common::String &path);
	void stepPlayScript();
	bool playIdle() const;
	void runPlayCommand(const PlayCommand &cmd);

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
	void dumpSfx();
	void syncGame(Common::Serializer &s);
	byte *readDosSave(const Common::String &file);
	void applyDosItems(const byte *block);
	bool importDosSave(const Common::String &file, bool apply);
	void dumpSaves();
	void checkSaveRoundTrip();
	void checkDosItemImport();
	void stopMusic();
	void dumpCutscenes();
	void sweepCutscenes();
	void dumpOcclusion();
	Common::String roomPlate(int room) const;
	Common::String occluderPlate(int room) const;
	void loadOccluder(int room);
	void reloadPlates();
	void bedroomSwitch(int anchorX, int anchorY);
	bool isBedroomSwitch(int obj) const;
	void applyOcclusion();
	bool triggerCutscene(byte id);
	void playCutsceneRecord(uint number);
	void runCutsceneProc(uint proc);
	void speakCutsceneLine(uint id, int anchorX, int anchorY);
	void dumpMusic();
	void sweepMusicCues();
	void sweepMusicRows();
	void renderMusic();
	void playVideo(Video::VideoDecoder &video, CDA2Decoder *subtitles = nullptr);
	void drawSubtitle(const CDA2Decoder &video, const byte *palette);
	uint subtitleLanguage() const;
	void startEnding();
	void armEnding();
	void stepEnding();
	void speakEnding(byte code);
	bool endingLineDone() const;
	void winGame();
	bool playLift();
	bool playCutscene(const char *file);
	void dumpVideo();
	void sweepVideoFrames();
	void sweepSubtitles();
	void sweepSounds();
	void sweepVoices();
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
	Graphics::Surface _occluder;	///< the room's MSCR sheet, its foreground pieces
	byte _palette[256 * 3];

	DL1Sprite _sprite;
	uint _spriteFrame;
	uint _spriteBank;				///< index into the room's manifest

	StaticTables _tables;
	OverlayIndex _overlays;
	RoomAssets _assets;
	int _room;
	bool _secondPlate;				///< showing the room's B plate rather than A
	int _roomWidth;					///< room's total pixel width; 320 unless wide (see [0xa0c0])
	int _scrollX;						///< live horizontal scroll offset (see [0xa0c4], sub_13bce)

	RoomScript _script;			///< the room's own reaction to a click
	AnimSlots _anims;			///< the room's DL1 banks and what is playing on them

	/// The resident sample bank, the three voices and the delay queue.
	SoundFX _sound;

	/// The module playing now, if any, and the mixer channel it is on. One track
	/// is resident at a time, the way MIDAS holds one module.
	S3MModule _music;
	Audio::SoundHandle _musicHandle;
	int _musicSlot;

	/// [0x33de]: the elevator clip plays once per session, on the first entry
	/// into one of the rooms the lift serves, and never again.
	bool _liftPlayed;

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

	/// Room 59's escape-pod sequence: its step [0xa49f], the timeline [0xa49c]
	/// one step waits on, the slot-4 cycle [0xa53e] leaves running, and the win
	/// flag [0x7dc5] the last step sets (ending.cpp).
	byte _endingStep;
	uint _endingPos;
	bool _endingLoop;
	bool _won;

	/// A cutscene owns the screen: no character, no inventory bar, no hover name.
	bool _cutscene;

	/// The sweep plays the scenes with the clock taken out: same step
	/// interpreter, same debug lines, no waiting on holds or on lines being read.
	bool _cutsceneFast;

	/// The scripted playthrough (milestone S), if --debugflags=play named one
	/// through the ALIEN_PLAY_SCRIPT environment variable.
	PlayScript _play;
	uint _playIndex;
	bool _playActive;
	uint32 _playLastTick;			///< the master tick stepPlayScript last acted on
	int _playWaitTicks;			///< ticks still to burn before the next command
	int _playSettleTimeout;		///< ticks left before a "settle" gives up
	bool _playSettling;
	uint _playFails;				///< number of failed "expect" assertions so far
};

} // End of namespace Alien

#endif
