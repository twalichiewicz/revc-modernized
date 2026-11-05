# Force-driven persistent contact experiment

2026-11-05. Continues [the first-contact investigation](2026-11-05-world-physics-continuous-contact.md), baseline `c4887ec`. Research code only; the installed game and plugin are unchanged.

## Result and scope

The new implicit coordinator passes **26 dynamics checks** and **12 incremental-material checks**. Two unequal-mass panels remain separated under opposing forces while sliding tangentially, release when the load reverses, and recontact. Explicit fixed supports receive the balancing reaction. Force-driven fracture splits a mount into actual registered bodies; a torn triangle disappears from actual Bullet face queries. Detached faces in one original asset subsequently recontact. A 200m/s point cannot cross an intact supported triangle in the tested 10ms interval; its clear-miss control crosses the plane freely.

This is a bounded improvement over prescribed first-event response, **not Task 3 completion or production SDK acceptance**. The original Bullet full-step gate remains a separate failed control, 42/50. The new coordinator has not replaced that gate's aircraft, rotating-surface, edge-crossing or intact folded-sheet fixtures. Dynamic rigid coupling, friction, calibrated shell bending/buckling, general remeshing, dense world contact, game rendering/aerodynamics and native ownership remain open. A point particle is not a finite-radius bullet penetration model.

## Reused implementation and custom responsibility

