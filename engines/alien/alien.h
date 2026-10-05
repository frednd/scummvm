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
#include "graphics/surface.h"

#include "alien/anim.h"
#include "alien/animfont.h"
#include "alien/charanim.h"
#include "alien/chat.h"
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

struct ADGameDescription;

namespace Alien {

class AlienEngine : public Engine {
public:
	static const int kScreenWidth = 320;
	static const int kScreenHeight = 200;

	/// Where OBJ:sub_08a39 clamps the cursor's hot spot: the arrow is 11 wide,
	/// so it never reaches the screen's last columns.
	static const int kCursorMaxX = 0x134;
	static const int kCursorMaxY = 0xc7;

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

	/// What the game loop sleeps between passes (alien.cpp). Here because a
	/// resumed play run sets the clock up as if it had just slept one
	/// (playrun.cpp).
	static const uint32 kLoopSleepMillis = kMasterTickMillis / 2;

	/// [0xac1e], how long a click's own line stands before the hover answers
	/// again: 0x8c frames for a verb (1021:0x897) and, for a walk, 0xbb8 --
	/// long enough that in practice the arrival is what ends it (1021:0xa16).
	static const uint kLabelHoldVerb = 0x8c;
	static const uint kLabelHoldWalk = 0xbb8;

	/// The one room whose left click reads "Swim to", 1021:0x9be.
	static const int kSwimRoom = 46;

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

	/// Hands the composed frame to the backend: every frame, but in turbo.
	void present();

	AlienEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~AlienEngine() override;

	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;
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

private:
	bool loadRoom(int room, bool secondPlate = false, bool keepPosition = false);
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

	// scale.cpp: [0xa888], the depth divisor the character is drawn through.
	uint16 charScale(int room, int y, bool ledge) const;
	void stepCharScale();
	void dumpScale();
	void sweepScale();

	void updateScroll(bool snap = false);
	void loadSpriteBank(uint bank);
	void stepSpriteBank(int delta);
	void redraw();
	void uploadPalette(const byte *source, int level);
	void fadeOut();
	void fadeIn();
	void showStill(const char *name, uint holdTicks);
	bool loadCursor();
	void handleEvents();
	void dumpScreen(const Common::String &name = Common::String());

	bool loadPlayScript(const Common::String &path);
	void stepPlayScript();
	bool playIdle() const;
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
	void reloadPlates();
	void bedroomSwitch(int anchorX, int anchorY);
	bool isBedroomSwitch(int obj) const;
	bool hijackBedroomLift(byte submode);
	void stepBedroomLift();
	void enterLanding(int room);
	void enterMansion(int room);
	void stepMansion();
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
	void armLab(int obj, bool item);
	void stepLab();
	void stepLabHole();

	/// Room 19's fuse-panel cover: the rectangle the lift could not carry
	/// because its width is computed, the look that opens it and the move that
	/// toggles it (observatory.cpp).
	bool armObservatoryPanel(int obj, byte verb);
	void armObservatoryLook(int obj, byte verb);
	void armObservatoryBreaker(int obj, byte verb);
	void stepObservatoryBreaker();
	void startObservatory();
	void stepObservatoryStairs();
	bool hijackObservatoryStairs(byte submode);
	void stepObservatoryPanel();
	void openObservatoryPanel();
	void closeObservatoryPanel();

	/// The lab computer and the lift car it parks (lift.cpp).
	bool armLiftCall(int obj, int anchorX, int anchorY);
	void stepLiftCall();
	void playLiftPanel();
	bool liftWipe(const Graphics::Surface &to);

	/// Room 28's telescope view and observatory computer, both of which the
	/// lifted table only ever arms as game_submode = 111 (telescope.cpp).
	bool runTelescopeScreen(int obj, byte verb, byte submode);
	/// Room 28's lever, the one writer of [0xa760] (telescope.cpp).
	void armTelescopeLever(int obj, byte verb);
	void stepTelescopeLever();
	void playTelescopeView();
	void playObservatoryScreen();
	void stepHallway();
	void playPeephole();

	void armSewer(int obj, byte verb);
	void enterSewer(int room);
	void openRoomPlate(int room);
	void dumpPlates();
	static Common::String argText(uint16 arg);
	void stepSewer();
	void sewerValve();

