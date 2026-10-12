#!/usr/bin/env python3
# ScummVM - Graphic Adventure Engine
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with this source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
"""The Alien Incident data pack: schema, writer, reader and validator.

The engine reads the game's own tables -- cutscenes, room scripts, the state
block a new game starts from -- out of `alien.dat` rather than carrying them as
code.  The pack is built from JSON layers:

    lift/*.json     the tables as they are in GAME.EXE and the scene overlays
    edits/*.json    an optional sparse overlay of deliberate changes

`create_alien.py` builds the shipped pack from the lift alone.  Tools outside
ScummVM that edit the game (see README) lay an edit layer over it with
`merge()` and write the result with `build()`; the engine reads either.

Layout, all little-endian:

    'ALPK'  u16 version  u16 sectionCount
    sectionCount * { u32 tag, u32 offset, u32 size }
    the sections, in TOC order

    'STRS'  u32 count, then count * { u16 length, bytes }   shared string table
    'TEXT'  u16 count, then the dialog overrides (see TEXT_FIELDS)
    'TOVR'  u16 count, then { u16 tal, u8 code, u8 n, u8 ids[n] }
    'TMAN'  u16 count, then { u16 tal, u8 entry, u32 hash }
    'TUNE'  u16 count, then { u16 name, i32 value }
    'EFFX'  u16 count, then the shared effect pool (ScriptEffect)
    'CPRC'  u16 count, then { u16 addr, u16 first, u16 count }
    'CREC'  u16 count, then the 218-byte scene records, lifted (CutsceneRecord)
    'CSTP'  u16 count, then { u8 op, u8 arg }   the step streams, end to end
    'CARM'  u16 count, then the scene-id dispatch arms (CutsceneArm)
    'CTRG'  u16 count, then { u8 room, u8 scene, guards[3] }   the call sites
    'AFRM'  u16 count, then that many bytes: the frame lists the play routines
            with a table read, one frame number per tick of the play (see
            cutscenes.h). An effect that uses one carries its first index in
            args[4] and its length in args[2], the play's own frame count.

    'REFX'  u16 count, then the room scripts' own effect pool (ScriptEffect)
    'RSCR'  u16 count, then the script blocks: { i16 obj, i16 verb, i16 item,
            u8 n, conds[4], u16 first, u16 count } (ScriptBlock)
    'RIDX'  u16 count, then { u8 room, u16 first, u16 count, u16 label }
    'FLAG'  u16 count, then { u16 addr, u8 value }   the new-game state block

Strings are raw CP850 bytes, the encoding the game's own text is in, and are
stored once in 'STRS'; every other section refers to them by index.

A new section is a tag, a row in SECTIONS and a pair of codec functions here,
and a matching reader in engines/alien/pack.cpp.
"""

import json
import os
import struct

MAGIC = b'ALPK'
VERSION = 1

NO_STRING = 0xFFFF

# Which fields of a dialog override are actually set. An unset field leaves the
# engine on the DOS behaviour, so a row can carry a duration and nothing else.
F_DURATION = 1 << 0
F_TICKS_PER_CHAR = 1 << 1
F_COLOR = 1 << 2
F_ANCHOR = 1 << 3
F_BAND = 1 << 4
F_LINES = 1 << 5

TEXT_FLAGS = {
    'duration': F_DURATION,
    'ticks_per_char': F_TICKS_PER_CHAR,
    'color': F_COLOR,
    'anchor': F_ANCHOR,
    'band': F_BAND,
    'lines': F_LINES,
}

ENCODING = 'cp850'

# The language trees the game ships, TALFILES/<lang> and NAMEROOM/<lang>. A
# text row that names one only applies to a run in that language; the pack
# stores it under the qualified name 'FIN/ROOM19.TAL', which the engine
# filters on as it loads and strips before anything else sees it.
LANGUAGES = ('ENG', 'FIN')

MAX_LINES = 5
MAX_OUTCOME_IDS = 10
ENTRY_COUNT = 100
OUTCOME_COUNT = 200

# The limits the engine reads these with, from roomscripts.h and cutscenes.h.
MAX_ARGS = 6
MAX_GUARDS = 3
MAX_CONDS = 4
MAX_ARM_GUARDS = 2
MAX_ARM_RECORDS = 2
BANK_SLOTS = 10
SUB_PROCS = 7

