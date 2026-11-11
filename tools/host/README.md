# Normal-game aircraft physics build

The default candidate runs the full Vice City game and existing mod with native rigid motion and a prepared playable airport Dodo. Its canonical material body owns holes, geometric connectivity, bounded wing bending, component injury and matching render/COL publication. Flight and fire consume that body state; legacy glass/section writers yield for this actor. The Dodo has three physical rolling tires, animated control surfaces, a seated pilot, textured cockpit and moving yoke/hand anchors. Other aircraft and cars still use their existing adapters; this controlled migration does not yet unify the entire world.

The dense implicit right-wing coordinator is **opt-in** (`VC_EXPERIMENTAL_WING=1`). It still runs in the ordinary game against actual static COL; its published wing faces are shared by native contacts, LOS and vertical queries. It is held out of normal launch because measured strike frames exceed an interactive budget. There is no separate scene or demo camera.

The original Metal executable and mod remain available through **Previous build** when holding Option at launch. The new GL source host lacks the old Metal RT hooks; this is a distinct source-host build, not a claim of renderer parity.

## Build and verify

On macOS ARM64, use the pinned dependencies in `source-lock.json`. The normal builder requires the static GLFW/audio dependency evidence produced by:

```sh
python3 tools/host/build_host.py --work-dir build/host-source/reviewed --control
```

It also requires the pinned, locally built research SDK under `build/world-physics-evaluation/`: IPC, double-precision conservative Bullet and their recorded dependencies. See `docs/research/2026-11-05-world-physics-evaluation.md` and `tools/physics/check_continuous.py`. No helper silently installs or upgrades global dependencies.

Build into a fresh directory:

```sh
python3 tools/host/build_regular.py --work-dir build/regular-release-installed
```

The recipe applies the complete engine/librw patches, exports the user's actual aircraft asset, builds and checks the physics adapter, builds the full game, and creates a UUID-specific mod. `manifest.json` records source pins, patches/helpers/export dependencies, SDK libraries/header fingerprints, toolchain, host, plugin and mesh identities. `--asan` instruments the game and mod; the separately recorded adapter/SDK libraries remain optimized without ASAN. The normal-game build is reproducible from these pinned inputs; byte-identical output across different build directories is not claimed.

Ordinary-world automation loads save slot1 in an isolated profile, uses the real airport/roster Dodo/hangar, renders the normal game and exits. It does not replace world simulation or the renderer:

```sh
python3 tools/host/smoke_regular.py --app /path/to/candidate.app \
  --profile /path/to/copied/profile --data /path/to/GameData \
  --work-dir build/new-evidence --case body-ground
```

`--case dodo` checks taxi/brakes, controls, pilot placement and actual takeoff.
`--case body-ground` uses the original departure heading to reproduce a real
full-speed wing clip against the airport building and checks retained wing bend.
`--case body-bullets` fires native instant rounds at the minigun's damage budget
through the ordinary weapon/world-query/damage path. It checks glazing/engine
isolation, partial versus complete wing cuts, independent wheels, tail isolation,
damaged clump replacement, repair, and native contacts/LOS/vertical agreement with
the visible surviving mesh. This exercises native round delivery, not the
minigun's entire trigger/animation/audio behavior. It does not enable the implicit
solver or create a separate scene.

Use fresh evidence directories. Default-path cases are `normal` (ordinary save/render), `dodo` (parked support, taxi/brakes, surfaces/yoke/seat, cockpit, force-driven takeoff), `impact` (a native building contact visibly deforms the nose while pilot survives), and `miss` (no coordinator admission). Every aircraft case now checks actual native runway coverage and ground penetration; previous green wing receipts did not establish terrain safety.

Use `--experimental-wing --case graze` to measure permanent wing deformation, contact/LOS/vertical agreement on damaged geometry, conservative body handoff and repair. `--experimental-wing --case pristine` checks external-force handoff from an actual sector-list callback. `hit`/`external` retain their stronger whole-fragment separation assertions; the current conservative terrain/body guard can retire this particular hangar approach before separation, so those old cases are not counted as current passing gameplay acceptance. Original solver/strike tests remain unchanged.

Reports separate solver substep time, total coordinator frame time, publication time and rendered frame intervals. The latter include rendering/vsync/OS scheduling and are not CPU-only solver measurements.

## Stage, install and recover

```sh
python3 tools/host/install_regular.py --build build/regular-release-installed \
  --work-dir build/installed-regular
```

Validate the staged app with the ordinary-game automation before installing. Then:

```sh
python3 tools/host/install_regular.py --build build/regular-release-installed \
  --work-dir build/installed-regular --install
```

Installation checks built host/plugin/mesh identities and signed payloads, records durable recovery information before either rename, preserves the old Metal executable/mod bytes and keeps a complete timestamped app backup. The obsolete separate scene is removed from the candidate. The source host uses `~/Library/Application Support/macVC-source`; only absent files are copied from `macVC`, preserving original saves/settings and existing source-profile progress. Generated texture archives, GPU capabilities and audio cache live under that profile's `host-cache`, outside the signed app. Ordinary-game smoke checks verify the entire bundle signature again after play, including cold and warm cache launches.

`build/installed-regular/package-manifest.json` records the exact installed and backup paths/fingerprints. Quit the game before recovery. The existing `app_package.restore` helper can restore this recorded backup while archiving the experimental app; it refuses changed fingerprints. Neither source profile progress nor original profile files are overwritten during recovery.

