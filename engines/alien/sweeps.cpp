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
#include "alien/detection.h"
#include "alien/occlusion.h"
#include "alien/roominit.h"

namespace Alien {

// How far the debug room tour walks before it stops. The mansion's exits lead
// back into each other, so a tour never ends on its own.
static const uint kTourHops = 20;

// How long the music loudness render runs.
static const uint kMusicSeconds = 10;

// How many frames of each cutscene the checksum sweep decodes. The intro is
// 74 MB; a prefix proves the codec and keeps the run short.
static const int kSweepFrames = 200;

void AlienEngine::dumpHotspots() {
	// Log the registered hotspots for this room in the current game state.
	debugC(1, kDebugHotspots, "spots: room %d, %u registered", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		Common::String outcomes;
		for (uint o = 0; o < spot.outcomeCount; o++)
			outcomes += Common::String::format("%s%u", o ? "," : "", spot.outcomes[o]);

		debugC(1, kDebugHotspots, "spot: %3d,%3d..%3d,%3d obj %3u verb %3u label %3u -> %s",
			   spot.x1, spot.y1, spot.x2, spot.y2, spot.obj, spot.verb, spot.label,
			   outcomes.c_str());
	}
}

void AlienEngine::sweepWalkGeometry() {
	// Resolve a click on the middle of every hotspot in the room and print the
	// target coordinates and facing direction.
	debugC(1, kDebugWalk, "geom: room %d, %u hotspots", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		const int x = (spot.x1 + spot.x2) / 2;
		const int y = (spot.y1 + spot.y2) / 2;

		WalkTarget target;
		if (!_script.walkTarget(x, y, spot.obj, target))
			continue;

		debugC(1, kDebugWalk, "geom: obj %3u click %3d,%3d -> %3d,%3d facing %2u submode %u",
			   spot.obj, x, y, target.x, target.y, target.facing, target.submode);

		// And the route the walker would take there from his current position.
		WalkRoute route;
		if (!_walk.plotRoute(_ben.walkX(), _ben.walkY(), target.x, target.y, route))
			continue;

		Common::String path;
		for (uint p = 0; p < route.count; p++)
			path += Common::String::format("%s%d,%d", p ? " " : "",
										   route.points[p].x, route.points[p].y);
		debugC(1, kDebugWalk, "route: %s", path.c_str());
	}
}

void AlienEngine::sweepClicks() {
	// Every hotspot of the room clicked in turn, with an empty hand, and the list
	// printed after each one. This runs the room's own bodies, so the puzzle state
	// moves as it would in play -- it is a diagnostic, not a way to play.
	debugC(1, kDebugItems, "click-sweep: room %d, %u hotspots", _room, _spots.size());

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot spot = _spots[i];
		const bool handled = _script.run(spot.obj, spot.verb);

		Common::String carried;
		for (uint index = 1; index < Inventory::kListSize; index++) {
			if (_inventory.at(index))
				carried += Common::String::format(" %u", _inventory.at(index));
		}

		debugC(1, kDebugItems, "click-sweep: object %3u verb %2u %s carrying:%s",
			   spot.obj, spot.verb, handled ? "handled" : "passed ", carried.c_str());
	}

	// And then every combination the room's own script has a body for, with the
	// item put in hand first: this is the path a click takes with something held,
	// so it exercises the third part of the block key end to end.
	uint count = 0;
	const ScriptBlock *blocks = scriptForRoom(_room, count);
	for (uint i = 0; i < count; i++) {
		if (!blocks || blocks[i].item < 0)
			continue;

		const byte item = (byte)blocks[i].item;
		const byte obj = blocks[i].obj < 0 ? 0 : (byte)blocks[i].obj;
		_inventory.add(item);
		const bool handled = _script.run(obj, kVerbUseTo, item);
		debugC(1, kDebugItems, "use-sweep: item %2u on object %3u %s, %s",
			   item, obj, handled ? "handled" : "passed ",
			   _inventory.has(item) ? "still carried" : "given up");
	}
}

