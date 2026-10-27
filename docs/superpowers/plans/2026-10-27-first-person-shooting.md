# First-person shooting (Plan 2 of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In first person on foot, a gun fires the frame RT is pulled, at its own cadence, kicks the view as
Counter-Strike 1.6 does, spreads with sustained fire and movement, and shows it on a four-line crosshair.

**Architecture:** A pure module (`fpfire`) holds the trigger's rules (when a shot leaves), the view punch and
the spread, each unit-tested with Counter-Strike's numbers. A runtime module (`fpshoot`) takes RT from the pad
before the game sees it (controls.cpp's trigger mapping), fires through `CWeapon::Fire` (so the sound, flash,
shells, police hearing it and the clip all work as before), adds the punch to the camera with the mod's
existing shake, and spreads each shot in `camera.cpp`'s `CWeapon::Fire` hook. The crosshair is drawn with the
HUD.

**Tech Stack:** C++17, the game's symbols; no new hooks (the patch table has room if one is needed).

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md` ("Shooting", "Camera": view punch).
Research: `docs/research/fps-feel-goldsrc.md` (§2: punch, KickBack, accuracy and spread).

## Global Constraints

- First person on foot only; `FirstPersonGuns = 0` gives today's firing exactly.
- A shot goes through `CWeapon::Fire`, never around it: the clip, reloads, the sound and flash, police hearing
  it and gunmen taking cover all stay the game's.
- The cadence is each gun's own (`CWeaponInfo` anim loop: 0x38 start, 0x3c end, seconds). Pistols, the Python
  and the shotguns fire once per pull; SMGs, rifles, the M60 and minigun while it's held (the flamethrower too).
- The first shot standing or walking goes exactly where the crosshair is.
- Game time (`CTimer`), so bullet time slows cadence and recovery; the look stays full speed (Plan 1).
- Scoped weapons (sniper rifles, rocket launcher) and thrown weapons keep the game's own firing.
- Controller first: RT. Keyboard and mouse keep the game's own firing in this round.

## Review Focus

1. **Holding RT through a switch or a reload.** Expected: no shot until the new gun's draw is done or the
   reload is over; an automatic resumes if still held. Pinned by `TestNoShotWhileNotReady`.
2. **Bullet time.** Expected: cadence and recovery on game time, so slowed with the world. Pinned by
   `TestCadenceOnGameTime`.
3. **A frame rate far from 30.** Expected: the same shots a second at 20 or 144 fps. Pinned by
   `TestCadenceAtAnyFrameRate`.
4. **Leaving first person mid-burst.** Expected: the game's own firing from the next frame, no stuck trigger.
   Checked in game.
5. **The punch never drifts the aim.** Expected: it decays back to exactly where you aimed. Pinned by
   `TestPunchReturns`.

---

### Task 1: The rules (`fpfire`)

**Files:** `src/fpfire.h`, `src/fpfire.cpp`, `tests/test_fpfire.cpp`, `Makefile`

**Interfaces (namespace `fpfire`):**
- `enum class Kind { kPistol, kShotgun, kSmg, kRifle, kHeavy }` and `Kind KindOf(int weaponType)`
  (Colt, Python: pistol; shotgun, SPAS, stubby: shotgun; Tec-9, Uzi, Ingram, MP5: SMG; M4, Ruger: rifle;
  flamethrower, M60, minigun: heavy). `bool Automatic(Kind)`.
- `class Trigger { public: bool Update(bool held, bool ready, float interval, bool automatic, float dt); }`:
  true the frame a shot leaves. The first on the press (if ready), then every `interval` while held for an
  automatic; a semi-automatic needs a release. Time carries across frames (a 0.1 s interval at 144 fps still
  gives 10 a second).
- Punch (degrees; CS's `len -= (10 + 0.5·len)·dt`): `struct Punch { float pitch, yaw; void Decay(float dt); void Kick(Kind, int shotsInBurst, float random01); }`
  — pistols ~2° up (shotguns 3.5°), automatics CS's KickBack shape: a base, growing with each shot of the burst,
  a little sideways that switches direction now and then, capped (M4: up 0.45 + 0.07/shot, cap 5.5°; side 0.3).
- Spread (radians): `class Spread { public: void Shot(); void Update(float dt); float Angle(Kind, Moving, bool crouched, bool aiming) const; int Burst() const; }`
  with `enum class Moving { kStill, kWalking, kRunning, kAir }`. Accuracy `shots³/200 + 0.35` capped at 1.25,
  and 0 for the first shot still or walking; the burst recovers after 0.4 s without a shot (pistols by time:
  0.3 s). Scale at accuracy 1: pistol 0.025, shotgun 0.02, SMG 0.04, rifle 0.03, heavy 0.05; running ×1.8, in
  the air ×4 (not 0 for the first shot), crouched ×0.7, aiming ×0.6.

- [ ] Step 1: tests (`tests/test_fpfire.cpp`): `TestFirstShotOnThePress`, `TestSemiAutoNeedsARelease`,
  `TestAutomaticCadence` (0.1 s interval held 1 s: 10 shots), `TestCadenceAtAnyFrameRate` (the same at 20 and 144
  fps), `TestCadenceOnGameTime` (half the dt: half the shots in the same real time), `TestNoShotWhileNotReady`,
  `TestPunchReturns` (a kick decays to under 0.01° in 0.5 s and never goes negative), `TestPunchGrowsInABurst`,
  `TestFirstShotExact`, `TestSpreadGrowsAndRecovers`, `TestMovingWidens`. They fail to compile.
- [ ] Step 2: implement; tests pass.

### Task 2: In the game (`fpshoot`)

**Files:** `src/fpshoot.h/.cpp`, `src/controls.cpp` (`TriggersAimAndFire`: in first person with a gun the mod
fires, RT is the mod's, not the game's), `src/camera.cpp` (the punch added with the shake; `Hook_Fire` spreads
the shot), `src/fpgun.h/.cpp` (`Ready()`: the shown gun is up and isn't switching), `src/player.cpp` (calls
`fpshoot::AfterWeapon(ped)` after `ProcessPlayerWeapon`), `src/minimap.cpp` (install), `README.md`

- `fpshoot::Takes(int weaponType)`: first person on foot, `FirstPersonGuns`, a gun that isn't scoped or thrown.
- `fpshoot::FromPad(bool rtDown)`: RT, recorded; `controls.cpp` then leaves the game's fire button up.
- `fpshoot::AfterWeapon(uint8_t *ped)`: `Trigger::Update` with the weapon's state (ready, not reloading), ammo in
  the clip, `fpgun::Ready()`, its interval; on a shot `CWeapon::Fire(weapon, ped, &eye)`, `Punch::Kick`,
  `Spread::Shot`. Every frame: `Punch::Decay`, `Spread::Update` on game time.
- `fpshoot::PunchAngles(float &pitch, float &yaw)` for the camera; `fpshoot::SpreadAngle()` for `Hook_Fire` (a
  random direction in the cone about the aimed front, uniform over its area).
- [ ] Step 1: implement; `make all`, `make test`.
- [ ] Step 2: install (game closed); hand to the player.

### Task 3: The crosshair

**Files:** `src/fpcross.h/.cpp` (pure: the four lines' gap in pixels from the spread angle and the view's
tan(fov/2)), `tests/test_fpcross.cpp`, `src/hud.cpp` (draw it in place of the hip dot in first person)

- Counter-Strike's four lines: 2 px thick at 1080 p (scaled), 9 px long, the gap the spread's angle on screen
  plus 4 px, in the game's crosshair colour, drawn with the mod's feathered 2D (`Canvas`).
- [ ] Step 1: tests: `TestGapFollowsSpread` (twice the angle, twice the gap past the minimum), `TestGapAtZeroSpread`
  (the minimum). Fail; implement; pass.
- [ ] Step 2: draw it; `make all`; install; hand to the player.