# ScriptOpcode, in the order roomscripts.h declares it: the pack stores the
# number, so the two have to agree.
OPCODE_NAMES = [
    'kOpUnsupported', 'kOpSetFlag', 'kOpActionHandled', 'kOpSubmode',
    'kOpQueueEvent', 'kOpAnimPlay1', 'kOpAnimPlay2', 'kOpAnimPlay3', 'kOpSound',
    'kOpPlaySample', 'kOpInvAdd', 'kOpInvRemove', 'kOpInvReplace', 'kOpInvHas',
    'kOpAddFlag', 'kOpCharPlace', 'kOpMusic', 'kOpAnimPlay4', 'kOpAnimPlay5',
    'kOpAnimPlay6', 'kOpAnimPlay7', 'kOpAnimPlay8',
]
OPCODE_NUMBER = {name: i for i, name in enumerate(OPCODE_NAMES)}

# The step stream's vocabulary (cutscenes.h).
STEP_PAUSE = 0
STEP_SPEAK_A = 1
STEP_SPEAK_B = 2
STEP_BEAT = 0x14
STEP_OPS = (STEP_PAUSE, STEP_SPEAK_A, STEP_SPEAK_B, STEP_BEAT)


class PackError(Exception):
    """A pack that cannot be built or read, with the reason the caller shows."""


def text_hash(lines):
    """FNV-1a over an entry's lines, the drift sentinel for an override.

    The engine computes the same hash over the bytes it read out of the TAL, so
    an override that was written against text which has since changed can be
    reported rather than silently applied to a different line.  The lines are
    joined with a NUL so that ["ab", "c"] and ["a", "bc"] do not collide.
    """
    h = 0x811C9DC5
    for i, line in enumerate(lines):
        if i:
            h = ((h ^ 0) * 0x01000193) & 0xFFFFFFFF
        for b in line.encode(ENCODING, 'replace') if isinstance(line, str) else line:
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


class StringTable:
    def __init__(self):
        self._index = {}
        self._items = []

    def add(self, s):
        if s is None:
            return NO_STRING
        raw = s.encode(ENCODING, 'replace') if isinstance(s, str) else bytes(s)
        if raw in self._index:
            return self._index[raw]
        if len(self._items) >= NO_STRING:
            raise PackError('string table is full')
        self._index[raw] = len(self._items)
        self._items.append(raw)
        return self._index[raw]

    def pack(self):
        out = [struct.pack('<I', len(self._items))]
        for raw in self._items:
            out.append(struct.pack('<H', len(raw)))
            out.append(raw)
        return b''.join(out)

    @staticmethod
    def unpack(blob):
        count, = struct.unpack_from('<I', blob, 0)
        p = 4
        items = []
        for _ in range(count):
            n, = struct.unpack_from('<H', blob, p)
            p += 2
            items.append(blob[p:p + n].decode(ENCODING, 'replace'))
            p += n
        return items


def normalize_tal(name):
    """TAL file names are matched on the bare upper-case base name.

    The engine reaches its files through SearchMan, so what it has in hand is
    'ROOM8.TAL' whether the file came out of TALFILES/ENG or a path a cutscene
    record spelled; keying on the base name is what makes an override written
    against one reach the other.
    """
    return os.path.basename(str(name).replace('\\', '/')).upper()


def text_name(row):
    """The TAL name a text or manifest row is stored under in the pack."""
    tal = normalize_tal(row['tal'])
    return '%s/%s' % (row['lang'], tal) if row.get('lang') else tal


def split_text_name(name):
    """'FIN/ROOM19.TAL' -> ('FIN', 'ROOM19.TAL'); a bare name has no language."""
    lang, sep, tal = name.partition('/')
    return (lang, tal) if sep and lang in LANGUAGES else (None, name)


def text_key(row):
    """What makes two text rows the same override: the language is part of it."""
    return (row.get('lang') or '', normalize_tal(row['tal']), row['entry'])


def rows_for_language(rows, language):
    """The text rows a run in `language` would apply, a qualified row winning
    over a neutral one for the same entry -- the order the engine resolves them in."""
    picked = {}
    for row in rows:
        if row.get('lang') not in (None, language):
            continue
        key = (normalize_tal(row['tal']), row['entry'])
        if key in picked and not row.get('lang'):
            continue
        picked[key] = row
    return list(picked.values())


def _check_range(what, value, lo, hi):
    if not isinstance(value, int) or not lo <= value <= hi:
        raise PackError('%s: %r is outside %d..%d' % (what, value, lo, hi))