void AlienEngine::dumpItemUses() {
	// Every item-use combination the room-script table holds, in the order the
	// overlays wrote them. This is the third part of a block's key, and it is
	// what a click with something in hand matches on.
	for (int room = 1; room <= StaticTables::kRoomCount; room++) {
		uint count = 0;
		const ScriptBlock *blocks = scriptForRoom(room, count);
		if (!blocks)
			continue;

		for (uint i = 0; i < count; i++) {
			if (blocks[i].item < 0)
				continue;
			debugC(1, kDebugItems, "use: room %2d block %2u item %2d (%s) on object %3d",
				   room, i, blocks[i].item,
				   _inventory.name((byte)blocks[i].item).c_str(), blocks[i].obj);
		}
	}
}

void AlienEngine::sweepItemLooks() {
	// Look at every item until its counter has been round its record and then
	// some, verifying wrap-around and stop behaviors.
	for (int item = 1; item <= StaticTables::kItemInUse; item++) {
		const int clicks = _tables.itemArity(item) + 2;
		for (int i = 0; i < clicks; i++)
			_inventory.lookOutcome(_tables, (byte)item);
	}
}

void AlienEngine::dumpSfx() {
	debugC(1, kDebugSound, "banks: %d in the table", StaticTables::kSfxBankCount);

	for (int bank = 0; bank < StaticTables::kSfxBankCount; bank++) {
		SoundBank loaded;
		if (!loaded.load(_tables.sfxName(bank)))
			continue;

		uint samples = 0;
		for (uint slot = 1; slot <= loaded.slotCount(); slot++) {
			if (loaded.sample(slot))
				samples++;
		}
		debugC(1, kDebugSound, "bank %2d %-18s %2u slots %2u samples", bank,
			   loaded.file().c_str(), loaded.slotCount(), samples);

		for (uint slot = 1; slot <= loaded.slotCount(); slot++) {
			const SoundBank::Sample *sample = loaded.sample(slot);
			if (!sample)
				continue;
			debugC(1, kDebugSound, "sample %2d %2u %6u bytes %5u Hz %s", bank, slot,
				   sample->length, sample->c2spd, sample->name.c_str());
		}
	}

	// Which bank each room's effects come out of. Index 0 is also the table's
	// fill value, so a room reading MAN_FX_1 may simply never have been assigned.
	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		const byte bank = _tables.sfxBank(room);
		debugC(1, kDebugSound, "room %2d bank %3d %s", room, bank,
			   bank == StaticTables::kSfxBankNone ? "-" : _tables.sfxName(bank).c_str());
	}
}

void AlienEngine::dumpMusic() {
	debugC(1, kDebugMusic, "music: %d modules, %d slots", StaticTables::kMusicCount,
		   StaticTables::kMusicSlotCount);

	for (int i = 0; i < StaticTables::kMusicCount; i++) {
		S3MModule module;
		if (!module.load(_tables.musicName(i)))
			continue;

		debugC(1, kDebugMusic, "module %2d %-18s %2u channels %3u orders %2u patterns "
			   "%2u samples speed %2u tempo %3u", i, _tables.musicName(i).c_str(),
			   module.channelCount(), module.orderCount(), module.patternCount(), module.sampleCount(),
			   module.speed(), module.tempo());
	}

	for (int slot = 0; slot < StaticTables::kMusicSlotCount; slot++) {
		const byte module = _tables.musicSlotModule(slot);
		debugC(1, kDebugMusic, "slot %2d module %2d order %3d %s", slot, module,
			   _tables.musicSlotOrder(slot), _tables.musicName(module).c_str());
	}
}

/** One effect's or block's guards, as `when [0xa6fa] == 0`. */
static Common::String condText(const ScriptCond *conds, uint count) {
	Common::String out;
	for (uint i = 0; i < count; i++) {
		out += i ? " && " : " when ";
		// An item guard names an item id, not an address (ScriptCondKind).
		if (conds[i].kind == kCondItem)
			out += Common::String::format("item %d %s %d", conds[i].addr,
										  conds[i].negate ? "!=" : "==", conds[i].value);
		// The ordering guard, which reads its address as a word.
		else if (conds[i].kind == kCondAbove || conds[i].kind == kCondAboveByte)
			out += Common::String::format("[0x%04x] %s %d", conds[i].addr,
										  conds[i].negate ? "<=" : ">", conds[i].value);
		else
			out += Common::String::format("[0x%04x] %s %d", conds[i].addr,
										  conds[i].negate ? "!=" : "==", conds[i].value);
	}
	return out;
}

