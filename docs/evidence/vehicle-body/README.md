# Actual vehicle asset inventory

The inventory reads the installed game's `data/default.ide` vehicle catalog and the corresponding DFFs from `models/gta3.img`. It contains all 107 native catalog entries (IDs 130–236): 78 automobiles, 6 bikes, 10 boats, 9 helicopters and 4 planes. Helicopter and plane counts include the playable vehicles carried by native automobile/boat classes. These effective handling classes do not imply a replacement native controller.

- `catalog-inventory.csv`: one row per model, with raw source triangle count, admitted high-detail triangle count, measured full model bounds and atomic frame names.
- `frame-inventory.csv`: all 3,550 DFF frames, including empty wheel, door and seat dummies, their parents and complete hierarchical affine transforms.
- `atomic-inventory.csv`: each real atomic's vertex/triangle counts, full hierarchical bounds and high-detail admission flag. Damaged and low-detail variants remain in source counts and have admission flag 0.

Bounds use the complete original DFF frame hierarchy in model metres. Counts include every eligible high-detail material triangle in the raw DFF. Native model loading can select optional extras; a resident actor captures only its selected, render-enabled atomics. The inventory describes original vehicle DFF geometry. Car wheel templates are instanced by native model loading; the normal-game smoke observes their instantiated geometry and animation as well as the selected extras. The raw catalog has 231975 source triangles; 166472 remain after damaged/low-detail variant filtering. Flatbed 185 is the largest admitted asset, with 3033 source triangles. Dodo's ordinary-game preparation adds its cockpit, independent tires and movable surfaces; its prepared geometry is covered separately by `test_dodo_body_assets`.

Regenerate and validate against the installed game data:

```sh
make build/test_vehicle_body_assets
build/test_vehicle_body_assets build/vehicle-body-atomic-inventory.csv build/vehicle-body-frame-inventory.csv
```

`test_vehicle_body_assets` initializes the actual complete high-detail geometry for every model and exercises a local solid-sheet bullet. Its independently checked ray avoids tires before asserting unrelated wheel geometry stays attached and unchanged. Armored Enforcer, Securicar and Rhino are checked for 200-unit round absorption. The fixture preserves DFF shared-material references, including Flatbed's four referenced faces. Source face/group identity, finite fragments, bounded admission and material lifetime cleanup are checked across the roster.

`smoke_regular.py --case body-vehicles` runs in the ordinary saved-game world. It streams a Landstalker, PCJ600, Speeder, Maverick and RCBaron, alongside the existing prepared Dodo and an extra-bearing Rumpo. It compares render triangles with native LOS, vertical, clear-line and sphere-contact queries before/after real instant rounds, after damaged clump replacement and after repair. It also checks a native bulletproof mission guard, native suspension lines, native impulse ownership, deletion and same-address/new-generation pool reuse, followed by 18 additional actors damaged without capture warmup. During that concurrent workload, the recycled car moves through native class physics; current rendered wheel triangles are compared with native queries across at least 10 animation samples, and translation and wheel rotation must both advance. The helper uses the existing regular-smoke environment switch and does not add a product mode.

Clump replacement deliberately calls `DeleteRwObject/CreateRwObject`, including the extra-bearing Rumpo. Native creation rebinds cached class frame nodes and preserves constructor visibility, including the Maverick's hidden wheel templates. Repeated rendered-wheel queries require the private geometry to survive native wheel LOD selection and account for scaled child frames with a full affine inverse.

The source-host ASan acceptance in `build/rollout-live-fifth/report.json` passes all 91 assertions, exits normally and reports no memory errors. All 7 representative actors pass intact, damaged, replaced and repaired geometry checks. Fourteen samples of the moving car's rendered wheels agree with native LOS, vertical and sphere-contact queries; observed LOS/render position errors range from 0.000002 to0.000278 metres. The test also verifies the native bulletproof guard and same-address pool reuse with a new source generation.

With AddressSanitizer enabled, the concurrent workload reaches 27 source owners: 7 representatives, 18 added actors and 2 ambient owners. Recorded body-update mean is 47.734ms and p95 is 49.128ms; the maximum 299.257ms includes cold publication of the added actors. Render-boundary publication averages 1.148ms. These are instrumented acceptance timings; release timing is recorded by the release run.


