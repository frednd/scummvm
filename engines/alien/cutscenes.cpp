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

// The cutscene tables are no longer written here.
//
// They used to be generated C++ -- 14 dispatch arms, 15 records out of
// GAME.EXE's data segment, 44 scene procedures lifted out of
// disasm/seg_cutscene.asm, and the 15 call sites the rooms raise their ids
// from -- so changing one meant regenerating this file and rebuilding. The lift
// now writes data/lift/cutscenes.json (tools/gen_cutscenes.py --json),
// tools/gen_pack.py turns that into ALIEN.DAT, and what is left here is the
// reader the rest of the engine already spoke to.
//
// See docs/data_pack.md.

#include "alien/cutscenes.h"
#include "alien/pack.h"

namespace Alien {

// The pack the tables are read out of, handed over as the engine starts. The
// accessors below are free functions the whole engine calls, which is why this
// is held here rather than passed through every one of them.
static const AlienPack *g_pack = nullptr;

void setCutscenePack(const AlienPack *pack) {
	g_pack = pack;
}

bool cutscenesLoaded() {
	return g_pack && g_pack->hasCutscenes();
}

const CutsceneArm *cutsceneArm(byte id) {
	if (!g_pack)
		return nullptr;
	const Common::Array<CutsceneArm> &arms = g_pack->arms();
	for (uint i = 0; i < arms.size(); i++)
		if (arms[i].id == id)
			return &arms[i];
	return nullptr;
}

uint cutsceneArmCount() {
	return g_pack ? g_pack->arms().size() : 0;
}

const CutsceneArm *cutsceneArmAt(uint index) {
	return index < cutsceneArmCount() ? &g_pack->arms()[index] : nullptr;
}

const CutsceneRecord *cutsceneRecord(uint number) {
	// Record 0 is the null one every unused pointer of a record points at, so
	// it is a slot rather than a scene: the loader's numbering runs from 1.
	if (!g_pack || number == 0 || number >= g_pack->records().size())
		return nullptr;
	return &g_pack->records()[number];
}

uint cutsceneRecordCount() {
	return g_pack && !g_pack->records().empty() ? g_pack->records().size() - 1 : 0;
}

const CutsceneStep *cutsceneSteps(const CutsceneRecord &record, uint &count) {
	count = 0;
	if (!g_pack)
		return nullptr;
	const Common::Array<CutsceneStep> &steps = g_pack->steps();
	if (record.firstStep + record.stepCount > steps.size())
		return nullptr;
	count = record.stepCount;
	return count ? &steps[record.firstStep] : nullptr;
}

const ScriptEffect *cutsceneProcEffects(uint proc, uint &count) {
	count = 0;
	if (!g_pack || proc >= g_pack->procs().size())
		return nullptr;
	const CutsceneProc &p = g_pack->procs()[proc];
	if (p.first + p.count > g_pack->effects().size())
		return nullptr;
	count = p.count;
	return count ? &g_pack->effects()[p.first] : nullptr;
}

uint cutsceneProcAddr(uint proc) {
	return g_pack && proc < g_pack->procs().size() ? g_pack->procs()[proc].addr : 0;
}

uint cutsceneProcCount() {
	return g_pack ? g_pack->procs().size() : 0;
}

const byte *cutsceneFrameList(uint first, uint count) {
	if (!g_pack || !count)
		return nullptr;
	const Common::Array<byte> &pool = g_pack->frameLists();
	if (first + count > pool.size())
		return nullptr;
	return &pool[first];
}

const ScriptEffect *cutsceneArmEffects(const CutsceneArm &arm, uint &count) {
	count = 0;
	if (!g_pack || arm.first + arm.count > g_pack->effects().size())
		return nullptr;
	count = arm.count;
	return count ? &g_pack->effects()[arm.first] : nullptr;
}

const CutsceneTrigger *cutsceneTriggers(int room, uint &count) {
	count = 0;
	if (!g_pack)
		return nullptr;

	// A room's triggers are stored together and in the order its enter routine
	// reaches them, so the run of rows for one room is what a caller wants.
	const Common::Array<CutsceneTrigger> &triggers = g_pack->triggers();
	for (uint i = 0; i < triggers.size(); i++) {
		if (triggers[i].room != room)
			continue;
		while (i + count < triggers.size() && triggers[i + count].room == room)
			count++;
		return &triggers[i];
	}
	return nullptr;
}

uint cutsceneTriggerCount() {
	return g_pack ? g_pack->triggers().size() : 0;
}

const CutsceneTrigger *cutsceneTriggerAt(uint index) {
	return index < cutsceneTriggerCount() ? &g_pack->triggers()[index] : nullptr;
}

} // End of namespace Alien
