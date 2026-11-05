# Reduced rigid bodies in the shared contact solve

2026-11-05. Continues the [persistent-contact experiment](2026-11-05-world-physics-persistent-contact.md), baseline `449c427`. Isolated research; no game installation or native physics transfer.

## Result and remaining rejection

The shared coordinator now advances explicitly authored rigid boxes and axial deformable assets in one implicit optimization. **47 bounded coupling checks pass**, alongside the existing **34 persistent**, **12 incremental-material**, **167 first-event** and **63 prescribed-geometry** checks. An off-center impact spins the actual registered rigid body. A supported panel carries contact load into its mount, deforms and tears; after the actual face is removed, the rigid body falls through the opening and the face query misses.

**The 144rad/s blade strike still fails the geometric refinement budget.** Its separately executable fast-rotation gate reports **1/2**, exit **1**: rollback passes, physical resolution fails. The 6rad/s strike and 144rad/s clear miss pass. The runner preserves that failure and returns aggregate exit1. This milestone does not close the rotating-contact requirement, select an SDK or replace the earlier Bullet geometric rejection (42/50). Task3 and the native/game integration gates remain open.

## Prior work and method choice

The [Rigid IPC paper](https://ipc-sim.github.io/rigid-ipc/assets/rigid_ipc_paper_350ppi.pdf) develops reduced rigid coordinates and curved collision paths; its rigid/deformable coupling discussion leaves that extension as future work. The reference implementation inspected here is pinned at `dfff467b3ddca626697d89e2119da2e296d72a9e`. Its [body energy](https://github.com/ipc-sim/rigid-ipc/blob/dfff467b3ddca626697d89e2119da2e296d72a9e/src/problems/distance_barrier_rb_problem.cpp) uses a rotation-matrix inertial potential and retains a matrix velocity. We did not build or integrate that complete application.

This experiment extends the existing custom coordinator rather than layering a separately stepped rigid world over the deformables. [IPC Toolkit](https://github.com/ipc-sim/ipc-toolkit/tree/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99) still supplies distances, candidates, contact barrier derivatives and projected contact Hessians. Reduced kinematics, inertia, integration, velocity constraints, geometric advancement, work receipts and publication are custom. No upstream IPC nonintersection or performance guarantee is attributed to this variant.

[Implementation](world-physics-persistent.h), [rigid asset](world-physics-rigid.h), [probe](world-physics-rigid-probe.cpp). Pinned Bullet/IPC libraries and prior patches are unchanged. Rigid IPC is an inspected reference, not a new compiled dependency.

## Rigid state and exact shape

A research `RigidBox` authors eight corner masses, twelve triangles, a zero-margin Bullet box and one actual registered body. Each corner has mass M/8. Its principal inertias are `M(hy²+hz²)`, `M(hx²+hz²)`, `M(hx²+hy²)` for half-extents h. This is an explicit lumped mass distribution, **not uniform-solid box inertia**. Current world inertia is the rotated full tensor; a rotated eccentric-load literal verifies its off-diagonal effect.

The body pose/linear/angular velocity are authoritative at interval boundaries. Temporary trial arrays are quadrature/optimization state. They do not add a second physical mass or run a separate Bullet dynamics step. Bullet's registered box, inverse mass/inertia, shape scale, pose and velocities are validated. Foreign collision objects, SDK constraints/multibodies, unsubmitted SDK force/torque accumulators and outside pose/velocity/shape edits are rejected. Callers submit constant world-space loads at the authored nodes/corners through the shared load matrix. Arbitrary existing native shapes, articulation, native force APIs and moving kinematic scripts are not implemented.

Rigid positions obey `q_i = c + R r_i` exactly. The current Jacobian maps a translation/rotation increment to `delta c + delta theta × (R r_i)`. The same objective as the node prototype combines mass-weighted inertial prediction, axial incremental material potential and contact barrier. All node/corner masses participate once. Reduced exact gradients couple both recipients; the positive Gauss-Newton search metric omits kinematic curvature rather than changing those gradients. Its convergence/cost must be measured, not assumed equivalent to exact Newton.

Trial rigid updates use a constant-axis exponential rotation plus translation. The geometry guard checks those actual curves, with same-rigid self-pairs excluded because their distances are invariant. Deformable points remain linear over each accepted subinterval. The old-to-final physical interpolation is checked separately from optimizer paths. A maximum0.25rad rigid interval and the initial angular rate can trigger subdivision; this prevents silently aliasing a large rotation into endpoint poses. It does not establish arbitrary high-speed contact acceptance.

## Velocity, momentum and work

After a constrained position update, nodal secant velocities generally contain radial components. The integrator computes that rigid body's centre velocity and angular momentum, then its tangent velocity field `v_i = v_c + omega × r_i`, using current full inertia. This local constraint update preserves the body's linear/angular momentum and records the removed radial kinetic energy. It is not a global correction to force a passing momentum total. Projection residuals are tested independently; projection loss remains part of numerical integration loss rather than material heat or fracture work.

Principal-axis free rotation has a hand solution for this discretization: angle `atan(h omega_old)` and `omega_new = omega_old/sqrt(1+h² omega_old²)`. Translation and external work also have literal force solutions. These pass, exposing the method's numerical damping rather than claiming exact free rotation.

An asymmetric freely rotating box after0.2s has angular momentum error about **0.01576, 0.00792 and 0.00397 N·m·s** at20,10 and5ms respectively. Both error and kinetic loss decrease with smaller steps. This is evidence of convergence in that fixture, not exact angular conservation or a chosen world timestep.

Constant force work is `F·(q_new-q_old)`. Applied angular impulse integrates the actual material point along the accepted rigid curve; using only its endpoint midpoint would be wrong. A separate Simpson quadrature check verifies the analytical curve average. Fixed supports receive balancing impulse/torque receipts. All trial state, receipts and accepted rigid publication values must pass finite/representability checks before any actual participant is updated. Deliberately exhausted mixed-body solves verify rollback of both recipients and the clock.

Material return/commit and topology release retain the preceding experiment's accounting. Contact does not replay a separate damage impulse or spend a duplicate work budget. The mounted panel uses an authored axial skin and strut, not a calibrated bending shell. A weak member removes its supported triangle; surviving collision faces and actual Bullet face queries follow the same revision. There is no new remeshing, fracture cap, renderer or aerodynamic consumer.

## Geometry accuracy and unresolved fast contact

The physical separation remains **2mm** and barrier activation **20mm**, with the same synthetic barrier stiffness **1e6 J/m⁴**. Prescribed queries retain a default **1e-7m** contact tolerance. The coordinator requests **1e-10m**, validated by two near-contact controls. This changes numerical precision, not the physical gap.

The initial fast-strike failure exhausted Newton iterations with velocity residual32.84m/s, dt4.069e-6s and line-search fraction0. The1e-7m guard envelope could trap a trial at the boundary even when a later step needed to move away. Finer precision removes that observed stall and reduces the slow strike from18 to10 accepted substeps. The fast strike then instead exhausts the unchanged **1024-refinement** continuous-distance budget. Conservative speed bounds are expensive near contact and cannot yet resolve this motion within the available work. Neither exhaustion is reported as clearance or an accepted impact.

The fast case remains under `world_physics_rigid --limits`, with original diagnostics retained in ignored build output. It starts with a144rad/s thin box crossing a fixed point over a nominal0.6rad interval. Failure leaves both actual poses/velocities, target revision and simulation time unchanged. Its miss at radius1.5m resolves in four substeps with zero contact records. The6rad/s strike registers contact and a support reaction, slowing the blade from6 to about2.97rad/s. It is a bounded demonstration, not a rotor material model or a converged aircraft crash.

General coupled self-contact/edge crossing, contact friction, complex rigid shapes, joints, calibrated shells, fracture convergence, robust recovery and world-scale scheduling remain open. If the current conservative advancement cannot meet these gates, it must be replaced or the candidate rejected rather than bypassed.

## Executed fixtures and workload

- A free translating box matches position, velocity, external work and implicit integration loss.
- Principal-axis spin matches the discrete hand solution; rotated eccentric force exercises current full inertia.
- A free point and box, centred/off-centre impacts and a clear miss check both reactions. An anchored-box variant retains its fixed pose and accounts the reaction into its support. The offset strike produces y spin about0.164rad/s, linear residual around4e-13 N·s and angular error around6e-6 N·m·s.
- Two unequal-mass boxes interact in both participant orders, with no sampled local triangle intersections and linear residual around2e-14 N·s.
- A mounted panel has about0.134m peak corner deflection and4.46N peak mount force under the tested load. The tearing variant removes one face and releases the rigid body through that actual opening. Segment/triangle checks show no sampled intersections with surviving faces; coplanar/degenerate predicates are not a universal robustness proof.
- SDK force/torque/constraint rejection, outside mutations, mixed rollback and curved angular-load accounting are asserted.

The benchmark uses one/four independent off-centre point/box pairs over120 steps at5ms:9/36 generalized DOFs,9/36 quadrature vertices and12/48 rigid triangles. Each takes335 nonlinear iterations,120 accepted substeps and no interval subdivision; peak contact counts are1/4. On the Apple M4 host the initial release measurement is roughly0.23ms/step for one pair and2.4ms/step for four, around15MB peak process RSS. Raw final output is authoritative. Brute-force feature enumeration and these tiny scenes do not establish a city budget or hard real-time bound.

## Reproduction and review boundary

```sh
python3 tools/physics/check_continuous.py \
  --ipc-source build/world-physics-evaluation/ipc-toolkit \
  --bullet-source build/world-physics-evaluation/bullet-conservative-experiment \
  --bullet-build build/world-physics-evaluation/bullet-conservative-build \
  --build-dir build/world-physics-evaluation/ipc-build \
  --output-dir build/world-physics-evaluation/coupled-final \
  --rigid
```

Expected aggregate exit is **1**, solely from the retained fast-rotation rejection. Build/provenance/process errors remain exit2. `--rigid` also runs the preceding persistent/event/query/material checks. A separate fully instrumented Bullet/IPC build with `--asan --rigid` retains the same physical failure. Standard repository tests and11 provenance/5 child-process regressions run separately. [Raw results](2026-11-05-world-physics-rigid-results.txt) retain individual exits; detailed hashes/build records are under ignored `build/world-physics-evaluation/coupled*`.

No gameplay behavior is inferred from headless probes. Native ownership, controller suppression, streaming/mission identity, actual vehicle assets, render/aero publication, damage-driven systems and the full-world migration remain the existing plan's open work.
