# Structural and geometric-contact evaluation

2026-11-05. Continues the [conservative contact experiment](2026-11-05-world-physics-conservation-followup.md) from `d83a636`. This is an isolated ARM64 research build. No new game physics or assets are installed.

## Decision

**The current Bullet path is not accepted for the shared-world migration.** The authored lattice demonstrates material yielding, load transmission and topology separation, but the geometric-contact gate fails. Conserving momentum while objects pass through each other is insufficient. Task 3 remains open, and the native ownership/query/lifecycle gate also remains open.

The passing mechanical cases are useful reusable evidence. They do not establish a calibrated aircraft, sheet-metal crumpling, perforation, aeroelasticity or a city performance budget. Chrono remains the structural/integration alternative; no Chrono build is claimed. The next collision comparison must cover all-feature continuous contact, including rotating trajectories, rather than just substituting another discrete engine.

## Authored structure and ownership

[Structure fixture](world-physics-structure-probe.cpp), [physical graph](world-physics-structure.h), [axial material law](world-physics-material.h), [material assertions](world-physics-material-probe.cpp).

The synthetic SI fixture has 20 point masses, 94 axial members and 36 surface triangles. Its five rectangular stations run from x=-1 to x=3m, with y=±0.4m and z=±0.12m. Two chassis stations weigh 200kg together; three wing stations weigh 16/8/4kg. Cross-bracing through the 0.24m depth provides coarse geometric bending. This is a spatial lattice, not an elastic cloth substituted for a spar.

| Property | Explicit fixture input |
| --- | --- |
| Member roles | Chassis, mount, spar, skin |
| Young's modulus | 70GPa; synthetic engineering-scale test input, no named alloy or measured asset provenance |
| Yield stress | 120MPa mount; 250MPa other members |
| Isotropic hardening | 1% of Young's modulus |
| Member cross-section | 20mm² mounts; 100mm² other members |
| Accumulated plastic strain at fracture eligibility | 0.025 mount; 0.08 other |
| Fracture work per cut cross-section | 25kJ/m² |
| Reserved fraction of plastic work | 0.2 |
| Nominal skin thickness | 1mm authoring metadata; no thickness-resolved plate constitutive/contact law |
| Collision margin | 10mm; separate numerical margin, not a claim of 10mm metal |

All values are estimates chosen for this feasibility fixture, not material certification or a Learjet model. Lumped point masses are explicit; the lattice's bar and surface metadata do not add a second copy of mass. No fatigue, buckling, plastic plate bending, mesh-objective crack propagation, thermal coupling or armor calibration is implemented.

One custom force owner holds **per-member** material state across all assets. Bullet groups Lagrangian forces by force-type enum; creating several spring-force objects with different coefficients would silently reuse the first object's law. The fixture therefore registers a single owner and applies equal/opposite central forces to each member's current endpoints before the shared contact solve. It uses the previously established explicit/nonprojected integration policy.

Material state updates once at the physical step boundary, not on each iterative force query. Fracture removes failed load paths and any surface triangle whose physical edge failed. Connected components then become separate Bullet bodies. Each original mass point belongs to exactly one component and retains its position, velocity and mass. No separation kick, health subtraction, global momentum correction or independent impact-energy budget is added.

The same surviving triangles define the headless canonical surface data available to visual/aerodynamic consumers and the rebuilt Bullet collision faces. Tests inspect actual Bullet face positions, ray hits through surviving triangles and misses through a torn region/gap. There is no game render adapter or aerodynamic-force model in this experiment. Whole-triangle removal is coarse authored topology, not arbitrary bullet-sized remeshing. No new fracture edge surfaces or caps are generated.

## Constitutive work accounting

The bar uses engineering strain ε=L/L₀−1, signed plastic strain εp and accumulated plastic strain α. With modulus E, hardening H and yield stress σy, the return increment is:

`Δγ = max(0, |E(ε−εp)| − (σy+Hα)) / (E+H)`.

The signed increment updates εp and the nonnegative increment updates α. Recoverable elastic and hardening energies are `½E(ε−εp)²AL₀` and `½Hα²AL₀`. Plastic work `σyΔγAL₀` is partitioned into a fracture reserve and dissipated work. The fixed-strain return projection also removes `½(E+H)Δγ²AL₀`; this discrete projection loss is tracked separately and included in dissipation, not presented as calibrated physical heat.

A member may fracture only after its strain criterion **and** available stored/reserved work meet `Gc×A`. That cost is spent once; remaining released energy is dissipated, without an impulse. Already dissipated work cannot purchase another fracture. Broken members retain their diagnostic state and exert no force.

Fifteen literal tests cover elastic force/energy, unloading, plastic set, hardening, compression, work partition, insufficient fracture work, repeated fracture sampling, and invalid/overflow inputs. An overflow test initially failed because a finite extreme length could partially mutate state; the return map now commits transactionally only after finite output validation. These are bar-law checks, not validation of a calibrated ductile sheet material.