def validate_text(rows):
    """Reject a dialog override the engine could not act on."""
    seen = set()
    for row in rows:
        tal = text_name(row)
        entry = row['entry']
        _check_range('%s entry' % tal, entry, 0, ENTRY_COUNT - 1)
        if (tal, entry) in seen:
            raise PackError('%s entry %d has two override rows' % (tal, entry))
        seen.add((tal, entry))

        lang = row.get('lang')
        if lang is not None and lang not in LANGUAGES:
            raise PackError('%s entry %d: language %r is not one of %s'
                            % (tal, entry, lang, ', '.join(LANGUAGES)))
        # Timing and placement mean the same in either language, but a line
        # of text is written in one: unqualified, an English line would be
        # spoken in the middle of the Finnish game.
        if 'lines' in row and lang is None:
            raise PackError('%s entry %d replaces the text but names no language'
                            % (tal, entry))

        if 'duration' in row:
            _check_range('%s entry %d duration' % (tal, entry), row['duration'], 0, 0x7FFF)
        if 'ticks_per_char' in row:
            _check_range('%s entry %d ticks_per_char' % (tal, entry),
                         row['ticks_per_char'], 0, 127)
        if 'color' in row:
            if len(row['color']) != 3:
                raise PackError('%s entry %d color needs three components' % (tal, entry))
            for c in row['color']:
                # The DAC the text entry is written through is six bits a gun.
                _check_range('%s entry %d color' % (tal, entry), c, 0, 63)
        if 'anchor' in row:
            if len(row['anchor']) != 2:
                raise PackError('%s entry %d anchor needs x and y' % (tal, entry))
            for v in row['anchor']:
                _check_range('%s entry %d anchor' % (tal, entry), v, -1, 0x7FFF)
        if 'band' in row:
            _check_range('%s entry %d band' % (tal, entry), row['band'], 0, 1)
        if 'lines' in row:
            if not 1 <= len(row['lines']) <= MAX_LINES:
                raise PackError('%s entry %d: %d lines, the renderer takes 1..%d'
                                % (tal, entry, len(row['lines']), MAX_LINES))
            for line in row['lines']:
                if len(line.encode(ENCODING, 'replace')) > 255:
                    raise PackError('%s entry %d: a line is longer than a Pascal string'
                                    % (tal, entry))


def validate_outcomes(rows):
    seen = set()
    for row in rows:
        tal = normalize_tal(row['tal'])
        code = row['code']
        _check_range('%s outcome' % tal, code, 0, OUTCOME_COUNT - 1)
        if (tal, code) in seen:
            raise PackError('%s outcome %d has two override rows' % (tal, code))
        seen.add((tal, code))
        ids = row['ids']
        if len(ids) > MAX_OUTCOME_IDS:
            raise PackError('%s outcome %d: %d ids, the chain holds %d'
                            % (tal, code, len(ids), MAX_OUTCOME_IDS))
        for i in ids:
            _check_range('%s outcome %d id' % (tal, code), i, 0, ENTRY_COUNT - 1)


def validate_tune(rows):
    seen = set()
    for row in rows:
        name = row['name']
        if name in seen:
            raise PackError('tunable %s is set twice' % name)
        seen.add(name)
        _check_range('tunable %s' % name, row['value'], -0x80000000, 0x7FFFFFFF)


