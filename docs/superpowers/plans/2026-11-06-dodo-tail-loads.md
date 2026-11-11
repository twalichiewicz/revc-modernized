# Dodo tail lifetime and material loads implementation plan

> Continue the user's authorized normal-game development/install workflow inline.
> Preserve the dirty checkout and original physics tests. No migration to other
> vehicles until this Dodo follow-up is testable in the installed normal game.

**Goal:** Prevent whole-aircraft disappearance after crash damage and subsequent
tail shooting; make weakened remaining wing/tail material flex and yield under
the load it carries before complete geometrical disconnection.

**Architecture:** Native physics exclusively owns rigid motion. The existing
canonical material body owns surviving polygons, load-bearing section properties,
bounded bending/yielding, physical crack growth, render/COL publication and debris
transfer. Reuse the source integration and existing material topology; the dense
implicit solver remains opt-in and its tests remain unchanged.

**Spec:** The user's report on2026-11-06: disappearance occurred after flying or
crashing, followed by tail shooting; remaining connections must have finite
strength and visibly flex/sag instead of supporting unlimited loads until cut.

## Decisions

The current two fixed-stiffness wing oscillators are insufficient: topology is
load-independent and the tail has no bending degrees of freedom. Measure remaining
thin-wall sections from structural surviving polygons, using the same skin mass
density as native debris. Use section stiffness/yield and actual carried load;
advance finite local cracks through the canonical material when overloaded.
Yielding alone produces plastic bending, rather than a role-wide removal.

Retuning shot/health thresholds would preserve the demonstrated defect. Expanding
the global nonlinear solver would not address the bounded normal-game integration
and performance requirements. The section model is a deliberate bounded beam
approximation; no continuum or detailed fastener-strength claim is made.

## Tasks and acceptance

- [x] Preserve the user's game/mod logs before further launches.
- [x] Confirm the current stiffness/load gap from production model code.
- [x] Reproduce crash→exit→shoot-tail with native world/weapon routines, tracking
  pool generation, position/velocity, world registration and visible geometry
  across each frame. Distinguish deletion, invisibility and physical escape.
- [x] Diagnose and regress the reproduced position-recovery fault; restore authoritative
  contact semantics without duplicating native rigid integration or hiding the
  problem through a respawn/teleport safeguard.
- [x] Add red actual-asset tests for stiffness loss, sustained own-weight sag,
  tail flex, load-dependent failure of a positive remaining bridge, and safe
  pristine behavior. Geometry and material strength determine the response.
- [x] Implement bounded section/load response in the canonical model. Native
  adapter supplies local apparent gravity from current attitude and origin
  acceleration; freefall must not create an artificial self-weight load.
- [x] Re-run native geometry query, lifecycle, flight, crash and weapon checks;
  preserve complete-cut behavior and independent wheels. Measure optimized cost.
- [x] Run project/asset/original physics checks, review, build reproducibly,
  install transactionally with a fresh profile snapshot and verify installed app.

## Review focus

- Material deformation is not an independent rigid velocity or a source of
  downward ground-recovery impulses. Sweeps, overlaps and shift witnesses must
  retain their distinct native meanings.
- Glazing, cockpit trim and wheels do not contribute fictitious section strength;
  structural struts and real surviving ligaments do.
- The aft boom shares a hull atomic with the cabin: deformation is spatially
  limited and must not move the cockpit or unrelated gear.
- Yielding and finite-work fracture are distinct. Remaining support must respond
  to load magnitude, direction, stiffness and lever arm, not bullet count.
- Rollback/capacity/lifecycle behavior retains a coherent committed body. No
  render-only deformation or stale collision replacement is acceptable.

## Evidence

Retained user logs: `build/dodo-tail-followup/user-evidence`. The last user actor
ref1286 received63 bullet and329 contact receipts. Existing logs lack the motion
receipts needed to identify the disappearance cause. Native source confirms that
unoccupied random vehicles belowZ−100 can be deleted independently of the roster's
ordinary traffic-retention flag; this is a diagnostic hypothesis, not an observed
cause yet.


## Reproduced fault and integration decision

The first ground-crash run retained the aircraft, but the airborne variant
reproduced an artificial position correction: it jumped from approximately
(−1569.956,−1276.513,15.328) to(−1544.566,−1272.130,31.207) while recorded speed
remained below0.9m/s. Its actor and9273 visible triangles remained live. This
proves a displacement fault, not deletion; the exact final disappearance in the
user's session remains unobserved. The old logs cannot distinguish that endpoint.

`ProcessEntityCollision` serves both collision detection and positional recovery.
The adapter had returned historical swept travel as penetration depth during
native shift/correction and its validation pass. Repeated correction could amplify
that stale depth. A scoped overlap query now covers the complete native
`CPhysical::ProcessShift`, including validation/early returns. Recovery queries
use current overlap only; correction callbacks do not queue new material work.
Thin walls remain two-sided. Only material emerging through stationary support
below an already above-floor carrier bypasses the reversed floor sweep.

The same crash→exit→138 native tail rounds→settling run passes with stable final
position. Native actor lifetime receipts now retain position and residency when
render/actor ownership ends, so a future disappearance can be distinguished from
a clump replacement or cleanup. No respawn, teleport, tolerance relaxation or
custom rigid integrator hides the fault.

The terrain observer was also measuring implicit-island admission around the
floor point: nearby dynamic debris correctly rejects that experimental admission.
Its coverage proof now checks the specific native floor entity, preserving the
full implicit admission's dynamic-object rejection. This does not alter native
contacts, penetration tolerances or the original52 physics tests.

## Material response and numerical limits

