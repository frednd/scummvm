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
};

Common::Error AlienMetaEngine::createInstance(OSystem *syst, Engine **engine, const ADGameDescription *desc) const {
	*engine = new Alien::AlienEngine(syst, desc);
	return Common::kNoError;
}

bool AlienMetaEngine::hasFeature(MetaEngineFeature f) const {
	return false;
}

#if PLUGIN_ENABLED_DYNAMIC(ALIEN)
	REGISTER_PLUGIN_DYNAMIC(ALIEN, PLUGIN_TYPE_ENGINE, AlienMetaEngine);
#else
	REGISTER_PLUGIN_STATIC(ALIEN, PLUGIN_TYPE_ENGINE, AlienMetaEngine);
#endif
