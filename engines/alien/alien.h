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

#include "common/keyboard.h"
#include "common/language.h"
#include "common/serializer.h"
#include "common/random.h"
#include "engines/engine.h"
#include "graphics/hotspot_renderer.h"
#include "graphics/surface.h"

#include "alien/anim.h"
#include "alien/animfont.h"
#include "alien/charanim.h"
#include "alien/chat.h"
#include "alien/console.h"
#include "alien/dl1.h"
#include "alien/font.h"
#include "alien/hotspots.h"
#include "alien/inventory.h"
#include "alien/mazetables.h"
#include "alien/overlay.h"
#include "alien/pack.h"
#include "alien/play.h"
#include "alien/s3m.h"
#include "alien/script.h"
#include "alien/sfx.h"
#include "alien/tables.h"
#include "alien/tal.h"
#include "alien/video.h"
#include "alien/walk.h"
#include "alien/rooms/room.h"

struct ADGameDescription;

namespace Alien {

class AlienEngine : public Engine {
	friend class AlienConsole;
	// The hand-written rooms (rooms/), which are the overlays' own code and
	// reach into the engine as freely as the overlays reach into the resident
	// units.
	friend class BossRoom;
	friend class HallwayRoom;
	friend class LabRoom;
	friend class BedroomRoom;
	friend class MansionRoom;
	friend class LiftRoom;
	friend class ObservatoryRoom;
	friend class SewerRoom;
	friend class BasementRoom;
	friend class CliffRoom;
	friend class LivingRoom;
	friend class MailboxRoom;
	friend class SluggsRoom;
	friend class CemeteryRoom;
	friend class TelescopeRoom;
	friend class MazeRoom;
	friend class ShoreRoom;
	friend class PoolRoom;
	friend class DivingRoom;
	friend class SteamRoom;
	friend class ParkRoom;
	friend class YodleRoom;
	friend class ForestRoom;
	friend class TeleportRoom;
	friend class CorridorRoom;
	friend class ScannerRoom;
	friend class StoreRoom;
	friend class HippieRoom;
	friend class LibraryRoom;
	friend class WaitingRoom;
	friend class JailRoom;

public:
	static const int kScreenWidth = 320;
	/// Ticks per frame for the debug run-everything animation, slow enough to watch.
	static const int kDebugAnimRate = 4;

	/// [0x7926]. The interrupt keeps counting while a blocking wait holds the
	/// loop up, so the ticks stepClock() has yet to catch up are counted too
	/// -- a scene's lines are stamped as they come up, not at the tick the
	/// scene started.
	uint32 masterTick() const;

	/// An item on an object no body answered: the room's table, else a refusal.
	void refuseItemUse(byte obj, byte item, int anchorX, int anchorY);
	/// [0xa651]/[0xa652]/[0xa653], the last item use and how often in a row,
	/// and [0x30e2], where the rotating refusals are up to.
	byte _useLastItem, _useLastObj, _useRepeat, _refusalNext;
	static const int kScreenHeight = 200;

	/// Where OBJ:sub_08a39 clamps the cursor's hot spot: the arrow is 11 wide,
	/// so it never reaches the screen's last columns.
	static const int kCursorMaxX = 0x134;
	static const int kCursorMaxY = 0xc7;

	/// The row the playfield ends on, and so the value OBJ:sub_08567 puts in
	/// [0xa8e4] as every room opens: the DL1 blitter draws nothing at or below
	/// it (see DL1Sprite::drawFrame and _clipBottom).
	static const int kPlayfieldBottom = 0x9b;

	/// Object ids are bytes, and the outcome rotation is kept per object.
	static const uint kObjectCount = 256;

	// The auto-dismiss countdown is three per character of text, floored at
	// 0x46. It is decremented under the tick pair gate rather than the
	// animation one, so it runs at half the master rate, not a quarter of it.
	// A cutscene's lines are timed the same way, so both files need them.
	static const int kTicksPerCharacter = 3;
	static const int kMinSpeechTicks = 0x46;