- Remaining structural polygon intervals supply section area and bending second
  moments. Collinear unions are transitive and order-independent; crack targeting
  retains actual source/group provenance. Each bending axis retains its own
  critical coordinate, carried mass and moment arm, so an orthogonal new wound
  cannot erase an existing weakness.
- Effective parameters:70GPa stiffness,100MPa yield,0.8mm skin thickness,
  2.7kg/m² supported surface mass and30kJ/m² through-thickness fracture energy.
  These are explicit approximation/calibration parameters, not measured Dodo
  material certification data. Glazing, cabin trim, doors and tires do not add
  fictitious section strength.
- Twelve cells per wing/tail retain separate critical section coordinates for two
  transverse axes. The critically damped reduced response has frequency bounded
  to0.5–100rad/s, local angles±0.65rad and displacement at most0.75span. General
  flat single-sheet fixtures can reach this angle cap before first yield; this
  is not an arbitrary-geometry continuum failure model. Actual Dodo lower-sheet
  overload cases were tested successfully.
- Yielded bending supplies finite local crack work; at most two≤12cm local crack
  advances occur per Tick. The canonical mesh determines disconnection and the
  existing transaction transfers actual connected pieces. Work/caps are staged
  with the candidate; finite update/cell limits retain time/mesh sensitivity.
- Native load input is world gravity minus measured origin acceleration, rotated
  into current body axes. Native gravity is20m/s². Pure-model input accepts at
  most2000m/s²; native sampling caps1000m/s². An interval that already spent native
  impact work holds the last clean load while refreshing velocity history,
  avoiding replay of that same shock as a new section load.
- Local aerodynamic pressure, rotational/centrifugal distributed loads, axial
  tension/torsion, detailed joints/fasteners, general large-strain folding and a
  closed coupled rigid/elastic energy solve are not implemented. Native contacts
  retain vertex sweeps/2cm endpoint probes, not arbitrary edge-edge CCD or deep
  initial penetration recovery. Material-graph save serialization is unchanged.
  These limits gate any migration beyond the tested Dodo; no other vehicle is
  migrated in this pass.

The section-property approach follows the relationship between section geometry
and beam response described in the primary [Abaqus section documentation](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEELMRefMap/simaelm-c-usingbeamgensect.htm).
The distinction between yield/initiation and finite-energy damage evolution is
supported by the primary [ductile damage documentation](https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMATRefMap/simamat-c-damageevolductile.htm).
This implementation is a reduced adaptation, not Abaqus or a shell FEM.

## Verification before installation

Actual prepared-asset regressions: intact/moderate wing sag2.56/4.02mm; tail
1.27/3.79mm. A lower-sheet positive ligament folds1.548m under ordinary weight
while the same weightless mesh stays undeformed. Sufficient load releases positive
wing/tail bridges without further bullets; equal100J contact bends the weakened
wing more than intact. UV/source IDs, one-time transfer, tires, fuel and engine
isolation remain covered.

Independent review found three additional real defects and verified their fixes:
interval union formerly inflated stiffness by17% with a different input order;
merged group provenance could make every crack attempt a no-op; a new orthogonal
wound could artificially reduce vertical sag by80%. New production-helper/public
regressions fail on the defective cases and pass after the fixes. Refreshed model,
section and asset ASan/UBSan suites pass. All project/asset checks and the fresh
builder's original52 physics checks pass.

The normal-world native shot sequence leaves a lower sheet, then stops firing:
wing sag reaches approximately0.91m. Contacts, LOS, vertical and visible RW
triangles agree at the new position; the old tip location is empty. Explicit
repair and real render-clump replacement retain their lifecycle assertions.
Optimized staged flight body updates mean0.324ms/max0.422ms; shot/load sequence
mean1.017ms/max12.883ms; rendered intervals median33.988ms/p95 35.228ms/max46.528ms.
These include renderer/vsync/scheduling and are bounded scene observations.

Final installed receipts and profile/signature checks follow below.


## Installed outcome

Installed in `/Applications/Grand Theft Auto Vice City.app`. Raw pinned host
SHA256`4bf4247f3f2c61f244713e98e9b37ce6dacdc15d32d345b4b27aab859192e8f0`.
Build manifest: `build/dodo-tail-release-final/manifest.json`; durable package
journal: `build/dodo-tail-install/package-manifest.json`. Backup:
`/Applications/Grand Theft Auto Vice City.before-regular-physics-20261106-075428.app`.
Final verification: `build/dodo-tail-followup/final-verification.json`.

All eight retained final acceptance reports pass: native ASan loaded geometry and
crash/tail sequence; optimized staged flight, loaded geometry and crash/tail; and
installed default-profile launch, loaded geometry and crash/tail. The installed
crash sequence includes138 native rounds after an actual airborne building clip,
exit and settling; the aircraft remains live and visible at a stable position.
All three tires retain92 triangles. No actor respawn/teleport masks the result.

Installed loaded-body updates mean1.009ms/max12.696ms; rendered intervals median
33.995ms/p95 35.268ms/max46.182ms. Installed combined crash/tail updates mean0.980ms/
max22.535ms; rendered intervals median33.971ms/p95 34.826ms/max46.853ms. This is not
an interactive worst-case guarantee for arbitrary whole-world damage.

Original52 strike assertions, full project/asset suites, model/section/asset
sanitizers and final independent reviews pass. The generated pinned source
contains the overlap scope in Physical.cpp; the reproducible patch includes it.
All11 current save/settings files retain their pre-install hashes, original Metal
host/mod hashes remain identical, and post-play deep/strict signature validation
passes. No other vehicles have been migrated.
