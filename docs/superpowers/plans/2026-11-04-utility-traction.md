# Utility Traction Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Track steps with checkboxes.

**Goal:** Healthy utility vehicles retain torque and traverse wet grass; offroad capability differs from sports tyres.
**Architecture:** handlingmodel owns common model/Kind lookup and torque factors; vehiclerain owns per-Kind ground profiles; existing rain wheel hook consumes them. Native replay locks down interactions.
**Tech Stack:** C++17, supported macVC1.0.5217 ARM64, original game assets and native consumers.
**Spec:** docs/superpowers/specs/2026-11-04-utility-traction-design.md

## Global Constraints

OffRoad/LightTruck/HeavyTruck accel1.10/1.10/1.15; top0.90/0.90/0.85 retained.
Keep other tuning, native gears/drive flags, damage/proof/repair ownership and configuration controls.
Terrain profiles and wet interpolation use exact table in spec; dry and paving unchanged.
Shared model lookup works independently of CarHandling; all aliases verified against default.ide.

## Review Focus

1. Native producer inputs (handling parse/flags/gear count, drive-wheel order, grip and timing) match replay assumptions.
2. RWD road trucks regain grass mobility without becoming equivalent to offroad tyres in mud/sand.
3. All model aliases and CarHandling=0 use correct profiles; unknown/noncar/noncontact calls remain safe.
4. Rain resistance remains bounded, symmetric and frame-rate reasonable; damage and native tyre grip still constrain drive.
5. More torque does not undo top-speed, ride-height or handling-field guarantees; live location/feel are unverified offline.

### Task 1: Reproduce and fix utility traction

**Files:** src/handlingmodel.h/.cpp, src/vehiclerain.h/.cpp, src/rainvehicles.cpp, handling/rain/asset tests and tests/test_utility_native.cpp, Makefile.
**Interfaces:** Car primary model ID; FindModel(model), alias normalization; OnGround(surface,soak,Kind=Car). Existing Ground outputs and native wheel ABI unchanged.
- [x] Reproduce using actual native converter/transmission/wheel consumers and installed handling rows. Expected: semi/SUVs stuck; sports move. Toggle acceleration, mass, rain independently.
- [x] Graduate diagnostic replay into regression, add class/alias/config/terrain tests; run RED.
- [x] Implement common classification, targeted torque and per-class ground factors; run GREEN.
- [x] Validate native forces, grade/reverse/grip/frame-rate/mixed-wheel cases, actual aliases/top-speed/suspension; expect all passed.

### Task 2: Verify, review and install

**Files:** README, tracked evidence, final regression fixes if required.
- [x] Full make all test test-game-assets; focused ASan/UBSan.
- [x] One independent whole-branch review; one TDD fix pass for Important/Critical findings.
- [x] Install game closed, compare installed plugin bytes, verify strict signatures, commit and retain branch.

## Execution record

- Base4845914; local handling-utility branch, established in-place workflow with cached native fixtures. No merge/push requested. Prior authorization covers normal installation.
- Tight RED command: `GAMEDATA='/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData' build/utility/repro` after compiling build/utility/repro.cpp. Native conversion272c1c..272db4, InitGearRatios279918..2799d4, CalculateDriveAcceleration279a1c..279da0, ProcessWheel27ca40..27cf68, physical force consumers1564c0..1565ac.
- Linerunner dry launch3.20→2.72m/s². Rain off moves; restoring mass keeps2.72, restoring acceleration gives3.20 but wet grass still effectively0.00m/s. Fresh undamaged states reproduce the symptom without mechanical hooks. The first prototype prescribed generous adhesion; the graduated fixture instead copies native loader flags, resolves actual native handling IDs, loads surface.dat and executes native surface adhesion/wetness consumers, with player axle bias. Baseline with the old factors/profile still fails (build/utility/baseline-final-red.log): Infernus beach28.12m/s; Landstalker/Bobcat/Linerunner/Barracks/Mule remain stationary.
- Ruling: restore purpose-specific acceleration and ground profiles, preserve current top speeds/masses/suspension — direct fix for measured native deficit — cost: initial factors need live feel tuning; detailed torque curves/tyre simulation remain later work.

- Native source confirms LoadHandlingData copies flags low byte into transmission+0x4b (record+0x7f), at0x272980..272984; GetHandlingId uses native14-byte identifiers. SurfaceTable::Initialise copies the raw symmetric6x6 surface.dat matrix. Wheel producer base adhesion is.004×traction/4/frequency×native surface limit×wet multiplier×player axle bias (0x24ae84..24b318). The fixture preserves those inputs and clears transmission/wheel scratch between independent cases.
- Focused tests GREEN: semi launch2.72→3.68m/s², wet grass0→16.01m/s after10s prescribed contacts; native low-grip case still bogs, broken upstream torque produces no motion. Prescribed5% hill, reverse, mixed contacts,25/50/100Hz agree. These numbers omit complete controller/map/air drag and are not claimed as live road speeds. All77 model/handling associations (71 primaries+6 aliases) and existing top-speed/ride-height tests pass.

- Full `make -j4 all test test-game-assets` exits0 with zero diagnostics (`build/utility/verification.log`). ASan/UBSan native replay and production rain hook fixtures exit0. Independent review/install pending.

- Independent review of4845914..de18e0e: no Critical/Important findings, ready for installation. Reviewer checked all utility/van rows using native wheel order; all traverse soaked grass. Native loader flags, wheel parity, tyre surface, axle bias and grip formula agree with the supported producer.
- Deferred Minor: isolated replay calls wheels0,1,2,3 instead of native0,2,1,3. A temporary native-order replay passes every assertion; semi speed16.014→16.016m/s. Production uses native order unchanged. Deferred under the one-review policy; future fixture fidelity work.
- Ruling: exact Dolphin Expy geometry, complete controller/air drag and subjective balance require the user's next drive — isolated consumers establish the regression and force margins — cost: live thresholds may need further tuning.

- `./install.sh` exits0 with game closed;154 hooks+6 skips. Installed plugin matches build bytes; strict plugin and executable (`--ignore-resources`) signatures pass. No config values overwritten. Work remains on local handling-utility branch.
- Live acceptance: repeat Linerunner on rainy grass near Dolphin Expy, compare Landstalker/Rancher/Patriot on grass/beach to a sports car, and check dry-road pull/speed. Old road top-speed and physical-weight/suspension targets remain; native damage can still reduce drive. Existing30s drive logs provide the next tuning evidence.
