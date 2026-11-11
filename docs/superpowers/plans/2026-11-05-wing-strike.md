# Moving aircraft / building strike implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline. Existing branch, isolated build directory. One fresh final reviewer; no implementer delegation.

**Goal:** Replace the model-only test with an installed, user-launchable, force-driven aircraft-wing/building interaction.

**Architecture:** Reuse the existing pinned IPC/Bullet persistent coordinator without independently advancing Bullet or native game physics. An authored chassis mass lattice carries the Dodo exterior wing skin; an explicitly anchored box is the building. Accepted positions, surviving faces and fragment identities publish together to native instance COL, rendering and queries. The actual aircraft body is a visual attachment to the solved chassis pose. The controlled interaction is ballistic with gravity; it is not a flight/engine/aerodynamic calibration.

**Tech stack:** Existing ARM64 source host and persistent coordinator, C++17 physics adapter behind a plain C interface, native C++11 render/host adapter, Python build/package helpers.

**Spec:** User's controlled aircraft/building milestone and latest rejection of a static model demo. Continue within authorized installed-app scope; do not ask for another design approval.

## Global constraints

- Preserve all existing physics tests, passing assertions and deliberately failed controls. No solver changes without a reproduced blocker.
- One owner advances every moving participant, including separated fragments. Native controllers and Bullet World::Step do not run in this mode.
- Use the actual Dodo exterior wing component, deduplicating coincident exported copies and excluding overlapping separate control-surface geometry. Record the asset transformation; no pretend collision between two different surfaces.
- Authoring constants, gravity/ballistic setup, numerical tolerances, unsupported game/flight lifecycle and measured cost remain explicit.
- Keep normal game/mod executable bytes and all existing rollback apps. Review a complete staging bundle; transactional installation must restore the previous installed build on failure.

## Review focus

- Misses producing damage, scripted rather than mechanically earned fracture, detached mass lost/duplicated or debris no longer colliding.
- Solver/render/native geometry or fragment identity diverging after motion, fracture, reset, pool reuse or failed steps.
- Native or Bullet integration running in addition to the coordinator, including foreign/stale state publication.
- Unbounded render-frame catch-up, rejected intervals presented as successful motion, fabricated real-time performance.
- Updating an already-installed test app damaging normal launch, stale packaged code/dependencies or recovery records.

### Task 1: Reusable strike adapter and physical controls

Files: `tools/host/WingPhysics.h/.cpp`, `tests/test_wing_strike.cpp`, build helper.

- [ ] Write and observe failing checks for ballistic motion, building strike vs miss, actual load/deformation/fracture, exact mass preservation, fixed support receipt, canonical surface export, stale generation rejection and reset/teardown.
- [ ] Implement the C boundary using existing coordinator/material/contact algorithms and pinned static libraries. Keep any rejected physical gate failed; do not weaken it to ship.
- [ ] Run controlled hit/miss and timestep comparisons; record force/reaction, fragments, geometry, energy and timing. Diagnose only demonstrated blockers.

### Task 2: Interactive native moving scene

Files: native scene/render adapter and source-host build helper.

- [ ] Publish accepted moving geometry/poses/fragment entities; native contact, LOS and vertical queries must resolve the same wing surface. No native integration calls in this explicit mode.
- [ ] Add launch, pause, reset, hit/miss and speed controls; make the aircraft, wall contact, deformation and debris readable in a following camera. Show the selected material/collision surface and current numerical/performance status.
- [ ] Exercise repeated resets/deletion/pool reuse, failed preparation and stale publication, plus a rendered hit/miss smoke. Capture/view frames and preserve original65-query proof.

### Task 3: Verified update at the requested app path

- [ ] Support packaging over the previously installed experimental mode; preserve normal game/mod and earlier full backups. Add regression tests for upgrade-specific failure cases.
- [ ] Run existing project/asset/physics checks appropriate to the unchanged solver and adapter. Obtain one fresh review and one tested material fix pass.
- [ ] Install and verify actual LaunchServices startup, geometry/physics smoke, normal game startup, signatures and rollback metadata at `/Applications/Grand Theft Auto Vice City.app`.
- [ ] Record cold/warm step/frame/publication/query percentiles, physical simulated/wall-time ratio, rejected work, allocations/counts and limitations. Keep whole-world/free-roam flight acceptance open.