	/// And the mouth stops with this many of those half ticks left, OBJ:0x86df.
	static const int kTalkStopTicks = 0x19;

	/// One master tick in milliseconds: the ~70 Hz retrace, scaled the way
	/// alien.cpp explains. Here because the palette fades wait on it directly
	/// rather than through the game loop (fade.cpp).
	static const uint32 kMasterTickMillis = 1000 * 2 / (70 * 3);

	/// What the game loop sleeps between passes. It has to stay under the tick
	/// or it becomes the clock: at ten milliseconds a nine millisecond tick can
	/// only ever fire once per pass, which caps the game below its rate. Here
	/// because a resumed play run sets the clock up as if it had just slept one
	/// (playrun.cpp).
	static const uint32 kLoopSleepMillis = kMasterTickMillis / 2;

	/// [0xac1e], how long a click's own line stands before the hover answers
	/// again: 0x8c frames for a verb (1021:0x897) and, for a walk, 0xbb8 --
	/// long enough that in practice the arrival is what ends it (1021:0xa16).
	static const uint kLabelHoldVerb = 0x8c;
	static const uint kLabelHoldWalk = 0xbb8;

	/// The one room whose left click reads "Swim to", 1021:0x9be.
	static const int kSwimRoom = 46;

	/// "Use to", the verb an item in hand puts the click under.
	static const byte kVerbUseTo = 2;

	/// The text block is placed against the midpoint of the speaking object's
	/// bounding box. Dialog stepped through from the keyboard has no object
	/// behind it and is anchored at the middle of the playfield.
	static const int kAnchorX = 160;
	static const int kAnchorY = 90;

	/// The clips. The elevator sequence installs with the game; the two CDA2
	/// cutscenes live on the CD, under CDA/, and are reached through
	/// --extrapath when the disc's files are kept somewhere of their own.
	static const char *const kLiftClip;
	static const char *const kCutscenes[2];

	/**
	 * The engine's clock, in place of the backend's (clock.cpp).
	 *
	 * Every loop in the port reads the time and sleeps through these two, so
	 * one setting decides how fast the game runs: `timescale=1` is real time,
	 * N > 1 runs N times faster in a window, and 0 is turbo -- a sleep advances
	 * a virtual clock instead of waiting, so a scripted run goes as fast as the
	 * machine composes frames and takes the same ticks it would at 1x.
	 */
	uint32 millis() const;
	void sleep(uint32 ms);
	void setTimescale(uint scale);
	bool turbo() const { return _timescale == 0; }
	/// The event recorder is recording or playing back, which pins the clock
	/// to real time.
	bool underRecorder() const;

	/// Stops the engine's clock while ScummVM holds the game (the GMM, a
	/// backend pause), so no tick is owed for the time spent there.
	void pauseEngineIntern(bool pause) override;
	/// The backend's time less every pause, frozen while one lasts.
	uint32 runMillis() const;

	/// Hands the composed frame to the backend: every frame, but in turbo.
	void present();

	/// ScummVM's hold-to-show hotspot markers (hotspots.cpp has the table):
	/// the rectangles the room registered, as the cursor would find them.
	void getHotspotPositions(Common::Array<Graphics::HotspotInfo> &hotspots) override;
	bool hotspotDirty() const override;

	AlienEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~AlienEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;
	/// Volumes and mutes as the base sets them, plus the film subtitle toggle.
	void syncSoundSettings() override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveAutosaveCurrently() override;

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
	byte heldItem() const { return _heldItem; }

private:
	/// Every room under rooms/, in the order each hook runs them (rooms.cpp).
	Common::Array<Room *> _rooms;
	void registerRooms();

	bool loadRoom(int room, bool secondPlate = false, bool keepPosition = false);
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
	void relightCharPalette();
	void stepLighting();
	void dumpLighting();
	void sweepLighting();

	// scale.cpp: [0xa888], the depth divisor the character is drawn through.
	uint16 charScale(int room, int y, bool ledge) const;
	void stepCharScale();
	void dumpScale();
	void sweepScale();

