# Dodo material owner follow-up

The user's ordinary-game reproduction invalidates the previous broad damage
claims. A ground wing strike changed rigid motion without bending the outboard
wing; shooting the tail triggered scalar part removal and unrelated fire/wheel
loss. The requested result remains the normal game installed in `/Applications`.

## Diagnosis and decision

The retained user logs and actual prepared Dodo asset reproduce the old defects:
the registered tip strike moves the tip by zero, and legacy right-wing removal
reduces its independently attached wheel from 92 triangles to one. The generic
component boxes also put cabin glazing inside an engine and miss the visible
outer wing. Several independent writers own holes, health, sections and meshes.

Use one canonical material body for the controlled ordinary airport Dodo. It
owns surviving material polygons, geometric connectivity, deformation, internal
injury and the resulting render/COL revisions. Flight and fire read its state;
legacy glass/airframe writers yield. Native physics exclusively integrates rigid
motion and collisions. The expensive implicit solver stays explicitly opt-in;
its original tests and numerical guards are preserved. Other vehicle adapters
remain legacy until they can adopt the same contracts; this is not a claim of
worldwide unification.

## Acceptance work

- [x] Record the original logs and a red actual-asset reproduction.
- [x] Test surviving polygon connectivity, including positive-area/interior
  joins, partial versus complete cuts, allocation rollback and one-time transfer.
- [x] Use measured Dodo engine/tank/control geometry and actual surviving skin
  work; rays through removed controls cannot absorb phantom damage.
- [x] Route accepted native contacts and weapon receipts to the single body.
- [x] Stage render/COL/debris publication at the world boundary; preserve UVs,
  glazing identity, control poses, mass and native origin-velocity semantics.
- [x] Preserve compatible body state across clump replacement and repair.
- [x] Re-run ordinary-game intact flight, actual ground wing strike and native
  weapon partial/complete wing and tail cuts in the final candidate.
- [x] Prove native contacts, LOS and vertical queries agree with render geometry
  before, after and following repair of the weapon-driven cut.
- [x] Review native lifecycle/transaction handling, run preserved project/physics
  checks, measure optimized ordinary-game performance, and install atomically.

## Explicit numerical scope

This is an analytic bounded wing bending/material-cut model, not a continuum
finite-element simulation. Contact work uses the accepted native impulse and
contact-point closing/separating velocity; the existing episode peak rule avoids
charging a manifold repeatedly. The beam approximation uses 70 GPa stiffness,
100 MPa effective yield, 0.8 mm skin, 1.5 m chord and 4.5 m span, with 0.65 rad
plastic rotation, 1.2 m local tear radius and 100 kJ incident caps. These are
explicit effective parameters, not aircraft certification measurements.

Native moving-body contacts use surviving material vertex sweeps plus 2 cm
resting probes. They do not implement arbitrary edge-edge continuous collision
detection or general deep initial penetration recovery. Native scalar turn mass
approximates inertia. Detached surface density is 2.7 kg/m²; islands smaller than
0.0025 m² become bounded debris particles, larger connected pieces become native
temporary bodies for 20 seconds. Capacity exhaustion retains the committed
revision and pending incidents rather than silently dropping material.

## Diagnosis and intermediate evidence

`build/dodo-damage-followup` retains the user evidence, legacy red reproduction,
actual-asset and model results, source builds and ordinary-game reports. Initial
ASan ordinary Dodo flight passes all 18 assertions. The first native weapon run
passes window isolation, partial/complete wing cut and tail isolation; its report
correctly fails because the terrain observer only tracked an occupied aircraft.
That observer is being corrected to follow the parked test aircraft too. Timing
and final installation are pending; no new candidate has yet been installed.


## Installed result

Installed in `/Applications/Grand Theft Auto Vice City.app`. Reproducible pinned
build record: `build/dodo-body-release/manifest.json`; raw host SHA256
`3306404721a2ddb038cd84ae69a26181e5e3ee4a27c43d0ee397435ee1be821a`.
Package/recovery journal: `build/dodo-body-install/package-manifest.json`.
Backup: `/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-065356.app`.
Final receipts: `build/dodo-damage-followup/final-verification.json`.

- `make -j8 test` and `make -j8 test-game-assets` both exit0. Original strike
  checks52/52 pass in the fresh builder, unchanged. Connectivity/body/actual-asset
  tests pass, as do native ASan acceptance and bounds-index ASan/UBSan checks.
- Ordinary optimized staged Dodo passes19 assertions: visible tire support,
  taxi/brakes, animated controls/seat, same-address invalidation and actual
  takeoff. Ground strike passes13 assertions; speed36.47m/s, peak tip movement
  1.63655m and retained1.46250m, with unchanged independent wheel geometry.
- Native round delivery: the partial wing cut retains lift0.9833; closing its
  remaining material releases a real component and reduces right lift to0.5900.
  All three tires retain92 triangles; the other wing, engine and fuel remain
  intact. Cutting the tail reduces pitch authority to0.0581 and yaw to0.0699,
  while both wings, all wheels, engine and fuel remain intact. These values are
  outcomes of the measured surviving geometry and local injury, not cut triggers.
- All nine native query stages pass: intact tip/root, partial tip, removed tip,
  retained root, replaced-clump removed tip/root, and repaired tip/root. Each
  compares world LOS, vertical, clear-line, native sphere contacts and actual
  visible RW triangles. No material state changes during observation. Typical
  render/COL point difference0.0001–0.0002m; the explicit tolerance is0.02m for
  native quantization/transform differences.
- Actual DeleteRwObject/CreateRwObject preserves the damaged material and mass.
  Explicit repair restores full geometry and2400kg. Debris retains its own native
  lifetime; compatible clump replacement does not respawn cut material.
- A CPU sample exposed repeated full-triangle scans. Conservative bounds lookup
  now selects triangles before the same native exact tests, preserves original
  tie order/bounds gates and falls back on allocation failure. Instrumented
  shooting p95 intervals fell from381.5ms to40.6ms in the diagnostic comparison.
- Installed optimized ground body updates mean0.316ms/max0.610ms; shooting body
  updates mean0.771ms/max13.644ms. Installed shooting rendered intervals median
  34.041ms/p95 35.469ms/max47.282ms. This includes renderer/vsync/OS scheduling;
  it is a bounded airport sequence, not a worst-case16ms whole-world guarantee.
- Final read-only native review found no remaining Important blocker after fixing
  allocation rollback, native origin velocity, buoyancy, streaming pins, query
  tie order, signed accepted work and retained repair. Original Metal host/mod
  hashes and all11 pre-install save/settings hashes remain identical. Default
  profile launch and post-play deep/strict bundle signature pass.

No save serialization of this material graph is implemented. Full-airframe
continuum crumpling, arbitrary edge-edge CCD, detailed fasteners/attachments and
migration of other vehicles remain unfinished. The recorded legacy red control
is retained under `tools/host/diagnostics/dodo_legacy_damage.cpp`; it deliberately
exercises the bypassed fallback and is not an ordinary passing test target.
