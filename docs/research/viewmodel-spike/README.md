# First-person viewmodel spike (reference only)

Throwaway Blender scripts from the 2026-10-26 design session, kept because they pin down facts the
real build needs. They are not built or shipped, and they aren't production quality. The design is
`docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md`.

- `first-person-colt45.png` (kept locally, not committed: it's over a game screenshot): the approved look. Top: first person over a game screenshot. Below:
  the grip from the trigger side, under the left, the left, and below.
- `colt45-atlas.png`: the Colt's 256 px "photo" texture (left side, top, back, front views).
- `vcdff.py`: reads a skinned Vice City DFF. Geometry chunks of RenderWare 3.3 (`0x0C02FFFF`) carry
  12 bytes of lighting after the header, and each skin inverse-bind matrix is preceded by
  `0xDEADDEAD`.
- `v3_rig.py`: Tommy's skeleton rebuilt from his bind matrices. His bind pose has y up and z forward,
  with his right arm along -x.
- `v3_surgery.py`: his finger slab (bone 25/35) cut into four fingers at quarter widths, capped, with
  base and tip bones per finger and two thumb bones.
- `v3_step3.py`: the pose. The gun goes in the hand bone's frame, as the game puts its Colt; arm
  bones are set directly (`F_k @ B_k^-1 @ rest_k`); each finger's two joints are IK-solved to anchors
  on the gun. Environment variables there hold the approved values (`VM_GUN_FWD=0.03`,
  `VM_PANEL_BACK=30`).
- `colt.py`, `v2_colt_low.py`, `v2_atlas.py`, `lib.py`: the detailed Colt (the photo shoot), the
  284-triangle Colt, and the atlas renderer.
- `vcextract.py`, `comp.py`: IMG/TXD extraction and screenshot compositing.

Run with Blender 5.2: `blender -b -P v3_step3.py -- <out> <GameData> <player.png> <atlas.png> <background.jpg>`.