/** The lifted opcodes by name, so a dumped effect reads as its call did. */
static const char *opName(byte op) {
	static const char *const kNames[] = {
		"unsupported", "set_flag", "set_action_handled", "set_game_submode",
		"queue_event", "anim_play_mode1", "anim_play_mode2", "anim_play_mode3",
		"sound", "play_sample", "inv_add", "inv_remove", "inv_replace", "inv_has",
		"add_flag", "char_place", "music_play_slot",
		"anim_play_mode4", "anim_play_mode5", "anim_play_mode6",
		"anim_play_mode7", "anim_play_mode8"
	};
	return op < ARRAYSIZE(kNames) ? kNames[op] : "?";
}

/**
 * The cutscenes: the scene-id dispatch, the records, and procedures.
 *
 * Dumps the cutscene definitions, records, and procedures.
 */
void AlienEngine::dumpCutscenes() {
	debugC(1, kDebugCutscene, "cutscenes: %d arms, %d records, %d procedures",
		   cutsceneArmCount(), cutsceneRecordCount(), cutsceneProcCount());

	for (uint i = 0; i < cutsceneArmCount(); i++) {
		const CutsceneArm &arm = *cutsceneArmAt(i);
		Common::String records;
		for (uint r = 0; r < arm.recordCount; r++)
			records += Common::String::format("%s%d", r ? "," : "", arm.records[r]);
		if (arm.studio)
			records = "studio";

		Common::String flags;
		uint count = 0;
		const ScriptEffect *effects = cutsceneArmEffects(arm, count);
		for (uint e = 0; e < count; e++)
			flags += Common::String::format(" [0x%04x] = %d", effects[e].args[0],
											effects[e].args[1]);

		debugC(1, kDebugCutscene, "arm %2d latch 0x%04x records %-6s%s%s", arm.id,
			   arm.latch, records.c_str(),
			   condText(arm.guards, arm.guardCount).c_str(), flags.c_str());
	}

	for (uint i = 0; i < cutsceneTriggerCount(); i++) {
		const CutsceneTrigger &trigger = *cutsceneTriggerAt(i);
		debugC(1, kDebugCutscene, "trigger room %2d scene %2d%s", trigger.room,
			   trigger.scene, condText(trigger.guards, trigger.guardCount).c_str());
	}

	for (uint n = 1; n <= cutsceneRecordCount(); n++) {
		const CutsceneRecord &rec = *cutsceneRecord(n);
		Common::String banks;
		for (uint b = 0; b < ARRAYSIZE(rec.banks); b++)
			if (rec.banks[b])
				banks += Common::String::format(" %d:%s", b, rec.banks[b]);

		Common::String subs;
		for (uint s = 0; s < ARRAYSIZE(rec.subProcs); s++)
			subs += Common::String::format("%s%d", s ? "," : "", rec.subProcs[s]);

		debugC(1, kDebugCutscene,
			   "record %2d %-13s %-13s music %2d proc %2d subs %s"
			   " pts %d,%d,%d,%d rgb %d,%d,%d,%d,%d,%d banks%s",
			   n, rec.pcx, rec.tal ? rec.tal : "-", rec.music,
			   rec.mainProc, subs.c_str(),
			   rec.points[0], rec.points[1], rec.points[2], rec.points[3],
			   rec.colors[0], rec.colors[1], rec.colors[2], rec.colors[3],
			   rec.colors[4], rec.colors[5], banks.c_str());

		uint stepCount = 0;
		const CutsceneStep *steps = cutsceneSteps(rec, stepCount);
		Common::String stream;
		for (uint s = 0; s < stepCount; s++) {
			stream += s ? " " : "";
			if (steps[s].op == kStepPause)
				stream += Common::String::format("pause %d", steps[s].arg);
			else if (steps[s].op == kStepBeat)
				stream += "beat";
			else
				stream += Common::String::format("%c:%d",
												 steps[s].op == kStepSpeakA ? 'a' : 'b',
												 steps[s].arg);
		}
		debugC(1, kDebugCutscene, "steps %2d %s", n, stream.c_str());
	}

	for (uint p = 0; p < cutsceneProcCount(); p++) {
		uint count = 0;
		const ScriptEffect *effects = cutsceneProcEffects(p, count);
		for (uint e = 0; e < count; e++) {
			const ScriptEffect &effect = effects[e];
			Common::String args;
			for (uint a = 0; a < effect.argCount; a++)
				args += Common::String::format("%s%d", a ? ", " : "", effect.args[a]);
			debugC(1, kDebugCutscene, "proc %2d 0c55:%04x %s(%s)%s", p,
				   cutsceneProcAddr(p), opName(effect.op), args.c_str(),
				   condText(effect.guards, effect.guardCount).c_str());
		}
	}
}