The coupled fixture records kinetic + current elastic + hardening + reserve + material dissipation + fracture work. It rejects sampled energy gains beyond its declared tolerance. Unresolved contact/numerical losses are not all converted to a physical heat field; a complete thermodynamic ledger remains future work.

## Wall clips, separation and inertia

The wing travels at 120m/s past a 0.44×0.4×4m obstacle, normally and at a 0.4rad obstacle angle, plus an actual clear miss. A finite 10000kg wall is held by a fixed constraint to a static empty support. Constraint feedback supplies an independent external force/torque receipt. This is an explicitly supported building fixture, not an immovable moving/native counterpart.

Tests run at 19200, 38400 and 76800Hz for 0.16s. All six clips sever the sixteen mount members, produce a 200kg parent and 28kg detached wing, retain positive geometric inertia and lose assembly forward speed. All misses retain one intact 228kg structure. A slower 30m/s exploratory clip bent and partially broke the mount without fully separating it; that observation is retained locally and was not reclassified as a full tear.

At 38400Hz, the normal clip loses about 5.34m/s of assembly forward speed; the oblique clip loses about 4.52m/s. Mount force peaks around 2.75kN per loaded member. Root momentum and angular momentum changes match the integrated member force/torque receipts near floating-point error. The wall-supported system's linear residual is below 1e-9N·s in these runs, and the largest sampled angular residual is about 0.004N·m·s. Tolerances are 0.01N·s and 0.1N·m·s; energy gain allowance is 2% + 0.01J. No gain above numerical roundoff was observed here.

The surviving chassis has Izz near its nominal 82kg·m²; detached wing inertia is recomputed from its deformed point masses. A separate unequal-mass split checks literal component masses 6/15kg and centroidal Izz=17/6kg·m². Snapshot/rebuild checks retain all original mass points and velocities, including angular momentum, through repeated tears.

After an oblique tear the surviving chassis can briefly accelerate by about 0.05m/s through elastic recoil while the assembly slows. The test reports this separately; it does not impose extra slowdown on each individual fragment.

The strong-impact outcome is not fully converged: normal-incidence face counts and fragment shapes differ across the tested rates. The passing assertions establish bounded reactions, state retention and detachment, not timestep-independent fracture geometry. Finite-margin friction is still unproven; these whole-world cases use zero friction.

## Geometric-contact gate: failed

| Case | Observation |
| --- | --- |
| 1000m/s, 10mm-radius shot through intact panel | No contact impulse at 480Hz or 19200Hz; contact at 192000Hz |
| Moving thin panel crossing a stationary shot | Miss at 480Hz; contact in refined controls |
| Shot through open space or an existing torn triangle | No phantom hit at all three rates; actual Bullet face rays agree |
| 144rad/s thin blade, target between endpoint orientations | Miss with 1 and 20 substeps over a 0.6rad sweep; contact with 100 substeps |
| Two thin deformables crossing between structural nodes | Pass through despite conserved momentum |
| Same crossing within one connected structure, self-collision enabled | Pass through; fixture verifies that both physical triangles exist |
| Separated fragments recontacting | Impulse transfer occurs, but current triangles intersect in 766 sampled frames |

The geometric suite deliberately returns **exit 1**, currently 42/50 checks passing. Positive fine-step controls show that the bullet/blade fixtures can generate real impulses; passing ray tests alone is not accepted as physical collision response. More substeps alone are not an established workload or general collision guarantee.

Fragment recontact uses independent segment/triangle intersection tests on current geometry. A negative mean-plane gap alone would be ambiguous for bending finite patches. The local test confirms actual intersection in this case. Its predicates exclude coplanar/degenerate edge cases; these remain additional acceptance work, not presumed successes. Empty-space shot controls represent a clear material miss, not a complete authored cabin/interior.

## SDK and fixture corrections retained

The [structural lifecycle patch](bullet-structure-lifecycle-experiment.patch) is separate from the earlier [contact correction](bullet-conservative-contact-experiment.patch). It retains upstream notices and marks alterations.

1. **Disconnected BVH roots:** The upstream bottom-up builder stops making progress when remaining roots have no adjacency. The fixture hung; a process sample located the loop. The correction pairs disconnected tree roots, creating no physical edge or phantom surface. A [focused probe](world-physics-face-tree-probe.cpp) tests empty, single, multiple and odd-count patches with face/gap rays. The pristine library times out; the corrected library passes.
2. **Topology with unchanged particle count:** Several simultaneous splits/removals can leave the same total particle count but a different body count/order. The old solver failed to resize body-indexed constraint arrays/update IDs, corrupting memory; the 32-structure run aborted. Reinitialization now refreshes on body-layout change and explicit topology invalidation. The same benchmark exits 0 after correction.
3. **Static obstacle setup:** The original research helper initialized only the world pose, leaving a static body's interpolation pose at the origin. Predicted face contacts use that pose. Contact/fracture assertions failed before switching initialization to `setCenterOfMassTransform`; the corrected benchmark exercises real contacts. This was a fixture error, not evidence of an inherent static-collision limitation.

These corrections do not repair the failed geometric gate. No upstream patch submission, normal plugin dependency change or production engine selection is implied.

## Host measurements

