# Regular-game aircraft physics integration

> **For agentic workers:** Execute inline using superpowers:executing-plans. One fresh final review after integration. This supersedes the wing-strike demo plan as the user deliverable.

**Goal:** Launch GTA:VC normally at the requested /Applications path, load ordinary saves, and encounter structural aircraft damage during normal gameplay.

**Architecture:** Use the pinned source host for authoritative entity collision publication. Preserve entity layouts with a sidecar registry, validate the existing mod's native ABI before loading a source-specific plugin, and transfer motion ownership for supported aircraft collisions to the existing coordinator. Native world contacts, LOS and vertical queries resolve one accepted instance mesh. Standalone scenes remain internal diagnostics.

**Tech stack:** Existing C++ engine, C++17 mod/physics adapter, IPC/Bullet coordinator, Python reproducible build/package tools.

**Spec:** User's latest correction: normal GTA:VC with the new systems visible in regular gameplay. Existing controlled island, lifecycle, numerical and source-availability requirements remain binding.

## Global constraints

- Do not present or install another separate test scene as the deliverable.
- Preserve original assets, saves, current Metal game, mod and rollback apps.
- Preserve physics tests and their thresholds, including failing controls. No solver expansion without a demonstrated integration blocker.
- Do not load a plugin with unverified layouts or run two motion owners on a supported island.
- Unsupported vehicles, encounters and numerical limits must remain explicit.

## Review focus

- Normal menu, save, mission, controller and vehicle behavior regressing on the source host.
- ABI mismatch allowing raw offsets to corrupt source engine objects.
- Regular-world damage remaining cosmetic or absent from native queries.
- Stream-out, deletion, repair or reused pools leaving stale geometry/physics owners.
- Frame stalls, rejected intervals or installation failures being hidden by isolated test success.

### Task 1: Full-game source compatibility

- [x] Reproduce ordinary save loading under ASAN; find and correct demonstrated controller, streaming semaphore and compressed texture errors.
- [x] Retain startup changes in reproducible patches and test their failure patterns.
- [x] Compile ABI checks against raw mod layouts, generate a source UUID-specific plugin and named hooks; resolve any demonstrated incompatibilities.
- [x] Load an ordinary save, render the world and mod, inspect captures, and exit cleanly under ASAN in an isolated profile. New-game/input/mission QA remains in the final gameplay verification.

### Task 2: Aircraft integration in the normal world

- [x] Accept actual aircraft mass, pose, spin, chassis bounds, full wing skin and exact static native COL through the existing adapter.
- [x] Connect exclusive motion ownership, accepted pose/skin/COL publication, native fragment objects and quiescent lifecycle handling to ordinary aircraft updates.
- [x] Exercise real normal-world hit/miss, external impulse, attached/detached contact/LOS/vertical agreement, repair and pristine callback traversal. Preserve original deletion/pool-generation query checks. Bullet material loading and arbitrary aircraft/mission integration remain outside this milestone.
- [x] Measure release solver/publication cost and explicitly hand unsupported body/curved/dynamic/capacity boundaries to native behavior. Do not claim acceptable whole-world solver performance.
- [x] Fix the demonstrated unsupported real-aircraft load path:48 triangulated root members with mass/gravity-scaled areas, actual chassis bounds; preserve diagnostic fixture, constitutive parameters and solver guards. Verify balanced and turning flight produce zero plasticity/mount failure.

### Task 3: Normal-game installation

- [x] Run preserved project and physics checks, fresh review and concrete fix pass.
- [x] Stage the normal-game bundle with protected old game/mod, verify ordinary saved-game render and actual damage encounter. All13 staged hit,11 miss and14 external assertions pass.
- [x] Install transactionally at /Applications/Grand Theft Auto Vice City.app and verify installed gameplay; record exact build and rollback identities.

## Evidence

- Reproducible final full-game build: `build/regular-cache-final/manifest.json`; exact source/header/artifact/dependency identities recorded. Its ordinary full-game executable and UUID-bound plugin replace neither source provenance with the Metal host nor its protected original binaries.
- New real load path ASAN lifecycle: `build/regular-game/pristine-loadpath/report.json`, all11 checks pass while applying a native external force during actual sector-list traversal, then restoring original mass/turn mass/COM at the next safe boundary.
- Final staged release: `build/regular-game/release-hit-logged/report.json`, `release-miss/report.json`, `release-external/report.json`. Real Dodo/hangar fracture with contact, same accepted native query geometry, zero overlap, retired repair, and external force applied once. Miss stays elastic with intact mounts through18.11 simulated seconds.
- Performance is experimental: hit max requested solver step750.793ms; mean/max publication0.0873/0.095ms. Clean-flight max239.412ms. No FPS or acceptable full-world performance claim.
- Project regressions: `build/regular-source/project-regressions.log`. Rebuilt original matrices: continuous79/79, incremental12/12, persistent34/34, coupled63/63; diagnostic strike52/52 unchanged. New world/load/ledger/contact-filter tests pass. Separately recorded SDK failing control remains failing.
- Final read-only review found no remaining blocker within the declared bounded slice and verified current release/staged input identities and protected original bytes. It required installation and installed-app verification to complete the task.
- Known exclusions and source-availability decision: `docs/research/2026-11-06-regular-game-wing-slice.md` and `tools/host/README.md`.

## Installed outcome

- Final signed package journal: `build/regular-installed/package-manifest.json`; installed at `/Applications/Grand Theft Auto Vice City.app`, prior bundle preserved at `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-010838.app`.
- A first attempt correctly rolled back when gameplay cache writes invalidated its signature. The exact four generated files now resolve to the source profile's `host-cache` across audio, FileMgr, RenderWare and CD streaming. Cold staged hit and warm staged miss pass with signatures intact; other asset paths remain unchanged.
- Actual installed default-profile normal game and wing strike pass (`build/regular-game/installed-normal/report.json`, `installed-hit/report.json`). The city/save/mod load normally; real wing fracture/repair and30 attached/42 detached native query checks pass; post-play signatures remain valid. Original game/mod/profile hashes remain unchanged.
- Cache-final hit step maxima582.524–828.115ms remain an explicit experimental performance limitation. Full details and artifact identities are in the research evidence document. No whole-world, arbitrary mission or calibrated-airframe claim.