[IPC's simulation tutorial](https://ipctk.xyz/tutorials/simulation.html) describes combining inertia, elasticity and a contact barrier in an implicit optimization. [IPC Toolkit](https://github.com/ipc-sim/ipc-toolkit/tree/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99) supplies collision mesh/candidate construction, primitive distances, barrier energy, gradients and projected Hessians. It is a toolkit, not the complete simulation application used here.

The custom [coordinator](world-physics-persistent.h) supplies the integration, sparse Newton solve, constitutive coupling, trial/commit boundary, geometric line search, adaptive interval splitting, support accounting, topology publication and diagnostics. The [material implementation](world-physics-incremental-material.h) supplies a one-dimensional incremental return map. These are substantial custom responsibilities; building IPC does not validate them.

The canonical `structure::Asset` graph remains the physical state. Actual registered Bullet nodes are read at the beginning and updated after acceptance. Trial arrays are temporary optimization state, not independently integrated physical mirrors. Bullet's `World::Step` is **not called** for these islands. Any rigid participant, foreign soft body, changed registration, externally revised asset or changed authored mass/connectivity is rejected. The assets must remain alive and under exclusive coordinator ownership. This is not a native scheduling or streaming implementation.

Dependencies remain the same pinned IPC and patched Bullet checkouts recorded by the prior evaluation. No new upstream source patch is introduced. Optional CPU build settings, dependency revisions, source archive fingerprints, compiler, caches, fixture/runner/library/executable hashes and physical process exits are retained in the reproducible runner's provenance files.

## Coupled timestep and geometry guard

For interval h, old position x, velocity v, finite mass m and constant external force F, the unconstrained prediction is `x+h v+h² F/m`. The objective, in joules, is:

`Σ m |q − prediction|²/(2h²) + incremental material potential(q) + contact barrier(q)`.

After convergence, `v_new=(q−x)/h`. Exact gradients determine equilibrium; positive semidefinite projected Hessians only determine search directions. An Armijo line search decreases the actual objective. The convergence threshold bounds the free-node velocity residual, default 1e-8m/s. Fixed nodes are removed from the solve and must start at rest. Their authored finite mass stays in the mass ledger; their support reaction is accounted separately. Stable asset/node identities preserve these constraints after fragments rebuild.

Before each trial update, the existing conservative distance-advancement adapter bounds all nonincident point/point, point/edge, point/triangle and edge/edge paths. Each path is linear over the proposed move. Initial overlap, exhausted geometric refinement and nonprogress are rejected. The accepted physical interpolation from the **old position directly to the final position** is checked separately: feasibility of the optimizer's multi-segment path alone would not prove the physical path clear.

This guard uses a brute-force enumeration of the small fixtures, not a city broad phase. It includes vertices and surviving triangle edges/faces; axial members do not yet have finite-section collision geometry. Point pairs are an additional conservative guard even where a pair is not an independent IPC surface stencil. Adjacent endpoints are excluded from edge/face pairs; the global point guard also prevents distinct nodes collapsing within the numerical gap. These restrictions can reject otherwise plausible motion and are not a general collision recovery policy.

All fixtures use a uniform **2mm numerical separation**, **20mm barrier activation** and synthetic stiffness **1e6 J/m⁴** for the squared-distance barrier. These are numerical experiment settings, not calibrated material contact properties or the individual surface thicknesses authored in the graph. The unchanged geometric tolerance is 1e-7m. Local current triangle distances are measured independently in the panel fixtures, including edge/edge distances for the two-asset cases; the solver checks the swept path on every accepted subinterval. No claim of arbitrary-scale exact arithmetic is made.

A failed nonlinear/energy/path check can halve the interval, to a maximum subdivision depth of ten. The entire requested interval stays local until it succeeds; rejected attempts do not publish positions, velocity, material history, revisions or time. Failed-attempt nonlinear iterations are counted as work. A deliberately exhausted solve and nonfinite loads verify rollback. This does not promise recovery from allocator failure during Bullet body reconstruction.

## Incremental material and energy accounting

The axial law retains engineering strain, signed plastic strain, accumulated plastic strain, elastic/hardening energy, fracture reserve, dissipation and finite fracture work. Newton evaluations calculate the minimized elastic + hardening + yield-dissipation potential without mutating history. The accepted subinterval commits the return map once. Projected Hessians do not change its force.

Unlike the older explicit fixed-strain projection policy, this solve already reduces stress during optimization. Adding that policy's separate fixed-strain projection loss would spend energy twice. The new law records only the accepted physical plastic work partition; historical projection losses, if present, remain historical dissipation. Literal tests check elastic and plastic forces/potentials/tangents, hardening, permanent set, read-only trials, repeated commits, finite work before fracture and failure without mutation.

Fracture consumes stored/reserved energy once, removes the failed member and any triangle supported by it, and partitions every original mass point among components without a kick or duplicate mass. **Any broken member** triggers reconstruction, including mounts that do not bound a face. A failing test exposed that distinction before the fix. Changed connectivity is committed at the publication boundary; stable support identities are retained.

Deleting a surface can remove numerical barrier potential. That released contact energy is kept in a separate cumulative numerical receipt, not supplied to material fracture or physical heat. A topology change that would create barrier energy is rejected; newly exposed-contact recovery is unimplemented. Surviving geometry is used by subsequent subintervals and actual registered Bullet face queries after publication. Whole-triangle removal is coarse authored fracture, without new fracture caps or bullet-hole remeshing.

The diagnostic balance includes current geometric elastic strain, hardening, reserve, material dissipation, fracture work, kinetic energy, barrier potential and released numerical contact energy. Initial strain is evaluated from geometry rather than a possibly stale cached elastic field. External work is `Σ F·(q−x)`. Remaining nonnegative loss is reported as integration loss, not disguised as physical heat. Significant gains are rejected; a roundoff allowance of `1e-8*(1+|initial energy|+|external work|)` J remains explicit.

Backward Euler dissipates energy and does **not** conserve angular momentum exactly. No global correction is applied. The freely rotating bar's angular error after 0.2s is approximately 0.001879, 0.000953 and 0.000478 N·m·s at 20, 10 and 5ms steps, respectively. Its error converges roughly linearly but is nonzero. Support/external angular receipts are diagnostics, not proof of angular acceptance for a complete world. This accuracy cost must be resolved against the eventual vehicle/world requirements.

## Tests and measured workload

The literal implicit-bar solution, constant-force mass and energy loss checks agree with hand calculations. The panel runs each cover 240 intervals at 5ms, including compression, tangential motion, release and recontact. Minimum local triangle distances are about 12.67mm for free unequal-mass panels and 9.54mm with one supported panel, both above the 2mm guard. End-to-end linear momentum residual is about 3e-13 N·s free and 3.9e-8 N·s including the support receipt. The same-asset fragment run retains two surfaces and 9kg, with minimum measured point/triangle distance about 15.55mm. These are synthetic axial panels, not calibrated vehicle skins.

The benchmark runs 120 steps at 5ms for one and four supported panel pairs: 6/24 nodes, 6/24 axial members, 2/8 triangles and 3/12 fixed nodes. Both runs require 433 nonlinear iterations, 120 accepted substeps, no interval subdivisions, and peak contact stencil counts 15/60. On this Apple M4 host the measured release workload is roughly **0.27ms/step** for one pair and **0.98ms/step** for four; peak process RSS is around 16MB. Raw output is authoritative. This tiny, sparse scene neither chooses a world timestep nor demonstrates a city budget, stiff vehicle structures, fracture-update scaling or hard real-time bounds.

Release and fully instrumented AddressSanitizer runs cover the 61 geometry, 3 graze, 167 first-event, 12 material and 26 persistent checks, plus both benchmark modes. Standard repository checks and provenance/process-exit regressions are recorded separately. Sanitizer success is memory-error evidence for these executed paths, not numerical acceptance beyond their assertions.

## Reproduction

```sh
python3 tools/physics/check_continuous.py \
  --ipc-source build/world-physics-evaluation/ipc-toolkit \
  --bullet-source build/world-physics-evaluation/bullet-conservative-experiment \
  --bullet-build build/world-physics-evaluation/bullet-conservative-build \
  --build-dir build/world-physics-evaluation/ipc-build \
  --output-dir build/world-physics-evaluation/persistent-final \
  --persistent
```

For a separate fully instrumented run, use the recorded ASAN Bullet build, a separate IPC build/output directory and `--asan --persistent`. Both modes retain physical nonzero exits rather than treating library compilation as a pass. Without `--persistent`, the runner retains the original first-event modes.

[Raw retained output](2026-11-05-world-physics-persistent-results.txt). Detailed source/dependency/build records and red/green diagnostics are under ignored `build/world-physics-evaluation/persistent*`.

Next gates are general rigid/deformable coupling, force-driven angular and edge/self-contact controls, heterogeneous authored shells and converged fracture, followed by the still-open native identity/query/streaming contracts. The comprehensive vehicle/person/weapon/world migration remains the existing shared-world plan; this milestone adds no separate damage owner and authorizes no game installation by itself.
