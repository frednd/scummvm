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

#include "backends/keymapper/action.h"
#include "backends/keymapper/keymapper.h"
#include "backends/keymapper/standard-actions.h"

#include "common/translation.h"

#include "engines/advancedDetector.h"

#include "alien/alien.h"

namespace Alien {

const char *AlienEngine::getGameId() const {
	return _gameDescription->gameId;
}

Common::Language AlienEngine::getLanguage() const {
	return _gameDescription->language;
}

} // End of namespace Alien

class AlienMetaEngine : public AdvancedMetaEngine<ADGameDescription> {
	const char *getName() const override {
		return "alien";
	}

	Common::Error createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const override;
	bool hasFeature(MetaEngineFeature f) const override;
	Common::KeymapArray initKeymaps(const char *target) const override;
};

Common::Error AlienMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new Alien::AlienEngine(syst, desc);
	return Common::kNoError;
}

bool AlienMetaEngine::hasFeature(MetaEngineFeature f) const {
	return f == kSupportsListSaves ||
		   f == kSupportsLoadingDuringStartup ||
		   f == kSupportsDeleteSave ||
		   f == kSimpleSavesNames ||
		   f == kSavesSupportMetaInfo ||
		   f == kSavesSupportThumbnail ||
		   f == kSavesSupportCreationDate ||
		   f == kSavesSupportPlayTime ||
		   f == kSavesUseExtendedFormat;
}

Common::KeymapArray AlienMetaEngine::initKeymaps(const char *target) const {
	using namespace Common;

	// The game is played with the mouse alone: the two buttons, and nothing
	// on the keyboard but Escape, which reaches the engine as a plain key.
	Keymap *engineKeyMap = new Keymap(Keymap::kKeymapTypeGame, "alien-default", _("Default keymappings"));
	Keymap *gameKeyMap = new Keymap(Keymap::kKeymapTypeGame, "game-shortcuts", _("Game keymappings"));
	Action *act;

	act = new Action(kStandardActionLeftClick, _("Left click"));
	act->setLeftClickEvent();
	act->addDefaultInputMapping("MOUSE_LEFT");
	act->addDefaultInputMapping("JOY_A");
	engineKeyMap->addAction(act);

	act = new Action(kStandardActionRightClick, _("Right click"));
	act->setRightClickEvent();
	act->addDefaultInputMapping("MOUSE_RIGHT");
	act->addDefaultInputMapping("JOY_B");
	engineKeyMap->addAction(act);

	act = new Action(kStandardActionToggleHotspots, _("Show hotspots"));
	act->setEvent(EVENT_HOTSPOTS_SHOW);
	act->addDefaultInputMapping("h");
	act->addDefaultInputMapping("JOY_X");
	gameKeyMap->addAction(act);

	KeymapArray keymaps(2);
	keymaps[0] = engineKeyMap;
	keymaps[1] = gameKeyMap;
	return keymaps;
}

#if PLUGIN_ENABLED_DYNAMIC(ALIEN)
	REGISTER_PLUGIN_DYNAMIC(ALIEN, PLUGIN_TYPE_ENGINE, AlienMetaEngine);
#else
	REGISTER_PLUGIN_STATIC(ALIEN, PLUGIN_TYPE_ENGINE, AlienMetaEngine);
#endif
