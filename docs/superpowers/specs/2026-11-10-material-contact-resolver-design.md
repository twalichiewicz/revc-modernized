# Material contact resolver for vehicles

## Why

The user plays the installed build and reports how cars behave. In their words:
- "hitting curbs basically stops the vehicles dead at points ... In real life, the car would deflect and damage itself"
- "the car either gets launched way into the sky or falls through the ground"
- "the SUV's roof is clipping into the ground when it's upside down"
- "the body clipped into the building and got stuck since it seemed to be stuck raised at 45 deg"
- "light bumps basically makes the car undrivable ... Refuses to accelerate"

All of these were reproduced in-game (`docs/evidence/vehicle-body/README.md`). They share one cause: the material body hands native GTA collision hundreds of vertex probes, but the native resolver was built for about 30 spheres and boxes per car. It only has two ways to deal with overlap:
- **Undo.** If any contact is rigid, `CPhysical::ProcessCollision` restores the frame's starting matrix.
- **All-or-nothing shift.** `ProcessShift` moves the actor by the contact depths, then undoes the move if any rigid contact remains. Each shift pass also damps speed by 0.707.

With many probes, one rigid contact nearly always remains. So a car either freezes (the 41° wedge against a facade, or a dead stop at a kerb), or, where we relaxed the rules, accepts penetration until it sinks or clips.

We have tried four classification fixes in the host contact code: inside-box exit, recovery of points that passed through a face, treating penetrating contacts as non-resting, and a 5 mm slop. Each fixed its own case and exposed the next. The yield model (lips and trim deform at sheet strength) is sound and stays. What has to change is the positional part of the resolution.

## What this is

For material-owned wheeled ground vehicles touching buildings and immovable objects, a host-side position resolver takes over persistent contact (resting, sliding and leftover penetration) from native frame undo and shift:
- **Impact frames keep native undo,** so a hit never visibly sinks into a wall.
- **Native velocity response stays.** Impulse, friction, the yield cap and damage booking are unchanged.
- **Persistent penetration is removed by position, a bounded amount every frame.** The resolver solves over all of the frame's contacts together, so no single contact can hold the car in place.

Everything else stays native: integration, suspension lines, tyres, handling, AI and scripts.

## Goals (each one testable)

1. **No freeze.** No material car is held by the undo loop, in `body-curb` or `body-bump`, release or ASan:
   - The signature to rule out is a pose frozen to within 1 mm for 30 frames while native velocity stays at or above 0.05 m/s (the step is being undone).
   - A car at rest that is physically propped, with no penetration beyond the slop, is allowed even if it's tilted (my call). An example is a nose resting on a facade like a ladder against a wall.
   - `no_wedged_rest` is replaced by `no_frozen_rest`, which checks the undo-loop signature, and `no_persistent_penetration`.
   - A car pressed into a building corner slides, settles or rests propped; it never freezes.
2. **No sink or clip.**
   - At rest, no material point is more than 0.10 m below the surface under the car, on wheels, side or roof (`inverted_rest_holds` and the `body-curb` limits).
   - After settling, no material point stays behind a wall face it entered by more than the slop. This needs a new assertion, `no_persistent_penetration`.
3. **Momentum and damage stay as they are now.** The release `body-bump` figures must hold:
   - Kerb strikes keep 48–82% of their speed, about the same as the stock twin.
   - The car drives away in place.
   - 8 m/s wall and pole strikes still stop it, with the current damage (cooling 0.656 and 0.377).
   - Light car-to-car bumps match stock.
4. **No launches or pops.** `body-curb` holds vz < 8 m/s, and the per-frame position correction is bounded (see below), so the original launch class can't come back as a teleport.
5. **Nothing regresses.**
   - Existing suites and smoke cases stay green: `make test test-game-assets`; release normal, body-vehicles, body-bullets, body-ground, body-tail, dodo, body-curb, body-bump; ASan body-curb, dodo, body-bump.
   - The resolver costs at most 0.5 ms per actor per frame in release in the curb and bump scenes, as measured by the smoke frame timings.

## Non-goals

- Changing integration, suspension lines, tyre and handling forces, or anything about driving feel. That is the separate driving-model rework.
- Actor-to-actor contacts (cars and peds). They keep the native path in this phase: car-to-car bumps showed no hidden momentum loss.
- Knockable props (lampposts, hydrants, bins, fences, glass), which take native's two-body branch. They keep the native path, including being knocked loose when the car is stuck against them (my call). A car wedged against an unbreakable prop may still freeze the old way. That's a known limit.
- Aircraft (Dodo, helicopters, planes) and boats. They keep their current contact path in this phase (my call): the Dodo has extensive ground and tail tests built on today's behaviour. Revisit once cars are proven.
- Continuous collision detection for rotation, and edge-edge contacts.

## Anti-goals (failure even if the tests pass)

- Resting cars visibly jitter or creep, because corrections oscillate or never settle.
- A thin wall pushes a car through to the far side: a correction moves the body across a face it was in front of.
- One-frame pops: large corrections that look like teleports. That is the original launch symptom.
- Energy injection: the resolver adds velocity. It corrects position, and at most removes the inward normal speed at the contacts it corrects. It never adds speed and has no velocity bias, so it can't launch anything.
- Gameplay that depends on native stuck detection behaves differently for non-material actors. Peds, objects and non-owned vehicles must be untouched.

