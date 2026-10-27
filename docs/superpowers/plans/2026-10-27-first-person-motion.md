# First-person motion (Plan 6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** The player's second round of feedback, all but sound and the scope (their own plan after research):
the view's step sway a third as strong; the gun visibly bobbing as he moves (none aimed); sprinting carries
the gun as a runner does, by its kind; every gun's barrel pointing where he aims; the dive keeping the new
gun and its crosshair.

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md`, as amended by the player's feedback
(2026-10-27): "The camera shake for movement is good but way too intense. It should maybe be 1/3"; "The guns
still just point down when moving, they should also bob with the movement unless the user is ADS"; "Sprinting
in first person should animate the guns really being moved around to look like running with that respective
weapon"; "The minigun always appears to be pointing about 15 degrees higher than where you're aiming"; "Max
Payne diving causes the crosshair to be replaced with the old one". The spec's "Dives count as standing for
spread" stands, which needs the mod's firing (and so its viewmodel) through the dive.

## Global Constraints

- Plans 1–5's. Don't commit; install only with the game closed.

## Review Focus

1. Aimed (LT), the gun doesn't bob at all, moving or sprinting.
2. Sprinting into firing, aiming, a reload or a gun switch: the carry eases out, never a snap.
3. His arms follow the sprint carry (no wrist bent 35°), but not the kick, reload tilt or bob.
4. Every built gun's barrel points within 3° of where he aims (vertically), converging inward, never outward.
5. Through the dive and the landing: the new gun, the mod's firing (spread as standing), its crosshair.

---

### Task 1: The view's step sway a third

- `fpbob`: the step dip, stride roll and yaw a third (0.18°, 0.17°, 0.12° running). Counter-Strike's strafe roll
  (2°) stays. Tests: `TestSwayGrowsWithSpeed` bounds (sprinting under 0.85°).

### Task 2: The gun's bob and sprint carry

- `vmanim`: `struct Move { Vec3 offset; float pitch, roll, yaw; }`; `AnimOutput` gains `yaw` and `Move carry`
  (the sprint carry: his arms follow it); `AnimInput` gains `stepPhase`, `stepSpeed`, `aimed` (0..1) and
  `Carry kind` (`kOneHanded`, `kTwoHanded`, `kHeavy`). `Anim::Update` adds the step bob (`GunBob`, larger: 1.2 cm
  side, 1 cm dip, 1.5° pitch, 2° roll running) while moving, none aimed, handing over to the carry's stride
  sprinting. The carry by kind, eased in and out (6/s), with its own stride swing:
  - one-handed: turned 15° across, canted out 20°, muzzle 25° down; the arm pumping 3.5 cm and 8° a stride.
  - two-handed: up 3 cm and 6 cm in, turned 35° across, canted in 40°; swaying 2 cm and 4° a stride, bouncing
    1.5 cm a step.
  - heavy: at the hip, turned 15° across, canted in 12°; half the swing.
  (Tuned by render: the first values, dropped and tipped down, took the M4 and the Colt out of view.)
- `vmhands`: `GunCarried(gun, framed, const Move &)`; `GunMoved` turns yaw too. fpgun and the tool: arms from
  the carried frame.
- Tests: `TestSprintCarriesByKind`, `TestSprintSwingsWithTheStride`, `TestBobVisibleRunning`,
  `TestNoBobAimed`, `TestCarryEasesOut`; `TestSprintLowers` becomes `TestSprintCarriesByKind`;
  `TestGunCarriedFollowsArms` (vmhands).

### Task 3: Barrels where he aims

- `TestEveryGunPointsWhereItAims` (test_vmgun, the embedded guns): framed at the hip, each barrel's elevation
  within 3° of the line from its muzzle to the crosshair 25 m out, and turned inward of it by no more than 6°.
- Reframe the guns that fail (the minigun −14°, M60 −7°, flamethrower −8.5° look down on them): level, the eye
  lowered so the gun stays in view; rebuilt with `make gun NAME=…`; checked by render.

### Task 4: The dive keeps the new gun

- `vmuse::Choose`: diving no longer falls back. `fpshoot::MovingOf`: the dive and landing are standing still.
- Test: `TestStatesThatFallBack` (diving → `kNew`).