## Final integrated acceptance

The immutable release build is `build/vehicle-rollout-final/manifest.json`; its fresh builder preserved all52 original wing-physics checks. The staged ordinary-game run `build/vehicle-rollout-final-all/report.json` passes93/93 assertions, including a saved damaged/missing-part car, native collision-counter isolation, initial geometry before any incident, wheel animation, direct bike garage repair, native recreation and healthy repair. The source-host ASan run `build/rollout-teardown-final-asan/report.json` passes92/92 checks, with no memory errors or native assertions. Material identity survives renderer opacity changes; helicopter visual blur geometry is excluded even after native frame-name collapsing.

In the optimized ordinary-world workload,30 source owners are live (the seven representative classes/extras, an additional saved-condition car,18 additional cars, and ambient vehicles). Aggregate material-body update mean is11.140ms and p95 is11.438ms during the concurrent phase; render-boundary updates average0.377ms. Rendered frame intervals have median33.920ms and p95 of35.996ms. Maximum body/frame intervals of60.344/105.725ms include cold admission and lifecycle work. These are observations for this scene, not a universal frame-rate guarantee.

Numerical, resource and serialization limitations are documented in [implementation.md](implementation.md). The product launcher remains ordinary GTA:VC; test cameras, spawns and shot sequences require explicit smoke environment variables.


Installed normal-game acceptance: `build/vehicle-rollout-installed-all/report.json` and `build/vehicle-rollout-installed-normal/report.json`. `build/vehicle-rollout-final-verification.json` records the installed payload hashes, deep signature check, unchanged original Metal executable/plugin, and all13 pre-existing save/settings/replay/config hashes. The recovery bundle is `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-095557.app`.


## Playtest follow-up (2026-11-10)

Changes, each with a fail-first test:
- **Crash work** (`test_vehicle_crash_tuning`, `test_body_shift_contact`):
  - Underbody and tyre contacts subtract a suspension/tyre allowance (½·m_eff·(7 m/s)²). A native pass's response is shared across the regions it hit.
  - Component crush uses joule capacities scaled by mass.
  - Bend stiffness comes from the intact section, accumulates as a vector, and is capped at 0.05 rad per episode.
  - Wheels and panels get up to three mounts. Car components are placed from the axles.
  - The ProcessShift separation pass now sweeps its own move. A stale body falls back to stock collision shapes.
- **Rotors** (`test_rotor_alpha`, `test_heli_material`, `test_blade_strike_assets`):
  - The stock blade/blur crossfade is restored.
  - Native blade contacts become material work on the struck blade, so blades break off as debris.
  - Owned helicopters skip the legacy binary rotor failure. Lift scales with surviving rotor material.