	void enterBasement(int room);
	void stepBasement();
	void takeBasementBattery(int obj);

	/// Room 11's television, the tape it plays and the arrow that comes down
	/// with it (living.cpp).
	bool armLiving(int obj, byte item, int anchorX, int anchorY);
	void livingCassette(int obj, byte item);
	void startTape();
	void stepLiving();

	/// Room 31's two ledges and the climb between them (cliff.cpp).
	void armCliff(int clickX, int clickY, WalkTarget &target);
	void stepCliff();

	/// Room 32's statue, the phrase it wants and the cave it guards
	/// (cemetery.cpp).
	void armCemeteryStatue(int obj, byte verb);
	void cemeteryArrival();
	void setCemeteryState(byte state);
	void cemeteryZap();
	void cemeteryStatuePick();
	void startCemetery();
	void stepCemetery();

	/// The room's hotspot table, rebuilt from the puzzle state -- and from the
	/// cell, in the two mazes. Defaults to the room the engine is in; loadRoom
	/// passes the room it is opening, which is not yet _room.
	void rebuildHotspots(int room = -1);

	/// Rooms 43 and 44, the two mazes: their per-cell backgrounds, arrows and
	/// routing, and the crystal door that ends maze A (maze.cpp).
	void startMaze();
	void stepMaze();
	void stepCrystal();
	void crystalSpeak();
	void crystalSay(byte outcome);
	bool mazeBackground(int room, Graphics::Surface &plate, byte *palette);
	void buildMazeHotspots(int room);
	void mazeEnter(int room);
	void mazePlace();
	bool mazeExit(byte &submode);
	bool mazeWalkTo(int x, int y, int arrivalFacing);

	/// A two-point route from where he stands, for the rooms with no walk mask.
	void straightWalkTo(int x, int y, int arrivalFacing);
	const MazeCell *mazeCell(int room) const;

	/// Room 48's pool, and the diving suit that opens the way down to room 46
	/// (pool.cpp).
	bool armPool(int obj, byte item);
	void stepPool();

	/// Room 46, underwater: its two views, the swim, the chest and its key,
	/// the propeller's bubbles and the water over all of it (diving.cpp).
	void loadDivingBanks(int room);
	void startDiving();
	bool armDiving(int obj, byte item);
	bool divingSwimTo(int x, int y, int arrivalFacing);
	void stepDiving();
	void stepDivingWater();
	void stepDivingWave();
	void drawDivingWater(Graphics::Surface &dest) const;
	bool divingTakeExit(byte submode);

	/// Room 41's door to the maze, simplified the same way (shore.cpp).
	void startShore();
	bool armShoreAxe(int obj, byte item);
	bool armShoreSuit(int obj, byte item);
	void shoreArrival();
	void stepShore();

	/// Room 21's Yodle: the picklock, the menu, and the teleporter he builds
	/// out of what is brought to him (yodle.cpp).
	void startYodle();
	bool armYodle(int obj, byte verb, int item);
	void yodleArm(byte which);
	void yodleCheckIn(byte &first, byte &count);
	void loadYodleScript(byte which);
	void yodleTalk(byte speaker, byte line, byte count);
	void yodleSpeak();
	void yodlePick();
	bool yodleRunDone() const;
	void yodlePose(byte pose);
	void yodleSounds(byte which);
	void loadYodleRoomScript();
	void yodleRoomScriptFor(int obj, byte verb, byte item);
	void stepYodle();
	void stepYodleWater();
	void yodleStoryWait();
	void drawYodleWater(Graphics::Surface &dest) const;
	bool dlgreqRunning() const;

	/// The ladder between rooms 49 and 50, and the steam over room 50's
	/// right half (steam.cpp).
	void startSteam();
	void steamClick(int roomX, int y);
	void armSteam(int obj, byte verb);
	void stepSteam();
	void stepSteamLight();

	/// Room 22's arrival scene: the voice in the booth, and the wreck it
	/// leaves behind (park.cpp).
	void startPark();
	void stepPark();

