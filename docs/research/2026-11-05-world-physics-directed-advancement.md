# Directed advancement for near-contact curved motion

2026-11-05. Continues the [rigid coupling investigation](2026-11-05-world-physics-rigid-coupling.md), baseline `27d11bf`. Research only; no game installation or native ownership transfer.

## Result

The previously rejected **144rad/s blade strike now resolves** with the existing2mm separation,1e-10m coordinator precision and1024-refinement limit. The final measured single requested interval accepts eight substeps with seven subdivisions; it registers contact/support reaction and ends near89.2rad/s. The default coupled suite includes this case, rather than leaving it only in an optional mode. `--limits` remains available as the same regression and now passes4/4.

The geometry suite passes **79 checks** and the coupled suite **63 checks**, including refinement of the fast strike. This closes that specific refinement failure, **not Task3, general collision acceptance or a game-ready physics engine**. General shapes/joints/friction, calibrated shells, force-driven edge/intact-self-contact, world scheduling and native integration remain open. The original Bullet full-step42/50 rejection is a separate control, not overwritten by these results.

## Diagnosis and prior work

The old loop advanced by0.8×remaining distance divided by the sum of maximum point speeds. This norm bound is conservative, but includes common and tangential motion that barely changes separation. A near-tangent slide with1nm excess clearance exhausted1024 refinements after advancing only around8e-7 of the normalized interval. A full-turn planar rotation had the same problem even though its normal clearance was constant.

The retained fast-strike trace isolates a point/triangle trial with speed bound0.000149991 per normalized interval. After1024 advances it had reached only t≈0.15988, with gap0.00200002594m. Its triangle vertices shared one rigid motion. Treating their whole world-speed norms as closing motion discarded that structure.