/**
 * Dumps every foreground occlusion rectangle, room by room.
 */
void AlienEngine::dumpOcclusion() {
	debugC(1, kDebugOcclusion, "occlusion: %u rectangles", occlusionRectCount());

	for (int room = 0; room <= StaticTables::kRoomCount; room++) {
		uint count = 0;
		const OcclusionRect *rects = occlusionRects(room, count);
		for (uint i = 0; i < count; i++) {
			const OcclusionRect &rect = rects[i];
			Common::String ben;
			if (rect.benYBelow >= 0)
				ben = Common::String::format(" y < %d", rect.benYBelow);

			uint guardCount = 0;
			const ScriptCond *guards = occlusionGuards(rect, guardCount);
			Common::String flags;
			for (uint g = 0; g < guardCount; g++)
				flags += Common::String::format(" [0x%04x] %s %d", guards[g].addr,
												guards[g].negate ? "!=" : "==",
												guards[g].value);

			debugC(1, kDebugOcclusion,
				   "rect room %2d src %3d,%3d dst %3d,%3d size %3dx%3d%s%s", room,
				   rect.srcX, rect.srcY, rect.dstX, rect.dstY, rect.width, rect.height,
				   ben.c_str(), flags.c_str());
		}
	}
}

/**
 * Every music cue the engine can fire, room by room.
 *
 * A room starts its theme either in room opening effects or in the body of
 * a click.
 */
void AlienEngine::sweepMusicCues() {
	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		uint initCount = 0;
		const ScriptEffect *init = roomInitEffects(room, initCount);
		for (uint i = 0; init && i < initCount; i++) {
			if (init[i].op != kOpMusic)
				continue;
			debugC(1, kDebugMusic, "cue room %2d entry           slot %2d%s", room,
				   init[i].args[0], condText(init[i].guards, init[i].guardCount).c_str());
		}

		uint blockCount = 0;
		const ScriptBlock *blocks = scriptForRoom(room, blockCount);
		for (uint b = 0; blocks && b < blockCount; b++) {
			const ScriptBlock &block = blocks[b];
			for (uint e = 0; e < block.count; e++) {
				const ScriptEffect &effect = *scriptEffect(block.first + e);
				if (effect.op != kOpMusic)
					continue;

				// A click's cue is guarded twice over: by the state the whole
				// body sits under, and by the arm inside it the cue is in.
				Common::String conds = condText(block.conds, block.condCount);
				const Common::String arms = condText(effect.guards, effect.guardCount);
				if (conds.empty())
					conds = arms;
				else if (!arms.empty())
					conds += " && " + Common::String(arms.c_str() + 6);

				debugC(1, kDebugMusic, "cue room %2d obj %3d verb %2d slot %2d%s", room,
					   block.obj, block.verb, effect.args[0], conds.c_str());
			}
		}
	}
}

/**
 * Render each module from its own start and log the loudness second by second
 * for verification.
 */
void AlienEngine::renderMusic() {
	Common::Array<int16> buffer;

	for (int i = 0; i < StaticTables::kMusicCount; i++) {
		S3MModule module;
		if (!module.load(_tables.musicName(i)))
			continue;

		Audio::AudioStream *stream = makeSongStream(module, 0);
		if (!stream)
			continue;

		// The tracker mixes in stereo, so a second is measured over the two
		// channels folded back into the one the modules were written for.
		const int frames = stream->getRate();
		const int channels = stream->isStereo() ? 2 : 1;
		buffer.resize(frames * channels);
		for (uint second = 0; second < kMusicSeconds; second++) {
			const int got = stream->readBuffer(&buffer[0], frames * channels) / channels;

			uint64 square = 0;
			for (int f = 0; f < got; f++) {
				int value = buffer[f * channels];
				if (channels == 2)
					value = (value + buffer[f * 2 + 1]) / 2;
				square += (int64)value * value;
			}

			const uint rms = got ? (uint)sqrt((double)(square / got)) : 0;
			debugC(3, kDebugMusic, "rms %-18s second %2u %5u", _tables.musicName(i).c_str(),
				   second, rms);
		}
		delete stream;
	}
}