	/// Room 26's parrot: the brush-off and the moldy bread it actually wants
	/// (forest.cpp).
	void armForestParrot(int obj, byte verb, int item);
	void stepForestParrot();
	void startForest();
	bool runYodleNote(int obj, byte verb, byte submode);
	void parrotSays(byte line);
	void parrotQuiet();
	void forestBenSays(byte line);
	void forestArm(byte step);

	/// Room 22's phone, which dials the teleporter, and room 56's chamber at
	/// the other end: the arrival, the pad and the trip back (teleport.cpp).
	void startTeleport();
	void advanceChamberView(int room);
	bool armTeleportPhone(int obj, byte verb, byte item);
	void stepTeleport();
	void shipArrivalScene(int room);
	void shipWalkTarget(byte obj, bool action, WalkTarget &target);
	void shipChamber(bool open);
	void stepShip();
	void startShipFlash();
	void stepShipFlash();
	void shipFromJail();

	/// Rooms 51/53/55/57, the ship's four elevator floors: room 53's
	/// maintenance man and his badge, room 57's loudspeaker and the network
	/// terminal behind its card slot (corridor.cpp, terminal.cpp).
	void startCorridor();
	void hallwayScene(int room);
	void corridorArrival();
	void playElevatorPanel();
	bool armHallway(int obj, byte verb, int item);
	void armHallwayCard(int obj, int item);
	bool hallwayOwesReply() const;
	void stepCorridor();
	void stepShipDoors();
	void stepHallMan();
	void stepHallwayDoors();
	void hallManPose(byte pose);
	void playNetTerminal();
	bool hallwayWalkTo(int x, int y, int arrivalFacing);
	bool jailWalkTo(int x, int y, int arrivalFacing);

	/// Room 52's security scanner: the resident gate in front of the room,
	/// its DL2 scan, and the robot's arrest (scanner.cpp).
	int scannerGate(int room);
	void playScannerScan(bool mask);
	void playDl2Clip(const char *clip, const char *plateName, uint music, const char *tag);
	void startScanner();
	void stepScanner();

	/// Room 54's call in to Jack's room: the way out through object 1, taken
	/// over once his number is up, and the door's own refusal (waiting.cpp).
	void startWaiting();
	bool hijackWaitingExit(byte submode);
	void waitingArrival();

	/// Room 54's ticket machine, which is not simplified: the [0xa49f] machine
	/// behind object 12 and the three numbers it gives out (waiting.cpp).
	bool armWaitingButton(int obj, byte verb);
	void stepWaitingMachine();
	void speakWaiting(byte code);
	bool waitingLineDone() const;

	/// Room 60, Jack's room: no overlay at all, and a conversation rather than
	/// a fight -- the boss hands over his escape pod card (boss.cpp).
	void startBoss();
	void stepBoss();
	void bossSpeak();
	bool bossWalkTo(int x, int y, int arrivalFacing);

	/// Room 58: the corridor's conversation through the bars and its two
	/// ways out, and the escape back to the ship, still simplified
	/// (jail.cpp).
	void startJail();
	bool armJailTalk(int obj, byte verb);
	bool armJailShackle(int obj, byte verb);
	void armJailLcd(int obj, byte item);
	void jailShaftArrival();
	void stepJail();
	void jailConversation();
	void jailSpeak();
	void jailUnclePose(byte pose);
	void jailYodlePose(byte pose);

	/// Room 58's guard, the force field over the cells, and the card the
	/// guard leaves behind (jailguard.cpp).
	void startJailGuard();
	void stepJailGuard();
	void jailGuardPose(byte pose);
	void jailGuardAnimate();
	void jailGuardMove();
	void jailGuardWalk(int x, int y);
	void jailGuardSpeak(byte code);
	void jailGuardMachine();
	bool jailGuardBusy() const;
	void jailGuardTrigger();
	void jailGuardAnchor(int &x, int &y) const;
	void buildJailHotspots(int room);
	bool armJailCard(int obj, byte verb);
	void drawJailField(Graphics::Surface &dest) const;
	void drawJailGuard(Graphics::Surface &dest) const;
	bool jailFieldOverBen() const;
	bool jailGuardHoldsChat() const;
	bool chatReplyOwed() const;

	/// The mailbox full of dynamite, and the road it blows him into
	/// (mailbox.cpp).
	void armMailbox(int obj, byte item);
	void stepMailbox();
	void enterTown(int room);
	void stepTown();

