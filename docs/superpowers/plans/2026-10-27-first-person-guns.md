# First-person guns (Plan 4 of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every gun gets Plan 3's viewmodel: Tommy's own arms (both, for two-handed guns) holding a new low-poly,
photo-textured model of it, with its moving parts animated (slide, bolt, pump, revolver cylinder, minigun
barrels, magazine).

**Architecture:** The gun format grows to carry each gun's framing, its left-hand hold and the way its left
forearm runs, so a gun's script says everything about it and the runtime has no per-gun code. Moving parts are
known by name. The offline tool (`vcmm-viewmodel`) takes any gun and checks both hands against it, so the guns
can be built in parallel, each checked by render and by how much of each finger ends up inside it. The runtime
gains the left arm and the parts' animations.

**Tech Stack:** as Plan 3 (C++17, Blender 5.2 by script, librw through the game's symbols).

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md` ("The viewmodel", "Weapons covered").
Plan 3 (`docs/superpowers/plans/2026-10-27-first-person-viewmodel.md`) built the Colt; `tools/guns/colt45.py`
is the worked example.

## Global Constraints

- Everything in Plan 3's Global Constraints.
- Handedness as Vice City holds each gun: one-handed — Colt 45, Python, Tec-9, Uzi, silenced Ingram; two-handed —
  the three shotguns, MP5, M4, Ruger, both sniper rifles, rocket launcher, flamethrower, M60, minigun.
- Vice City fidelity: pistols and SMGs 250–450 triangles, long guns 350–700; one 256×256 atlas each.
- Each gun is Vice City's own, remodelled at its real dimensions, scaled to the game's model of it
  (`tools/guns/gamefiles.py` reads its extents), held where the game's model sits in his hand.
- Scoped weapons (both sniper rifles, the rocket launcher) only need the hip view: they fire from the game's scope.

## Review Focus

1. **A two-handed gun's left hand on its handguard for every outfit.** Expected: the fit keeps his left
   fingers out of the gun as it does his right; checked by the tool's per-hand report for all 12 outfits.
2. **Switching between one- and two-handed guns.** Expected: the left arm appears with the draw and goes with
   the holster, never a frame of it hanging in view; it's drawn exactly when the shown gun is two-handed
   (fpgun's `Draw` and `PoseHand` both key it off the gun the animation shows) — see the ledger's ruling.
3. **Guns whose parts the animation doesn't know** (a part named outside the list). Expected: drawn still,
   never moved wrongly; pinned by `TestUnknownPartStaysPut`.
4. **The heavy guns' framing** (minigun, flamethrower, M60: big, held at the hip). Expected: in view, not filling
   it; checked by the FP renders.
5. **A gun that fails to load** (a bad asset). Expected: that gun falls back to today's viewmodel, logged, the
   rest unaffected; pinned by Plan 3's `TestRefusesBadData` and the per-gun load log.

---

### Task 1: The format carries framing and the left hand (VMG2)

**Files:** `src/vmgun.h/.cpp`, `tests/test_vmgun.cpp`, `tools/guns/kit.py`, `tools/guns/colt45.py`,
`assets/guns/colt45.vmg`, `src/vmview.h/.cpp`, `tests/test_vmview.cpp`

**Interfaces:**
- Produces (`vmgun.h`): `Gun` gains `bool twoHanded; Frame holdLeft; Vec3 leftForearm; Framing framing;`
  (`Framing` moves from `vmview.h` into `vmgun.h`). The format becomes `VMG2`: after `sightFront`,
  `u32 twoHanded; f32 holdLeft[12]; f32 leftForearm[3]; f32 hipEye[3], hipYaw, hipPitch, aimBack, aimAbove`.
  `holdLeft`: the gun frame in his left hand bone's frame (as `hold` is in his right's). `leftForearm`: the
  way his left forearm runs back from the wrist, in the gun frame (zero for one-handed guns).
- Part names the runtime animates (anything else stays put): `slide`, `bolt` (back on each shot, held
  back empty), `pump` (back and forward after each shot), `trigger`, `magazine` (the reload), `cylinder` (turns
  a sixth about its axis through its pivot each shot), `barrels` (spin about the axis while firing).
- Anchors: `r_index`, `r_middle`, `r_ring`, `r_pinky`, `r_thumb` and, for two-handed guns, `l_index` …
  `l_thumb` (on the handguard, pump or foregrip; a pump's on the `pump` part).
- `kit.write_vmg` takes `framing`, `two_handed`, `hold_left`, `left_forearm`.

- [ ] Step 1: tests in `tests/test_vmgun.cpp`: `TestParsesAHandMadeGun` writes VMG2 with a left hold, framing and
  `twoHanded`, and reads them back; `TestRefusesBadData` refuses a `VMG1` magic ("rebuild with make guns");
  `TestTheColt` checks its framing is the spike's (`{-0.150, -0.195, 0.160}`, 13, -3, 0.22, 0.02) and
  `twoHanded` false. Run them: they fail to compile.
- [ ] Step 2: the format in `vmgun.cpp`, `kit.py` and `colt45.py`; rebuild the Colt (`make guns`); tests pass.

### Task 2: The tool takes any gun and both hands

**Files:** `tools/viewmodel/viewmodel.cpp`, `tools/viewmodel/render.py`, `Makefile` (`make gun NAME=<gun>`:
builds one gun's asset; `make viewmodel GUN=<gun>`: the check for one gun)

- `vcmm-viewmodel <GameData> <out> [--gun <name>] [outfit …]`: the gun by its asset name (default `colt45`,
  read from `assets/guns/<name>.vmg` so a gun being built needn't be embedded yet). For a two-handed gun both
  hands are fitted and posed; the report gives each hand's fingers inside the gun; the OBJs include the left
  arm. `render.py … fp/close` renders the gun named in the folder's `gun.txt`.
- [ ] Step 1: write it; run it for the Colt: the same report and render as Plan 3's.

### Task 3: Two hands and moving parts in the game

**Files:** `src/vmpose.h/.cpp` (`ArmAngles` gains `Vec3 forearm` — when nonzero, the forearm's direction from
the wrist in world space, overriding the down/out angles), `src/vmanim.h/.cpp` (outputs gain `boltBack`,
`pumpBack`, `cylinderTurn`, `barrelsTurn`), `src/fpgun.cpp` (the left arm, the parts, framing from the
asset), `src/gunassets.S/.cpp` (each gun), tests: `tests/test_vmanim.cpp`, `tests/test_vmpose.cpp`,
`tests/test_fpgun.cpp`

- [ ] Step 1: tests: `TestPumpCyclesAfterAShot` (pump back 0.07 m by 0.15 s after a shot, home by 0.35 s),
  `TestCylinderTurnsASixth`, `TestBarrelsSpinWhileFiring` (vmanim); `TestForearmOverride` (vmpose: a nonzero
  `forearm` sets the elbow's direction); `TestLeftArmOnlyForTwoHanded`, `TestUnknownPartStaysPut` (fpgun's
  pure helpers). They fail.
- [ ] Step 2: implement; the left arm posed from `holdLeft` with `l_*` anchors fitted as the right's are, drawn
  with the right; parts moved by name; `Covered` is any embedded gun. Tests pass; `make all` clean.

### Task 4: The guns (in parallel)

Each gun: `tools/guns/<name>.py` on `kit.py` (as `colt45.py`), its asset `assets/guns/<name>.vmg`, its
`make gun NAME=<name>` build, checked with the tool for all 12 outfits (fingers inside the gun: under 8 per
finger, the trigger finger's pad on its trigger) and rendered first-person (`fp`) and close up (`close`).
Groups, one builder each:

| Group | Guns (asset names) | Hands |
|---|---|---|
| A | Python (`python`) | one |
| B | Tec-9 (`tec9`), Uzi (`uzi`), silenced Ingram (`ingramsl`) | one |
| C | shotgun (`chromegun`), SPAS 12 (`shotgspa`), stubby (`buddyshot`) | two (pump on the shotgun, SPAS) |
| D | MP5 (`mp5lng`), M4 (`m4`), Ruger (`ruger`) | two |
| E | sniper rifle (`sniper`), laser-scope sniper (`laser`), rocket launcher (`rocketla`) | two, hip view only |
| F | flamethrower (`flame`), M60 (`m60`), minigun (`minigun`) | two, heavy |

- [ ] Step 1 (per group): build, check and render each gun; report triangle counts, the tool's per-outfit
  report and the renders' paths.

### Task 5: Integration

- [ ] Step 1: each gun in `gunassets.S/.cpp` by its weapon type (Colt 17, Python 18, shotgun 19, SPAS 20,
  stubby 21, Tec-9 22, Uzi 23, Ingram 24, MP5 25, M4 26, Ruger 27, sniper 28, laser 29, rocket 30, flame 31,
  M60 32, minigun 33); `make test`, `make all`; install when the game is closed; README's list of covered guns.
- [ ] Step 2: hand to the player: each gun drawn, held and animated; switching between them; the log's load
  lines.
