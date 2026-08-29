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

#include "alien/alien.h"
#include "alien/charanim.h"
#include "alien/detection.h"

namespace Alien {

// The escape pod, and the seven steps of the sequence it runs.
//
// Room 59's per-frame entry (ovr_3b_0fa2, 0x64b-0x750) is a state machine on
// [0xa49f], which its opening subroutine starts at 0x32 -- but only when
// [0xa7d2] is set, the flag that says the pod is ready to leave. Every step but
// the first waits for the line before it to come down ([0xad1c], the pulse the
// dialog unit leaves when a line clears) or for the timeline [0xa49c] to run
// out, then speaks the next line and moves the pod's animation on.
//
// The last step is the end of the game: it sets [0x7dc5], the flag GAME.EXE
// reads at its exit cleanup (seg_main.asm:1614) to leave with `ax = 0x7b`.
// AI.COM tests exactly that code and runs `ANIMPLAY ALIEND.CDA /ALIENINCI
// /INFO1` on a match, so the ending clip is the launcher's doing, not the
// game's (docs/playthrough_findings.md findings #5 and #19). The port has no
// launcher to hand the win to, so it plays the clip itself once the loop the
// win flag stops has come apart -- which is the same order the original runs
// in, with the process boundary taken out.
static const int kEndingRoom = 59;
static const uint16 kEndingFlag = 0xa7d2;
static const uint kEndingSlot = 4;			///< the pod's own animation slot
static const uint kEndingWait = 0x19;		///< [0xa49c] > 0x19, the pause in step 0x34

// The dialog the machine speaks, one outcome code per step, out of the room's
// own TAL. Step 0x35's line goes through DLGREQ:sub_0c275 rather than
// DIALOG:queue_event: that routine also fires a per-room MIDAS cue keyed on
// handler_code, and none of its arms is room 59's, so what is left of it here
// is the line.
static const byte kEndingLine1 = 0x14;
static const byte kEndingLine2 = 0x15;
static const byte kEndingLine3 = 0x16;
static const byte kEndingLine4 = 0x17;

/**
 * Starts the sequence, if this room is the pod and the pod is ready.
 *
 * The opening subroutine's other business -- the extra bank, the slot-4 frame
 * and the pan to x = 0xc1 -- is either already in the room's lifted opening
 * effects (roominit.cpp guards a slot-4 play on the same [0xa7d2]) or is the
 * camera, which the port drives from the character instead.
 */
void AlienEngine::startEnding() {
	if (_room != kEndingRoom || _script.flag(kEndingFlag) != 1)
		return;

	_endingStep = 0x32;
	_endingPos = 0;
	_endingLoop = false;
	debugC(1, kDebugEnding, "ending: room %d, step 0x32", kEndingRoom);
}

/// Arms the sequence from the debug channel, which is the only way to reach it
/// until a playthrough can set [0xa7d2] by playing the game to the pod.
void AlienEngine::armEnding() {
	if (_room != kEndingRoom)
		return;

	_script.setFlag(kEndingFlag, 1);
	startEnding();
}

/// [0xad1c]: the line the machine put up has come down and nothing follows it.
bool AlienEngine::endingLineDone() const {
	return !_speech && _queueNext >= _queueCount;
}

void AlienEngine::speakEnding(byte code) {
	queueOutcome(_tal, code, _ben.walkX(), _ben.walkY() - Walker::kWalkPointY);
}

/**
 * One step of the machine, on the animation tick its per-frame entry ran on.
 */
void AlienEngine::stepEnding() {
	if (!_endingStep)
		return;

	// [0xa49c] advances with the animation tick (docs/timing.md), which is what
	// the pause in step 0x34 counts.
	_endingPos++;

	// [0xa53e]: while phase one is up, the room's tick calls MIDAS:0x112d on
	// slot 4 every frame, which restarts the cycle whenever it has run out.
	if (_endingLoop && !_anims.isBusy(kEndingSlot))
		_anims.play(kEndingSlot, 1, 9, 4, 1);

	switch (_endingStep) {
	case 0x32:
		speakEnding(kEndingLine1);
		_endingStep = 0x33;
		break;

	case 0x33:
		if (endingLineDone()) {
			_endingPos = 0;
			_endingStep = 0x34;
		}
		break;

	case 0x34:
		if (_endingPos > kEndingWait) {
			speakEnding(kEndingLine2);
			_endingStep = 0x35;
		}
		break;

	case 0x35:
		if (endingLineDone()) {
			speakEnding(kEndingLine3);
			// MIDAS:sub_198fe(1): the pod's engines, left cycling.
			_anims.play(kEndingSlot, 1, 9, 4, 1);
			_endingLoop = true;
			_endingStep = 0x36;
		}
		break;

	case 0x36:
		if (endingLineDone()) {
			// sub_198fe(2): the cycle stops on its first frame.
			_endingLoop = false;
			_anims.play(kEndingSlot, 1, 1, 0, 1);
			speakEnding(kEndingLine4);
			_endingStep = 0x37;
		}
		break;

	case 0x37:
		if (endingLineDone()) {
			// sub_198fe(3): the pod leaves.
			_anims.play(kEndingSlot, 10, 19, 5, 1);
			_endingPos = 0;
			_endingStep = 0x38;
		}
		break;

	case 0x38:
		// The original's guard here is [0xa4ee] == 0, a mode byte outside the
		// state block this port tracks and zero for the whole of a normal game.
		winGame();
		break;

	default:
		break;
	}

	debugC(2, kDebugEnding, "ending: step 0x%02x pos %u", _endingStep, _endingPos);
}

/**
 * [0x7dc5]: the game is won, so the loop stops and the clip plays after it.
 */
void AlienEngine::winGame() {
	_endingStep = 0;
	_won = true;
	_quit = true;
	debugC(1, kDebugEnding, "ending: won -- GAME.EXE exits with 0x7b here, and "
						   "AI.COM runs ANIMPLAY on the ending clip");
}

} // End of namespace Alien