	void stepCameraHold();
	void updateScroll(bool snap = false);
	void loadSpriteBank(uint bank);
	void stepSpriteBank(int delta);
	void redraw();
	void uploadPalette(const byte *source, int level);
	void fadeOut();
	void fadeIn(bool relight = true);
	void showStill(const char *name, uint holdTicks);
	bool loadCursor();
	void handleEvents();
	/// Runs what the console queued (console.cpp), once it has closed.
	void runConsoleActions();
	Common::Array<ConsoleAction> _consoleActions;
	void dumpScreen(const Common::String &name = Common::String());

	bool loadPlayScript(const Common::String &path);
	void stepPlayScript();
	bool playIdle() const;
	bool roomBusy() const;
	void runPlayCommand(const PlayCommand &cmd);

	void walkTo(int x, int y, int arrivalFacing = Walker::kFacingKeep);

	/**
	 * Plays a bank that draws the character himself.
	 *
	 * Half a dozen rooms hand Ben over to an animation slot -- the rope in the
	 * basement, the cliff, the plank in the lab, the stethoscope in the library,
	 * the blast in the park, getting up off the road in town -- and each of them
	 * brackets the play with [0xa94d], the byte every room's tick tests before
	 * it draws the walker. This is both halves of that in one call: the byte
	 * goes down and the slot is remembered, so showCharacter() can take the
	 * slot down again when the walker comes back. Without that the drawn-on Ben
	 * is left standing wherever the range ended -- a bank that ships fewer
	 * frames than the room plays, or a machine that hands him back before the
	 * terminator, and there are two Bens on screen.
	 */
	void playCharacterAnim(uint slot, int first, int count, int rate, int mode);

	/// Hides the walker without starting anything: for a machine that plays its
	/// bank on a later step than the one that takes the character away.
	void hideCharacter(uint slot = kNoCharacterSlot);

	/// Gives the walker back and clears every slot playCharacterAnim() started.
	void showCharacter();

	/// No slot: hideCharacter() taking the character away on its own.
	static const uint kNoCharacterSlot = 0xFFFF;
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
	bool combineItems(byte held, byte clicked);
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
	uint32 stateHash();
	void checkDosItemImport();
	void stopMusic();
	void dumpCutscenes();
	void sweepCutscenes();
	void dumpOcclusion();
	Common::String roomPlate(int room) const;
	Common::String occluderPlate(int room) const;
	void loadOccluder(int room);
	void applyOcclusion();
	bool triggerCutscene(byte id);
	void roomCutscenes(int room);
	void stepCutsceneTimers();
	void tickCutsceneTimers();
	uint32 cutsceneTimer(uint16 addr) const;
	bool cutsceneTimerDue(byte scene) const;
	void playCutsceneRecord(uint number);
	void endCutscene(int room, int benX, int benY, int benFacing);
	void playStudio();
	void runCutsceneProc(uint proc, bool quiet = false);
	void speakCutsceneLine(uint code, int anchorX, int anchorY);
	bool nextCutsceneLine();
	void dumpMusic();
	void sweepMusicCues();
	void sweepMusicRows();
	void renderMusic();
	void playVideo(Video::VideoDecoder &video, CDA2Decoder *subtitles = nullptr);
	void drawSubtitle(const CDA2Decoder &video);
	uint subtitleLanguage() const;
	void startEnding();
	void armEnding();
	void startOpening();
	void stepOpening();
	void stepRoomClock();
	uint16 stateWord(uint16 addr) const;
	void setStateWord(uint16 addr, uint16 value);
	void resetRoomClock(int room);

	void openRoomPlate(int room);
	void dumpPlates();
	static Common::String argText(uint16 arg);

	/// The room's hotspot table, rebuilt from the puzzle state -- and from the
	/// cell, in the two mazes. Defaults to the room the engine is in; loadRoom
	/// passes the room it is opening, which is not yet _room.
	void rebuildHotspots(int room = -1);

	/// A two-point route from where he stands, for the rooms with no walk mask.
	void straightWalkTo(int x, int y, int arrivalFacing);

