# First person 1/4: Feel (movement, look, camera) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In first person on foot, the stick moves Tommy from the first frame with CS 1.6's acceleration and friction, the right stick turns like a modern console shooter's (with light aim slowdown), and the camera is fixed to his body with CS's bob.

**Architecture:**
- Three pure, unit-tested modules hold the maths:
  - `fpmove`: GoldSrc ground and air movement.
  - `fplook`: stick to turn rates, aim scaling, slowdown.
  - `fpbob`: CS's `V_CalcBob`.
- Thin game-side glue applies them:
  - `fpwalk` rewrites his velocity after `CPlayerPed::ProcessControl`.
  - `controls.cpp`'s `StickLook` uses `fplook` in first person.
  - `aimslow` finds the targets under the crosshair.
  - `camera.cpp` places the eye and bobs it.

**Tech Stack:** C++17, the mod's statically patched hooks (`hooks::Install`), `make test` unit tests (`tests/test_*.cpp`, `CHECK` macro, no framework).

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md` ("Movement", "Look", "Camera", "Settings"). Research behind the numbers: `docs/research/fps-feel-goldsrc.md` §1, §4.1 and `docs/research/fps-feel-controller.md` §1–2.

**Later plans:** 2/4 shooting (instant fire, punch, spread, crosshair, throwing), 3/4 viewmodel runtime (Tommy's articulated arms, drawing, anchors, animations, the Colt), 4/4 the other guns. This plan doesn't touch firing or the viewmodel.

## Global Constraints

- **Target binary:** macVC 1.0.5217. Offsets below are from it.
- **Style:** match the surrounding code: tabs, comment density, `namespace x { namespace { ... } }`, `Log(...)` for one-line diagnostics, no new dependencies.
- **First person on foot only.** Third person, vehicles, cutscenes and scripted cameras behave exactly as before. Every new behaviour checks `camera::FirstPersonOnFoot()` (last frame's value is fine) and its own ini switch.
- **Switches, on by default,** written into the default ini with a comment like the others: `FirstPersonMovement`, `FirstPersonLook`, `AimSlowdown`.
- **Units:** `EntitySpeed` is metres per 1/50 s (× 50 for m/s). Game-time `dt = *api.timeStep / 50` (slows in bullet time). Look uses controls.cpp's real-time `dt`, which doesn't slow.
- **Commits:** don't commit. The user decides when.
- **Game state:** `./install.sh` refuses to run while the game is open. Ask the user to quit it, and never kill it.
- **New hooks:** none needed. Everything goes through existing hooks (`CPad::Update`, `CPlayerPed::ProcessControl`, `CCamera::Process`).

## Review Focus

1. **Walking into a wall or a car:** he stops against it and slides along it, with no jitter and no pushing through, because velocity is recomputed from what collision left each frame. Task 4 has a manual check.
2. **Crouch walking:** going down, walking, standing up by sprinting or jumping. Speed stays continuous, and crouch.cpp's legs keep stepping. Task 4 has a manual check.
3. **Bullet time:** movement slows with the world (game `dt`) while turning stays at full speed (real `dt`). Task 5 has a manual check.
4. **Keyboard and mouse:** WASD moves him through the same path (the game writes keys into the pad's stick), and the mouse look is untouched. Tasks 4 and 5 have manual checks.
5. **Losing control mid-run** (cutscene, getting busted, entering a car): the override stops at once, and the game's movement takes over without a jump in speed. Pinned by `TestNoWishDecaysSmoothly` (Task 1) and a manual check in Task 4.

---

### Task 1: Ground and air movement (`fpmove`)

**Files:**
- Create: `src/fpmove.h`, `src/fpmove.cpp`, `tests/test_fpmove.cpp`
- Modify: `Makefile`: add `src/fpmove.cpp` to `PLUGIN`, a `$(BUILD)/test_fpmove` target like `test_turns`, and add it to `test:`.

**Interfaces:**
- Produces:
  ```cpp
  namespace fpmove {
  struct Tuning {
      float accel = 5.0f, friction = 4.0f; // CS's sv_accelerate, sv_friction
      float stopFraction = 0.3f;           // stop speed as a fraction of top speed (CS 75 of 250)
      float airAccel = 10.0f, airWishCap = 0.76f; // m/s: CS's 30 u/s
  };
  // Horizontal velocity (m/s) after dt s on the ground: PM_Friction, then PM_Accelerate.
  // wishDir is a unit vector or zero; wishSpeed <= topSpeed (m/s).
  Vec2 Ground(Vec2 velocity, Vec2 wishDir, float wishSpeed, float topSpeed, float dt, const Tuning &t);
  // In the air: PM_AirAccelerate (no friction), the wish speed capped at airWishCap.
  Vec2 Air(Vec2 velocity, Vec2 wishDir, float wishSpeed, float dt, const Tuning &t);
  }
  ```
  `Vec2` is from `types.h`.

- [ ] **Step 1: Write the failing tests** in `tests/test_fpmove.cpp` (same `CHECK` scaffold as `tests/test_turns.cpp`; `Run(...)` steps `Ground` at a fixed rate and returns the time a condition first holds):
  - `TestMovesOnFirstFrame`: from rest, `wishDir {0,1}`, `wishSpeed = topSpeed = 6.4`, one step of 1/60 s → `velocity.y > 0.2`.
  - `TestReachesTopSpeedLikeCS`: same, at 60 Hz. 90% of 6.4 first reached at t in [0.30, 0.36] s, and ≥ 99.5% at t in [0.37, 0.44] s.
  - `TestStopsLikeCS`: from `{0, 6.4}` with no wish (`wishDir {0,0}`, `wishSpeed 0`), at 60 Hz. Speed ≤ 0.025 at t in [0.45, 0.60] s, and exactly 0 once below 0.025.
  - `TestRateIndependent`: the 90% time at 30 Hz and at 100 Hz differ by < 0.04 s.
  - `TestDiagonalNotFaster`: `wishDir` normalised `{1,1}`, 2 s at 60 Hz → speed ≤ 6.4 + 1e-3.
  - `TestReversesDirection`: from `{0, 6.4}`, wish `{0,-1}` at 6.4, 1 s at 60 Hz → `velocity.y < -6.0`.
  - `TestNoWishDecaysSmoothly`: from `{0, 6.4}`, no wish. No step's speed drop exceeds `friction · 6.4 · dt + 1e-4` (no sudden stop).
  - `TestAirAddsOnlyUpToCap`: `Air` from `{0, 6.4}`, wish `{1,0}` at 6.4, 0.5 s at 60 Hz → `|velocity.x| <= 0.76 + 1e-3`, and `velocity.y` unchanged within 1e-4.

- [ ] **Step 2: Run them to verify they fail**
  Run: `make build/test_fpmove`
  Expected: a compile error (`fpmove.h` not found).

- [ ] **Step 3: Implement `Ground` and `Air`** in `src/fpmove.cpp`, as GoldSrc does (`docs/research/fps-feel-goldsrc.md` §1.2):
  - **Friction:** `control = max(speed, stopFraction·topSpeed)`. Scale the velocity by `max(0, speed − control·friction·dt) / speed`.
  - **Accelerate:** `add = wishSpeed − dot(v, wishDir)`. If `add > 0`, `v += wishDir · min(add, accel·wishSpeed·dt)`.
  - **Zero:** speed < 0.025 m/s after both steps.
  - **Air:** the same accelerate step, with `wishSpeed` clamped to `airWishCap` in the `add` calculation and `airAccel·wishSpeed·dt` (unclamped wish) as the step limit, as `PM_AirAccelerate`.

- [ ] **Step 4: Run them to verify they pass**
  Run: `make test`
  Expected: no `FAIL` lines; all test binaries exit 0.

---

### Task 2: Stick to turn rates, aim scaling and slowdown (`fplook`)

**Files:**
- Create: `src/fplook.h`, `src/fplook.cpp`, `tests/test_fplook.cpp`
- Modify: `Makefile`: `src/fplook.cpp` into `PLUGIN`, a `test_fplook` target, added to `test:`.

**Interfaces:**
- Produces:
  ```cpp
  namespace fplook {
  struct Tuning {
      float deadZone = 0.12f, full = 0.95f, power = 2.0f;
      float yawRate = 4.19f, pitchRate = 2.53f;   // rad/s at full push: 240 and 145 deg/s
      float rimYaw = 0.5f, rimPitch = 0.2f;       // extra at the rim, as a fraction
      float rimDelay = 0.05f, rimRamp = 0.35f;    // s
  };
  struct Rates { float yaw = 0, pitch = 0; };   // rad/s, signs as the stick's x and y
  // The right stick's turn rates. Stateful only for the rim boost's timer; dt is real seconds.
  class Stick {
  public:
      Rates Update(float x, float y, float dt, const Tuning &t);
  private:
      float rimTime_ = 0;
  };
  // Aiming's turn scale: tan(fovAim/2)/tan(fovHip/2) (degrees in) × aimSensitivity / its default.
  float AimScale(float fovHipDeg, float fovAimDeg, float aimSensitivity, float aimSensitivityDefault = 0.45f);
  // How far (radians) the ray from `eye` along unit `dir` passes outside a target's bounds: a
  // vertical capsule from `feet` to `head` of `radius`. 0 or less means on it.
  float OffAngle(const CVector &eye, const CVector &dir, const CVector &feet, const CVector &head, float radius);
  // The turn-rate multiplier (1 = none) for a target `offAngle` outside, `distance` m away.
  float Slowdown(float offAngle, float distance, bool aiming, float push);
  // Eases the applied multiplier: a stronger slowdown applies at once, and it lets go at
  // 4 per second (Lyra's fade-out; about 0.15 s from a typical 0.5).
  class Assist {
  public:
      float Update(float target, float dt);
  private:
      float value_ = 1.0f;
  };
  }
  ```
  `CVector` is from `game.h`. Use only its fields, so the test links without the game.

- [ ] **Step 1: Write the failing tests** in `tests/test_fplook.cpp`:
  - `TestDeadZone`: `Stick().Update(0.10f, 0, 1/60.f, {})` gives a yaw of 0, and `0.13f` gives a small positive yaw (< 0.01).
  - `TestCurve`: half way between the dead zone and full (`x = 0.12 + 0.5·0.83`) gives a yaw of `0.25·4.19` within 1e-3. Full push (`x = 1`, before the rim delay) gives 4.19.
  - `TestPitchUsesItsOwnRate`: `y = 1` gives a pitch of 2.53.
  - `TestRimBoostRamps`: holding `x = 1`: at 0.05 s the yaw is still 4.19 ± 0.01. At 0.05 + 0.35 s it's `4.19·1.5` ± 0.02. Dropping to `x = 0.9` resets the timer, so a later return to `x = 1` starts at 4.19.
  - `TestAimScale`: `AimScale(70, 52, 0.45f)` is `tan(26°)/tan(35°)` (≈ 0.697) within 1e-3. `AimScale(70, 52, 0.9f)` is double that.
  - `TestOffAngle`: eye at the origin, dir `{0,1,0}`. A capsule at y = 10 from z −1 to z 0.8, radius 0.35:
    - straight ahead it's ≤ 0;
    - with x = 1 it's ≈ `atan(1/10) − atan(0.35/10)` within 2e-3.
  - `TestSlowdown`:
    - on target (`offAngle` 0, 10 m, push 0.5): 0.5 at the hip, 0.4 aiming;
    - 1.5° outside and beyond: 1.0;
    - push 0.9: 1.0 (a flick isn't slowed);
    - 70 m: 1.0;
    - 40 m: between 0.5 and 1 (fading from 20 to 60 m);
    - 1 m: between the 10 m value and 1 (weaker point-blank: strength × (0.5 + 0.5·d/3) inside 3 m).
  - `TestAssistInAtOnceOutSlowly`: `Assist` with target 0.5 gives 0.5 in one call. Back to target 1.0, after 0.075 s it's between 0.6 and 0.9, and after 0.2 s it's 1.0.

- [ ] **Step 2: Run them to verify they fail**
  Run: `make build/test_fplook`
  Expected: a compile error (`fplook.h` not found).

- [ ] **Step 3: Implement `fplook`** in `src/fplook.cpp`.
  - **Stick:**
    - `m = |(x, y)|`; return zeros below `deadZone`.
    - `n = min(1, (m − deadZone)/(full − deadZone))`, `s = n^power`.
    - `yaw = x/m · s · yawRate`, `pitch = y/m · s · pitchRate`.
  - **Rim boost:** while `m ≥ full`, `rimTime_ += dt`; otherwise reset it to 0. `ramp = clamp((rimTime_ − rimDelay)/rimRamp, 0, 1)`, then multiply yaw by `1 + rimYaw·ramp` and pitch by `1 + rimPitch·ramp`.
  - **Slowdown:**
    - strength = 0.5 at the hip, 0.6 aiming;
    - zone factor = 1 on the target, falling linearly to 0 at 1.5° (0.02618 rad) outside;
    - range factor = `0.5 + 0.5·d/3` under 3 m, 1 to 20 m, falling linearly to 0 at 60 m;
    - push ≥ 0.85 → return 1;
    - return `1 − strength·zone·range`.
  - **Assist:** if `target < value_`, snap to it. Otherwise move toward it at 4 per second (`value_ += min(target − value_, 4·dt)`).

- [ ] **Step 4: Run them to verify they pass**
  Run: `make test`
  Expected: no `FAIL` lines.

---

### Task 3: CS's bob (`fpbob`)

**Files:**
- Create: `src/fpbob.h`, `src/fpbob.cpp`, `tests/test_fpbob.cpp`
- Modify: `Makefile`: `src/fpbob.cpp` into `PLUGIN`, a `test_fpbob` target, added to `test:`.

**Interfaces:**
- Produces:
  ```cpp
  namespace fpbob {
  struct State { float time = 0, last = 0; };
  // The eye's vertical bob (m) this frame: V_CalcBob with cl_bob 0.01, cl_bobcycle 0.8, cl_bobup 0.5,
  // at `speed` m/s on the ground; frozen at its last value in the air. Clamped to [-0.178, 0.102] m
  // (CS's [-7, 4] u).
  float Bob(State &s, float speed, float dt, bool onGround);
  }
  ```

- [ ] **Step 1: Write the failing tests** in `tests/test_fpbob.cpp`:
  - `TestStillIsFlat`: speed 0 → 0 at every step.
  - `TestRunRange`: at 6.4 m/s over one 0.8 s cycle at 120 Hz, the minimum is ≈ −0.0256 and the maximum ≈ 0.064 (within 2e-3).
  - `TestPeriod`: at 6.4 m/s the bob at t and t + 0.8 s agree within 1e-3.
  - `TestFrozenInAir`: after 0.3 s on the ground, three more calls with `onGround = false` all return the last ground value.

- [ ] **Step 2: Run them to verify they fail**
  Run: `make build/test_fpbob`
  Expected: a compile error.

- [ ] **Step 3: Implement `Bob`** in `src/fpbob.cpp`, as `docs/research/fps-feel-goldsrc.md` §4.1. Speed in m/s × 0.01 gives metres, since CS's 0.01 u per u/s is unit-free.

- [ ] **Step 4: Run them to verify they pass**
  Run: `make test`
  Expected: no `FAIL` lines.

---

### Task 4: He moves by the stick in first person (`fpwalk`)

**Files:**
- Create: `src/fpwalk.h`, `src/fpwalk.cpp`
- Modify:
  - `src/player.cpp`: `Hook_ProcessControl`.
  - `src/controls.cpp:740-741`: the end of `Hook_Update`.
  - `src/config.h`, `src/config.cpp`: the switch and two tunables.
  - `src/game.h`: one helper.
  - `Makefile`: `src/fpwalk.cpp` into `PLUGIN`.

**Interfaces:**
- Consumes: `fpmove::Ground`, `fpmove::Air`, `fpmove::Tuning` (Task 1); `camera::FirstPersonOnFoot()`; `swim::Swimming()`, `climb::Active()`, `shootdodge::Posing()`, `aimcam::Aiming()`.
- Produces:
  ```cpp
  namespace fpwalk {
  bool Install();                  // binds CWeaponInfo::GetWeaponInfo and CCamera::bFreeCam; logs what's missing
  void TakeStick(const uint8_t *pad); // from CPad::Update, before crouch::TakeStick: the left stick's push
  bool BeforeControl();            // before CPlayerPed::ProcessControl: the strafing control in first person
  void AfterControl(uint8_t *ped, bool strafed); // after it: his velocity and facing
  bool Active();                   // the override ran this frame (the camera's fixed eye uses it)
  }
  ```
  - `game.h`: `inline bool PedDucking(const uint8_t *ped) { return (ped[0x18c] & 0x10) != 0; }` (CPed::bIsDucking, as crouch.cpp reads it). Make crouch.cpp's `Ducking` use it.
  - `Config`: `bool firstPersonMovement = true; float fpAccel = 5.0f, fpFriction = 4.0f;`. The ini keys are `FirstPersonMovement`, `FirstPersonAccel`, `FirstPersonFriction`.

- [ ] **Step 1: Add the settings.** Add the three `Config` fields, their parsing in `config.cpp`, and default-ini lines in the "On foot and gunfights" block:
  ```
  FirstPersonMovement=1 ; first person: the stick moves him at once, CS 1.6's acceleration and friction
  FirstPersonAccel=5    ; ... how fast he gets up to speed (CS: 5; higher is snappier)
  FirstPersonFriction=4 ; ... how fast he stops (CS: 4)
  ```

- [ ] **Step 2: Implement `fpwalk`.**
  - **`TakeStick`:** record the pad's left stick after the mod's processing (`PadNew(pad, kPadLeftStickX)/128`, and the Y axis with up as forward). Clamp the push to 1.
  - **`BeforeControl`:** in first person on foot with the switch on, set `CCamera::bFreeCam` to 0 for the duration of `ProcessControl`, so the game uses its strafing control (`PlayerControl1stPersonRunAround`) and the legs play strafe and backpedal animations. Return whether it changed. aimcam's `BeforeControl`/`AfterControl` already do this while aiming: if aimcam already set it, do nothing.
  - **`AfterControl`:** restore `bFreeCam` if `strafed`. Then, when all of these hold:
    - the switch is on;
    - `camera::FirstPersonOnFoot()`;
    - `ped == api.FindPlayerPed()`, not in a vehicle, and `api.IsPedInControl(ped)`;
    - he isn't swimming, climbing or in the shootdodge (`shootdodge::Posing()`);
    - his state isn't die, dead or get-up;

    compute his horizontal velocity:
    - **Wish direction:** the push in the camera's frame. Forward is the camera's forward (`theCamera + 0x10`) flattened, and right is `{f.y, −f.x}`.
    - **Top speed:**
      - crouched (`PedDucking`): 1.6 m/s (crouch.cpp's `kPace`);
      - sprinting (`PedMoveState(ped) == 5`): 9.8 m/s (Vice City's `sprint_panic` root motion);
      - aiming (`aimcam::Aiming()`): 0.52 × the run speed (CS's walk multiplier). aimcam's 0.45 stick scaling happens after `TakeStick`, so the recorded push is already unscaled;
      - otherwise running: 6.4 m/s (`run_player`), or 5.0 m/s (`run_armed`) with a two-handed weapon. Two-handed means `CWeaponInfo::GetWeaponInfo(type)` has no flag `0x80` at `+0x5c`, aimcam's `TwoHanded` rule.
    - **Wish speed:** push × top speed.
    - **On the ground** (`PedFlags(ped) & kPedStanding` and his state isn't jump or fall): `fpmove::Ground` with `{gConfig.fpAccel, gConfig.fpFriction}`, then `FaceHeading(ped, CameraHeading())`.
    - **Otherwise:** `fpmove::Air`.
    - Write x and y back to `EntitySpeed(ped)` / 50. Leave z alone.
    - Set `Active()` true for the frame.

    `dt = *api.timeStep / 50`.
  - **Logging:**
    - once, when it first engages: `Log("fpwalk: first-person movement on")`;
    - when `VCHUD_LOG_SPEED` is set in the environment, his horizontal speed once a second (for checking the top speeds against third person).

- [ ] **Step 3: Wire it in.**
  - `player.cpp` `Hook_ProcessControl`: `fpwalk::BeforeControl()` after `aimcam::BeforeControl()`; `fpwalk::AfterControl(ped, fpStrafed)` after `crouch::AfterControl(ped)` and before `shootdodge::AfterControl(ped)`.
  - `controls.cpp`: `fpwalk::TakeStick(pad);` just before `crouch::TakeStick(pad);`.
  - Call `fpwalk::Install()` where the other modules install (search for `crouch::` or `aimcam::Install()` in `game.cpp`/`hooks.cpp` and follow the same place).

- [ ] **Step 4: Build.**
  Run: `make all && make test`
  Expected: it builds without warnings in the new files, and there are no `FAIL` lines.

- [ ] **Step 5: Manual check in game** (ask the user to quit the game first if it's running, then `./install.sh`; the user plays):
  1. First person on foot: push the stick. He moves at once, strafes and backpedals without turning his body, and stops in about half a second.
  2. Third person: nothing has changed.
  3. Walk into a wall at an angle: he slides along it without jitter.
  4. Crouch, walk crouched, then sprint: speed stays continuous.
  5. Jump while running: he keeps his speed in the air, with a little steering.
  6. Get into a car mid-run, and let a cutscene start: no jolt.
  7. WASD moves him the same way.
  8. With `VCHUD_LOG_SPEED=1`, run and sprint in third person, then in first person. The steady speeds in `vcmodernhud.log` should match within about 10%. If they don't, set the 6.4 / 5.0 / 9.8 constants to what third person logs, and note it.

---

### Task 5: The right stick in first person, with aim slowdown (`StickLook`, `aimslow`)

**Files:**
- Create: `src/aimslow.h`, `src/aimslow.cpp`
- Modify:
  - `src/controls.cpp`: `StickLook`, around lines 483-505.
  - `src/config.h`, `src/config.cpp`.
  - `Makefile`: `src/aimslow.cpp` into `PLUGIN`.

**Interfaces:**
- Consumes: `fplook::Stick`, `fplook::Rates`, `fplook::AimScale`, `fplook::OffAngle`, `fplook::Slowdown`, `fplook::Assist` (Task 2).
- Produces:
  ```cpp
  namespace aimslow {
  bool Install(); // binds CPools::ms_pPedPool; logs if missing (slowdown then never applies)
  // The strongest slowdown (multiplier, 1 = none) over the targets near the view this frame.
  float Multiplier(bool aiming, float push);
  }
  ```
  - `Config`: `bool firstPersonLook = true, aimSlowdown = true; float fpLookYaw = 240.0f, fpLookPitch = 145.0f;` (degrees per second). The ini keys are `FirstPersonLook`, `AimSlowdown`, `FirstPersonLookYaw`, `FirstPersonLookPitch`.

- [ ] **Step 1: Add the settings** (fields, parsing, default-ini lines in "Controls and camera"):
  ```
  FirstPersonLook=1         ; first person: the right stick turns like a modern console shooter's
  FirstPersonLookYaw=240    ; ... its top turning speed, degrees a second (it speeds up 50% held at the rim)
  FirstPersonLookPitch=145  ; ... up and down
  AimSlowdown=1             ; first person: the right stick slows a little over an enemy (never pulls)
  ```

- [ ] **Step 2: Implement `aimslow::Multiplier`.**
  - Walk the ped pool as `witness.cpp`'s `ForEachPed` does: entries of 0x8d0 bytes, flags `& 0x80` free.
  - **Targets:** alive (`Health > 0`), not the player, not in a vehicle, within 60 m, and either fighting the player (`*(uint8_t **)(ped + 0x1a8) == player`, `m_pedInObjective`) or a cop (`PedType == 6`) while he's wanted (`*(int32_t *)(*(uint8_t **)(player + 0x778) + 0x20) > 0`).
  - **Bounds:** a capsule from `pos.z − 1.0` to `pos.z + 0.8`, radius 0.35 m.
  - **Angle:** `fplook::OffAngle` from the camera's position (`theCamera + 0x30`) along its forward (`+0x10`).
  - **Line of sight:** only for targets within 3° of the crosshair, from the eye to the ped's chest (`pos.z + 0.4`), with `api.GetIsLineOfSightClear` and the arguments combat.cpp uses for "can see".
  - **Result:** the minimum `fplook::Slowdown` over visible targets, else 1.

- [ ] **Step 3: Use `fplook` in `StickLook`.**
  - When `camera::FirstPersonOnFoot()`, `gConfig.firstPersonLook` and the follow camera is active, replace the `StickRates` call with a file-static `fplook::Stick`:
    - pass `now.rx` and `(*api.invertLookForPad ? now.ry : -now.ry)`;
    - set the tuning's rates from `gConfig.fpLookYaw`/`fpLookPitch` in radians;
    - multiply both rates by `gConfig.lookSensitivity`.
  - When aiming, multiply by `fplook::AimScale(70, 52, gConfig.aimSensitivity)`. 70 is the follow camera's FOV and 52 is aimcam's `kAimFov`.
  - When `gConfig.aimSlowdown`, multiply by `assist.Update(aimslow::Multiplier(aiming, push), dt)` (a file-static `fplook::Assist`). `push` is the right stick's magnitude.
  - The conversion to mouse movement below stays as it is.
  - In third person, and with the switch off, nothing changes.
  - Call `aimslow::Install()` next to `fpwalk::Install()`.

- [ ] **Step 4: Build.**
  Run: `make all && make test`
  Expected: it builds, and there are no `FAIL` lines.

- [ ] **Step 5: Manual check in game** (as Task 4 step 5):
  1. First person: small pushes make fine corrections, and a full push turns fast, speeding up after a moment at the rim.
  2. Aiming (LT): turning is slower in proportion to the zoom, and the Controls page's aim sensitivity still scales it.
  3. Sweep across a gunman shooting at you: the stick slows a little over him, nothing pulls, and a flick isn't slowed.
  4. Bullet time: turning stays at full speed while he moves slowly.
  5. Third person and the mouse: unchanged.

---

### Task 6: The eye fixed to his body, and CS's bob (`camera.cpp`)

**Files:**
- Modify:
  - `src/camera.cpp`: `OnFoot`, around lines 296-322.
  - `src/config.h`, `src/config.cpp`: `HeadBob1stPerson` from `macVC.ini`.
  - `README.md`: the first-person section, and the settings table rows for the new keys.

**Interfaces:**
- Consumes: `fpbob::Bob`, `fpbob::State` (Task 3); `fpwalk::Active()` (Task 4).
- Produces: `Config::headBob` (`bool`, from `macVC.ini`'s `HeadBob1stPerson`, default true if missing). It's read in `LoadConfig` from `SupportDir() + "/macVC.ini"` (a `HeadBob1stPerson=` line, any case, outside comments).

- [ ] **Step 1: Read `HeadBob1stPerson`** in `LoadConfig` (a plain line scan; `0` means false). Log its value once.

- [ ] **Step 2: Change `OnFoot`.**
  - **When `fpwalk::Active()`:**
    - the eye is his body's position plus `kEyeAhead` along the flattened view forward;
    - its height is his head's local z (from `Head(ped)`), eased with a 0.25 s settle, so crouching follows smoothly and the run animation's head bob is filtered out;
    - his head's local x and y are ignored;
    - the existing `gHeight` step smoothing of the body's z stays.
  - **Otherwise** (climbing, getting up, and so on): keep today's `SteadyHead` path.
  - **The vertical bob:** replace `bob`/`sway` with `fpbob::Bob(state, horizontalSpeed, dt, onGround)` when `gConfig.headBob`, else 0. There's no sideways sway (CS has none).
  - Keep updating `gBobPhase`/`gBobAmount`, because `viewmodel.cpp` reads them through `StepPhase`/`StepAmount` until plan 3 replaces it.

- [ ] **Step 3: README.** In "First person", replace the light step bob sentence. In "On foot" and the settings table, add the movement, look and slowdown behaviour and the new keys, written plainly like the surrounding text:
  - The stick moves him at once in first person with CS 1.6's acceleration.
  - The right stick has a rim boost.
  - There's light aim slowdown that never pulls.
  - Bob follows `HeadBob1stPerson`.

- [ ] **Step 4: Build and test.**
  Run: `make all && make test`
  Expected: it builds, and there are no `FAIL` lines.

- [ ] **Step 5: Manual check in game** (as before):
  1. **Running in first person:** no head sway from the animation. With `HeadBob1stPerson=1` there's an even 0.8 s bob. With 0 (the user's current setting) there's none.
  2. **Crouching:** the eye lowers smoothly.
  3. **Stairs:** the eye follows without a jolt.
  4. **Climbing and vaulting:** the view still follows his head as before.