	/// Room 34's Sluggs, the one hand that gives out the observatory keys
	/// (sluggs.cpp).
	bool armSluggs(int obj, byte verb, byte item);
	void enterSluggs(int room);
	void stepSluggs();
	void sluggsTalk(byte speaker, byte line, byte count);
	void sluggsSpeak();
	void sluggsPose(byte pose);

	/// Room 23's hippie, chained to the tree: the six dialog files
	/// his conversation swaps between, and the walkman traded for his game
	/// (hippie.cpp).
	bool armHippie(int obj, byte verb, int item);
	void startHippie();
	void stepHippie();
	void hippiePick();
	void hippieAnswer();
	void hippiePose(byte pose);
	void loadHippieScript(byte which);

	/// The 24h antique store, which is one long [0xa49f] machine and no
	/// rectangles at all (store.cpp).
	void startStore();
	void stepStore();
	void storeSpeak(byte code);
	void storeSalesmanLine(byte code);
	void storePose(byte pose);
	void storeWalk(int viaX, int viaY, int x, int y, int facing);
	void loadStoreScript(const char *name);
	bool roomHasSharedExit(int room) const;
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

	bool armLibrary(int obj, byte verb, bool item, int anchorX, int anchorY);
	void stepLibrary();
	void armLibrarySafe(int obj, byte item);
	void enterLibrary(int room);
	bool runLibraryBody(int obj, bool item);
	void standClearOfSafe(byte obj, WalkTarget &target) const;
	void stepLibrarySafe();
	void nextSpeech();
	void stopSpeech();

	/// Every room's dialog, printed the way tools/check_dialog.py prints it
	/// from the files: what is said, and how long each line stands (tal.cpp).
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

	/// Room 8's [0xa49f] machine: talking to the owl, and cracking the safe.
	byte _libraryStep;

	/// [0xa49c] as room 8 reads it: the free-running counter its safe steps
	/// time themselves off, cleared where each of them starts.
	uint16 _libraryPos;

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

	/// The room clock (roomtick.cpp): how long the player has been standing in
	/// the room this visit, in the units that room's own tick counts. Every
	/// room that has one zeroes it as its overlay opens, so one counter serves
	/// them all and loadRoom resets it.
	uint16 _roomClock;

	/// Room 3's [0xa49f] machine, less the opening the two steps in opening.cpp
	/// carry (lab.cpp). Zero when nothing is running.
	byte _labStep;
	/// Room 7's walk into the lift car: 0 idle, 1 his line, 2 WALKINC1 (bedroom.cpp).
	byte _bedroomLiftStep;
	/// Room 27's [0xa49f]: 3 while the manhole climb has the character (sewer.cpp).
	byte _mansionStep;
	bool _liftPending;			///< the computer's line is up; the panel follows it

	/// Room 19: the cover has been looked at and its line is still up. The
	/// cover comes off when the line comes down (observatory.cpp).
	bool _observatoryLook;
	bool _observatoryBreaker;	///< room 19's [0xa49f] 0x14, the walker away
	byte _observatoryStep;		///< room 19's [0xa49f]: 1-4 the slip, 0x64 the stairs
	uint16 _observatoryPos;		///< and its [0xa49c]
	bool _observatoryEncore;	///< the slip under way is the port's own
	byte _stairsDescents;		///< trips down from room 28, for the port's own fall (saved)
	bool _restoring;			///< syncGame is reloading the room, not an entry through a door

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

	/// The slots playing a bank that draws the character himself, one bit per
	/// slot. playCharacterAnim() sets a bit and showCharacter() takes the slot
	/// down again, so that the drawn-on Ben leaves the screen on the same frame
	/// the walker's Ben comes back to it.
	uint16 _characterAnimSlots;

	/// [0xa948] as the loop last saw it: the frame it comes back on is the one
	/// that has to rebuild the hover, since the port only hovers on motion.
	bool _cursorWasVisible;

	/// Room 35's [0xa49f] machine (sewer.cpp). Zero when nothing is running.
	byte _sewerStep;

	/// Room 13's, which is the same byte in the original (basement.cpp).
	byte _basementStep;