## Explicit limits

- The opt-in solver covers one roster Dodo/right outer wing versus a bounded exact static COL neighborhood. This is not a whole-world, mission-aircraft or bullet/material solver migration.
- Native pose integration is skipped while owned and for the remainder of a pass already published by the coordinator. External native impulses return ownership before application. Rejected unpublished intervals are discarded and handed to native control, never presented as accepted material steps.
- The chassis is a motion proxy. Conservative bounds return ownership before unsupported body/left-wing contacts; curved primitives, neighborhood capacities and other unsupported boundaries also return to native behavior. Native behavior after handoff is an approximation, not continued full material simulation. Debris lifetime ends at this boundary, with safe deferred deletion.
- Repair, pristine restoration and COL registration changes are deferred to a safe world-update boundary. Pool generation/model/clump checks revoke stale writers. Physics material state is local to the active lifetime; permanent retired damage requires repair. Save serialization of the material graph is not implemented.
- Authored axial bars/weak mounts and lumped masses, not calibrated continuum elements. Native gravity is20m/s²; the old diagnostic fixture retains9.81. Flight supplies a frame-held net wrench distributed across attached mass, without a resolved pressure field or detached-fragment aerodynamics. Connected authored flap overlap excludes intra-fragment self contact; separated fragments still collide. Contact is frictionless.
- The real 2400kg aircraft uses its recorded chassis bounds and48 triangulated root members, replacing the diagnostic fixture's12 nearly horizontal connectors. Section areas scale with supported mass and gravity; modulus, yield, hardening and failure strain retain their original values. This is a newly authored virtual load path, with higher aggregate capacity, not calibrated Dodo fastener strength or a mechanically equivalent remesh. The original diagnostic fixture and its52 assertions are unchanged. A one-second turning-flight regression with the actual chassis bounds requires zero plastic strain and zero broken mounts.
- Original energy/clearance guards and failing SDK controls remain. Source adapter velocity tolerance is1e-7m/s, numerical barrier stiffness1e9, contact gap2mm and maximum requested interval5ms. Admission filters3–120m/s; this does not certify that whole range. Synthetic40/80m/s cases and the actual approximately40m/s approach are the specific evidence.
- Physical-energy monotonicity is not guaranteed for arbitrary nonconvex barrier motion. A demonstrated chassis-edge case fails the original guard; deeper subdivision costs seconds and is not enabled. No tolerance relaxation hides that failure.
- Impact solves can hitch for hundreds of milliseconds on this machine. The implicit coordinator is disabled on normal launch, retained behind the explicit environment opt-in, and is not a performance-ready full-world solver. No solver tolerances, energy guards or original physics assertions were relaxed to hide the cost.
- The default canonical Dodo publishes surviving polygons to render and native COL together. Its moving-body contact adapter sweeps material vertices and uses 2cm resting probes, accelerated by conservative triangle bounds before the same native exact tests. It does not implement arbitrary edge-edge CCD or general deep initial penetration recovery. Detached connected material becomes native temporary bodies; islands below0.0025m² become bounded dust particles. Bending uses an explicitly bounded analytical wing approximation, not full-body continuum crumpling. Native scalar inertia and2.7kg/m² detached skin density are approximations. Detailed parameters and acceptance evidence are in `docs/superpowers/plans/2026-11-06-dodo-cohesive-body.md`.
- Managed Dodo/Learjet contacts already feed localized material work and occupant shock. Their old additional scalar-health/explosion calculation is now a fallback only for aircraft without a material owner; the same native impulse is not charged twice.

Earlier `WingLab`/`WingStrike` files and their evidence remain internal diagnostics. Their hidden-scene measurements are not normal-game performance or gameplay validation.


## Current installed Dodo revision (2026-11-06)

Build: `build/dodo-tail-release-final/manifest.json`. Installed package/recovery
journal: `build/dodo-tail-install/package-manifest.json`. Acceptance/performance:
`build/dodo-tail-followup/final-verification.json`. This supersedes the earlier
`dodo-body-release` installation.

Native shift/correction now requests current overlap instead of replaying swept
travel. The crash→exit→138-round tail sequence retains the actual aircraft through
settling. Remaining wing/tail structural sections supply finite bending stiffness,
carried weight, yielding and local crack work. The normal-world shot sequence
produces approximately0.91m own-weight sag, with contact/LOS/vertical/render
agreement at the moved surface and empty old location. Ordinary flight, wheels,
complete cuts, component isolation, repair and clump lifecycle checks pass.

Original52 strike assertions and the project/asset suites pass. The original
Metal host/mod and all11 pre-install save/settings files retain their hashes.
Installed shooting body updates mean1.009ms/max12.696ms; rendered intervals median
33.995ms/p95 35.268ms/max46.182ms. These are bounded airport observations, not a
whole-world worst-case budget.

The model has12 effective cells with separate weak sections for two bending axes,
explicit material/load/crack/rotation caps, and remains a reduced approximation.
Full continuum crumpling, general flat-sheet/large-rotation failure, torsion/axial
failure, local aerodynamic pressure, arbitrary edge-edge CCD and material save
serialization remain unfinished. No other vehicles have been migrated. See
`docs/superpowers/plans/2026-11-06-dodo-tail-loads.md` for the diagnosis, primary
references, numerical limits and complete evidence.
