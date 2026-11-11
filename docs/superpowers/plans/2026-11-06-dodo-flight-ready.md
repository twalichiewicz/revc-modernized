# Dodo flight readiness implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline. The user has authorized development and installation in the normal game; do not stop for another design interview or commit the dirty checkout.

**Goal:** Make the ordinary airport Dodo usable for takeoff and the existing wing-damage playtest, with proper rolling gear, articulated control surfaces, a seated pilot and visible cockpit/yoke.

**Architecture:** Reuse the Learjet's native wheel contact controller and model preparation machinery with measured Dodo-specific geometry. Prepare only the generation-validated roster actor's private clump, leaving shared original assets/ambient aircraft intact. Use a source lifecycle coordinator to retain the cockpit donor and reprepare replacement clumps; existing source wing ownership remains authoritative.

**Tech stack:** C++17 mod, pinned reVC source host/librw, original Dodo and Police Maverick assets, existing Python reproducible build/transactional installer.

**Spec:** User's four Dodo corrections on2026-11-06, following the installed normal-game milestone. Same treatment as Learjet; ordinary game installation required.

## Decisions and constraints

- Reuse existing contact/model/hand systems with Dodo-specific measured anchors. A wholly new wheel solver duplicates working code; a stretched Learjet model would replace the requested aircraft. Neither is needed.
- No custom solver expansion. Preserve all original physics assertions and unsupported-boundary behavior.
- Original Dodo tire geometry: radius.258m, main hubs at X±1.79086/Y−.432198/Z−1.24355; nose at X0/Y2.649681/Z−1.24355. Tire bottom−1.50155m. Verify actual mesh in asset tests before hardcoding.
- Fit cockpit donor around pilot root(-.32,.25,.05), replacing the old forward1.25m root; confirm actual seated head/windscreen clearance in-game. Use original textured seats/instruments and a separate animated yoke with named hand anchors.
- Keep the right supported wing's original neutral triangles compatible with its canonical material asset. While material-owned, that wing's deformation is authoritative; no competing flap-frame writer. Reset the supported flap before admission and retain material-damaged geometry until repair.
- Clump replacement/deletion/pool reuse must not retain raw frame writers or donor references. Prepared geometry/materials belong to the native actor lifetime. Cache files remain outside the signed app.
- Retain previous Metal fallback, original assets/saves, complete backup and journal. Install only while no user game is running.

## Review focus

- Tire mesh and contact hub coordinates agree; suspension supports three wheels without body dragging, correct brakes/steer/spin and no Automobile-only fields.
- Wheel/flight controllers do not oppose each other's ground yaw/braking; actual runway takeoff, level flight and landing remain testable.
- Pilot, eye, seat, yoke and named hand anchors agree in model/world space; instrument panel does not obscure windscreen.
- Rig animation must not move the canonical material wing independently or resurrect detached geometry.
- Donor streaming, allocation failure, repair/reload/model replacement and shared geometry ownership remain safe.

### Task1: Rolling Dodo gear

- [x] Add a failing native-shaped Dodo wheel regression: three contacts,500-step settling at actual tire height, physical spin/braking/steering and ambient exclusion.
- [x] Add measured Dodo profile to existing jetwheels; accept only its roster owner, keep fixed gear deployed, use current profile in all contact/load/rotation paths.
- [x] Route Dodo ground classification to that controller and park/board at the actual tire plane. Verify existing Learjet/Skimmer force and wheel regressions.

### Task2: Rig, cockpit and pilot

- [x] Add actual-asset failing preparation/pose tests using original Dodo hierarchy plus original textured cockpit donor.
- [x] Extend existing aircraft mesh preparation for Dodo: split its real tires, flaps/ailerons/elevators/rudder, preserve skin/UVs/materials, add cockpit, yoke and named grip frames; animate from actual physical wheel snapshots/flight inputs.
- [x] Add source private-clump lifecycle/donor coordinator. Repair and streamed replacement rebuild the rig safely; never edit shared model data.
- [x] Correct pilot root/orientation and resolve hands from moving named yoke anchors. Verify seated head and forward clear view with actual assets.
- [x] Preserve canonical wing ownership/query proof across neutral admission, animation, fracture and repair.

### Task3: Ordinary game acceptance and installation

- [x] Build source host/plugin with recorded identities, run project/asset/physics checks and fresh independent review.
- [x] Ordinary saved-game automation: ground settle, taxi/brake/steer, animated surface/yoke checks, seated/cockpit capture and real force-driven takeoff; compare against the pre-fix baseline.
- [x] Repeat normal-world wing deformation/queries/repair and post-play signature checks (opt-in graze; separation assertion retained separately and not relabeled passing).
- [x] Install transactionally in /Applications/Grand Theft Auto Vice City.app, verify there with normal profile and preserve old app/profile hashes; record evidence and numerical/visual limits.

## Execution evidence

- Gear regression RED: original Learjet passes, Dodo fails all five contact/settle/spin/brake/steer checks. GREEN after measured profile/ownership integration. Existing Learjet ground/flight and roster regressions pass; the obsolete forward-seat expectation was replaced with exact transformed(.32,−.25,.05) placement and observed RED→GREEN.
- Actual Dodo asset preparation RED (`Prepare` rejected its six-atomic clump), then GREEN with three92-face tires, textured donor cockpit, moving surfaces/yoke, named grips, material-owned right-flap exclusion, idempotence, allocation rollback and zero leaked native-shaped objects. Shared source geometry fingerprint stays identical.
- `make -j8 test` exit0, `build/dodo-review/project-tests.log`. Native ASAN source host compiles (`host-build3.log`); source plugin/full-game acceptance in progress.
- Mesh cuts require interpolation from original canonical triangles. `WingMapping.h` adds only render-vertex barycentric mapping, no solver nodes/formulas. Original material mesh/test fixtures remain unchanged. Hidden original LOD atomics are excluded from publication, preventing duplicate fragment geometry.
- Work remains in the existing dirty `vehicle-damage-followup` branch, preserving all prior changes and build evidence; no commits or new worktree are needed for the already-authorized continuation.