## Constraints

- **Engine.** The engine is reVC pinned at a26fff4, patched by `tools/host/regular-engine.patch`. The patch must keep applying cleanly to a pristine checkout. Host code goes in `tools/host`, with pure logic in headers like `BodyContact.h` so it can be unit-tested without the engine.
- **Test-only code** stays behind the `VC_REGULAR_*` smoke switches.
- **Verification** uses fresh `build_regular.py` work dirs in release and ASan. Release must be clean, and ASan must have no sanitizer errors. Don't install without the user's approval.
- **Native frame cadence.** The timestep is capped at about 3 (60 ms), and ASan runs near that cap. Large timesteps are where these bugs show up, so every assertion has to pass there too.

## How (decided)

1. **Scope of capture.** This applies to material-owned wheeled vehicles only: automobile and bike kinds with a captured, current body. It covers contacts with entities that take native's immovable branch, meaning buildings and infinite-mass or fixed objects (my call). Knockable props and dynamic entities keep the native path and native restore.
2. **Impact versus persistent contact (my call).** It's a hybrid, because the user reported visible clipping into buildings.
   - **Impact frame.** In a pass, any rigid in-scope contact closes faster than v_impact = 2 m/s. That threshold sits above the largest gravity step at the timestep cap (g·dt ≈ 1.2 m/s). The frame keeps native undo, so the car stays outside the wall and the native response runs again from the restored pose.
   - **Persistent contact.** Every other in-scope contact (resting, sliding, slow pressing and leftover penetration) never makes `ProcessCollisionSectorList` report a collision for restore, and never marks the actor stuck. So `ProcessCollision` keeps the integrated pose, and `ProcessShift` doesn't run or damp because of these contacts. The resolver removes their penetration.
3. **Contact input.** The host contact code already produces contacts: crossings, rest probes, inside-box exits, and points recovered from behind a face. The resolver takes each one's world point, outward normal and *true* penetration:
   - a crossing's depth;
   - an inside-box contact's distance to the nearest face;
   - a recovered point's distance behind the face.

   It does **not** take the skin-inflated depth. Contacts whose part yielded this frame are excluded, because they deform (plastic push) instead of moving the car. Wheels are already excluded from the sweep, and native suspension lines keep supporting the car.
4. **Solver.** A position-level projected Gauss-Seidel step on the rigid body, run once per collision pass after the velocity responses:
   - It uses native mass `m_fMass`, scalar turn mass `m_fTurnMass` (native GTA inertia is isotropic) and the centre of mass `m_vecCentreOfMass`.
   - For each contact with penetration d > slop, solve a correction λ for the target (d − slop)·β. Clamp the accumulated λ to be non-negative, and apply translation λn/m plus rotation about the centre of mass.
   - Iterate over all contacts a fixed small number of times.
   - Clamp the total correction per frame, both translation and rotation.
5. **Velocity.** Native velocity responses keep their role. A single averaged native response per frame can leave inward speed at one face of a corner. So after correcting position, the resolver removes any remaining inward normal speed at the contacts it corrected, at the contact point, through the same mass and turn-mass terms. It only removes; it never adds or reflects speed (my call). Without this step, a position correction capped per frame would fall behind steady gravity or a steady push.
6. **Instrumentation.** Under `VC_REGULAR_SMOKE` and the trace switches, log one line per resolve: contact count, max penetration before and after, applied translation and rotation, and iterations used. Add `no_frozen_rest` and `no_persistent_penetration` to `body-curb` and `body-bump`. Add a deterministic wedge case that starts from the penetrating pose recorded in ASan `body-curb` run 16, the SUV at 41° against the site 0 facade (`build/playtest-fixes-3-v4asan-body-curb/run.log`), so the case fails on today's code first. Within 3 s it must slide, settle or rest propped without penetration, in release and ASan.

## Left to the builder

- **Tuning, within these bounds:**
  - iteration count 4–8;
  - β 0.6–1.0;
  - slop: keep the existing 5 mm;
  - per-frame clamps: translation 0.05–0.15 m, rotation 0.02–0.08 rad.
  Justify each choice with test data.
- Where exactly in `ProcessCollision`/`ProcessCollisionSectorList` to collect contacts and run the resolve. The behaviour must be as stated.
- How the existing yield, recovery and inside-box helpers feed the resolver, and which of today's stuck-exclusion rules (`AircraftBodyRigid`, `Resting`, slop) become redundant and are removed.
- Data structures, and how the pure solver is split from the engine glue.

## Verification the builder runs

- **Pure unit tests for the solver,** each fail-first:
  - one floor contact resolves to the slop;
  - two opposing faces and a corner (two perpendicular faces) converge without freezing;
  - a many-point inverted rest resolves upward;
  - the per-frame clamp holds;
  - velocity is untouched;
  - non-finite or zero mass input is rejected.
- **Smoke:** the full release and ASan matrix above, plus the deterministic wedge case and `no_persistent_penetration`.
- **Table:** before and after, material against stock, per strike type, for momentum kept, damage, drive-away distance and the worst tilt at rest.
