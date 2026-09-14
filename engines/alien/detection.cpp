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

#include "base/plugins.h"

#include "engines/advancedDetector.h"
#include "engines/game.h"

#include "alien/detection.h"

static const PlainGameDescriptor alienGames[] = {
	{"alien", "Alien Incident"},
	{nullptr, nullptr}
};

static const DebugChannelDef debugFlagList[] = {
	{Alien::kDebugGraphics, "graphics", "Graphics and blitting"},
	{Alien::kDebugResource, "resource", "Resource loading"},
	{Alien::kDebugAnim, "anim", "Play every animation slot on entering a room"},
	{Alien::kDebugWalk, "walk", "Resolve a click on every hotspot on entering a room"},
	{Alien::kDebugHotspots, "hotspots", "List the rectangles a room registers on entering it"},
	{Alien::kDebugRooms, "rooms", "Room exits: what a click arms and where it leads"},
	{Alien::kDebugItems, "items", "The inventory: the list, the bar and item use"},
	{Alien::kDebugSound, "sound", "Sound effects: the sample banks, the voices and the queue"},
	{Alien::kDebugSave, "save", "Save and load: the original saves and the round trip"},
	{Alien::kDebugMusic, "music", "Music: the S3M replayer, its module table and its slots"},
	{Alien::kDebugVideo, "video", "Video: the MA1 elevator clip and the CDA2 cutscenes"},
	{Alien::kDebugPlay, "play", "Run a scripted playthrough from a command file"},
	{Alien::kDebugCutscene, "cutscene", "Cutscenes: the scene-id dispatch, the records and their procedures"},
	{Alien::kDebugOcclusion, "occlusion", "The foreground rectangles that hide the character"},
	{Alien::kDebugEnding, "ending", "Room 59's escape-pod sequence, and the ending clip after it"},
	{Alien::kDebugBedroom, "bedroom", "Room 7's light switch, and the plates it swaps"},
	{Alien::kDebugLift, "lift", "The lab computer, its panel and the lift car [0xa700]"},
	{Alien::kDebugLight, "light", "The room light maps, and the character palette they scale"},
	{Alien::kDebugChat, "chat", "The conversation menu and the tree behind it"},
	{Alien::kDebugPlate, "plate", "What a room stamps into its plate as it opens"},
	{Alien::kDebugScale, "scale", "The depth scale the character is drawn at, and the rooms that set it"},
	{Alien::kDebugDialog, "dialog", "Every room's lines: what is said, and how long each one stands"},
	DEBUG_CHANNEL_END
};

namespace Alien {

static const ADGameDescription gameDescriptions[] = {
	// CD release, installed to hard disk. One install carries all four text
	// languages (USESTRS.ENG/FIN/FRA/GER plus TALFILES/ENG and TALFILES/FIN),
	// so the entry is multi-language rather than one entry per language.
	{
		"alien",
		"CD",
		AD_ENTRY2s("GAME.EXE", "3f8b9a6c0dfd515a632154eb82dfec35", 202768,
				   "OBJFILE.PCX", "d6115a4bc1bbffde628a74978b6b5a0f", 17844),
		Common::UNK_LANG,
		Common::kPlatformDOS,
		ADGF_NO_FLAGS,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace Alien

class AlienMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	AlienMetaEngineDetection() : AdvancedMetaEngineDetection(Alien::gameDescriptions, alienGames) {
	}

	const char *getName() const override {
		return "alien";
	}

	const char *getEngineName() const override {
		return "Alien Incident";
	}

	const char *getOriginalCopyright() const override {
		return "Alien Incident (C) 1996 Housemarque";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}
};

REGISTER_PLUGIN_STATIC(ALIEN_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, AlienMetaEngineDetection);
