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

#ifndef ALIEN_ROOMS_ROOM_H
#define ALIEN_ROOMS_ROOM_H

#include "common/scummsys.h"

namespace Graphics {
struct Surface;
}

namespace Alien {

class AlienEngine;
struct WalkTarget;

/**
 * The hand-written half of a room: what its overlay does that the lifted
 * tables cannot carry.
 *
 * Each file under rooms/ is one of these. The engine holds them all and calls
 * every hook on every one of them, in the order rooms.cpp registers them; a
 * hook answers only for the rooms it belongs to and does nothing anywhere
 * else, the way each overlay's code only ever runs while its own room is up.
 * The order matters only where two files share a room (3, 22, 58), and there
 * it is the order the original's entry points run them in.
 *
 * The hooks are named for where the engine calls them, and each defaults to
 * doing nothing.
 */
class Room {
public:
	explicit Room(AlienEngine *vm) : _vm(vm) {}
	virtual ~Room() {}

	/// \name Opening a room, in the order loadRoom() runs them.
	/// @{

	/// The room actually entered for a way into `room`; not on a self-reload.
	virtual int redirect(int room) { return room; }
	/// The way in, before anything is read; not on a self-reload.
	virtual void approach(int room) {}
	/// Before the plate is read.
	virtual void preload(int room) {}
	/// A plate composed by the room rather than read from the table.
	virtual bool loadBackground(int room, Graphics::Surface &plate, byte *palette) { return false; }
	/// OBJ:sub_08567, the shared open of `room`: whatever state the visit
	/// before left.
	virtual void reset(int room) {}
	/// Before anything draws or registers by the room's view.
	virtual void chooseView(int room) {}
	/// After the table's banks are in.
	virtual void loadBanks(int room) {}
	/// Before the resident 10c9 plate routine.
	virtual void restorePlate(int room) {}
	/// After the plate routine.
	virtual void enter(int room) {}
	/// After the character is placed.
	virtual void placed(int room) {}
	/// After the room's dialog file is read.
	virtual void scriptLoaded(int room) {}
	/// The room is up: _room is set and the way in has been spoken.
	virtual void start() {}
	/// The last thing the open does: a camera the room holds.
	virtual void holdCamera() {}
	/// The scenes the room raises once it is up, ahead of the table's own.
	virtual void raiseScenes(int room) {}
	/// Rectangles the table cannot express, after it has registered its own.
	virtual void buildHotspots(int room) {}
	/// @}

	/// \name The game loop.
	/// @{

	/// Every master tick.
	virtual void tick() {}
	/**
	 * Every tick pair, ahead of the machines: what the room does on its own
	 * while nobody touches it -- a counter that runs while the player is in
	 * the room and fires once it is full. True when it moved a byte a hotspot
	 * guard reads, which in the original entry 1 answers on the next frame.
	 */
	virtual bool roomClock() { return false; }
	/// Every tick pair, the gate the rooms' own loops run under.
	virtual void tickPair() {}
	/// A line has just come down.
	virtual void lineDown() {}
	/// Every frame, after the conversation menu.
	virtual void frame() {}
	/// Every tick, once the camera has followed the walker.
	virtual void walkPhase() {}
	/// Every animation tick, ahead of the walker: the machines that are not
	/// tied to the room's loop.
	virtual void machine() {}
	/// @}

	/// \name Clicks, in the order finishAction() runs them.
	/// @{

	/// A left click, before the walk it starts is worked out.
	virtual void leftClick(int roomX, int y) {}
	/// The walk a click starts, once the geometry has named it.
	virtual void walkTarget(byte obj, int roomX, int y, bool action, WalkTarget &target) {}
	/// Ahead of everything, the room's script included.
	virtual void preAction(int obj, byte verb, byte item) {}
	/// The whole action; true when nothing else may see the click.
	virtual bool takeAction(int obj, byte verb, byte item, int anchorX, int anchorY) { return false; }
	/// Just ahead of the room's script.
	virtual void beforeScript(int obj, byte verb, byte item) {}

	enum Body {
		kBodyNone,		///< the room's script runs and decides
		kBodyHandled,	///< the room answered, and its script runs as well
		kBodyReplaced	///< the room answered instead of its script
	};
	/// A body of the room's own, in place of the lifted row or beside it.
	virtual Body body(int obj, byte verb, byte item, int anchorX, int anchorY) { return kBodyNone; }
	/// After the room's script.
	virtual void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) {}
	/// After the outcome the script queued is spoken, when it queued none.
	virtual void afterQueue(int obj, int anchorX, int anchorY) {}
	/// A submode the click ends in; true when the room plays it itself.
	virtual bool takeSubmode(int obj, byte verb, byte submode) { return false; }
	/// @}

	/// \name Walking and leaving.
	/// @{

	/// A walk the room moves itself, ahead of the router.
	virtual bool walk(int x, int y, int arrivalFacing) { return false; }
	/// A walk the router had no mask for.
	virtual bool walkUnrouted(int x, int y, int arrivalFacing) { return false; }
	/// Whether the room's tick runs the shared arrival test.
	virtual bool sharedExit(int room) const { return true; }
	/// An exit armed and reached; true when the room plays it itself.
	virtual bool hijackExit(byte submode) { return false; }
	/// An exit taken; true when the room answers it without leaving.
	virtual bool turnExit(byte &submode) { return false; }
	/// @}

	/// \name Drawing, in the order redraw() composes.
	/// @{

	/// Over the slots, under everything else.
	virtual void drawOverSlots(Graphics::Surface &dest) const {}
	/// Under the character.
	virtual void drawUnderCharacter(Graphics::Surface &dest) const {}
	/// Over the character and his foreground.
	virtual void drawOverCharacter(Graphics::Surface &dest) const {}
	/// Over the whole playfield.
	virtual void drawOverPlayfield(Graphics::Surface &dest) const {}
	/// @}

	/// \name What the rest of the engine asks.
	/// @{

	/// [0xa4a1]: a conversation of the room's own is running.
	virtual bool conversationRunning() const { return false; }
	/// The room has the character, so he may not idle.
	virtual bool holdsCharacter() const { return false; }
	/// The room has the input, so no click and no hover.
	virtual bool holdsInput() const { return false; }
	/// The room still has a reply to a conversation pick to speak.
	virtual bool replyOwed() const { return false; }
	/// The room is in the middle of something a scripted run must wait out.
	virtual bool busy() const { return false; }
	/// The game is not starting fresh: a load, or a run that names its room.
	virtual void skipOpening() {}
	/// @}

protected:
	AlienEngine *_vm;
};

} // End of namespace Alien

#endif
