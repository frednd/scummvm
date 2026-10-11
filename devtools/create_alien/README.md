Alien Incident engine data
-------------------------------------------------------------------------------

`alien.dat` holds the tables the Alien Incident engine would otherwise carry
as code. They are taken from the DOS game's executable (GAME.EXE) and its scene
overlays (GAME.OVR):

  - the cutscene records, their step streams, the scene-id dispatch and the
    procedures the scenes run;
  - the per-room scripts: what each object does for each verb and item, and
    under which conditions;
  - the state block a new game starts from.

The file holds no graphics, sound or text from the game. Dialog text and
everything else is still read from the player's own copy.

The tables are in `lift/` as JSON. They were extracted with the tooling that
was used to reverse-engineer the game for this port, which works on a
disassembly of the executable and so is not part of ScummVM. The JSON is the
source of the data file. Edit it there and rebuild:

  ./create_alien.py

This writes `dists/engine-data/alien.dat`. `./create_alien.py --check` reports
whether that file still matches `lift/`. `packlib.py` has the file layout, and
`engines/alien/pack.cpp` reads it. The two must agree on `VERSION`/`kVersion`.

The engine looks for `alien.dat` in the game directory before ScummVM's own
data, so a modified file placed next to the game files takes precedence.