	/// Room 11's [0xa49f] machine, which is the tape the remote control starts
	/// (living.cpp).
	byte _livingStep;

	/// Room 31's, and the [0xa738] beside it that says which climb an arrival
	/// is going to start (cliff.cpp). [0xa49c] is kept per room, the way the
	/// lab and the library keep theirs.
	byte _cliffStep;
	byte _cliffClimb;
	uint16 _cliffPos;

	/// The point the climb waits for him at, and -- for the way up -- the point
	/// the click that started it had asked for, the original's [0xa732],
	/// [0xa734] and [0xa736].
	int _cliffX, _cliffY, _cliffFacing;
	int _cliffResumeX, _cliffResumeY, _cliffResumeFacing;

	/// Room 25's [0xa49f] machine and the counter it times itself off, and
	/// room 33's own state for the landing at the other end (mailbox.cpp).
	byte _mailboxStep;
	uint16 _mailboxPos;
	byte _townStep;

	/// Room 34's [0xa49f] machine and the conversation running beside it: the
	/// step, its counter, the outcome the two speakers are taking turns over
	/// ([0xa4a2]), whose turn it is ([0xa4a3]), how many lines are still owed
	/// ([0xad40]) and whether Sluggs has a line standing (sluggs.cpp).
	byte _sluggsStep;
	uint16 _sluggsPos;
	byte _sluggsLine;
	byte _sluggsSpeaker;
	byte _sluggsLeft;
	bool _sluggsSpeaking;
	bool _sluggsTalking;

	/// Room 32's statue: waiting for the look's own line to come down before
	/// the topic-0 conversation reopens (cemetery.cpp).
	uint16 _cemeteryPos;		///< room 32's [0xa49c]
	int _cemeteryZapPos;		///< [bp-4] after a zap, -1 when none is pending
	bool _cemeteryCursorOwed;	///< the phrase took the cursor; its line gives it back
	byte _cemeteryStep;	///< [0xa49f] while the cave is being walked into

	/// The two mazes: the step of the crystal-door scene, and the submode the
	/// last cell was left by, which says where the next one is entered from
	/// (maze.cpp). The cell itself is a state-block flag, [0xa77c].
	byte _mazeStep;
	byte _crystalStep;
	uint _crystalWait;

	/// Room 45's two-speaker run, the same four the DLGREQ runner keeps
	/// everywhere it appears (sluggs.cpp): whose turn, which outcome, how many
	/// are left, and whether a run is standing at all.
	byte _crystalSpeaker;
	byte _crystalLine;
	byte _crystalLeft;
	bool _crystalSpeaking;
	byte _mazePose;

	/// Room 48's pool: the step of the dive machine (pool.cpp).
	byte _poolStep;

	/// Room 46: the [0xa49f] machine step, the submode a click armed
	/// ([0xa87d]), the two bubble pools at ds:0xb06a and ds:0xb51e, and the
	/// water's row table ([0xae28]) with its phase ([0xb066]) (diving.cpp).
	struct DivingBubble {
		uint16 x;		///< 8.8
		uint16 y;		///< 9.7, dead past 0xf0 rows
		uint16 vy;
		int16 drift;
	};
	enum { kDivingBubbles = 100, kDivingWaveRows = 0x11e };
	byte _divingStep;
	byte _divingExit;
	DivingBubble _divingBubbles[2][kDivingBubbles];
	int16 _divingWave[kDivingWaveRows];
	uint16 _divingWavePhase;

	/// Room 41's own machines: the step of the pick-axe swing, or of whichever
	/// climb between the two cave mouths is running (shore.cpp).
	byte _shoreStep;

	/// Rooms 49 and 50's [0xa49f] step, their [0xa49c] count, and the steam
	/// flag as the last tick saw it (steam.cpp).
	byte _steamStep;
	uint _steamPos;
	byte _steamWas;
	/// A ladder arrival still owes the character his new room's light.
	bool _steamRelight;
	/// Room 49's light: passes left of a flicker burst, and the palette entries
	/// the level darkens, as the room loaded them (steam.cpp).
	uint _steamBurst;
	byte _steamLight[0x1a * 3];

	/// Room 22's arrival scene: the step of its machine, and ticks left of
	/// whichever of its two waits is running (park.cpp).
	byte _parkStep;
	uint _parkWait;
	bool _parkLock;				///< [0xa8a4]: the scene holds the camera