	/// Room 40's shared files and the one item-use line it has (cave.cpp).
	void loadSharedTal(uint n);
	void enterCave(int room);
	bool armCave(int obj, byte item, int anchorX, int anchorY);

	bool dlgreqRunning() const;
	bool roomHoldsCharacter() const;

	void playDl2Clip(const char *clip, const char *plateName, uint music, const char *tag);

	bool chatReplyOwed() const;

	void walkHandRoute(int viaX, int viaY, int x, int y, int facing);
	void cancelOpening();
	void stepEnding();
	void speakEnding(byte code);
	bool endingLineDone() const;
	void winGame();
	bool playLift();
	bool playCutscene(const char *file);
	void dumpVideo();
	void dumpVideoFont();
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
	/**
	 * queue_event: speak an outcome's chain of lines at an anchor.
	 *
	 * `benSpeaks` is false for a line another character speaks, which the
	 * original sends through DIALOG:sub_0b63a with an anchor of its own: that
	 * path raises [0x293b] rather than the talk flag [0x2938], so his mouth
	 * stays shut while someone else talks.
	 */
	void queueOutcome(const TalFile &tal, byte code, int anchorX, int anchorY,
					  bool benSpeaks = true);
	void speakEntry(const TalFile::Entry &entry, int anchorX, int anchorY, int ticks);
	bool speechDone() const { return !_speech && _queueNext >= _queueCount; }

	void sweepChatTrees();
	void openChat(uint topic);
	void hideBar();
	void showBar();
	void stepChat();

	void caveWalkTarget(byte obj, WalkTarget &target);
	void nextSpeech();
	void stopSpeech();

	/// Dumps every room's dialog from the TAL files: what is said, and how
	/// long each line stands (tal.cpp).
	void sweepDialog();

	/**
	 * A constant the pack is allowed to have an opinion about.
	 *
	 * The caller passes the value the original works out to, so a build with no
	 * pack -- or with a pack that does not name this one -- is the game as it
	 * shipped. This is how the numbers buried in the hand-written room machines
	 * are reachable without another rebuild.
	 */
	int tunable(const char *name, int fallback) const { return _pack.tunable(name, fallback); }

	/// How long one entry's text stands, the pack's overrides included.
	int speechTicksFor(const TalFile &tal, uint id) const;

	/// The colour, anchor and layout an override asks for, as the line goes up.
	void applyTextOverride(const AlienPack::TextOverride *ov);

	void setTextColor(byte r, byte g, byte b);
	void uploadTextColor();
	/// The ambient speaker colour of the rooms that set one (text.cpp).
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

	/**
	 * A wide room's background page is a 320-wide buffer the window starts
	 * `scroll` bytes into (the screen's own layout), so it holds only what is
	 * in view: a column that scrolls out is overwritten by the one coming in,
	 * and comes back from the plate. A slot baked into it is lost that way,
	 * column by column. These are the pixels a bake changed, with what was
	 * under them, put back as they leave the window (dropBakedOutOfView).
	 */
	struct BakedPixel {
		int16 x, y;
		byte under;
	};
	Common::Array<BakedPixel> _baked;
	const void *_bakedPlate;	///< the _background the list belongs to

	/** AnimSlots::bake into _background, remembering a wide room's changes. */
	void bakeSlots();
	void stepLoopSounds();
	void sceneClockTick();
	uint _sceneClockPhase = 0;	///< sceneClockTick()'s tick pair
	/** Puts the plate back under baked pixels the window has left. */
	void dropBakedOutOfView();
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
	/// [0xa884]: clear from room init until the first composed frame, and
	/// the upload is gated on it (lighting.cpp).
	bool _lightArmed;

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
	int32 _scrollPos;					///< [0xa0c8]: _scrollX in 1/1024 px
	int _scrollVel;						///< [0xa0cc]: pan speed, 1/1024 px a tick
	byte _scrollState;					///< [0xa0ce]: 1 coasting right, 0 left, 0xff neither
	int _scrollFocus;					///< [0xa0c2]: the x the camera is making for
	uint32 _roomEnterTick;				///< the master tick the room was loaded on
	uint _scrollPlacements;				///< Walker::placements() last snapped to

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

