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
#include "alien/chat.h"
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

	/// The row the playfield ends on, and so the value OBJ:sub_08567 puts in
	/// [0xa8e4] as every room opens: the DL1 blitter draws nothing at or below
	/// it (see DL1Sprite::drawFrame and _clipBottom).
	static const int kPlayfieldBottom = 0x9b;

	/// How far down the character the sewer's water stands before it is
	/// drained, the value GAME.EXE ships in [0x4382] (sewer.cpp).
	static const uint16 kSewerDepthStart = 0x32;

	/// Object ids are bytes, and the outcome rotation is kept per object.
	static const uint kObjectCount = 256;

	// docs/dialog_system.md 2: the auto-dismiss countdown is three per character
	// of text, floored at 0x46. It is decremented under the tick pair gate rather
	// than the animation one, so it runs at half the master rate, not a quarter
	// of it. A cutscene's lines are timed the same way, so both files need them.
	static const int kTicksPerCharacter = 3;
	static const int kMinSpeechTicks = 0x46;

	/// And the mouth stops with this many of those half ticks left, OBJ:0x86df.
	static const int kTalkStopTicks = 0x19;

	/// One master tick in milliseconds: the ~70 Hz retrace, scaled the way
	/// alien.cpp explains. Here because the palette fades wait on it directly
	/// rather than through the game loop (fade.cpp).
	static const uint32 kMasterTickMillis = 1000 * 2 / (70 * 3);

	/// [0xac1e], how long a click's own line stands before the hover answers
	/// again: 0x8c frames for a verb (1021:0x897) and, for a walk, 0xbb8 --
	/// long enough that in practice the arrival is what ends it (1021:0xa16).
	static const uint kLabelHoldVerb = 0x8c;
	static const uint kLabelHoldWalk = 0xbb8;

	/// The one room whose left click reads "Swim to", 1021:0x9be.
	static const int kSwimRoom = 46;

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

	/// game_mode and game_submode as the last transition left them. Public for
	/// the same reason: a guard lifted on [0xa880] or [0xa87e] is answered from
	/// here rather than from the state block (see RoomScript::holds).
	byte gameMode() const { return _mode; }
	byte gameSubmode() const { return _lastSubmode; }