- **Health and fire** (`test_carfire_material`, `test_source_vehicle_contacts`):
  - Occupant crash shock applies to material-owned vehicles.
  - Native health is capped by material engine/fuel/structure damage.
  - Aircraft crashes ignite and explode unless quenched. Cars still burn out without a blast (#44).
- **Learjet** (`test_learjet_body`): interior, lining, trim and steps are render-only (1,935 faces admitted). The main wing, engine pods and inboard fuel are classified from measured geometry.
- **Glass stopgap** (`test_vehicle_glass_stopgap`):
  - The old cabin-glass rules decide what is glass, so decals and lamps are sheet.
  - A penetrating round removes a tempered side or rear pane whole. Windscreens are laminated and keep holes.
  - There are no shards or sound yet.
- **Publication** (`test_bodypublication`, `test_bodypublication_assets`):
  - Capacity failures retire their incidents. Resource failures retry for at most 30 frames, and pose-only commits don't reset that.
  - Idle bodies tick in place. Pose-only frames patch moved collision vertices. Ownership lookups are indexed.
- **Sector grid:** the grid is x [−2400, 1600), y [−2000, 2000), and `CWorld::GetSector` clamps only the upper index.
  - Skyline airtrains fly south of y = −2000. Re-registering them, or adding their debris, wrote before `CWorld::ms_aSectors`. ASan reproduced this as a global-buffer-overflow in `CPhysical::Add`.
  - Bodies outside the grid now neither tick nor publish, and debris outside it is discarded.

Evidence:
- `make test test-game-assets` passes (227 suites).
- ASan Dodo case: 19/19 twice, with no sanitizer errors (`build/playtest-fixes-2-smoke-asan-dodo-*`). Before the grid guard it failed on every run.
- Release smoke: normal 10/10, body-vehicles 93/93, body-bullets 31/31, body-ground 13/13, body-tail 15/15, Dodo 19/19 ×3.
- Installed app (`build/playtest-fixes-2-installed-*`): normal, body-vehicles and Dodo pass. The signature is intact, and all 13 profile files are unchanged.
- The previous app is `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261110-080405.app`.

Known limits:
- The `impact` smoke's `visible_body_deformation` fails on the previously installed build too. It compares vertices only when the hull's vertex count is unchanged, and the impact now detaches parts.
- Skyline airtrains still tear and republish under scripted path motion while inside the grid. This was pre-existing; the suspected cause is load sampling of prescribed motion.
- A single bullet hit still costs about 2.5–10 ms, depending on hole count (kernel cost).


## Curb launch / fall-through root cause (2026-11-10)

**Cause.** Material contacts were written into native colpoint slots with plain assignment. reVC's `CColPoint::operator=` copies everything except `depth`, as the original game did (`// no depth?`).
- In a shift pass those slots are an uninitialised stack array, and `ProcessShiftSectorList` moves the car by `normal × depth`. Stack leftovers therefore became the shift.
- Reproduced in-game on an ASan build: z went 9.93 → 500.36 m in one frame (stored depth −1308, which is the car's own y coordinate), 9.94 → −381.7 m, NaN positions, and one jump to (−2.8e35, −3.1e35, 2.0e37).
- Every unfixed run showed thousands of mismatched depths. Whether a run launched depended on the stack.

**Fix.** `bodycontact::Store` copies depth explicitly at both host copy sites (`test_body_contact_depth`: 8 failures before the fix, passing after).

**Grid clamp (defense in depth).** `CWorld::GetSectorIndexX/Y` now clamp both ends of the grid, so an entity outside the grid registers in the edge sector instead of writing outside `ms_aSectors`. The user's `RemoveAndAdd` crash faults inside sector lists, which is consistent with that earlier out-of-grid corruption. Its exact trigger (a launched car or a skyline airtrain) was not isolated.

**`body-curb` smoke.**
- Finds two real kerbs: a narrow pavement against a facade, and the longest open kerb.
- Drives Deluxo 211 and Landstalker 130 at 10, 20 and 30 m/s, head-on and at 35°, mounting, dropping off, and over repeated bumps.
- Asserts per frame: vz < 8 m/s, at most 0.3 m below ground, stays in the grid, every stored depth equals the computed depth. It also includes an out-of-grid teleport probe.

**Verification (`build/playtest-fixes-3*`).**
- `make test test-game-assets`: 246 suites.
- Release smoke: normal 10/10, body-vehicles 93/93, body-bullets 31/31, body-ground 13/13, body-tail 15/15, Dodo 19/19, body-curb 22/22 (50 runs, 0 depth mismatches, worst vz 7.48 m/s, worst rise 1.51 m).
- ASan smoke: body-curb 22/22 (worst vz 7.38 m/s, rise 0.79 m), Dodo 19/19, body-vehicles 93/93, with no sanitizer errors.

**Known limit.** The worst bounce is a real contact: a 30 m/s, 35° approach where the front bumper corner catches the kerb's bevelled edge (3.6 mm depth, closing 17.6 m/s). The car hops about 1.5 m and lands. Native response treats the bumper as rigid; bumper and tyre compliance on kerbs belongs to the driving-model rework.

**Installed (`build/playtest-fixes-3-inst-*`).**
- Installed binaries are byte-identical to the staged candidate.
- Smoke: normal 10/10, body-vehicles 93/93, Dodo 19/19, body-curb 22/22 (0 depth mismatches, worst vz 7.47 m/s).
- Deep signature check passes. All 108 files under `macVC`/`macVC-source` are unchanged.
- The previous app is `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261110-094135.app`.


## Kerb strikes, bumps and contact resolution (2026-11-10, second round)

Reproduced in-game, each alongside an unowned stock twin given the same start and input:
- **Dead stops at kerbs.** A low car's lip (0.15 m above the road) met the 0.17 m kerb face before the wheel lines reached it. Native stuck handling then damped the speed by 0.707 per shift pass with no impulse and no damage, so a material car kept 0–10% of its speed while the stock car kept 62–71%.
- **"Refuses to accelerate."** Held against the kerb face, the material car moved 0.00 m in place; the stock car moved 22–25 m.

Fixes, each with a fail-first test:
- **Yield model** (`test_body_yield`):
  - A struck part carries at most its yield force over the native step. Sheet edges and low trim yield at the plate-collapse force (2.5 kN on the Deluxo); structure yields at the body crush force (231 kN on the Deluxo, 262 kN on the Landstalker).
  - Above that cap the material deforms plastically, in the body frame and never toward the underside.
  - Yielding contacts are kept out of native stuck handling.
- **Inside-box exit** (`test_body_inside_box`): material probes inside a solid box now leave by the nearest face.
  - Native `ProcessSphereBox` takes the normal from the box centre and the depth as the largest of the side distances.
  - So a bumper corner 5 cm inside a palm trunk (a 1.4 m × 28 m box) read as 0.62 m deep, pointing straight down, and the car dropped 0.44 m in one frame.
- **Points that passed through a face** (`test_body_sunk_support`):
  - Native sphere-triangle tests are two-sided. A material point just behind a static face reads the face's back, gets no response, and keeps moving in.
  - Crossings up to 2 cm deep had also been counted as resting. Together these ratcheted a car on its roof through the street: in release and ASan, the Landstalker ended 52 m below the road and the Deluxo 108 m.
  - A point behind a face the body is in front of now exits back through it. The reference is the body's world top for floors and its centre otherwise; the local top is wrong when the car is on its roof.
  - Penetrating contacts count as resting only within a 5 mm slop.

Smoke additions, all behind the test-only switches:
- `VC_REGULAR_CURB_RUNS` (replay filter)
- `VC_REGULAR_TRACE_MODEL`
- an inverted-rest phase (`inverted_rest_holds`)
- `no_wedged_rest`: still for 30 frames while tilted 20–70°
- the `body-bump` case: kerb, wall, pole and parked or moving cars at up to 8 m/s, each with an A/B drivability probe

Verification (`build/fix-*-4`, `build/playtest-fixes-3-v4*`):
- `make test test-game-assets` passes.
- Release smoke: normal 10/10, body-vehicles 93/93, body-bullets 31/31, body-ground 13/13, body-tail 15/15, Dodo 19/19, body-curb 25/25, body-bump 21/21.
- ASan smoke: Dodo 19/19, body-bump 21/21, body-curb 24/25, with no sanitizer errors.
- Upside down on flat street, both cars rest within 0.1 cm (Landstalker) and 2.2 cm (Deluxo) of the road, with no drift.
- Kerb strikes keep 48–82% of their speed, the same as stock, and the car drives away in place (8.2 m).

Known limits:
- **Wedged rest under ASan** (`body-curb` run 16): the SUV at 30 m/s and 35° into the site 0 facade freezes at 41°.
  - Native resolution undoes the whole frame on any rigid contact, and then undoes a shift if any rigid contact remains afterwards.
  - With many material points, one always remains. That needs a material positional resolver, not more classification rules.
- **Beached car, seen once under ASan** after the `pole8` strike: some part hung about 20 cm low, the wheels were unloaded, and the probe moved 0.00 m.
  - It didn't recur in the next run, which logged the lowest part as `wing_lf_dummy` at the normal height.
  - Offline, the kernel's front hit (20–60 kJ) lowers no point at all. The cause is unidentified.
- **Wall hits book about 29% more geometric deformation than the kinetic energy available.** Damage itself isn't overbooked.