	/// Whether the films show their subtitle track; the game's own dialog is
	/// text alone and always shows.
	bool _subtitles;

	Walk _walk;
	WalkRoute _route;
	bool _showWalk;					///< draw the mask, the node ring and the route
	bool _showSpots;				///< outline the hotspots entry 1 registered

	Walker _ben;					///< the player character walking that route
	uint32 _lastTick;				///< when the master clock last advanced
	uint32 _tick;					///< master ticks since the engine started

	uint _timescale;				///< 0 turbo, 1 real time, N times real time
	uint32 _virtualMillis;			///< turbo's clock, advanced by sleep()
	uint32 _clockRealBase;			///< backend time when the scale last changed
	uint32 _clockVirtualBase;		///< engine time at that moment
	uint32 _presentLast;			///< turbo: engine time of the last frame shown
	uint32 hotspotStamp() const;
	uint32 _hotspotStamp;			///< hotspotStamp() when the markers were last built
	uint32 _pausedMillis;			///< backend time spent paused, all pauses together
	uint32 _pauseStart;				///< backend time the current pause began
	bool _clockPaused;				///< a pause is in progress

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

	/// Whether the GUI's own menu is up. Nothing in the original has a
	/// counterpart -- it is the autosave, which ScummVM takes behind the menu
	/// and again on the way into a load (canSaveAutosaveCurrently, saveload.cpp).
	/// Not part of the save file: it is true only while a modal dialog is open.
	bool _inMenu;

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
	/// ANIMPLAY.EXE's own face, the one the clips' subtitles are set in; loaded
	/// the first time a clip plays, since nothing else uses it.
	AnimFont _videoFont;
	Font _labelFont;				///< the shorter face the status line is set in
	Font _chatFont;				///< and the third face, the one the options are set in
	/// ALIEN.DAT: what the port reads instead of carrying it as code. Optional,
	/// and absent means the lifted defaults (pack.cpp).
	AlienPack _pack;

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
	/// Whether the line up is Ben's own, and so moves his mouth (queueOutcome).
	bool _speechBen;

	/// The conversation menu, and the room machine that is the port's first
	/// caller of it (chat.cpp, library.cpp).
	ChatMenu _chat;

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

	/// The bar has slid off the bottom of the screen (OBJ:sub_02f27) and not
	/// yet come back (OBJ:sub_030f4): those forty rows are black, or the menu's.
	bool _barHidden;
	/// And the conversation is what took it, so the conversation's end is what
	/// brings it back. A room that runs its own machine over the menu (the
	/// store) takes that over.
	bool _chatOwnsBar;

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

	byte _stairsDescents;		///< trips down from room 28, for the port's own fall (saved)
	bool _restoring;			///< syncGame is reloading the room, not an entry through a door

	/// [0xa94d]: whether the character is drawn at all. Every room's tick tests
	/// it before calling OBJ:sub_06466, and a room clears it while it plays the
	/// character's own action on an animation slot -- otherwise the slot's Ben
	/// and the walker are both on screen.
	bool _drawCharacter;

	/// The slots playing a bank that draws the character himself, one bit per
	/// slot. playCharacterAnim() sets a bit and showCharacter() takes the slot
	/// down again, so that the drawn-on Ben leaves the screen on the same frame
	/// the walker's Ben comes back to it.
	uint16 _characterAnimSlots;

	/// [0xa948] as the loop last saw it: the frame it comes back on is the one
	/// that has to rebuild the hover, since the port only hovers on motion.
	bool _cursorWasVisible;

	/// [0xa8e0]/[0xa8e2]: a scene holding the camera on a point of its own
	/// rather than on Ben, or -1 while it follows him.
	int _scrollHold;
	int _cameraLastX;				///< [0xa8f0], the x room 50 tests a crossing against
	uint _cameraPlacements;