void AlienEngine::dumpVideo() {
	// What the two containers say about themselves, mirrored by
	MA1Decoder lift;
	if (lift.loadFile(Common::Path(kLiftClip))) {
		debugC(1, kDebugVideo, "ma1 %s: %d frames %dx%d", kLiftClip, lift.getFrameCount(),
			   lift.getWidth(), lift.getHeight());
		lift.close();
	}

	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;
		debugC(1, kDebugVideo, "cda2 %s: %u frames %dx%d %u Hz %u languages",
			   kCutscenes[i], video.frameCount(), video.getWidth(), video.getHeight(),
			   video.sampleRate(), video.languageCount());
		video.close();
	}
}

/**
 * Log the subtitle font face glyph by glyph, along with bitmap checksums.
 */
void AlienEngine::dumpVideoFont() {
	if (!_videoFont.isLoaded() && !_videoFont.load())
		return;

	for (uint i = 0; i < _videoFont.glyphCount(); i++) {
		const AnimFont::Glyph &g = _videoFont.glyph(i);
		uint32 sum = 2166136261u;
		for (uint p = 0; p < g.pixels.size(); p++)
			sum = (sum ^ g.pixels[p]) * 16777619u;
		debugC(4, kDebugVideo, "font glyph %3u code %02x w%d h%d y%d %08x",
			   i, g.code, g.width, g.rows, g.yAdjust, sum);
	}
}

/**
 * Decode frames without showing them and print a checksum of each.
 *
 * Frames are differences against the frames before them, so a checksum that
 * matches the Python decoder's for frame n says every frame up to n decoded the
 * same way, palette and all.
 */
static uint32 frameChecksum(const Graphics::Surface &frame, const byte *palette) {
	uint32 sum = 2166136261u;
	const byte *pixels = (const byte *)frame.getPixels();
	for (int i = 0; i < frame.w * frame.h; i++)
		sum = (sum ^ pixels[i]) * 16777619u;
	if (palette) {
		for (int i = 0; i < 256 * 3; i++)
			sum = (sum ^ palette[i]) * 16777619u;
	}
	return sum;
}

void AlienEngine::sweepVideoFrames() {
	MA1Decoder lift;
	if (lift.loadFile(Common::Path(kLiftClip))) {
		for (uint i = 0; i < lift.getFrameCount() && !shouldQuit(); i++) {
			const Graphics::Surface *frame = lift.decodeNextFrame();
			if (!frame)
				break;
			debugC(2, kDebugVideo, "ma1 frame %4u %08x", i, frameChecksum(*frame, lift.getPalette()));
		}
		lift.close();
	}

	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;

		// The intro is 74 MB and the ending 41 MB; a prefix of frames is
		// sufficient to verify codec decoding.
		for (int f = 0; f < kSweepFrames && !shouldQuit(); f++) {
			const Graphics::Surface *frame = video.decodeNextFrame();
			if (!frame)
				break;
			debugC(2, kDebugVideo, "cda2 %s frame %4d %08x", kCutscenes[i], f,
				   frameChecksum(*frame, video.getPalette()));
		}
		video.close();
	}
}

void AlienEngine::sweepSubtitles() {
	for (uint i = 0; i < ARRAYSIZE(kCutscenes); i++) {
		CDA2Decoder video;
		if (!video.loadFile(Common::Path(kCutscenes[i])))
			continue;

		for (uint lang = 0; lang < video.languageCount(); lang++) {
			video.setLanguage(lang);
			Common::String last;
			for (uint frame = 0; frame < video.frameCount(); frame++) {
				const Common::String line = video.subtitle(frame);
				if (line == last)
					continue;
				last = line;
				if (line.empty())
					continue;

				Common::String flat(line);
				for (uint c = 0; c < flat.size(); c++) {
					if (flat[c] == '\n')
						flat.setChar('@', c);
				}
				debugC(3, kDebugVideo, "subtitle %s lang %u frame %4u %s", kCutscenes[i],
					   lang, frame, flat.c_str());
			}
		}
		video.close();
	}
}

