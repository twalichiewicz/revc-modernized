# First-person polish (Plan 5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** The player's feedback after playing Plans 1–4: aiming doesn't move the view; reloads use the off hand
and keep his shoulder out of view; looking down shows his body, not the inside of his neck; the view and the
gun move with his steps, more running; a crisp Counter-Strike crosshair.

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md`, as amended by the player's feedback
(2026-10-27): "Reloads don't move the off-hand to actually do the reloading… the shoulder clip[s] awkwardly high
into the view"; "Looking down at your own body doesn't make sense… you're looking down at your own neck"; "Need
camera shake/gun sway when moving and especially when running"; "Crosshairs look weird"; "ADS causes the aim to
move down about 30 degrees… ADS aiming at something shouldn't move your aim".

## Global Constraints

- Plans 1–4's constraints. Don't commit; install only with the game closed.
- The spec's "no idle sway" stands for standing still; the player asked for motion when moving.
- Camera motion has its own setting, `FirstPersonBob = 1` (it no longer follows macVC.ini's `HeadBob1stPerson`,
  which the player has off and still wants motion).

## Review Focus

1. Aiming with LT never changes where the view points (only its zoom).
2. A reload of a one-handed gun shows the left hand only while it fetches the magazine; a gun without a magazine
   part (pumps, the revolver, the minigun, the flamethrower) keeps today's reload.
3. The reload's tilt and the draw never swing his upper arm or shoulder into view.
4. Looking straight down shows his torso and legs, no neck ring around the eye.
5. Bob and sway are zero standing still, grow with speed, and never move the shots off the crosshair.

---

### Task 1: Aiming doesn't move the view

- `camera.cpp` `OnFoot`: the view's level raise (`kLevel`, 10°) is constant, not eased out as he aims (shots and
  the crosshair follow the view itself). A log line the first few times aiming starts: the view's pitch then and
  0.3 s later, so anything else moving it shows up.

### Task 2: The arm's direction from the gun as framed

- `vm::ArmDirections(const Frame &hand, const ArmAngles &, bool left, Vec3 &forearm, Vec3 &upper)`: the
  directions `Pose` works out from a hand frame, exposed. `fpgun` works them out from the gun as framed (before
  the draw, kick, reload tilt and sway) and passes them as `ArmAngles::forearm/upper`, so only the hand moves
  with the gun's animation.
- Test: `TestArmDirectionsAsPose` (Pose with the overrides from `ArmDirections` equals Pose without).

### Task 3: The off hand reloads

- `vm::ReloadHand(float progress, bool twoHanded) -> {float atMagazine, away; bool shown}` (vmanim): a two-handed
  gun's left hand goes from the handguard to the magazine (0.1–0.18), pulls it out with it, drops away out of
  view (0.3–0.4), comes back up with the new one (0.4–0.5) and rides it in, back to the handguard (0.7–0.8); a
  one-handed gun's left hand comes up from out of view with the new magazine (0.38–0.5), rides it in, and goes
  (0.68–0.8). Only for guns with a `magazine` part.
- `vm::MagazineGrip(const Gun &) -> Frame`: his left hand's frame (gun frame) holding the magazine's base: palm
  up under it, fingers forward along it, from the magazine part's lowest point along its axis.
- `vm::Blend(const Frame &a, const Frame &b, float t)` (vmmath): positions and axes blended, re-orthonormalised.
- `fpgun`: the left hand's frame: the handguard hold, the magazine grip (moving with the magazine part), and
  `away` 0.35 m down in the view; the left arm drawn for one-handed guns while `shown`.
- Tests: `TestReloadHandTwoHanded`, `TestReloadHandOneHanded`, `TestMagazineGripUnderTheBase`, `TestBlendEnds`.

### Task 4: Bob and sway

- `fpbob::Sway(float phase, float speed, float side) -> {pitch, roll, yaw}` (degrees) for the camera: a step-timed
  shake growing with speed (running and sprinting most), and Counter-Strike's strafe roll (2° at a run's speed).
  The CS bob (`fpbob::Bob`) runs when `FirstPersonBob` is on.
- `vm::GunBob(float phase, float speed) -> {offset, pitch, roll}` (vmanim): the gun's figure-eight with his
  steps, zero standing, larger running.
- Tests: `TestSwayStillIsZero`, `TestSwayGrowsWithSpeed`, `TestStrafeRolls`, `TestGunBobStillIsZero`,
  `TestGunBobGrowsWithSpeed`.

### Task 5: Looking down

- On foot his neck is hidden with his head (as the seat view does), and the eye moves forward and down as he
  looks down: `fpbob::LookDown(float frontZ) -> {ahead, down}` metres (none above 20° down; 0.16 ahead and 0.05
  down looking straight down). Test: `TestLookDownPush`.

### Task 6: A crisp crosshair

- `fpcross::Lines(float cx, float cy, float gap, float h, Rect out[4])`: the four lines as whole-pixel
  rectangles (2 px thick at 1080, 10 long, at least 5 apart), drawn as plain quads in the game's crosshair
  colour with a 1 px dark edge. Tests: `TestLinesOnWholePixels`, `TestLinesSymmetric`.