Directed motion bounds in conservative advancement are established prior work; [C2A](https://gamma-web.iacs.umd.edu/papers/documents/articles/2009/tang09.pdf) bounds motion along a separating direction rather than using undirected travel alone. This experiment applies that idea using IPC geometry and a custom co-moving-frame Taylor certificate. It does not implement C2A's complete hierarchy or claim its benchmark results.

[IPC Toolkit](https://github.com/ipc-sim/ipc-toolkit/tree/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99) supplies primitive distances and gradients. The gradient proposes a separating direction; every vertex-pair projection is checked, so correctness does not require it to be a perfect closest normal. The trajectory algebra, certificates and work-budget handling are custom [adapter code](world-physics-ccd.h).

No upstream CCD time or miss is adopted as a safe prefix. The earlier rejected `no_zero_toi` shortcut remains removed.

## What a certificate establishes

For point/point, point/edge, point/triangle and edge/edge, let A and B be the two sets of primitive vertices. A direction n separates the two convex primitives by at least d when **every** pair satisfies `n·(a_i-b_j) > d |n|`. A plane valid only for a chosen closest point would be insufficient.

The adapter evaluates an exact linear/constant-axis rigid trajectory and its derivative at the current known-safe time. When a primitive's vertices share identical rigid-motion metadata, it works in that moving rigid frame. Those vertices become constant, removing their irrelevant endpoint travel. Otherwise the same reasoning uses a fixed world frame.

For a reference rotation R and centre c, the relative coordinate is `u=Rᵀ(p-c)`. With constant reference angular rate w, its derivatives are:

`u' = Rᵀ(p'-c') - w×u`

`u'' = Rᵀp'' - 2w×Rᵀ(p'-c') + w×(w×u)`.

A uniform projected bound is `|n·u''| ≤ |n| A + |n×w|(2V+|w|U)`, where A bounds the other trajectory's acceleration, V its velocity relative to the translating frame, and U its relative position. Linear motion contributes zero physical acceleration. Constant-axis rigid motion contributes angular speed squared times the perpendicular local radius. U uses the maximum norm of the affine centre difference at interval endpoints, plus the other rigid local radius. These are analytic bounds over the remaining interval, not maxima inferred from samples.

For every vertex pair, the adapter constructs a lower bound on projected clearance:

`margin(h) ≥ margin(0) + velocity(0) h - ½ acceleration_bound h²`.

This lower bound is concave. Positive values at both interval endpoints certify positive clearance throughout. Otherwise, the smallest positive root limits the advance; the0.8 safety factor leaves it inside that bound. The original global speed bound remains available, and the larger certified safe advance is used. No unverified time segment is skipped.

A result reported as an actual hit still satisfies both distance bounds around the requested material separation. A certificate may establish a physical miss for a path that stays strictly outside material but comes within the root-refinement tolerance; that tolerance is not extra material thickness. Initial near-contact handling remains conservative. The returned refinement count and certificate count expose work without increasing the1024 limit.

## Numerical and extension limits

The path family is deliberately narrow. Only the **exact dynamic types** `LinearPath` and `RigidPath` receive analytical certificates. Unknown classes and subclasses that override virtual motion retain the bounded norm fallback. Actual wave-trajectory regressions initially exposed false misses when a subclass was mistaken for its base formula. A changed nonunit rigid axis is rejected before using the rotation assumptions.

Finite position/velocity/acceleration calculations are required. Margins and derivatives include scale-dependent floating-point guards, and acceleration bounds are inflated. Unrepresentable or unusable certificates fall back to the old conservative loop. These safeguards and tests are not a formal directed-rounding proof for arbitrary coordinate magnitudes, turn counts or user-supplied speed bounds. Large/cancelling coordinates can still make a query unresolved; no universal floating-point claim or city budget follows.

Full/reverse/multiple turns, different rigid frames, common fast translation, parallel edges, point/edge motion, the recorded trial and near-initial first-event controls are exercised. Unknown trajectories still reject on budget exhaustion instead of being declared clear. The physical gap, first-contact distance assertions and failed-interval publication policy remain active.

## The recorded query really crosses later

The partial failure trace did **not** establish a clear whole path. Replaying its full data gives minimum sampled distance0.00199998947m, below the0.002m separation. An initial fixture that assumed a miss caught this incorrect inference; the oracle was corrected to the actual geometry rather than relaxing the hit assertion.

The independent control samples10001 times and subtracts the original global Lipschitz speed bound times half the sample spacing. It certifies the prefix through t=0.2 above0.00200001669m. A later sample crosses separation at t=0.7577. The new query reports t≈0.755499 with both gap bounds satisfied, using **five refinements and four certificates**. The small early-time difference is consistent with the1e-10m root tolerance and very small normal speed. This is a certified clear prefix plus observed later penetration, not a claim that sampling alone proves every intermediate configuration clear.

The near-tangent analytical controls separately test true misses and a known approach. They initially failed5/5 and now pass without budget changes. Derived wave classes and an altered axis initially failed their rejection/first-hit assertions and now exercise the safe fallback/validation boundary.

## Force-driven reaction, energy and convergence

The fast blade is still an exact rigid-box research asset with an authored corner-mass inertia, striking an explicitly fixed point. It is not a deformable rotor or material-failure model. The resolved interval proves the requested interval resolves contact rather than passing through the target or leaving a partially published state. Its point/box geometry and support receipts are checked.

Caller intervals4,8 and16 over the same nominal0.6rad/144rad/s duration produce:

| Intervals | Accepted substeps | Linear residual (N·s) | Angular error (N·m·s) | Work/energy residual (J) |
| --- | --- | --- | --- | --- |
| 4 | 8 | 2.31e-8 | 14.28 | 1.46e-11 |
| 8 | 10 | 2.86e-8 | 6.98 | 8.91e-11 |
| 16 | 16 | 5.17e-8 | 4.00 | 8.19e-11 |

Angular error decreases but is nonzero. Numerical integration loss is approximately16.2kJ,15.6kJ and15.3kJ, respectively. That loss includes the dissipative implicit/contact discretization and the documented rigid tangent-velocity projection. It is **not** converted into physical heat, fracture work or an aircraft damage budget. Final spin varies about89.2→90.3→90.7rad/s with refinement; no global momentum correction is applied. Accurate/calibrated impact dissipation remains unresolved.

The earlier asymmetric free-spin controls and existing rigid/panel/tear/foundation tests remain separate. Solving the geometric bottleneck does not turn a numerically damped boxed fixture into a validated vehicle structure.

## Measured cost

On this Apple M4 host,20-call release medians are about0.71µs for the near-tangent plane,0.83µs for the full-turn tangent plane and1.29µs for the near-inbound plane. They use1,1 and3 outer refinements respectively, with their hit/miss assertions active. The recorded Newton approach uses five refinements instead of exhausting1024.

The complete fast strike is much more expensive: the measured standalone4.167ms simulated interval takes about54ms wall time, counting303 nonlinear iterations including failed attempts, eight accepted substeps and seven subdivisions. This is **not a real-time world budget**. The generic four-pair benchmark remains around2.3ms per5ms step; neither small workload validates a city, calibrated stiff structures or hard scheduling bounds. Raw final timings, including sanitizer overhead, are retained separately.

## Fresh review and repeatability

A fresh reviewer independently reproduced79 geometry,3 graze,4 fast-gate and63 coupled checks, confirmed matching current fixture/executable hashes, and found no Critical, Important or actionable Minor findings. The certificate algebra was reviewed separately from the author's sanitizer, repository and provenance runs. Those broader runs remain author evidence, not independently repeated checks.

Repeated executions of the same binary vary in adaptive work and final trajectory values: the reviewer observed7 accepted substeps/6 subdivisions/235 nonlinear iterations instead of the retained8/7/303, and decreasing angular errors14.28→7.55→3.75Nms rather than14.28→6.98→4.00. Both measured runs pass the bounded checks. The cause and acceptable range of this sensitivity are unestablished; deterministic replay and full-world accuracy are not claimed. The timings/tables above describe retained individual measurements.

## Reproduction

```sh
python3 tools/physics/check_continuous.py \
  --ipc-source build/world-physics-evaluation/ipc-toolkit \
  --bullet-source build/world-physics-evaluation/bullet-conservative-experiment \
  --bullet-build build/world-physics-evaluation/bullet-conservative-build \
  --build-dir build/world-physics-evaluation/ipc-build \
  --output-dir build/world-physics-evaluation/projected-final \
  --rigid
```

The updated runner expects aggregate **exit0** for these bounded suites, including the former fast gate. An unresolved physical case would still return1 and a build/provenance/process failure2. The added near-motion workloads keep actual hit/miss assertions in benchmarks.

Run the separate ASAN Bullet/IPC build with `--asan --rigid` **after** the release configuration completes. Tight Inclusion configures a generated header in its shared source-cache directory; concurrent configurations produced a real build error in this investigation. Sequential reruns refresh dependency/build records. No upstream source modification is needed for the physics change.

[Retained output](2026-11-05-world-physics-directed-results.txt) includes individual exits and measured query/solve workload. Detailed source/library/executable hashes, build records and red/green traces remain under ignored `build/world-physics-evaluation/projected*`.

The installed executable/plugin remain unchanged. Task3 still requires broader coupled geometry/material/authoring acceptance, and Task4/5 still require native ownership, authoritative queries, lifecycle, render/aero and gameplay verification before any game migration.