Apple M4, 16GiB, native ARM64 Release, double precision, Apple Clang 21, C++17. The benchmark uses the measured 38400Hz fixture rate, 0.16 simulated seconds, simultaneous 120m/s clips against immovable walls, and includes authoring-state preparation, stepping, synchronization checks and rebuilds. Timings include fixture overhead and are not a renderer/game budget. Memory is whole-process peak RSS reported by macOS `getrusage`, not incremental bytes per vehicle.

| Workload | Initial points / members / triangles | Contact records / peak fragments | Measured cost |
| --- | --- | --- | --- |
| 1 structure | 20 / 94 / 36 | 4 / 2 | About 0.020s for 0.16 simulated seconds |
| 32 structures | 640 / 3008 / 1152 | 128 / 64 | About 3.30s for 0.16 simulated seconds; about 20.6× real-time cost |

The 32-structure sample had 742 rebuilds taking about 0.023s altogether, 536µs per measured step, roughly 16.1MB peak process RSS, and no native rigid constraints. Counts are exposed in the retained output. The single-wing supported-wall cases additionally have one fixed constraint. Exact timings vary with host load. **No city-scale rate or detail policy is accepted.**

## Existing robust-contact work to evaluate

[IPC Toolkit](https://github.com/ipc-sim/ipc-toolkit/blob/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99/README.md) supplies contact potentials and linear/nonlinear continuous collision detection, but explicitly is not a physics solver. Its [MIT license](https://github.com/ipc-sim/ipc-toolkit/blob/54d58ebcf1f6dd0cf95bcc58afd79839ca603a99/LICENSE) makes it a candidate to investigate; dependencies need their own review. The [nonlinear trajectory documentation](https://ipctk.xyz/tutorials/nonlinear_ccd.html) covers curved trajectories of linear primitives. The authors' [Rigid IPC work](https://ipc-sim.github.io/rigid-ipc/) addresses conservative curved rigid-motion contact and reports a performance cost relative to alternatives.

**Inference:** These projects directly address the feature/trajectory coverage missing in the failed tests and are better starting points than writing another bullet/wing-specific hit rule. They have not been built or integrated here. A CCD query alone does not supply conservative contact response, material fracture, coupled joints or native ownership. Any reused code must feed one authoritative solve and pass the same reaction/topology tests. Chrono's coupled FEA/plastic beam capabilities remain a separate structural comparison, as described in the [prior-art audit](2026-11-05-world-physics-prior-art.md).

## Reproduction

Use a separate checkout at Bullet `d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`, then apply the two retained experimental patches with `git apply --unidiff-zero`. Do not apply these to the installed game.

```sh
python3 tools/physics/check_conservation.py \
  --source build/world-physics-evaluation/bullet-conservative-experiment \
  --build-dir build/world-physics-evaluation/bullet-conservative-build \
  --output-dir build/world-physics-evaluation/structure-final \
  --explicit-forces --structure
```

Expected overall exit: **1**, because geometric contact is rejected. Other outputs are separate: original five-case fixture, 44 analytical contacts, 70 whole-step checks, 15 material checks, six BVH configurations, 71 structural checks and the benchmark. The runner rebuilds the library, records source patch/checksums/compiler/options and preserves individual exits. A build/harness failure returns 2; it is not a physical failure.

[Retained results](2026-11-05-world-physics-structure-results.txt) include pass/fail cases and raw measurements. A fresh reviewer independently reproduced the 15 material checks, six BVH configurations, then-current 66 mechanical checks, 44 analytical contacts, 70 step checks, the 42/50 rejection and benchmark modes. The review identified a reporting issue: signal-terminated probes shared aggregate exit 1 with completed physical rejection. Five real-child-process tests now enforce exit 2 for signals/unexpected exits, with failure taking precedence over physical rejection.

A separate final self-audit reproduced four NaN position/velocity cases falsely passing synchronization because comparisons against NaN are false. Explicit finite/binding validation now rejects them; the mechanical suite is 71/71 including restored-state acceptance. These two safeguards were fixed in one tested pass, without a second review. The current native, render/aero, calibrated-shell and production-suitability exclusions remain the acceptance decisions; their cost if assumed away is stale geometry, duplicate integration or unjustified physical behavior.

The original project `make test` suite passes. AddressSanitizer runs of mechanics, the 32-structure lifecycle workload and the deliberately failing geometric gate report no address errors after the lifecycle correction. This is not proof against every memory/lifecycle defect. The bounded native ownership regression still has its expected guarded passes and unguarded failures. Ten Python runner/provenance regressions pass, separately from `make test`. The reviewer did not independently rerun these author-executed sanitizer/native/project-suite checks.

Full provenance, initial red runs, the hanging-process sample and sanitizer builds remain under ignored `build/world-physics-evaluation/structure*`. No live gameplay observation is inferred from this headless fixture.

Subsequent work: the [continuous-contact comparison](2026-11-05-world-physics-continuous-contact.md) now builds IPC Toolkit and proves bounded prescribed-trajectory/first-contact events. The failed persistent/full-step gate in this structural note remains unaccepted.