## Terrain/crash regression and architecture decision (latest user steering)

The earlier release wing/miss receipts are insufficient: their logs show accepted Z below−300m while `report.json` omitted terrain assertions. Preserve them as failed evidence, not proof of safe flight. The underlying world-neighborhood helper used `FindObjectsInRange`, which filters entity origins and omitted large runway COL surfaces far from their model origins.

The replacement walks the same native sector/overlap lists as contacts, LOS and vertical queries, deduplicates entities, culls transformed COL bounds/triangles against the swept island, and fails closed on capacities/unsupported primitives. Whole-frame conservative body admission replaces a5ms-only initial envelope. Only a coming wing contact can acquire the coordinator; a clear flight interval retires it. A native vertical-to-neighborhood coverage check and continuous ground-penetration assertion now run in the real airport smoke.

A second defect charged managed Dodo/Learjet impacts both to the existing material/occupant system and to a scalar health/explosion formula. The latter now runs only without a material owner. RED→GREEN roster regression and ordinary-game nose contact prove survival plus actual mesh movement (ASAN nose displacement.141964m, pilot100, hull1000). This is localized deformation, not full-body crumpling.

The instrumented implicit graze still measured424.060ms for a coordinator frame (345.211ms single substep) despite correct ground coverage. Decision: keep native motion/collision and localized materials as the normal launch path; retain the implicit wing solver explicitly opt-in in the ordinary world. Do not expand its numerical development in this pass. Measure the optimized build independently before recording final numbers. Its original52 strike assertions, SDK failing controls, tolerances and energy guards remain intact. Full-airframe deforming triangle contact/constitutive structure remains a future architecture milestone; native moving-body sphere/line narrowphase is not that capability.

Fresh final build, stage validation and transactional installation are pending below; the older `dodo-ready` candidate must not be installed.


## Optimized release evidence and review

- Host built from the pinned source recipe: `build/dodo-terrain-release/manifest.json`, raw host SHA256 `f49df248b853f6cdc4c76ead092e342fa768dda1e08270ad5d7434646e61bce3`; unchanged exported aircraft mesh `33bea8c76d38b658731273e5f1aaf95329a6222674fc0bc1e2108f4775db8729`. Original strike52/52 pass in the builder's physics log.
- Initial optimized staged Dodo: all18 checks pass (`build/dodo-review/final-dodo/report.json`), physical takeoff43.46646m/s, altitude gain18.23737m, health1000; rendered intervals median33.989ms/p95 34.289ms/max35.740ms. These intervals include rendering/vsync/scheduling, not only physics.
- Initial optimized staged native body impact: all14 checks pass, actual nose vertex displacement.142846m, hull1000/pilot100. The optimized opt-in graze passes all18 safety/geometry/repair checks but costs312.748ms for a coordinator frame (273.926ms substep). This independently confirms the decision to exclude the implicit solver from default launch. These reports used the plugin before the contact-readiness review fix.
- Fresh focused review found one conditional issue: material Effects ownership can become available before all contact hooks install. Suppressing the fallback on Effects alone would lose both consumers after incomplete installation. Fixed with `AirframeContactsManaged`, published by carimpact and requiring actual readiness, actor identity and a material receiver. RED→GREEN roster regression covers absent consumer; native-contact fixture checks readiness false when hooks unavailable. The reviewer found no further Important normal-path issue.
- Native host inputs did not change for this final readiness fix. Rebuilt the complete matching plugin with `build_plugin.py` against that exact raw host; refreshed the parent build's embedded plugin manifest and recorded previous manifest/refresh recipe. New plugin SHA256 `c8cf740acfc9617aaa8150a6345a7bb6d9744c607961e769897be6728bab87a0`. A new stage `build/dodo-flight-final-install` is used; earlier staging journals are preserved.
- `make -j8 test` passes after all code changes (`build/dodo-review/project-tests-contact-final.log`). Dodo model, actual Learjet gear/cabin and actual-asset hand checks also pass. Final refreshed-plugin stage/installation acceptance follows.

- The reviewer rechecked the readiness fix and confirmed the conditional Important finding resolved. Refreshed-plugin stage native impact passes all14 checks with terrain coverage, survival, mesh deformation and intact bundle signature (`safe-stage-impact/report.json`).


## Installed outcome

Installed on2026-11-06 in `/Applications/Grand Theft Auto Vice City.app`; rollback bundle `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-024123.app`. Durable package journal: `build/dodo-flight-final-install/package-manifest.json`. The original root Metal executable/mod hashes remain unchanged. All nine original/default source profile INI/save files retain their before-install hashes, including after an actual normal-profile launch.

The final installed app passed normal save/render/mod routing, all18 Dodo checks, and all14 native body-impact checks. Dodo rendered-frame median33.991ms/p95 34.259ms/max35.769ms; native impact median33.994ms/p95 34.240ms/max56.537ms, nose displacement.142851m and pilot/hull100/1000. These are bounded automated airport runs, not a claim of complete landing/crash coverage or a16ms worst-case CPU budget. Post-play deep/strict bundle signature passes. Final receipts and metrics: `build/dodo-review/final-verification.json` and its individual report paths.

Full-body crumpling and native collision proxies following every visual body dent remain unfinished. The expensive implicit solver is opt-in; no normal-launch claim implies full material simulation of the fuselage. The shared damaged-wing contact/LOS/vertical proof remains an explicitly measured experiment. No additional nonlinear solver development was introduced for this pass.