private:
	bool loadRoom(int room, bool secondPlate = false);
	void stepRoom(int delta);
	static int roomWidth(int room);
	const char *charPaletteFile(int room) const;
	void applyCharPalette(int room);

	// lighting.cpp: the room's FADE plate, and the character's own palette
	// entries it scales.
	const char *lightMapFile(int room, bool second) const;
	void loadLightMap(int room);
	void freeLightMap();
	void keepCharPalette(int room, const byte *palette);
	void uploadCharPalette(bool alt);
	void stepLighting();
	void dumpLighting();
	void sweepLighting();
	void updateScroll();
	void loadSpriteBank(uint bank);
	void stepSpriteBank(int delta);
	void redraw();
	void uploadPalette(const byte *source, int level);
	void fadeOut();
	void fadeIn();
	bool loadCursor();
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
	void drawSpotOverlay();

	void updateHover(int x, int y);
	bool clickBar(int x, int y, bool rightButton);

	/// Whether the bar's save button does anything in the room on show.
	bool menuAllowed() const;
	void holdItem(byte item);
	void lookAtItem(byte item);
	void dumpItems();
	void dumpSfx();
	void syncGame(Common::Serializer &s);
	byte *readDosSave(const Common::String &file);
	byte *readDosThumbnail(const Common::String &file, int &width, int &height);
	int thumbnailRoom(const byte *thumb, int feetX, int feetY, int &score);
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
	void roomCutscenes(int room);
	void stepCutsceneTimers();
	void tickCutsceneTimers();
	uint32 cutsceneTimer(uint16 addr) const;
	void playCutsceneRecord(uint number);
	void runCutsceneProc(uint proc, bool quiet = false);
	void speakCutsceneLine(uint code, int anchorX, int anchorY);
	bool nextCutsceneLine();
	void dumpMusic();
	void sweepMusicCues();
	void sweepMusicRows();
	void renderMusic();
	void playVideo(Video::VideoDecoder &video, CDA2Decoder *subtitles = nullptr);
	void drawSubtitle(const CDA2Decoder &video, const byte *palette);
	uint subtitleLanguage() const;
	void startEnding();
	void armEnding();
	void startOpening();
	void stepOpening();
	void stepRoomClock();
	void armLab(int obj, bool item);
	void stepLab();
	void stepLabHole();
	void stepHallway();
	void playPeephole();

	void armSewer(int obj, byte verb);
	void enterSewer();
	void openRoomPlate(int room);
	void dumpPlates();
	static Common::String argText(uint16 arg);
	void stepSewer();
	void sewerValve();
	void cancelOpening();
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
	void speakEntry(const TalFile::Entry &entry, int anchorX, int anchorY, int ticks);
	bool speechDone() const { return !_speech && _queueNext >= _queueCount; }

	void sweepChatTrees();
	void openChat(uint topic);
	void stepChat();

	bool armLibrary(int obj, byte verb, bool item, int anchorX, int anchorY);
	void stepLibrary();
	void nextSpeech();
	void stopSpeech();

	void setTextColor(byte r, byte g, byte b);
	void uploadTextColor();
	/// The ambient speaker colour of the rooms that set one (alien.cpp).
	void stepTextColor();
	void characterAnchor(int &x, int &y) const;
	Common::String labelText(int x, int y) const;
	Common::String hoverName() const;
	void setClickLabel(const Common::String &verb, uint hold);
	void refreshLabel(int x, int y);
	void resetLabelColors();
	void stepLabelFade();
	void applyLabelColors();
	void drawLabel();
	void drawSpeech(const TalFile::Entry &entry, int anchorX, int anchorY);
	void drawBand(const TalFile::Entry &entry);
	void showDialog(uint id);

	Graphics::Surface _screen;		///< 320x200 staging buffer, 8bpp
	Graphics::Surface _background;	///< the room plate as decoded
	Graphics::Surface _occluder;	///< the room's MSCR sheet, its foreground pieces
	byte _palette[256 * 3];

	/// The room's light map, its left half and -- when it has one -- its right,
	/// 320x150 of brightness bytes each (lighting.cpp).
	byte *_lightMap[2];

	/// The character's own palette entries 1..24 as the room loaded them, before
	/// the light level scales them, plus room 49's second set.
	byte _charPalette[24 * 3];
	byte _charPaletteAlt[24 * 3];
	bool _charPaletteAltLoaded;

	/// [0x7d9c] and [0x7d9d]: the level the map yielded this frame and last.
	byte _lightLevel;
	byte _lightPrev;

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
	bool _showSpots;				///< outline the hotspots entry 1 registered

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

	/// The first screen row a DL1 blit must not touch, the original's [0xa8e4]
	/// (see DL1Sprite::drawFrame). Room init puts back 0x9b, the row the
	/// playfield ends on; the only room that moves it is the sewer, whose water
	/// line cuts the character and its own slots off at the surface.
	int _clipBottom;

	/// game_mode [0xa880]: the room the character came *from*. The original sets
	/// it as a room's tick loop ends, so the pair the transition chain matches
	/// on is (the room being left, the submode its exit armed).
	byte _mode;

	/// game_submode [0xa87e] as that same moment leaves it: the exit taken. A
	/// room reads the pair back on entry -- room 35 plays a different scene
	/// depending on which way in it was.
	byte _lastSubmode;

	/// What the player carries, the bar it is shown in, and which part of that
	/// bar the cursor is over.
	Inventory _inventory;
	int _hoverSlot;
	Inventory::Arrow _hoverArrow;
	bool _hoverMenu;

	/// [0xa821]: a click on the bar's save button, waiting for the top of the
	/// next frame. The original polls the flag from the room's tick rather than
	/// opening anything on the click itself.
	bool _menuRequest;

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
	Font _chatFont;				///< and the third face, the one the options are set in
	TalFile _tal;
	TalFile _labels;				///< NAMEROOM/<lang>/R<n>.TAL, the hover names
	TalFile _talkall;				///< TALKALL.TAL, the answers no room owns
	const TalFile *_speechTal;		///< which of the two the queue came out of
	uint _labelSlot;

	/// The status line's own three colours, [0xa80d]..[0xa812], and whether they
	/// are on their way out. The label font draws in palette entries 66..68 and
	/// the original fades those to black once the cursor leaves whatever it was
	/// naming, rather than blanking the line (OBJ:sub_06d66 and sub_082fd).
	byte _labelColors[6];
	bool _labelFading;
	Common::String _labelText;		///< [0xaaec], what the line reads this frame
	Common::String _labelShown;		///< and what is still drawn, fade included

	/// [0xac1e]: frames left before the hover may rebuild the line. A click sets
	/// it and the room's entry 1 spends it, one a frame, registering nothing
	/// while it lasts. _walkReported marks the kind that a walk's end cuts short
	/// rather than letting it time out.
	uint _labelHold;
	bool _walkReported;

	uint _dialogId;

	/// [0xacf6]: the outcome id the last queue_event was raised with, which one
	/// room reads back once its lines have been spoken (hallway.cpp).
	byte _lastEvent;

	/// A line that is not one of the file's entries whole: the slice of an
	/// entry a conversation option is (chat.cpp). While this is set the drawn
	/// text comes from _speechEntry rather than from _dialogId.
	TalFile::Entry _speechEntry;
	bool _speechCustom;

	/// The conversation menu, and the room machine that is the port's first
	/// caller of it (chat.cpp, library.cpp).
	ChatMenu _chat;

	/// Room 8's [0xa49f] machine: talking to the owl.
	byte _libraryStep;

	/// [0x8d02] and [0x8d04]: where the cursor was when the hover last ran. The
	/// conversation menu tests the cursor against its own bands from the room's
	/// tick rather than from an event, so it reads this rather than the host's
	/// pointer -- which is also what lets a play script's `hover` reach it.
	int _cursorX;
	int _cursorY;

	/// The eight palette entries the conversation menu owns while it is up
	/// (66, 67 and 74..79), as the room left them, and whether they are still
	/// the menu's to give back.
	byte _chatPalette[14 * 3];
	bool _chatColorsHeld;

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

	/// Room 3's opening machine: the step it is on, and [0x7dc4] -- whether the
	/// game has yet to say its first words. A restored save clears the flag the
	/// way MAIN's load branch does.
	byte _openingStep;
	bool _openingPending;

	/// The room clock (roomtick.cpp): how long the player has been standing in
	/// the room this visit, in the units that room's own tick counts. Every
	/// room that has one zeroes it as its overlay opens, so one counter serves
	/// them all and loadRoom resets it.
	uint16 _roomClock;

	/// Room 3's [0xa49f] machine, less the opening the two steps in opening.cpp
	/// carry (lab.cpp). Zero when nothing is running.
	byte _labStep;

	/// [0xa49c] as room 3 keeps it: a free-running counter its tick advances on
	/// every tick pair, which two of that room's steps time themselves off.
	uint16 _labPos;

	/// [0xa7a4]/[0xa7a5]: whether the character stood inside room 3's creak
	/// rectangle on the last tick, which is the edge the two samples fire on.
	bool _labNearHole;

	/// [0xa94d]: whether the character is drawn at all. Every room's tick tests
	/// it before calling OBJ:sub_06466, and a room clears it while it plays the
	/// character's own action on an animation slot -- otherwise the slot's Ben
	/// and the walker are both on screen (playtest report 3).
	bool _drawCharacter;

	/// Room 35's [0xa49f] machine (sewer.cpp). Zero when nothing is running.
	byte _sewerStep;

	/// The sewer's water, which the original keeps as four words of its own in
	/// the data segment rather than in the state block: [0x4380] is the phase
	/// into the thirty-entry ripple table at [0x4362], [0x4382] the depth the
	/// drain has taken off, [0x4384] the divider that lets the depth move once
	/// every third animation frame, and [0x4385] whether the drain is running.
	/// GAME.EXE ships them as 0, 0x32, 0, 0 and nothing else in the game reads
	/// or writes them, so they are here rather than in RoomScript.
	uint16 _sewerPhase;
	uint16 _sewerDepth;
	byte _sewerDivider;
	byte _sewerDraining;

	/// Whether the room now composed still owes its fade in (fade.cpp). Room
	/// init leaves the palette black and the loop's tail raises it, which is
	/// the order the original works in too.
	bool _fadePending;

	/// Whether the room now composed still owes the scenes it raises as it
	/// opens. Set by loadRoom and spent by the loop once the room's own frame
	/// is on the screen, so a scene never plays over the room being left.
	bool _pendingCutscenes;

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
