# Car Systems Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Track steps with checkboxes.

**Goal:** Local impacts disable dedicated brakes, steering, drivetrain, fuel and electrics.
**Architecture:** Extend mechanicalmodel's volumes/state/effects, consume them in the existing scoped carmechanics physics hooks, and expose a read-only lighting supply to carlights.
**Tech Stack:** C++17, supported macVC1.0.5217 ARM64, asset/native fixtures.
**Spec:** docs/superpowers/specs/2026-11-04-car-systems-design.md

## Global Constraints

Fourteen modules; retain existing indexes0..5, bounded256-slot history and350 ms episodes.
Braking1−0.9d; steering authority1−0.8d, bias<=0.06 rad; drivetrain1−0.8d; electrical torque1−0.65d and lighting1−0.8d. Brake/drivetrain/electrical zero at d>=0.98.
Fuel leak0.012d²/s, empty<=0.01; intact fuel unchanged. Tick<=1s in<=0.05s substeps; larger gaps ignored.
Native handling, actor refs, native protections, output pointers and visual tuning unchanged.

## Review Focus

1. Front/rear layouts and spread keep individual brakes, rack, tank, gearbox and electrics local.
2. Native steering direction/sign, service braking and drivetrain result actually affect forces without mutating shared configuration.
3. Fuel/electrical failures differ from instant engine-health penalties; parked leaks and slow frames work.
4. Light supply validates generation/lifecycle outside control scopes and cannot dim unrelated actors/world lights.
5. Repair, render replacement, recycling, nested callbacks and feature switches retain existing ownership rules.

### Task 1: Model and native mechanics

**Files:** src/mechanicalmodel.*, src/carmechanics.*, tests/test_mechanicalmodel.cpp, tests/test_carmechanics.cpp, tests/test_mechanical_native.cpp, tests/test_mechanical_assets.cpp.
**Interfaces:** named module indexes, State fuel/steeringBias, WheelEffect steering, Lighting(State), carmechanics::LightingPower(vehicle). Existing Doses/Apply/Tick/Power unchanged signatures.
- [x] Add independent system and layout/clock tests; run RED on current code.
- [x] Implement fourteen volumes/effects, bounded integration and native wheel steering adaptation. Run focused fixtures; expect all passed.
- [x] Extend real native brake/steer/drivetrain checks and actual layout validation; expect changed force/yaw with native storage intact.

### Task 2: Electrical lighting and completion

**Files:** src/carlights.cpp, tests/test_carsystem_lights.cpp, Makefile, README and execution evidence.
**Interfaces:** carmechanics::LightingPower returns1 for unknown/protected/disabled actors, otherwise current supply. carlights reads it for all custom and native car light paths.
- [x] Add production-hook lighting tests for partial/failed supply, unrelated cars/world emissions and configuration/lifecycle; run RED.
- [x] Apply supply with nested/exception-safe emitting-car scope; run GREEN.
- [x] Full make all test test-game-assets, focused sanitizers, independent final review and one fix pass if needed.
- [x] Install game closed; compare bytes, verify strict signatures; record results and commit.

## Execution record

- Base8f7a2f5 on local car-systems branch; existing in-place isolated feature workflow keeps native build fixtures available. User requested continuation and installation is already authorized.
- Successful live Deluxo run: coolant0%, heat0.67→1.00, power90%→40%, 0–100 roughly3.5→5.9s. New work leaves those performance curves unchanged.
- Ruling: accept <=1s game-time intervals via bounded substeps now that fuel joins cooling; ignore larger discontinuities. Prior <=250ms gate froze damage at slow update rates.

- Pure/owner RED→GREEN verified14 modules, isolated caliper/rack/drivetrain/electrical failures, delayed parked fuel leak and300ms integration. Sampling the thermal discontinuity fixture at80s retains partial power after350ms setup intervals now integrate. Hub toe remains capped0.10 rad; total front direction can additionally include rack bias0.06 rad.
- All76 actual asset layouts pass; real native caliper force−0.010000→0 and rack yaw−0.011335→−0.002303. Native gear storage/scratch and caller wheel vectors/outputs remain intact. Initial full suite exits0.
- Lamp hook tests failed on custom/native supply, nested car scope and exception cleanup before implementation, then passed. Supply owner tests cover out-of-control reads, render replacement, disabled/protected/recycled actors and repair.

- Full verification `build/car-systems/verification.log`: exit0, no compiler warnings/errors. ASan/UBSan model, owner, real native and production lamp-hook fixtures exit0 (`build/car-systems/*-sanitized.log`). Independent final review/install pending.

- Independent final review found two Important issues: unprojected actor basis interpreted terrain tilt as steering (zero-input tilted-plane reproduction injected0.2574 rad), and broad render scope dimmed DoSunGlare/nonlamp/foreign emissions. Both reproduced RED, then fixed GREEN in one pass. Steering measures angle in the native wheel plane; electrical dimming uses native lamp IDs{0,1,6,7,14,15,21..24} and point-light return offsets{252fb4,253668,256e1c,256e9c,257178}. New regressions cover two actor headings, zero/signed steering, sun glare, foreign emissions, cabin containment, all native producers and custom-glow disabled.
- Review rulings: native projected ground light patches retain original brightness (cost: visible ground patches after lamp supply fails); detailed model internals/projectiles/persistence/engine audio remain future scope (cost: coarse internals and native audio after drive failure); live balance/appearance remains the user's acceptance (cost: offline tests do not establish feel).

- Final fix-pass verification: `make -j4 all test test-game-assets` exit0, zero warnings/errors/failing assertions (`build/car-systems/verification-final.log`); updated owner/native/lamp ASan/UBSan runs exit0. No review Minor findings. Native disassembly retained under `build/car-systems/native-*-review.disasm`.

- `./install.sh` exit0 with game closed,154 hooks+6 skips. Installed dylib matches build byte-for-byte; strict plugin and executable (`--ignore-resources`) signature verification both exit0. Completion stays on local car-systems branch; no merge/push requested.
- Next live drive: hard corner impacts should weaken the relevant brake and steering response; bay/gearbox/electrical hits reduce drive, electrical damage dims lamps; tank breaches leak over time, including while parked, then remove drive. Native Fix restores14 modules, coolant/fuel and heat. Logs include module integrity, power/coolant/heat and fuel bands. Ground light projections/native engine audio and detailed internal/ballistic/persistent simulation remain the documented limits; live feel still needs user acceptance.