	/// Room 26's parrot machine (forest.cpp).
	byte _forestStep;
	uint _forestWait;
	bool _forestTalk;		///< a plain verb on the parrot waits for its line

	/// Room 21's Yodle: the step of his machine, [0xa49c] beside it, the run
	/// standing in [0xa4a2] and the menu reply owed (yodle.cpp).
	byte _yodleStep;
	uint _yodlePos;
	byte _yodleLine;
	byte _yodleLeft;
	byte _yodleSpeaker;
	bool _yodleSpeaking;
	byte _yodleReply;
	TalFile::Entry _yodleReplyEntry;
	int _yodleReplyTicks;
	bool _yodleAnswer;
	bool _yodleTalking;			///< his talk loop is up until the line comes down
	byte _yodleWater;			///< [0xa5eb], the puddle's frame, 1..30
	bool _yodleWaterDue;		///< [0xa5f9], the tick pair it waits out between frames

	/// Room 22's [0xa49f] machine for the phone, ticks left of its current
	/// wait, and room 56's simplified return, clicks taken so far
	/// (teleport.cpp).
	byte _teleportStep;
	uint _teleportWait;
	/// Room 56's [0xa49f] machine and its [0xa49c] wait (teleport.cpp).
	byte _shipStep;
	uint _shipWait;
	/// The hum's flashes (the room's tick, 0x07cc): [0x33ea] flashes left,
	/// [0x33e8] the level, [0x33ec] the tick-pair count, [0x33ee] a mix due,
	/// and the palette they mix from.
	byte _shipFlashes;
	int _shipFlashLevel;
	uint _shipFlashCount;
	bool _shipFlashDue;
	byte _shipFlashSource[256 * 3];

	/// Room 53's maintenance man: the room's [0xa49f] machine, his pose byte
	/// [0xa52c], and the answer owed to the conversation's last pick, which
	/// waits for the pick's own line to come down (corridor.cpp).
	struct HallMan {
		byte step;
		byte pose;
		byte reply;
		TalFile::Entry replyEntry;
		int replyTicks;
		uint replyTopic;
		bool answer;
		bool ended;				///< [0xa60f]: the pick ended the conversation
		bool speaking;			///< a line was up last tick, for [0xad1a]

		HallMan() : step(0), pose(0), reply(0), replyTicks(0), replyTopic(0),
					answer(false), ended(false), speaking(false) {}
	};
	HallMan _hallMan;

	/// Room 57's [0xa49f]: the card has gone into the terminal's slot and its
	/// line is up; the terminal opens once it is down (corridor.cpp).
	byte _terminalStep;

	/// Room 52: the gate sent him in without the mask, and the room's
	/// [0xa49f] machine step that arrests him (scanner.cpp).
	bool _scannerArrest;
	byte _scannerStep;

	/// Room 54's [0xa49f] machine: the ticket machine, and the number that is
	/// finally called (waiting.cpp).
	byte _waitingStep;

	/// [0xa49c] as room 54 reads it, cleared where each of its steps starts.
	uint16 _waitingPos;

	/// Room 60's [0xa49f] machine and [0xa49c] as it reads it (boss.cpp).
	byte _bossStep;
	uint16 _bossPos;

	/// And its conversation: whose turn it is, the outcome standing next, how
	/// many are left, and whether the boss's talking loop is up.
	byte _bossSpeaker;
	byte _bossLine;
	byte _bossLeft;
	bool _bossSpeaking;
	bool _bossTalking;

	/// Room 58's [0xa49f] and [0xa49c], the DLGREQ run the conversation is
	/// working through (the same runner boss.cpp has), where Ben's lines are
	/// anchored, the two prisoners' pose bytes [0xa52b]/[0xa52a], and the
	/// [0xa7bd]/[0xa7be] clock that settles the uncle after a talk.
	byte _jailStep;
	uint16 _jailPos;
	byte _jailSpeaker;
	byte _jailLine;
	byte _jailLeft;
	bool _jailSpeaking;
	int _jailBenX, _jailBenY;
	byte _jailUncle;
	byte _jailYodle;
	byte _jailClock;
	uint16 _jailClockPos;