void AlienEngine::sweepSounds() {
	// Every sound the script table can make, with the sample it names resolved
	// through the bank the room it belongs to loads. A trigger naming an empty
	// slot would be silent in the original too, so it is reported rather than
	// treated as an error.
	SoundBank bank;
	int loadedIndex = -1;

	for (int room = 0; room < StaticTables::kSfxRoomCount; room++) {
		uint blockCount = 0;
		const ScriptBlock *blocks = scriptForRoom(room, blockCount);
		if (!blocks)
			continue;

		const byte index = _tables.sfxBank(room);
		if (index != StaticTables::kSfxBankNone && (int)index != loadedIndex)
			loadedIndex = bank.load(_tables.sfxName(index)) ? (int)index : -1;

		for (uint b = 0; b < blockCount; b++) {
			const ScriptBlock &block = blocks[b];
			for (uint e = 0; e < block.count; e++) {
				const ScriptEffect &effect = *scriptEffect(block.first + e);
				if (effect.op != kOpSound && effect.op != kOpPlaySample)
					continue;

				const uint slot = effect.args[0];
				const bool queued = effect.op == kOpPlaySample;
				const uint32 rate = queued ? (((uint32)effect.args[1] << 16) | effect.args[2])
										   : SoundFX::kDefaultRate;
				const byte volume = queued ? (byte)effect.args[3] : SoundFX::kFullVolume;
				const int panning = queued ? (int)(int8)(int16)effect.args[4] : 0;
				const uint delay = queued ? effect.args[5] : 0;
				const SoundBank::Sample *sample = bank.sample(slot);

				debugC(1, kDebugSound, "trigger: room %2d obj %3d verb %2d slot %2u "
					   "%5u Hz vol %2u pan %3d delay %3u bank %3d %s", room, block.obj,
					   block.verb, slot, rate, volume, panning, delay, loadedIndex,
					   sample ? sample->name.c_str() : "(empty slot)");
			}
		}
	}
}

void AlienEngine::sweepVoices() {
	// The two things about playback that are behaviour rather than table: the
	// three voices are handed out round-robin, so every fourth effect reuses the
	// first, and a queued trigger waits out its countdown on the tick pair. Both
	// are driven here against the room's own bank.
	const SoundBank &bank = _sound.bank();
	debugC(1, kDebugSound, "voices: bank %d %s, %u slots", _sound.bankIndex(),
		   bank.file().c_str(), bank.slotCount());

	Common::Array<uint> filled;
	for (uint slot = 1; slot <= bank.slotCount(); slot++) {
		if (bank.sample(slot))
			filled.push_back(slot);
	}

	for (uint i = 0; i < filled.size(); i++)
		_sound.play(filled[i], SoundFX::kDefaultRate, SoundFX::kFullVolume, 0);

	// Queued out of order on purpose: the queue is scanned slot by slot, so what
	// decides the firing order is the countdown, not when the trigger was made.
	static const uint16 kDelays[] = { 3, 1, 0, 2 };
	for (uint i = 0; i < ARRAYSIZE(kDelays) && i < filled.size(); i++)
		_sound.queue(filled[i], SoundFX::kDefaultRate, SoundFX::kFullVolume, 0, kDelays[i]);

	for (uint tick = 0; tick < 8; tick++) {
		const bool pairTick = (tick & 1) == 0;
		_sound.tick(pairTick);
		debugC(1, kDebugSound, "queue: tick %u %s, %u waiting", tick,
			   pairTick ? "pair" : "odd ", _sound.pending());
	}
}

