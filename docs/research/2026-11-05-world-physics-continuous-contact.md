# Continuous detection and first-contact response

2026-11-05. Continues the [structural evaluation](2026-11-05-world-physics-structure-evaluation.md) from repository baseline `9e4874b`. Isolated CPU research only; no game installation or normal plugin dependency change.

## Result and acceptance boundary

The bounded distance-advancement adapter using IPC geometry passes **59 prescribed-trajectory checks**, **three close-graze checks**, **167 contact-event checks** and five measured query workloads. A shot at 1000 or 10000m/s detects intact material at a 480Hz interval, while the tested missing triangle and open-space controls receive no impulse. Curved rotor sweeps detect an intermediate strike even after a full revolution with identical endpoint poses. Actual Bullet participants receive balanced normal impulses and off-center torque.

**This does not close Task 3 or select a production engine.** These are trajectory queries and first-event replays, not a persistent, force-driven contact timestepper. The original Bullet world still fails its geometric gate **42/50**. No end-to-end deforming-fragment recontact, maintained self-contact, friction, dense multi-contact solve or updated native/game geometry is established by this continuation. The 59/167 passing counts must not replace that failed gate.

## Existing implementation reused

Built [IPC Toolkit](https://github.com/ipc-sim/ipc-toolkit/blob/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99/README.md) at `54d58ebcf1f6dd0cf95bcc58afd79839ca603a99`, native ARM64, C++17, double-precision geometry, Release, minimum macOS15. Its CPU nonlinear CCD uses Tight Inclusion queries on bounded pieces of a curved trajectory. The upstream [trajectory documentation](https://ipctk.xyz/tutorials/nonlinear_ccd.html) explicitly separates the trajectory function from its conservative deviation bound.

IPC Toolkit supplies detection, distance/closest-feature calculations and contact potentials. After the review counterexample below, this adapter uses its distance and closest-feature code; it does **not** use upstream nonlinear CCD times or misses to certify a collision-free prefix. It does not supply the dynamics world or full solver in this experiment. The normal impulse operator and trajectory/witness adapters below are custom research code. Bullet still owns the registered test bodies, mass/inertia and the existing structural state. No second physical body mirror is created.

Optional CUDA, filib, Abseil, robin-map and MeshFEM sparse assembly are off. The chosen trajectory bounds need no optional interval-arithmetic implementation. The root [IPC license](https://github.com/ipc-sim/ipc-toolkit/blob/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99/LICENSE) is MIT; dependencies have separate licenses. The recorded build includes Eigen, libigl/predicates, TBB, Tight Inclusion, Scalable CCD, TinyAD, spdlog, xsimd and geogram multiprecision. Their resolved revisions/archive fingerprint and source patches are in the input record. No deployment/license compatibility decision is inferred from compilation.

The existing Bullet checkout remains pinned at `d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`, with the previously retained contact/lifecycle patches. No new Bullet source patch was required for these first-event experiments.

## Supported trajectories and actual contact witnesses

[Detector adapter](world-physics-ccd.h), [query fixture](world-physics-ccd-probe.cpp), [closest-feature witness](world-physics-witness.h).

The supported paths are straight translation and constant-axis, constant-angular-speed rigid motion plus translation. They are prescribed over a normalized interval. They are not an assertion that arbitrary force-driven deformation or an asymmetric torque-free body follows those paths.

For rigid local offset r, rotation angle θ and interval width h, the second derivative norm is bounded by `|r|θ²`. Linear-interpolation error is therefore at most `|r|θ²h²/8`; `2|r|` supplies another bound for any turn count or sign. The path interface exposes the smaller bound, rounded outward, for comparison with upstream curved-trajectory methods; it is not used to skip motion in the corrected distance-advancement loop. This avoids the invalid zero-envelope assumption when the start/end poses coincide after a full revolution. The maximum speed bound is `|translation|+|θ||r|`; linear paths use their displacement magnitude. These are conservative real-arithmetic bounds for this restricted path family. Floating-point behavior is checked within the declared geometry tolerance, not claimed exact for all possible coordinate scales.

The corrected adapter starts at the known initial state and uses actual primitive distance throughout. It never adopts an upstream CCD time as a safe starting state, nor an upstream miss as proof of clearance. Point/triangle relative speed is point speed plus maximum triangle-vertex speed; edge/edge uses the sum of the two maximum endpoint speeds. This bounds how quickly distance can decrease. Advancing by0.8×remaining-gap/speed cannot skip contact in exact arithmetic for these supported paths. Both lower and upper separation bounds are checked; deep initial overlap needs a recovery policy and is rejected. Exhausted/nonprogressing motion is unresolved, rather than reported clear or forced into a hit.

A single **1024-step** refinement policy resolves the tested close-graze cases. The earlier64-step policy could not resolve the10-micron miss/hit controls; their failing outputs remain retained. A one-step control still rejects unresolved motion. The1e-7m geometry tolerance is unchanged. Twenty near-initial rotor cases additionally check analytical first-event time and both separation bounds. These are restricted-path checks, not universal floating-point guarantees or a chosen city timestep.

Closest witnesses use IPC's feature classification for triangle interiors, all three edges and all three vertices. Projection to the infinite triangle plane alone would yield invalid weights outside an edge. Seven skew-triangle literal cases verify the barycentric weights; degenerate/nonfinite witnesses are rejected by the adapter.

## Rejected upstream time shortcut and review correction

The first adapter repeatedly submitted a returned time back to nonlinear CCD. A simple shot's first result left about2.136microns above the requested0.01m separation; its next query stalled beyond45s. A retained process sample located Tight Inclusion's root finder. One-million root iterations also cost about70ms per shot. Reducing that search budget initially improved the measured cost to about0.7ms, but did not establish correctness.

The fresh reviewer then found that the reported time can already violate the **original requested separation**. The [Tight Inclusion retry implementation](https://github.com/Continuous-Collision-Detection/Tight-Inclusion/blob/6f84001a790a9d3362b7c13d410037140d83fa2c/src/tight_inclusion/ccd.cpp) permits `no_zero_toi` retries to reduce minimum separation. Treating its returned time as a safe prefix was invalid.

A radius1.5m point at initial edge distance0.03001m, a0.6rad rotating-edge sweep and requested separation0.03m reproduced an analytical first time of`0.0000111133347415`. The old adapter instead returned`0.0134382247925` at gap`0.0179171739568`: about12mm into the requested separation. Merely rejecting that late point would not prove an earlier transient contact was preserved.

The correction removes the shortcut entirely and advances from time0 under the analytic relative-speed bound, using IPC's primitive distances/features. The reproduced case now returns about`0.0000110677325087` at gap`0.0300000410338`, within the unchanged1e-7m distance allowance. Twenty near-initial sweep/margin cases and an initial-overlap rejection test took the geometry regression from43/59 to59/59. No late library time or miss is trusted by the corrected loop.

The resulting query benchmark reports roughly0.8microseconds per simple shot and1.5microseconds per tested rotor hit. These small controlled queries do not establish a structural-step or city-scale budget. Upstream direct-CCD build/code remains available for comparison, but it is not the accepted minimum-separation shortcut in this prototype.

## One normal response operator on actual participants

[Contact operator](world-physics-contact.h), [contact/replay fixture](world-physics-contact-event-probe.cpp).

A contact side references either one rigid body at a world-space material point, or weighted soft-body nodes. The same operator handles rigid/face, weighted edge/edge and rigid/rigid events, including same-structure soft nodes. It reads and changes actual registered Bullet participant velocities, using Bullet's rigid impulse method and current world inertia. It does not create a separate damage/health owner or an after-the-fact global correction.

For a unit normal n, the generalized inverse effective mass is:

- Node stencils: combine each node's signed coefficient across both sides first, then sum `coefficient²×inverseMass`.
- Each rigid participant: `inverseMass + (r×n)·I_world_inverse(r×n)`.

For closing normal relative speed vn, the inelastic impulse is `−vn/effectiveInverseMass`. Separating contacts get no impulse. Normal kinetic work loss is `−½vn×impulse`; it is returned as a receipt, not independently spent on deformation, fracture and heat. Stationary fixed supports return explicit linear/angular reactions. Driven support work, constrained rigid motion factors and friction require policies this operator does not implement.

The operator checks normalized weights, finite states, unit normals and prospective velocity outputs before applying an impulse. Normal contact points must be collinear with n so the impulse pair does not introduce a spurious couple across a margin. Repeated stencils sharing a node are combined before effective-mass calculation; a literal shared-node example gives impulse8N·s with no net change to the shared node. This does not replace actual topology-aware collision filtering in a full world.

Literal references include an asymmetric rigid/face impulse `2432/859 N·s`, unequal edge/edge impulse `277200/32383 N·s`, and eccentric rigid/rigid impulse `238/237 N·s`. Tests vary participant side, mass scale20, rotated/translated frames, tangential velocities, static rigid/node supports and rigid spin. Linear/angular balance includes fixed-support reactions. A repeat-contact check rejects double-spending normal work.

Energy comparison uses `1e-8J + 32ε(|K_before|+|K_after|)` as a roundoff allowance. In the 10000m/s moving-skin case, bulk energy is1e9J and the measured work residual is about2.6e-8J, below the approximately1.4e-5J rounding allowance. An absolute1e-8J threshold was below that subtraction's numerical resolution. No state is corrected to fit the energy reference.

## First-event replays and topology

The replay advances prescribed force-free motion to the detected first event, then applies the common operator to the registered participants. It does not run a full Bullet step and then compensate its velocity.

- The 0.01kg, 10mm-radius shot contacts a triangle in a 20kg panel at1000/10000m/s. At1000m/s the measured normal impulse is about9.9931N·s. Reversing relative motion by moving the panel produces the same impulse. The event eliminates closing normal velocity and matches momentum/angular/work receipts.
- A prescribed local strain breaks the physical edge of one triangle, invoking the existing structural topology rebuild. The subsequent sweep rereads surviving physical faces; the removed region and an actual outside-space control receive no impulse. This tests current topology, not an authored cabin with internal components or bullet-driven remeshing.
- A rotating physical capsule beam and dynamic sphere model the first rotor strike, with finite mass/inertia on both sides. The tested0.6rad, full-turn and reverse-turn cases transfer motion and torque; rotor speed falls from144rad/s to about92.18rad/s. Their angular and energy receipts balance.

The replay does not establish elastic-force prediction, persistent contacts, simultaneous contact iteration, coplanar/degenerate contact resolution, friction, arbitrary spinning shapes, shell fracture under bullets or perforation/armor. In particular, maintaining separation when bent fragments recontact remains unproved. Collision/render/aerodynamic revision publication and native authority remain separate open gates.

## Measurements and reproduction

Apple M4, 16GiB, Apple Clang21, native ARM64. Query benchmarks make20 calls per workload and verify every hit/miss. They measure individual narrow queries, not broad phase, a structural timestep or a game frame. Peak RSS is whole-process macOS `getrusage` and varies with allocator/host state.

Retained [results](2026-11-05-world-physics-continuous-results.txt) include geometry/events, close grazes, per-query medians/maxima, prior failed controls and input provenance. A separate AddressSanitizer build covers both libraries/probes where instrumented; regular project tests and Python provenance/exit regressions are recorded separately. Passing a first-contact replay does not turn the original failed full-step gate green.

Use the existing pinned/patched Bullet source/build and a separate pristine checkout of the IPC revision above:

```sh
python3 tools/physics/check_continuous.py \
  --ipc-source build/world-physics-evaluation/ipc-toolkit \
  --bullet-source build/world-physics-evaluation/bullet-conservative-experiment \
  --bullet-build build/world-physics-evaluation/bullet-conservative-build \
  --build-dir build/world-physics-evaluation/ipc-build \
  --output-dir build/world-physics-evaluation/continuous-final
```

Expected exit0 for this bounded suite. The runner rebuilds Bullet and the IPC/probe targets, records both source pins/patches, dependency revisions/archive hashes, fixture/library checksums, compiler/options, CMake caches and compile commands. The [CMake hook](world-physics-ccd.cmake) adds only research targets; it does not edit upstream source or install a package. Signal/unexpected exits are harness failures (exit2), not completed physical rejection (exit1).

The fresh review also found two input-record defects. An actual temporary CMake build regression now rejects a Bullet build whose cached source differs from the recorded source. A real-file/symlink regression now gives distinct archive fingerprints using entry-type tags. The source/build match and directory/file/link distinctions are checked before retaining final provenance. The six focused provenance tests pass after both regressions failed first.

The review independently reproduced the pre-fix38/3/167 checks and benchmarks. The corrected59-case geometry suite, sanitizer and full project suite were verified in the single author fix pass; no second review is implied.

For sanitizer reproduction, use `--asan`, separate build/output directories and the existing matching instrumented Bullet build. Build logs retain upstream predicates C-prototype warnings; no project-source warning suppression or dependency installation was used.

The next acceptance work is a force-driven, persistent multi-contact integration that uses these detections and reactions once per physical interval, preserves local nonintersection during repeated tears/recontact, and meets a measured budget. Native controller/query/lifecycle closure and actual render/aero bindings are still required before gameplay migration. No new SDK or game integration is accepted by this note.
