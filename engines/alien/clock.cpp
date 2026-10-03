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

#include "common/system.h"

#include "alien/alien.h"

namespace Alien {

/**
 * The engine's own time.
 *
 * The original has no clock of its own either: everything it does is counted
 * in retraces (docs/timing_model.md). The port turns retraces into
 * milliseconds in one place per loop, and every one of those loops reads the
 * time and sleeps through millis() and sleep() below, so the speed of the
 * whole game is one setting (docs/autoplaytest_plan.md, phase 3):
 *
 *  - 1, real time: the backend's clock, untouched.
 *  - N > 1, watchable: the backend's clock run N times faster, and every sleep
 *    N times shorter, so a scripted run plays out in a window at speed.
 *  - 0, turbo: no waiting at all. A sleep moves a virtual clock forward by
 *    the time it asked for, so every loop sees exactly the time it would have
 *    seen at 1x -- the same ticks in the same order -- with nothing between
 *    them. Wall time drops out of the game altogether.
 */
uint32 AlienEngine::millis() const {
	if (_timescale == 0)
		return _virtualMillis;
	if (_timescale == 1)
		return g_system->getMillis();
	return _clockVirtualBase + (g_system->getMillis() - _clockRealBase) * _timescale;
}

void AlienEngine::sleep(uint32 ms) {
	if (_timescale == 0) {
		_virtualMillis += ms;
		return;
	}
	g_system->delayMillis(_timescale == 1 ? ms : ms / _timescale);
}

/**
 * Change speed without the clock jumping: whatever the engine time is now
 * stays the engine time, and only the rate from here on changes. The loops all
 * measure intervals against a time they read earlier, so a jump either way
 * would have run a burst of ticks or frozen the game for a while.
 */
void AlienEngine::setTimescale(uint scale) {
	const uint32 now = millis();
	_timescale = scale;
	_virtualMillis = now;
	_clockRealBase = g_system->getMillis();
	_clockVirtualBase = now;
	_presentLast = now;
}

/**
 * Turbo still composes every frame, so the screen a snap takes is the one 1x
 * would have shown, but handing it to the backend is pure cost: only a few
 * frames a second of game time go out.
 */
void AlienEngine::present() {
	static const uint32 kTurboPresentMillis = 100;

	if (_timescale == 0) {
		const uint32 now = millis();
		if (now - _presentLast < kTurboPresentMillis)
			return;
		_presentLast = now;
	}
	g_system->updateScreen();
}

} // End of namespace Alien
