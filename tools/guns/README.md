# The viewmodel's guns

Blender scripts that build each first-person gun (developers only; the built `assets/guns/*.vmg` are
committed and built into the mod by `src/gunassets.S`). Each script builds a detailed model at real size,
photographs it into a 256×256 atlas (left side, top, back, front, and any moving part that needs its own
photo), builds the low-poly model in its moving parts wearing that atlas, and writes the `.vmg`
(`src/vmgun.h` has the format) with where the gun sits in his hand and where his fingers go on it.

    GAMEDATA="/path/to/GameData" make guns      # needs Blender 5.2 on the PATH (or BLENDER=...)

The game's own model of each gun is read only to measure it (its length and where it sits in his hand).
Nothing of it is kept. `build/guns/` gets each atlas and a preview render.

- `lib.py`: hard-surface parts from 2D profiles (from the viewmodel spike).
- `gamefiles.py`: the game's IMG archive and a DFF's vertices, without Blender.
- `kit.py`: the studio, the photo shoot, UV projection, the palette and the `.vmg` writer.
- `colt45.py`: the Colt 45, the worked example: its anchors and hold are the ones the player approved
  in `docs/research/viewmodel-spike/`.
