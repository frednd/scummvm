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
"""Build alien.dat, the Alien Incident engine's data file, from lift/*.json.

    ./create_alien.py                 # writes dists/engine-data/alien.dat
    ./create_alien.py -o alien.dat
    ./create_alien.py --check         # is the shipped file up to date?

The same inputs always give the same bytes.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import packlib  # noqa: E402

LIFT_DIR = os.path.join(HERE, 'lift')
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, '..', '..', 'dists',
                                            'engine-data', 'alien.dat'))


def build(lift_dir):
    doc = packlib.merge(packlib.read_layer(lift_dir), {})
    # The manifest records the shipped text behind each dialog override, and
    # the lift has no overrides.
    doc['manifest'] = []
    return packlib.build(doc)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--out', default=DEFAULT_OUT, help='where to write the file')
    ap.add_argument('--lift-dir', default=LIFT_DIR)
    ap.add_argument('--check', action='store_true',
                    help='compare against the existing file instead of writing it')
    args = ap.parse_args()

    try:
        blob = build(args.lift_dir)
    except packlib.PackError as e:
        print('create_alien: %s' % e, file=sys.stderr)
        return 1

    if args.check:
        try:
            with open(args.out, 'rb') as f:
                current = f.read()
        except OSError as e:
            print('create_alien: %s' % e, file=sys.stderr)
            return 1
        if current != blob:
            print('%s is out of date: run create_alien.py' % args.out)
            return 1
        print('%s is up to date' % args.out)
        return 0

    with open(args.out, 'wb') as f:
        f.write(blob)
    print('wrote %s: %d bytes' % (args.out, len(blob)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
