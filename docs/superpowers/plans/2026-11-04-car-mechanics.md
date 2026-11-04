# Localized Mechanical Damage Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline. Track steps with checkboxes.

**Goal:** Local collision damage changes engine/cooling and individual wheel performance.
**Architecture:** `mechanicalmodel` owns pure layouts/doses/thermal/effect rules; `carmechanics` owns actor history and scoped native physics. Existing collision owner becomes `carimpact`, feeding dents and mechanics.
**Tech Stack:** C++17, macVC1.0.5217 ARM64, native/asset fixtures.
**Spec:** docs/superpowers/specs/2026-11-04-car-mechanics-design.md

## Global Constraints

Six modules, 256 actor slots; native protections/health/tyres/configuration retained.
4.5 m/s minimum; dose /26 capped0.55; 350 ms/module contact gap; toe cap0.10 rad.
Full-damage wheel grip/drive/brake 0.45/0.65/0.50, puncture threshold0.75.
Engine 1−0.85d (zero >=0.98); coolant leak0.025d²/s. CarMechanics switch.
No changed visual severity, shared handling writes, aircraft physics or save formats.

## Review Focus

1. Surface position and per-model engine/wheel layouts select the underlying module, not global health.
2. Collision-piece IDs and strongest native record remain correct; disabled deformation still permits mechanics.
3. Transmission/wheel hooks preserve native pointer/scratch/output lifetime and other actors' physics.
4. Cooling stays stable intact and frame-rate independent; pauses, repairs and rewinds cannot inject heat or stale effects.
5. Native tyre/proof flags, frame mapping, nested/recycled identity and render clump replacements preserve ownership.

### Task 1: Pure mechanical model

**Files:** src/mechanicalmodel.h/.cpp, tests/test_mechanicalmodel.cpp.
**Interfaces:** Layout built from model/body bounds/four native wheel positions; Doses(Layout, carimpact::Collision); Apply(State,doses,normal); Tick(State,seconds,engineOn,load,speed); Power(State), Wheel(State,index).
- [x] Tests RED→GREEN: engine location by layout, isolated wheel/cabin hits, severity, accumulation, effect caps, intact/failed cooling across frame rates, invalid input.

### Task 2: Shared certified impacts and native mechanics

**Files:** rename cardeform to carimpact, src/carmechanics.h/.cpp, config/minimap/Makefile/patcher, collision/mechanics tests.
**Interfaces:** carimpact::Collision {point,inward,severity,piece}; carmechanics::Collision(vehicle,event); Install(). Native transmission result scaling and ProcessWheel per-wheel input effects under a validated control scope.
- [x] Tests RED→GREEN for correct native collision-piece routing, disabled dents with mechanics enabled, and preserved veto/script/repair/certification rules.
- [x] Tests RED→GREEN for engine power, wheel effects, protected punctures, original handling/scratch/refs, nested actors, render-only replacement, repair/restart/recycling and off.
- [x] Implement checked native layout reads, bounded persistent history, thermal ticking and damage transition logs. Expect focused fixtures all passed.

### Task 3: Real assets/native consumers and completion

**Files:** tests/test_mechanical_assets.cpp, native mechanical fixture support, README, tracked execution evidence.
- [x] Validate original car layouts and native wheel ordering; execute native transmission and wheel physics with intact/damaged effects. Expect lower drive, asymmetric wheel forces and preserved native scratch/output writes.
- [x] Full make all test test-game-assets; focused sanitizers and impact/lifecycle regressions.
- [x] One independent whole-branch review; no Critical/Important findings.
- [x] Install game closed, verify bytes/signatures and record live performance checks. Commit records completed phase.

## Execution record

- User selected mechanical damage first. New local car-mechanics branch from10214ec; continue established checkout workflow, no merge/push requested.
- Native transmission receives the original handling+0x34 allocation, writes +0x5c scratch and retains gear pointers. Scale returned drive acceleration rather than replacing handling.
- Native wheel solver takes per-wheel force/grip and direction vectors, preserves native speed/state output pointers, and composes with the rain hook. Native wheel frame table is {4,6,1,3}; BurstTyre maps collision pieces13..16 to {0,2,1,3} and retains its tyre-proof gate.
- Native VehicleDamage's dispatch sends pieces1..4 to body damage and pieces13..16 directly to the health tail. Earlier deformation filtered1..4 as wheel pieces incorrectly; the new impact-routing regression must pin that distinction.
- Body-piece/wheel routing regression failed before the fix, then passed in ordinary/native broker modes. Renamed the shared collector to carimpact; CarMechanics remains active independently of CarDeformation.
- Pure/owner/asset checks pass. All76 original car DFF/COL layouts validate. Native InitGearRatios/CalculateDriveAcceleration/ProcessWheel/ApplyMoveForce/ApplyTurnForce execute in isolated fixtures with external vector math and deterministic RNG relocated. Sample engine hit changes native drive0.007167→0.004483; bent wheel creates lateral0.003431 and yaw−0.001577; reduced grip lowers real traction-circle force. Native gear pointer and current-speed scratch remain in the original shared handling allocation.
- ASan/UBSan pure/owner/native/broker ordinary+native cases pass. Scratch patch154 hooks+6 skips. Full suite and independent review running; live performance acceptance remains pending.
- Initial full `make -j4 all test test-game-assets`: exit0, no warnings/errors. Independent review found no Critical/Important findings. Native actor references, wheel outputs/frame ordering, shared transmission storage and protected tyre requests agree with the inspected call sites.
- Deferred Minor: coolant/heat ticking discards control gaps above250 ms. Repeated300 ms updates freeze thermal progression; native timer behavior at such low update rates was not established. Bounded substeps with a separate discontinuity rule are subsequent work. Normal25/50 Hz integration agrees; dedicated final owner checks cover a large pause, clock rewind/resumption and actual render deletion/rebuild without healing damage.
- Native fixtures execute individual transmission/wheel/force consumers and scoped ownership contracts, not the complete live controller/broker/rain chain. Gameplay balance, native engine audio/start behavior and detailed internals remain explicit live/future scope.
- Final `make -j4 all test test-game-assets` exits0 with no warnings/errors, including all lifecycle cases and eight patch-capacity checks. Owner/native ASan/UBSan reruns pass. Logs: `build/car-mechanics/verification-final.log`, `lifecycle-checks.log`, `patch-check.log`.
- `./install.sh` exits0 with the game closed, installing154 hooks+6 skips. Installed dylib is byte-identical to build; strict executable/plugin signatures pass. `CarMechanics` defaults on when missing from the existing ini; `CarMechanics=0` disables this pass.
- Live performance check: ordinary front-engine car frontal/deep bay hits lose acceleration, Deluxo rear-bay hits lose acceleration, cooling damage grows into heat/power loss while running, an injured corner pulls/slips/brakes weakly, and repair restores performance. Read `car mechanics:` integrity/power/coolant/heat transitions in the existing log. First pass is collision-driven; per-model internal refinement, projectile penetration and the deferred slow-update thermal edge remain future work.