	/// What the conversation menu's last pick was, for the rooms that answer
	/// one: the reply is the fourth byte of the tree record -- a dialog id,
	/// which DIALOG:sub_0bf08 hands straight to the renderer -- and the topic
	/// and option are [0xa634] and [0xa60c], which OBJ's two per-handler hooks
	/// test (chat.cpp).
	byte _chatPickReply;
	byte _chatPickNext;
	uint _chatPickTopic;
	uint _chatPickChoice;
	bool _chatPickNew;

	/// GFX:sub_26ebc, the resident random the store picks its refusal with.
	Common::RandomSource _rnd;

	/// Whether the room now composed still owes its fade in (fade.cpp). Room
	/// init leaves the palette black and the loop's tail raises it, which is
	/// the order the original works in too.
	bool _fadePending;

	/// Whether the room now composed still owes the scenes it raises as it
	/// opens. Set by loadRoom and spent by the loop once the room's own frame
	/// is on the screen, so a scene never plays over the room being left.
	bool _pendingCutscenes;
	/// endCutscene's reload: the room is put back, not entered (RoomScript::enterRoom).
	bool _sceneHandBack;
	/// Room 7's [0xa8f0]/[0xa8f2]: where the sprite stood last frame (roomtick.cpp).
	int _darkLastX, _darkLastY;

	uint _endingPos;
	bool _endingLoop;
	bool _won;

	/// A cutscene owns the screen: no character, no inventory bar, no hover name.
	bool _cutscene;

	/// The sweep plays the scenes with the clock taken out: same step
	/// interpreter, same debug lines, no waiting on holds or on lines being read.
	bool _cutsceneFast;

	/// The scripted command sequence, if --debugflags=play named one
	/// through the ALIEN_PLAY_SCRIPT environment variable.
	PlayScript _play;
	uint _playIndex;
	bool _playActive;
	uint32 _playLastTick;			///< the master tick stepPlayScript last acted on
	int _playWaitTicks;			///< ticks still to burn before the next command
	int _playSettleTimeout;		///< ticks left before a "settle" gives up
	int _playUntilTimer;			///< `until timer`: the scene waited on, -1 = none
	int _playUntilRoom;				///< `until room`: the room waited on, -1 = none
	bool _playSettling;
	uint _playFails;				///< number of failed "expect" assertions so far
	bool _playPaused;				///< P: the script holds before its next command
	bool _playStep;				///< N: let one command through while paused
	bool _playSkippable;			///< `skippable`: Escape each cutscene as it starts
	bool _playHurry;				///< `hurry`: click each line away as it goes up
	void offerSkip();

	/// Strict mode (playrun.cpp): no cheats, the bar by its clicks, fail fast.
	bool _playStrict;
	PlayCommand _playBar;			///< the bar command being clicked through
	bool _playBarBusy;
	uint _playBarClicks;			///< clicks spent on it, against a runaway

	bool startPlayRun();
	bool playClickAllowed(const PlayCommand &cmd);
	void stepPlayBar();
	void playBarClick(Common::Point at, bool right);
	static const char *playBarVerb(PlayCommand::Type type);
	void runBarShortcut(const PlayCommand &cmd);
	void playFailed(uint line);
	void playGameWon();
	void playLeftOpen(const char *what);
	void writeCheckpoint(const PlayCommand &cmd);
	void writeCheckpointScreen(const Common::String &name);
	void stopAtCheckpoint(const PlayCommand &cmd);
	bool resumePlayRun();
	void restorePlayState(Common::SeekableReadStream *in, uint32 sinceTick);
	bool scrollWouldPan() const;
	void writePlayCamera(Common::WriteStream *out) const;
	void readPlayCamera(Common::SeekableReadStream *in);
	void writeFailureState(uint line);

	/// The keys a watched run answers to; false for any other key.
	bool playHotkey(const Common::KeyState &key);
	void playMessage(const Common::String &text);
	void playClick(int x, int y, bool right);
	void playQuit();
};

} // End of namespace Alien

#endif
