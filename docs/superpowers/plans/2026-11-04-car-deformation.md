# Car Deformation Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline; checkbox steps track progress.

**Goal:** Fix Deluxo rear glass, then add persistent localized visual collision dents.
**Architecture:** Pure dent math in dentmodel, certified native impulses in cardeform, one shared private mesh owner in carglass.
**Tech Stack:** C++17 plugin, exact macVC 1.0.5217 ARM64, NULL librw fixture.
**Spec:** docs/superpowers/specs/2026-11-04-car-deformation-design.md

## Global Constraints

16 deformed cars, 128 total records, 64 parts/car, 16 MiB including offsets.
Radius 1.25 m; start 3 m/s; depth ×0.018, max 0.18 m; total displacement max 0.25 m.
Native damage record match, 200 ms event age, 350 ms contact gap; CarDeformation switch.
Preserve native health, hulls, handling, glass, hinges, detached objects and repairs.

## Review Focus

1. No dents from line queries, failed collisions, stale/recycled identities or scripts.
2. Glass and dents survive either operation order; repair/deletion restores/releases both.
3. Native hidden variants and swinging/loose parts keep their own transforms and visibility.
4. Geometry/normals/bounds remain finite and bounded under repeated impacts; allocation failure is transactional.
5. Native impulse ABI and shadow CPU-vertex consumer agree with actual supported build.

### Task 1: Deluxo rear glass

**Files:** glassmodel.cpp, carglass.cpp, their tests and test_glass_assets.cpp.
**Interfaces:** existing Classify and native Bumper callback.
- [x] Watch actual asset rear-pane assertion and shallow-rear/quarter-crash fixtures fail.
- [x] Lower measured slope exclusion and trigger fixed rear quarters on severe rear damage; focused tests pass.
- [x] Full regression/real mesh checks and review alongside this phase; commit at phase completion.

### Task 2: Pure dents and shared geometry

**Files:** src/dentmodel.h/.cpp, src/carglass.h/.cpp, tests/test_dentmodel.cpp, tests/test_carglass.cpp, tests/test_carglass_rw.cpp, Makefile/config.
**Interfaces:** dentmodel::Impact {point,inward,depth,radius}; Make(point,inward,severity,Impact&), Move(original,current,Impact), Region(point,inward), Body(frame); carglass::Dent(vehicle,Impact) returns successful mutation.
- [x] Write/run failing force/falloff/cap/invalid-input tests, implement pure math, expect all passed.
- [x] Write/run failing owner tests for dents, both glass orders, hidden variants, repair, budget/car limit and allocation rollback; implement local offsets and atomic rebuilds, expect all passed.
- [x] Run real librw copies including dents, normals and bounds; expect all passed with all references freed.

### Task 3: Certified native collision owner

**Files:** src/cardeform.h/.cpp, tests/test_cardeform.cpp, config/minimap/patcher/README, native ABI/RT evidence.
**Interfaces:** cardeform::Install(); chain ProcessControl, VehicleDamage, Fix/Delete, and three CPhysical ApplyCollision overloads. Consume carglass::Dent, partsmodel contact episodes.
- [x] Write/run failing native-shaped contact/damage fixtures: successful matching impulse deforms correct car once, stale/query/script/protected/wheel/ground contacts do not, nested actors and repairs remain scoped.
- [x] Implement bounded identities/pending events, force normalization and unchanged native arguments/results; run focused tests, expect all passed.
- [x] Verify native callers/offsets/return ABI and CPU dynamic-shadow geometry consumer against pristine image; record exact coverage limits.

### Task 4: Verify, review and install

- [x] Full make all test test-game-assets, ASan/UBSan focused suites, patch capacity and real mesh builder.
- [x] Independent whole-branch read-only review; one RED→GREEN fix pass for Important/Critical findings.
- [x] Install with game closed; verify installed bytes/signatures and record remaining live checks. Commit records this completed phase.

## Execution record

- Continuation uses the existing local checkout on new branch car-deformation from 90e2582; no merge/push requested.
- Rear repro: actual Deluxo inventory lacked Pane::Rear; unit shallow rear assertions and fixed-quarter collision owner assertion failed, then passed. 77 car assets now expose all six Deluxo pane regions with zero exclusions regressions.
- Native evidence in build/car-deformation/: collision-native.disasm, sector-native.disasm, control-native.disasm, damage-native.disasm. Car control consumes damage at 0x100249ad0 before clearing its native impulse at 0x10024a0ec; world collision records must persist until that later control, rather than requiring collection inside the control scope.
- Ruling: Single collision overload has no other-entity input, so force/piece/normal certify that overload; pair/alternate also match the other entity. Its real ARM64 body executes in `test_cardeform --native`, with external vector math relocated. Pair/alternate callers and output/return ABI were inspected statically.
- Sixteen NULL-librw strip/list, broken/intact, glass-only/mixed, dented/intact combinations pass. The exact native `appendAtomicTris` consumer reads each deformed copy's current CPU vertex positions, including stable collapsed glass ordinals; no geometry or material references remain allocated at completion.
- Initial full `make -j4 all test test-game-assets`: exit 0, no warnings/errors, including eight patch-capacity cases. Shared owner, collision owner (ordinary/native) and pure model pass AddressSanitizer/UndefinedBehaviorSanitizer. Scratch patch fits 153 hooks + six skips. Independent review remains in progress.
- Independent review: one Important finding, no Critical or Minor findings. Native collision keeps its first strongest record on an equal-force tie; the capture was replacing it. Added regression failed in both ordinary/native modes, then passed after capture also used strict `>`. Final full `make -j4 all test test-game-assets` exits 0, no warnings/errors. Collision owner ASan/UBSan ordinary/native rerun also passes. Logs: `build/car-deformation/verification-final.log`, `native-mesh-final.log`, `review-regression-green.log`, `patch-check.log`.
- Remaining live checks: Deluxo rear glass and quarters from bullets/severe rear collision; localized visible dents from front/rear/side hits; repeated contact holds one stage, separate hits deepen; swinging/detached parts retain their appearance, other cars stay intact, Pay 'n' Spray restores car geometry, and moving Metal shadows follow the visible body. Pair/Alt full native routines were inspected statically; native Single collision and the dynamic CPU triangle consumer were executed in isolated tests. No live-world rendering or hinge/debris presentation was automated.
- `./install.sh` exits 0 with the game closed, installing 153 hooks + six skips. Installed dylib matches the build byte-for-byte; strict executable and plugin code-signature checks pass. `CarDeformation` defaults on when absent from the existing ini; `CarDeformation=0` disables the pass. #37 remains open for live deformation acceptance/tuning.
- Following drive: user confirms visible deformation, though minor. Log records Deluxo dent depths0.09,0.10,0.12 and0.18m with gameplay continuing. User chose mechanical damage first for the follow-up, keeping visual severity. Native collision-piece routing is corrected there: body IDs1..4 qualify; actual tyre IDs13..16 stay rigid. The certified collector is renamed carimpact and shared by both passes.