def _pack_text(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        flags = 0
        for key, bit in TEXT_FLAGS.items():
            if key in row:
                flags |= bit
        color = row.get('color', (0, 0, 0))
        anchor = row.get('anchor', (-1, -1))
        lines = row.get('lines', [])
        out.append(struct.pack('<HBHhbBBBhhBB',
                               strings.add(text_name(row)),
                               row['entry'],
                               flags,
                               row.get('duration', -1),
                               row.get('ticks_per_char', -1),
                               color[0], color[1], color[2],
                               anchor[0], anchor[1],
                               row.get('band', 0),
                               len(lines)))
        for line in lines:
            out.append(struct.pack('<H', strings.add(line)))
    return b''.join(out)


def _unpack_text(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        (tal, entry, flags, duration, tpc, r, g, b, ax, ay, band,
         lineCount) = struct.unpack_from('<HBHhbBBBhhBB', blob, p)
        p += struct.calcsize('<HBHhbBBBhhBB')
        lines = []
        for _ in range(lineCount):
            idx, = struct.unpack_from('<H', blob, p)
            p += 2
            lines.append(strings[idx])
        lang, name = split_text_name(strings[tal])
        row = {'tal': name, 'entry': entry}
        if lang:
            row['lang'] = lang
        if flags & F_DURATION:
            row['duration'] = duration
        if flags & F_TICKS_PER_CHAR:
            row['ticks_per_char'] = tpc
        if flags & F_COLOR:
            row['color'] = [r, g, b]
        if flags & F_ANCHOR:
            row['anchor'] = [ax, ay]
        if flags & F_BAND:
            row['band'] = band
        if flags & F_LINES:
            row['lines'] = lines
        rows.append(row)
    return rows


def _pack_outcomes(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        ids = row['ids']
        out.append(struct.pack('<HBB', strings.add(normalize_tal(row['tal'])),
                               row['code'], len(ids)))
        out.append(bytes(ids))
    return b''.join(out)


def _unpack_outcomes(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        tal, code, n = struct.unpack_from('<HBB', blob, p)
        p += 4
        rows.append({'tal': strings[tal], 'code': code, 'ids': list(blob[p:p + n])})
        p += n
    return rows


def _pack_manifest(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<HBI', strings.add(text_name(row)),
                               row['entry'], row['hash']))
    return b''.join(out)


def _unpack_manifest(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        tal, entry, h = struct.unpack_from('<HBI', blob, p)
        p += 7
        lang, name = split_text_name(strings[tal])
        row = {'tal': name, 'entry': entry, 'hash': h}
        if lang:
            row['lang'] = lang
        rows.append(row)
    return rows


def _pack_tune(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<Hi', strings.add(row['name']), row['value']))
    return b''.join(out)


def _unpack_tune(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        name, value = struct.unpack_from('<Hi', blob, p)
        p += 6
        rows.append({'name': strings[name], 'value': value})
    return rows


def _pack_cond(cond):
    """One ScriptCond: five bytes, the shape every guard is stored in."""
    return struct.pack('<HBBB', cond.get('addr', 0) & 0xFFFF, cond.get('value', 0),
                       1 if cond.get('negate') else 0, cond.get('kind', 0))


def _unpack_cond(blob, p):
    addr, value, negate, kind = struct.unpack_from('<HBBB', blob, p)
    return {'addr': addr, 'value': value, 'negate': bool(negate), 'kind': kind}, p + 5


def _pack_conds(conds, slots):
    padded = list(conds) + [{}] * (slots - len(conds))
    return b''.join(_pack_cond(c) for c in padded[:slots])


def _unpack_conds(blob, p, slots, count):
    out = []
    for i in range(slots):
        cond, p = _unpack_cond(blob, p)
        if i < count:
            out.append(cond)
    return out, p


def _pack_effects(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        args = list(row.get('args', []))
        slots = args + [0] * (MAX_ARGS - len(args))
        out.append(struct.pack('<BB', row['op'], len(args)))
        out.append(struct.pack('<6H', *[a & 0xFFFF for a in slots[:MAX_ARGS]]))
        guards = row.get('guards', [])
        out.append(struct.pack('<BBB', row.get('dyn_args', 0), row.get('bias', 0),
                               len(guards)))
        out.append(_pack_conds(guards, MAX_GUARDS))
    return b''.join(out)


def _unpack_effects(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        op, argc = struct.unpack_from('<BB', blob, p)
        args = list(struct.unpack_from('<6H', blob, p + 2))
        dyn, bias, guardc = struct.unpack_from('<BBB', blob, p + 14)
        guards, p = _unpack_conds(blob, p + 17, MAX_GUARDS, guardc)
        rows.append({'op': op, 'args': args[:argc], 'dyn_args': dyn, 'bias': bias,
                     'guards': guards})
    return rows


def _pack_frames(rows, strings):
    """The frame lists modes 6, 7 and 8 read, end to end.

    Those three play routines do not step a frame: they step a *cursor* into a
    far table of one byte per tick, and what they draw is the byte. That is why
    every one of the game's mode 8 plays starts at zero -- zero is an index, not
    a frame -- and why reading the cursor as the frame put a blank frame into
    every talk cycle in the game.
    """
    return struct.pack('<H', len(rows)) + bytes(b & 0xFF for b in rows)


def _unpack_frames(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    return list(blob[2:2 + count])


def _pack_procs(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<HHH', row['addr'], row['first'], row['count']))
    return b''.join(out)


def _unpack_procs(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    return [dict(zip(('addr', 'first', 'count'),
                     struct.unpack_from('<HHH', blob, 2 + i * 6)))
            for i in range(count)]


def _pack_records(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        banks = list(row.get('banks', [])) + [None] * BANK_SLOTS
        subs = list(row.get('subs', [])) + [0] * SUB_PROCS
        points = list(row.get('points', [])) + [0] * 4
        colors = list(row.get('colors', [])) + [0] * 6
        out.append(struct.pack('<HHh', strings.add(row.get('pcx')),
                               strings.add(row.get('tal')), row.get('music', -1)))
        out.append(struct.pack('<10H', *[strings.add(b) for b in banks[:BANK_SLOTS]]))
        out.append(struct.pack('<H', row.get('main', 0)))
        out.append(struct.pack('<7H', *subs[:SUB_PROCS]))
        out.append(struct.pack('<4h', *points[:4]))
        out.append(bytes(colors[:6]))
        out.append(struct.pack('<HH', row.get('first_step', 0), row.get('step_count', 0)))
    return b''.join(out)


def _unpack_records(blob, strings):
    def name(index):
        return None if index == NO_STRING or index >= len(strings) else strings[index]

    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        pcx, tal, music = struct.unpack_from('<HHh', blob, p)
        banks = struct.unpack_from('<10H', blob, p + 6)
        main, = struct.unpack_from('<H', blob, p + 26)
        subs = list(struct.unpack_from('<7H', blob, p + 28))
        points = list(struct.unpack_from('<4h', blob, p + 42))
        colors = list(blob[p + 50:p + 56])
        first_step, step_count = struct.unpack_from('<HH', blob, p + 56)
        p += 60
        rows.append({'pcx': name(pcx), 'tal': name(tal), 'music': music,
                     'banks': [name(b) for b in banks], 'main': main, 'subs': subs,
                     'points': points, 'colors': colors,
                     'first_step': first_step, 'step_count': step_count})
    return rows


def _pack_steps(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<BB', row['op'], row['arg']))
    return b''.join(out)


def _unpack_steps(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    return [{'op': blob[2 + i * 2], 'arg': blob[3 + i * 2]} for i in range(count)]


def _pack_arms(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        guards = row.get('guards', [])
        records = list(row.get('records', [])) + [0, 0]
        out.append(struct.pack('<BHB', row['id'], row.get('latch', 0), len(guards)))
        out.append(_pack_conds(guards, MAX_ARM_GUARDS))
        out.append(struct.pack('<BBBHHB', len(row.get('records', [])),
                               records[0], records[1], row.get('first', 0),
                               row.get('count', 0), 1 if row.get('studio') else 0))
    return b''.join(out)


def _unpack_arms(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        id_, latch, guardc = struct.unpack_from('<BHB', blob, p)
        guards, p = _unpack_conds(blob, p + 4, MAX_ARM_GUARDS, guardc)
        recordc, r0, r1, first, count_, studio = struct.unpack_from('<BBBHHB', blob, p)
        p += 8
        rows.append({'id': id_, 'latch': latch, 'guards': guards,
                     'records': [r0, r1][:recordc], 'first': first, 'count': count_,
                     'studio': bool(studio)})
    return rows


def _pack_triggers(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        guards = row.get('guards', [])
        out.append(struct.pack('<BBB', row['room'], row['scene'], len(guards)))
        out.append(_pack_conds(guards, MAX_GUARDS))
    return b''.join(out)


def _unpack_triggers(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        room, scene, guardc = struct.unpack_from('<BBB', blob, p)
        guards, p = _unpack_conds(blob, p + 3, MAX_GUARDS, guardc)
        rows.append({'room': room, 'scene': scene, 'guards': guards})
    return rows


def _pack_blocks(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        conds = row.get('conds', [])
        out.append(struct.pack('<hhhB', row['obj'], row['verb'], row['item'],
                               len(conds)))
        out.append(_pack_conds(conds, MAX_CONDS))
        out.append(struct.pack('<HH', row['first'], row['count']))
    return b''.join(out)


def _unpack_blocks(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    p = 2
    rows = []
    for _ in range(count):
        obj, verb, item, condc = struct.unpack_from('<hhhB', blob, p)
        conds, p = _unpack_conds(blob, p + 7, MAX_CONDS, condc)
        first, count_ = struct.unpack_from('<HH', blob, p)
        p += 4
        rows.append({'obj': obj, 'verb': verb, 'item': item, 'conds': conds,
                     'first': first, 'count': count_})
    return rows


def _pack_rooms(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<BHHH', row['room'], row['first'], row['count'],
                               strings.add(row.get('label'))))
    return b''.join(out)


def _unpack_rooms(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    rows = []
    for i in range(count):
        room, first, count_, label = struct.unpack_from('<BHHH', blob, 2 + i * 7)
        rows.append({'room': room, 'first': first, 'count': count_,
                     'label': None if label == NO_STRING or label >= len(strings)
                              else strings[label]})
    return rows


def _pack_flags(rows, strings):
    out = [struct.pack('<H', len(rows))]
    for row in rows:
        out.append(struct.pack('<HB', row['addr'], row['value']))
    return b''.join(out)


def _unpack_flags(blob, strings):
    count, = struct.unpack_from('<H', blob, 0)
    return [{'addr': struct.unpack_from('<H', blob, 2 + i * 3)[0],
             'value': blob[4 + i * 3]} for i in range(count)]


def validate_room_scripts(doc):
    """The bodies and the pool they index into, resolved against each other."""
    effects = doc.get('room_effects', [])
    blocks = doc.get('blocks', [])

    for i, block in enumerate(blocks):
        if block['first'] + block['count'] > len(effects):
            raise PackError('script block %d runs past the effect pool' % i)
        if len(block.get('conds', [])) > MAX_CONDS:
            raise PackError('script block %d has %d conditions, the table holds %d'
                            % (i, len(block['conds']), MAX_CONDS))

    for row in doc.get('rooms', []):
        if row['first'] + row['count'] > len(blocks):
            raise PackError('room %d names blocks past the end of the table'
                            % row['room'])

    for row in doc.get('flags', []):
        _check_range('initial flag', row['addr'], 0, 0xFFFF)
        _check_range('initial flag 0x%04x' % row['addr'], row['value'], 0, 255)


def validate_effects(rows):
    for i, row in enumerate(rows):
        _check_range('effect %d opcode' % i, row['op'], 0, len(OPCODE_NAMES) - 1)
        if len(row.get('args', [])) > MAX_ARGS:
            raise PackError('effect %d has %d arguments, the table holds %d'
                            % (i, len(row['args']), MAX_ARGS))
        if len(row.get('guards', [])) > MAX_GUARDS:
            raise PackError('effect %d has %d guards, the table holds %d'
                            % (i, len(row['guards']), MAX_GUARDS))


def validate_cutscenes(doc):
    """The cutscene tables, checked against each other rather than one by one.

    Everything here is an index into another section: a record names its step
    stream and its procedures, an arm names its records, a procedure names a
    slice of the effect pool. An index that points past its section would be a
    scene that plays garbage, so they are all resolved once, here.
    """
    effects = doc.get('effects', [])
    procs = doc.get('procs', [])
    records = doc.get('records', [])
    steps = doc.get('steps', [])
    arms = doc.get('arms', [])

    for i, proc in enumerate(procs):
        if proc['first'] + proc['count'] > len(effects):
            raise PackError('procedure %d runs past the effect pool' % i)

    for i, rec in enumerate(records):
        if rec['first_step'] + rec['step_count'] > len(steps):
            raise PackError('record %d runs past the step stream' % i)
        for slot, proc in [('main', rec.get('main', 0))] + \
                [('sub %d' % s, p) for s, p in enumerate(rec.get('subs', []))]:
            if proc >= len(procs):
                raise PackError('record %d names procedure %d for %s, and there are %d'
                                % (i, proc, slot, len(procs)))
        for c in rec.get('colors', []):
            _check_range('record %d colour' % i, c, 0, 63)

    for step in steps:
        if step['op'] not in STEP_OPS:
            raise PackError('step opcode 0x%02x is not one of the four' % step['op'])

    for arm in arms:
        if arm['first'] + arm['count'] > len(effects):
            raise PackError('arm %d runs past the effect pool' % arm['id'])
        if len(arm.get('guards', [])) > MAX_ARM_GUARDS:
            raise PackError('arm %d has %d guards, the table holds %d'
                            % (arm['id'], len(arm['guards']), MAX_ARM_GUARDS))
        for number in arm.get('records', []):
            if number >= len(records):
                raise PackError('arm %d plays record %d, and there are %d'
                                % (arm['id'], number, len(records)))
        # The TV news studio is the one arm with no record: its scene is
        # hardcoded in the original's own code segment.
        if not arm.get('records') and not arm.get('studio'):
            raise PackError('arm %d plays nothing and is not the studio' % arm['id'])

    ids = {arm['id'] for arm in arms}
    for trigger in doc.get('triggers', []):
        if trigger['scene'] not in ids:
            raise PackError('room %d raises scene %d, which no arm handles'
                            % (trigger['room'], trigger['scene']))
        if len(trigger.get('guards', [])) > MAX_GUARDS:
            raise PackError('room %d scene %d has %d guards, the table holds %d'
                            % (trigger['room'], trigger['scene'],
                               len(trigger['guards']), MAX_GUARDS))


# tag -> (json key, packer, unpacker, validator). The order is the order the
# sections are written in, which keeps a pack byte-identical across rebuilds.
SECTIONS = [
    (b'TEXT', 'text', _pack_text, _unpack_text, validate_text),
    (b'TOVR', 'outcomes', _pack_outcomes, _unpack_outcomes, validate_outcomes),
    (b'TMAN', 'manifest', _pack_manifest, _unpack_manifest, None),
    (b'TUNE', 'tune', _pack_tune, _unpack_tune, validate_tune),
    (b'EFFX', 'effects', _pack_effects, _unpack_effects, validate_effects),
    (b'CPRC', 'procs', _pack_procs, _unpack_procs, None),
    (b'CREC', 'records', _pack_records, _unpack_records, None),
    (b'CSTP', 'steps', _pack_steps, _unpack_steps, None),
    (b'CARM', 'arms', _pack_arms, _unpack_arms, None),
    (b'CTRG', 'triggers', _pack_triggers, _unpack_triggers, None),
    (b'AFRM', 'frame_lists', _pack_frames, _unpack_frames, None),
    (b'REFX', 'room_effects', _pack_effects, _unpack_effects, validate_effects),
    (b'RSCR', 'blocks', _pack_blocks, _unpack_blocks, None),
    (b'RIDX', 'rooms', _pack_rooms, _unpack_rooms, None),
    (b'FLAG', 'flags', _pack_flags, _unpack_flags, None),
]


def prepare(doc):
    """Flatten what the JSON keeps nested, ready for `build`.

    A record's step stream is written inline with the record, because that is
    where it belongs to a reader and to an editor -- a stream on its own is a
    list of numbers nothing owns. The pack stores every stream end to end in one
    section with the record naming its slice, which is how the engine walks it,
    so the flattening happens here rather than in either of the two files.
    """
    out = dict(doc)
    records = []
    steps = list(doc.get('steps', []))

    for row in doc.get('records', []):
        row = dict(row)
        if 'steps' in row:
            inline = row.pop('steps')
            row['first_step'] = len(steps)
            row['step_count'] = len(inline)
            for step in inline:
                if isinstance(step, dict):
                    steps.append({'op': step['op'], 'arg': step.get('arg', 0)})
                else:
                    steps.append({'op': step[0], 'arg': step[1]})
        records.append(row)

    out['records'] = records
    out['steps'] = steps
    return out


def nest(doc):
    """The inverse of `prepare`: give each record its own stream back."""
    out = dict(doc)
    steps = doc.get('steps', [])
    records = []
    for row in doc.get('records', []):
        row = dict(row)
        first = row.pop('first_step', 0)
        count = row.pop('step_count', 0)
        row['steps'] = [[s['op'], s['arg']] for s in steps[first:first + count]]
        records.append(row)
    out['records'] = records
    out.pop('steps', None)
    return out


# How an edit row finds the lift row it replaces. A section that is not listed
# is one table rather than a set of rows, and an edit layer carrying one
# replaces the lift's outright.
MERGE_KEYS = {
    'text': text_key,
    'outcomes': lambda r: (normalize_tal(r['tal']), r['code']),
    'tune': lambda r: r['name'],
    'records': lambda r: r.get('number'),
    'arms': lambda r: r['id'],
    'triggers': lambda r: (r['room'], r['scene']),
    # A body is identified by where it sits in the table: the lift writes that
    # position out so an edit can name one body without carrying the rest.
    'blocks': lambda r: r.get('index'),
}

SORT_KEYS = {
    'text': MERGE_KEYS['text'],
    'outcomes': MERGE_KEYS['outcomes'],
    'manifest': text_key,
    'tune': MERGE_KEYS['tune'],
    'records': lambda r: r.get('number', 0),
    'blocks': lambda r: r.get('index', 0),
}


def validate(doc):
    """Raise PackError on anything the engine would have to guess about."""
    doc = prepare(doc)
    for _tag, key, _pack, _unpack, check in SECTIONS:
        if check:
            check(doc.get(key, []))
    validate_cutscenes(doc)
    validate_room_scripts(doc)


def build(doc):
    """Serialize a merged document into the pack's bytes."""
    validate(doc)
    doc = prepare(doc)

    strings = StringTable()
    blobs = []
    for tag, key, packer, _unpack, _check in SECTIONS:
        blobs.append((tag, packer(doc.get(key, []), strings)))

    # The string table is written first but filled last, so it is packed here
    # rather than in the loop above.
    blobs.insert(0, (b'STRS', strings.pack()))

    header = struct.pack('<4sHH', MAGIC, VERSION, len(blobs))
    toc_size = len(blobs) * 12
    offset = len(header) + toc_size

    toc = []
    body = []
    for tag, blob in blobs:
        toc.append(struct.pack('<4sII', tag, offset, len(blob)))
        body.append(blob)
        offset += len(blob)

    return b''.join([header] + toc + body)


def read(blob):
    """Decode a pack back into the document `build` would take."""
    if len(blob) < 8 or blob[:4] != MAGIC:
        raise PackError('not a pack: bad magic')
    _magic, version, count = struct.unpack_from('<4sHH', blob, 0)
    if version != VERSION:
        raise PackError('pack version %d, this tool writes %d' % (version, VERSION))

    sections = {}
    for i in range(count):
        tag, offset, size = struct.unpack_from('<4sII', blob, 8 + i * 12)
        if offset + size > len(blob):
            raise PackError('section %s runs past the end of the file' % tag)
        sections[tag] = blob[offset:offset + size]

    if b'STRS' not in sections:
        raise PackError('pack has no string table')
    strings = StringTable.unpack(sections[b'STRS'])

    doc = {}
    for tag, key, _pack, unpacker, _check in SECTIONS:
        doc[key] = unpacker(sections[tag], strings) if tag in sections else []
    return nest(doc)


def read_layer(directory):
    """Read one layer's JSON files into a single document.

    A file holding a list is that one section -- `text.json` is the text rows.
    A file holding an object carries several at once, which is how a lift that
    produces a set of tables together writes them: `cutscenes.json` has the
    records, the steps, the arms, the triggers and their effects, and splitting
    those across five files would only make them easier to get out of step.
    """
    doc = {}
    if not os.path.isdir(directory):
        return doc

    for name in sorted(os.listdir(directory)):
        if not name.endswith('.json'):
            continue
        with open(os.path.join(directory, name)) as f:
            content = json.load(f)
        if isinstance(content, dict):
            for key, rows in content.items():
                doc.setdefault(key, []).extend(rows)
        else:
            doc.setdefault(name[:-len('.json')], []).extend(content)
    return doc


def load_json(path):
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return json.load(f)


def write_json(path, doc):
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    with open(path, 'w') as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write('\n')


def merge(lift, edits):
    """Lay the edit overlay over the lift.

    A text or outcome edit replaces the lift row with the same key rather than
    being folded into it: an override is already a sparse statement, so two of
    them for one entry would be ambiguous about which field wins.  Everything
    the overlay does not name is the lift's.
    """
    out = {}
    for _tag, key, _pack, _unpack, _check in SECTIONS:
        rows = list(lift.get(key, []))
        overlay = edits.get(key, [])
        keyed = MERGE_KEYS.get(key)

        if keyed is None:
            # One table rather than a set of rows: an edit layer that carries
            # one replaces the lift's, because half an edited effect pool and
            # half a lifted one would index into neither.
            if overlay:
                rows = list(overlay)
        else:
            index = {keyed(r): i for i, r in enumerate(rows)}
            for row in overlay:
                slot = index.get(keyed(row))
                if slot is None:
                    index[keyed(row)] = len(rows)
                    rows.append(row)
                else:
                    rows[slot] = row

        # Sorted so that a rebuild of the same inputs is the same bytes, which
        # is what lets a pack be diffed. The sections with no sort key are in
        # the order the lift found them, which is the order the engine walks
        # them in -- a dispatch tests its arms in order, and a room raises its
        # scenes in the order its enter routine reaches them.
        if key in SORT_KEYS:
            rows.sort(key=SORT_KEYS[key])
        out[key] = rows
    return out