void AlienEngine::dumpItems() {
	// The static half of the inventory: what each item is called, where its icon
	// is cut from, and the codes a repeated look rotates through.
	debugC(1, kDebugItems, "items: %d in the tables", StaticTables::kItemInUse);
	for (int item = 1; item <= StaticTables::kItemInUse; item++) {
		const byte arity = _tables.itemArity(item);
		Common::String codes;
		for (int c = 1; c <= arity; c++)
			codes += Common::String::format(" %u", _tables.itemOutcome(item, c));

		debugC(1, kDebugItems, "item %2d %-24s icon %3d,%3d arity %u %s outcomes:%s",
			   item, _inventory.name((byte)item).c_str(), _tables.itemIconX(item),
			   _tables.itemIconY(item), arity,
			   _tables.itemCycles(item) ? "cycles" : "clamps", codes.c_str());
	}

	debugC(1, kDebugItems, "bar: page %u of %u showing", _inventory.page(),
		   _inventory.pageCount());
	for (uint page = 1; page <= _inventory.pageCount(); page++) {
		for (uint slot = 0; slot < Inventory::kSlotCount; slot++) {
			debugC(1, kDebugItems, "bar: page %u slot %u item %3u %s", page, slot,
				   _inventory.itemOn(page, slot),
				   _inventory.name(_inventory.itemOn(page, slot)).c_str());
		}
	}
}

void AlienEngine::dumpExits() {
	// Log every exit from the room: the object, the submode its approach point arms,
	// and where the transition routes it.
	debugC(1, kDebugRooms, "exits: room %d, %u hotspots", _room, _spots.size());
	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		const int x = (spot.x1 + spot.x2) / 2;
		const int y = (spot.y1 + spot.y2) / 2;

		WalkTarget target;
		if (!_script.walkTarget(x, y, spot.obj, target) || !target.submode)
			continue;

		debugC(1, kDebugRooms, "exit: obj %3u submode %3u at %3d,%3d facing %2u -> room %d",
			   spot.obj, target.submode, target.x, target.y, target.facing,
			   _script.nextRoom((byte)_room, target.submode));
	}
}

bool AlienEngine::takeFirstExit() {
	// The debug shortcut behind the 'x' key: leave by the first exit the room
	// has, without walking there. Puts the character on its approach point first
	// so the arrival test sees what a real walk would leave behind.
	return takeAnyExit(nullptr, 0);
}

bool AlienEngine::arriveAt(const WalkTarget &target) {
	// Put the character where the walk would have left him and arm the exit the
	// same way a click does, so the tour runs the real arrival test rather than
	// jumping straight to the chain.
	const int room = _room;
	_ben.place(target.x, target.y,
			   target.facing == Walker::kFacingKeep ? _ben.facing() : target.facing);
	_armed = target.submode;
	_armedX = target.x;
	_armedY = target.y;
	_armedFacing = target.facing;
	checkExit();
	return _room != room;
}

bool AlienEngine::takeAnyExit(const int *avoid, uint avoidCount) {
	// The first exit of the room, preferring one that does not lead to a room in
	// `avoid` -- which is how the tour covers ground instead of stepping through
	// the same door and back again.
	int fallback = -1;

	for (uint i = 0; i < _spots.size(); i++) {
		const Hotspot &spot = _spots[i];
		WalkTarget target;
		if (!_script.walkTarget((spot.x1 + spot.x2) / 2, (spot.y1 + spot.y2) / 2,
								spot.obj, target) || !target.submode)
			continue;

		bool seen = false;
		const int room = _script.nextRoom((byte)_room, target.submode);
		for (uint a = 0; a < avoidCount; a++)
			seen = seen || avoid[a] == room;

		if (seen) {
			if (fallback < 0)
				fallback = (int)i;
			continue;
		}

		return arriveAt(target);
	}

	if (fallback >= 0) {
		const Hotspot &spot = _spots[fallback];
		WalkTarget target;
		if (_script.walkTarget((spot.x1 + spot.x2) / 2, (spot.y1 + spot.y2) / 2,
							   spot.obj, target))
			return arriveAt(target);
	}

	debugC(1, kDebugRooms, "exits: room %d has none", _room);
	return false;
}

void AlienEngine::tourRooms() {
	// --debugflags=rooms --debuglevel=2 walks the mansion instead of playing it:
	// leave every room by its first exit and say where that landed, which runs
	// the whole path -- geometry, arrival, chain, room load -- without a player.
	int visited[kTourHops + 1];
	uint count = 0;
	visited[count++] = _room;

	for (uint hop = 0; hop < kTourHops; hop++) {
		const int from = _room;
		if (!takeAnyExit(visited, count))
			break;
		debugC(2, kDebugRooms, "tour: room %d -> room %d", from, _room);
		visited[count++] = _room;
	}
}

} // End of namespace Alien
