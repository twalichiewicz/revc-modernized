# Aircraft systems consistency implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline, task by task, with one fresh final whole-change review.

**Goal:** Consistent ongoing aircraft damage, detached sections and rotor sound; control-attached hand grips; airport aircraft present when approached in a car and a random helicopter on the stock helipad.

**Architecture:** Extend the existing certified projectile/private-mesh/component owners. Keep physical damage, rendering and sound keyed to the same generation-valid aircraft systems. Reuse native physics, streaming and first-person hand posing; add aircraft control tracking points and a bounded helipad roster.

**Tech stack:** C++17, native ARM64 macVC5217, RenderWare; native instruction and installed asset fixtures.

**Spec:** User playtest report in this conversation; continuation of docs/superpowers/plans/2026-11-04-helicopter-presentation-fixes.md. Persistent execution preferences: proceed inline, no workflow interview, local build/install with game closed, no push/merge.

## Constraints and review focus

- Mission/proof/config authority, exact CPlane/CHeli allocation bounds and generation/pose certificates remain enforced.
- Damage must continue past64 hits within mesh/memory capacity; private geometry and shared originals stay correct through repair, streaming and allocation failures.
- Detached sections must affect the matching systems; absent main rotors cannot produce rotor spin-up audio or powered lift.
- Actual plane and helicopter model/class contracts, animated controls, cockpit donor meshes and first-person hand/finger alignment need asset/native checks.
- Airport streaming must operate while driving. Helipad placement comes from stock Helipad0 geometry; obstruction, model readiness, pool scarcity, mission/cutscene and occupied lifetimes stay safe.

## Tasks

- [x] 1. Add regressions for >64 separate hole strikes and real fixed-wing projectile delivery; remove fixed-count stopping in favor of existing topology/memory bounds. Test test_carglass, test_carglass_rw, test_shotreceipt_native and aircraft asset fixtures.
- [x] 2. Reproduce native rotor audio after detachment; couple its queue to actual rotor state. Extend private-mesh section loss and native debris beyond doors/rotors, tied to localized structural integrity and underlying systems. Verify real helicopter tail/airframe and plane wing/tail skin, repair and type-safe native physical debris.
- [x] 3. Add control tracking points following the rendered stick/yoke, feed steering hand frames and articulated finger contacts; native outfit/hierarchy and stock control movement checks. Tracking points are attachment data; debug dots only in diagnostics.
- [x] 4. Reproduce Learjet spawn gating while driving, then allow proximity streaming in vehicles. Add one random suitable stock helicopter at the airport Helipad0 placement, with asynchronous model requests, clearance and generation-safe turnover. Test test_airportjetspawners/native plus helipad asset/lifecycle fixtures.
- [x] 5. Full build/unit/native/assets and focused sanitizer/real RenderWare verification; one fresh review and tested material fixes; closed-game install, byte/signature verification, local commits. Record evidence in build/aircraft-systems-20261104.

Evidence at start: playtest.log model199 and227 reach exactly64 recorded holes; localized damage continues afterward. Main/tail rotor loss recorded, Sparrow entered afterward. airportjets::Frame explicitly excludes PedInVehicle. Whole airframe section removal and aircraft control grips are not currently implemented.

Tasks3/4 evidence: actual stock cockpit selection tests pass Polmav/Maverick/Sparrow/Hunter and transformed Learjet cabin; cached barycentric attachment points follow the current rendered control geometry/frame. Finger contacts use the actual handle cylinder. Original car steering remains separate. Helipad source model876 includes more geometry than its landing area; actual landing-mark centre is local(10.02,-2.03,1.29), verified from its two yellow triangles. Spawn follows the source transform, asynchronous model/clearance/pool gates, preserves occupied actors, uses native virtual deletion and does not require a non-exported destructor symbol. Learjet in-car spawning regression is green.

Task2 evidence: root-member hits share the projectile energy budget with existing modules; separate left/right/tail integrity removes private skin transactionally, retains damaged mesh variants and actual ballistic openings, and updates systems intersecting the lost material. Independent rotor meshes survive body cuts. A multi-atomic native temporary CObject carries detached skin using the stock body-part collision proxy/lifetime. Native vehicle collision remains the stock coarse hull. Allocation failure rollback, repair/stream reapplication, native actor replacement during section callbacks and actual RenderWare ownership tests pass.

Final review: one fresh reviewer found one Important helipad model/TXD streaming flag ownership bug, now covered by failing-then-passing cancellation, pre-existing ownership and spawn checks. Uses metadata-valid owned keep bits, preserving script/priority flags. Real RenderWare ASan/UBSan also found an unaligned packed debris flag access; byte access and neighboring-byte sentinel pass. Final full suite and installation verified below. Arbitrary fracture simulation and replacing native physical collision hulls remain future work; visible/ballistic/internal separation is implemented in three structural regions.


Verified completion (2026-11-04)

- Code head c515577; local branch vehicle-damage-followup retained. Fresh review covered e67916f..bead6a9; one Important finding fixed with RED→GREEN evidence, no deferred minors.
- `make -j4 all test test-game-assets` exited0 on the reviewed fixes; full output build/aircraft-systems-20261104/verification-reviewed.log. Includes13 stock aircraft layouts, real native projectile/controller callers, airport lifecycle/assets and9 patch-capacity tests.
- `make test-carglass-rw LIBRW_ROOT=build/car-glass/librw LIBRW_STATIC=build/car-glass/librw-build/src/librw.a` exited0; real RW source mesh/holes/section/debris ownership passed stock Sparrow, Maverick, Dodo and Skimmer. Output real-rw-reviewed.log.
- Six focused ASan/UBSan fixtures passed with UBSan halt: carglass, airframedamage, shotreceipt_native, aircraftcontrols_assets, airportheli and carglass_rw. Native JIT vector output in the RW fixture is explicitly annotated because the external instructions do not update libc++ sanitizer annotations. Native JIT instructions and the prebuilt librw archive are not sanitizer-instrumented. Output sanitizers-reviewed.log.
- New native bindings resolve in pristine5217: airportheli13, aircraftcontrols14, airframedebris27. No game was launched for verification.
- Closed-game `./install.sh` exited0;166 hooks and10 skips. Installed plugin byte-identical to the build; strict plugin and executable code signature checks exited0.
- Plugin SHA256:14065cc060793bc6b27f3dcf926c74a9be0eb93ebda335584b1c763bf945b664. Installed executable SHA256:2ca038b07e6117173f0085d70eaf7384cf7102172846f9ea302173f570510415. Installation output install.log in the same evidence folder.
- Playtest still needed for visual feel/hand pose and damage tuning. Section fractures use three structural regions; the retained native physical hull can still collide where private skin has separated, and detached debris uses a coarse native proxy. This is a bounded systems extension, not arbitrary fracture/FEM simulation.
