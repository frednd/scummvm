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

#ifndef ALIEN_ROOMS_JAIL_H
#define ALIEN_ROOMS_JAIL_H

#include "alien/charanim.h"
#include "alien/rooms/room.h"

namespace Alien {

/// Room 58: the corridor's conversation through the bars and its two ways out,
/// the escape back to the ship (jail.cpp), and the guard, the force field over
/// the cells and the card the guard leaves behind (jailguard.cpp).
class JailRoom : public Room {
public:
	explicit JailRoom(AlienEngine *vm);

	void start() override;
	void buildHotspots(int room) override;
	void tickPair() override;
	void beforeScript(int obj, byte verb, byte item) override;
	void afterScript(int obj, byte verb, byte item, int anchorX, int anchorY) override;
	bool walkUnrouted(int x, int y, int arrivalFacing) override;
	void drawUnderCharacter(Graphics::Surface &dest) const override;
	void drawOverCharacter(Graphics::Surface &dest) const override;
	bool conversationRunning() const override { return _jailSpeaking; }
	bool replyOwed() const override;
	bool busy() const override;

private:
	void armJailLcd(int obj, byte item);
	bool armJailTalk(int obj, byte verb);
	bool armJailShackle(int obj, byte verb);
	void jailShaftArrival();
	void stepJailDoor();
	void jailConversation();
	void jailSpeak();
	void jailUnclePose(byte pose);
	void jailYodlePose(byte pose);

	void startJailGuard();
	void stepJailGuard();
	void jailGuardPose(byte pose);
	void jailGuardAnimate();
	void jailGuardMove();
	void jailGuardWalk(int x, int y);
	void jailGuardSpeak(byte code);
	void jailGuardMachine();
	void jailGuardTrigger();
	void jailGuardAnchor(int &x, int &y) const;
	bool armJailCard(int obj, byte verb);
	void drawJailField(Graphics::Surface &dest) const;
	void drawJailGuard(Graphics::Surface &dest) const;
	bool jailFieldOverBen() const;

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

	/// Room 58's guard: the second character CHARANIM keeps
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
};

} // End of namespace Alien

#endif