	/// Room 58's guard (jailguard.cpp): the second character CHARANIM keeps
	/// for this one room. His sprite set, JAIL_GUA; where he stands, [0xa8c6]
	/// and [0xa8ca], and the 10.6 fixed-point mover behind it; the frame
	/// stepper at [0xbee2..0xbf02]; and his machine, [0xbefc], with the clock
	/// [0xbefa] it counts in.
	struct JailGuard {
		CharAnim anim;
		int x, y;
		int32 fx, fy;			///< [0xa8ce], [0xa8d2]: position << 6
		int16 vx, vy;			///< [0xa8d6], [0xa8d8]
		uint16 steps;			///< [0xbf00]
		byte heading;			///< [0xa8dc]
		bool arrived;			///< [0xbef8], the tick pair the steps run out

		int frame;				///< [0xa8da], a record of JAIL_GUA.DAT
		const byte *list;		///< the pose's frame list, or null
		int index;				///< [0xbf02]
		int start;				///< [0xbeea]
		int base;				///< [0xbef2]
		uint16 count;			///< [0xbee4]
		uint16 countInit;		///< [0xbeec]
		byte rate;				///< [0xbee6]
		byte rateCount;			///< [0xbee7]
		bool loop;				///< [0xbee2]
		bool backward;			///< [0xbee8]
		bool done;				///< [0xbef9], the tick pair the pose runs out
		byte pose;				///< [0xad19]

		byte step;				///< [0xbefc]
		uint16 clock;			///< [0xbefa]
		bool talking;			///< a line of his is up in pose 0xb
		byte reply;				///< the menu pick's answer, owed once Ben's line is down
	};
	JailGuard _guard;

	/// JAIL_RED.TBL: eight 256-entry colour maps, the force field's tint
	/// (CHARANIM:sub_14aa2), and the phase [0xa7af] it flickers between.
	byte _jailRed[8 * 256];
	bool _jailRedLoaded;
	byte _jailFieldPhase;

	/// [0xa8e0]/[0xa8e2]: a scene holding the camera on a point of its own
	/// rather than on Ben, or -1 while it follows him.
	int _scrollHold;

	/// Room 23's [0xa49f] machine and the answer standing beside it: the step,
	/// its counter, the dialog id owed to the pick that was just made, whether
	/// it is still waiting for that pick's own line to come down, and whether
	/// the hippie has a line standing (hippie.cpp).
	byte _hippieStep;
	uint16 _hippiePos;
	byte _hippieReply;
	TalFile::Entry _hippieReplyEntry;
	int _hippieReplyTicks;
	bool _hippieAnswer;
	int _hippieHandOff;		///< the file topic 6 swaps in once its reply is down, or -1
	bool _hippieTalking;

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

	/// Room 30's own machine: the step, its counter, the line the two speakers
	/// are taking turns over and whose turn it is, and the item the player has
	/// offered over the counter (store.cpp). The line survives the room, the
	/// way [0xa4a2] does: the return visit with the suit picks up where the
	/// trade's own banter stopped.
	byte _storeStep;
	uint16 _storePos;
	byte _storeLine;
	byte _storeSpeaker;
	byte _storeOffer;
	bool _storeWalked;
	bool _storeTalking;

	/// GFX:sub_26ebc, the resident random the store picks its refusal with.
	Common::RandomSource _rnd;

	/// Whether slot 0 is running room 13's arrival climb, which is what the
	/// original can read off the slot itself (basement.cpp).
	bool _basementClimbing;
	bool _basementWalkOff;		///< [0xa5fe]: walking off the ladder, cursor still away

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
	bool resumePlayRun();
	void restorePlayState(Common::SeekableReadStream *in, uint32 sinceTick);
	bool scrollWouldPan() const;
	void stepJailDoor();
	void stepParkHold();
	void stepYodleHold();
	void writePlayCamera(Common::WriteStream *out) const;
	void readPlayCamera(Common::SeekableReadStream *in);
	void writeFailureState(uint line);

	/// The keys a watched run answers to; false for any other key.
	bool playHotkey(const Common::KeyState &key);
	void playMessage(const Common::String &text);
};

} // End of namespace Alien

#endif
