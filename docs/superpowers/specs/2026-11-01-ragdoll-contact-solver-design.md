# Articulated NPC contact solver

The user authorizes designing and implementing a comprehensive correction of the
shared police/civilian ragdoll controller. The review's executable reproductions
are in `build/ragdoll-review`; the starting source is preserved under
`build/ragdoll-fix/baseline`. This work builds on the current uncommitted features.

## Intent and acceptance

Keep localized hits, living balance/bracing, momentum, native health and native
get-up behavior. Correct shortened limbs on landing, persistent overlap near cars
and walls, world collision gaps along limbs, and overlapping articulated people.
Ordinary unsupported falls must still fall, survivors must recover, and fatal
bodies must remain dead. A frozen pose is not an acceptable substitute for contact.

Acceptance uses the actual captured native rig and real production solver. Check
every simulation frame, not only the final settled pose: measured bone lengths
within 1 cm where geometric constraints are feasible; complete limb volumes outside
terrain within 2 mm; no certified trajectory through a thin wall; finite positions
and bounded work. Include the review's gravity-only fall, the seeded directional
impacts, a low obstruction through a shin, an acute wall corner with a clear exit,
and two approaching bodies. Initial overlap and truly impossible narrow spaces
require bounded progress/escape or a coherent last feasible pose, never a torn mesh
or invented upward energy.

## Approaches

1. Patch thresholds and the final projection: least code, but leaves separate,
   conflicting anatomy, point collision and crowd models. Rejected.
2. Rebuild coupled constraints and contact using shared measured body volumes.
   Chosen by the implementer under the user's instruction to prioritize a better
   comprehensive implementation. Retains the existing C++/native integration.
3. Add an external rigid-body engine: powerful but requires a new world geometry,
   streaming, ABI and animation integration. Not warranted for these defects.

## Modules and behavior

`reactshape` owns measured capsule geometry and interpolation/closest-point math
shared by world contacts and contacts between people. Segment coverage must include
the skin between joints. A bounded conservative sphere cover can use the existing
sphere Sweep interface; it must cover the whole capsule rather than sample isolated
points with unprotected gaps. Preserve measured torso, neck and skull volumes.

`reactcontact` remains the native query adapter. Resolve a native vehicle envelope
without a real contact against its actual collision model, and continue checking
other geometry after a proven-clear candidate. Never turn a stale contact into a
normal, turn an unknown query into free space, or treat a broad vehicle envelope as
an immovable surface forever. Preserve native temporary buffers and any scoped
query filters. Use bounded contact manifolds to resolve acute corners rather than
discarding all progress after eight alternating pushes. All movement accepted after
separation must still receive swept clearance checks. Keep missing bindings and
query exhaustion conservative and observable.

`reactphysics::Body` owns prediction, anatomy and coupled world contact. Replace
the independent final joint clamp with a solve that alternates contact and body
constraints, then validates the complete proposed pose. Contacts distribute their
correction to a segment's endpoints by inverse mass. Distances and contact must be
satisfied together; reject an incoherent candidate as a complete pose rather than
publishing a collection of independently stopped points. Avoid deriving artificial
momentum from position repairs. Keep the 60 Hz step, maximum four catch-up steps,
balance/support decisions and native animation targets. The no-world adapter must
continue to work for isolated solver tests.

`reactcrowd` resolves contacts between the at most twelve active articulated
people. Use measured volumes, a bounded broadphase, stable ordering, and swept
contact for fast approaches. Preserve body shape and certify corrections against
the world, especially for a body pinned to a wall. Remove closing normal velocity
without restitution or health damage. Native-owned balanced actors and protected
actors cannot acquire a second root owner. A late world-processing phase performs
crowd resolution after the individual native controls/solvers; root publication
then reflects the corrected physical pose exactly once.

`reactions` retains generation/clump validation, distance caps, load/cutscene
cleanup, native fall timers and get-up queries. Publish only valid bodies; no
mission, seated, attached actor or player acquires ambient ragdoll ownership.
Rebuild diagnostics when layouts change. Native integration tests must exercise
the actual hook ordering and cleanly restore timers, matrices and collision flags.

## Constraints and verification

- C++17, native Apple Silicon macVC 1.0.5217, no new external dependency.
- Keep twelve active bodies and at most four 1/60-second steps per update.
- Preserve accepted native damage and existing localized hit directions/points.
- Preserve player, mission, vehicle-entry/exit, attachment and cutscene owners.
- Do not publish, install, launch or manipulate the user's game as part of this
  source implementation. Build the plugin and report native visual acceptance
  separately; whole-body ground tunneling was not reproduced in the offline review.
- Keep unrelated uncommitted work intact; do not stage or commit that work.

Tests run at the geometry adapter, captured-rig simulation and actual hook seams.
Check flat/sloped floors, corners, vehicle envelope versus real contact, thin finite
walls, crowd contact near a wall, high momentum, 30/60 Hz updates, nonfatal recovery,
fatal settling, invalid input and lifetime resets. Run the existing project suite,
the feedback suite, focused sanitizers and a production plugin build. Review the
result independently before reporting completion. Performance checks compare
native-query counts and release-mode solver timings against the saved baseline;
offline timings do not establish in-game frame performance.

Remaining implementation details (iteration counts, manifold storage, native
narrowphase binding and internal helpers) belong to the implementer, subject to
these invariants and measured test results.


## Implementation decisions and verification limits

The final solve uses measured rigid-core shape matching, inverse-mass-coupled
self contacts, a real longitudinal skull volume, and staged stationary then swept
whole-pose validation. Candidate failure retains only a currently certified full
pose; failure to certify even that pose hands the actor back to native collision.
Supported corpses sleep and recheck both clearance and support before remaining
asleep. A passive upper-body landing budget prevents numerical contact repair
from sustaining kinetic motion; this is not a claim of exact total-energy
conservation, and free fall/living active controllers remain unaffected.

Crowd contact uses mass-weighted center velocities and paired normal impulses,
preserving internal velocity differences. A deterministic horizontal escape uses
certified copied poses when ordinary peer separation is pinned by the world.
Four searches per call, bounded trials and a blocked-pair geometry cache bound
fallback work. Continuous contact excludes only initially penetrating volume
pairs; static/no-prediction cases do not invent historical entry planes.

A final native `CWorld::Process` wrapper commits crowd corrections after the
original world pass. Generation, clump, ownership and body revision checks protect
copied solves against nested native events. The source implementation, full
suite, focused sanitizers, plugin build and independent review pass. Installation
and native visual/performance acceptance are pending; see the implementation
plan's final verification section for exact evidence and the dense-scene cost.
