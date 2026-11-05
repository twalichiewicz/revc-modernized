# First local world-physics feasibility experiment

2026-11-05. This is an isolated decision experiment, not a game modification, engine selection, or completion of the [shared-world design](../superpowers/specs/2026-11-05-world-physics-design.md).

Follow-up: the [conservation investigation](2026-11-05-world-physics-conservation-followup.md) retains a stronger corrected prototype and its passing bounded contact checks. The original failures and minimal patch below are preserved as the initial evidence, not the latest candidate's status.

## Outcome

Bullet's CPU libraries compile and run natively on this Apple M4 / 16 GiB host with Apple Clang 21.0.0, CMake 4.4.4, C++17 and double precision. Rigid breakage/contact and elastic surface contact can run in the same `btDeformableMultiBodyDynamicsWorld`. **The unmodified candidate fails the initial contact fixture.** An isolated analytical test identifies an unequal-mass impulse error; a separate experimental checkout with that one-line correction and projection enabled passes all five initial world cases. A stronger light-face contact test then exposes energy gain despite balanced momentum. Keep this as diagnostic evidence, not an accepted fix, SDK selection or full design acceptance.

No production files, game binary, installed plugin, preferences or saves were modified by this experiment. Checkout, static libraries, executables and working logs are in ignored `build/world-physics-evaluation/`. The retained [fixture source](world-physics-probe.cpp) and [raw results](2026-11-05-world-physics-probe-results.txt) make the observations reproducible.

## Scope and setup

Upstream revision: `d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`. CPU-only Release build, ARM64, minimum macOS 15.0. The baseline uses unmodified upstream code. The fixture uses explicit stepping at 1/480 second, 40 solver iterations, zero gravity, no air density, zero contact friction and no restitution. This timestep is an experimental setting, not a proposed city-wide default.

All participating soft nodes are free. Sheets are 2 m square, 9 × 9 nodes, mass-spring stiffness 100, damping 0.1, bending connections, 0.03 m collision margin, with `SDF_RD | SDF_RDF | VF_DD` collision flags. These are deliberately simple elastic samples, not calibrated vehicle metal. Momentum is the sum of rigid mass × velocity and each soft node's velocity / inverse mass. Static-wall reaction impulses are accounted separately in the rigid case. The reported error is final linear momentum error, not a measurement of all transient, angular or energy errors.

| Fixture | Result under baseline settings | What it establishes |
| --- | --- | --- |
| Rigid wing misses wall | No failure, slowdown or reaction; PASS | This configuration does not manufacture a strike for a clear miss. |
| Rigid wing clips wall at 50 m/s | Joint breaks; 0.555556 m/s chassis slowdown; 2702.992514 N·s wall impulse; linear momentum residual rounds to zero; PASS | Ordinary rigid contact loads a constrained pair, affects the chassis and breaks a configured connection. |
| 1 kg sphere hits 20 kg sheet at 5 m/s | Shape change 0.132770 m; final linear momentum error 0.908488460 N·s versus 0.05 tolerance; FAIL | Contact and elastic deformation occur, but this configuration fails conservation. This sphere is not a bullet/penetration test. |
| Two 1 kg sheets with opposite 1 m/s velocities | Positive final mean-plane separation and changed motion; momentum residual rounds to zero; PASS | A symmetric deformable contact example works. Symmetry makes this a weak conservation check by itself. |
| 1 kg sheet at −1 m/s meets 2 kg sheet at +0.25 m/s | Positive final mean-plane separation; momentum error 0.692458044 N·s versus 0.05 tolerance; FAIL | The symmetric result does not generalize to unequal inputs. |

The rigid model uses a 900 kg chassis, 100 kg wing and a deliberately arbitrary 500 N·s breaking-impulse setting. The wall is static. It does not test spar yielding, a deformable wing root, change in aerodynamic area, angular-momentum conservation, thin-wall CCD, rotary blade collision or fracture topology. Rigid bodies participating in a deformable/multibody-capable world do not establish that reduced-coordinate multibody mechanisms were tested.

## Controlled follow-ups

Each variant changes one setting from the baseline; duration and acceptance tolerances stay fixed. All eight invocations exit 1 because at least one physical check fails.

| Variant | Rigid/sheet momentum error (N·s) | Unequal-sheet momentum error (N·s) |
| --- | ---: | ---: |
| Baseline | 0.908488460 | 0.692458044 |
| Disable split impulse | 0.875365803 | 0.692458044 |
| Zero spring damping | 0.953817948 | 0.692458044 |
| Disable face/rigid contact | 0.000000000, **sphere passes through** | 0.692458044 |
| Implicit integration | 12.558604653 | 0.692458044 |
| Enable projection | 0.021969265, within tolerance | 0.692458044 |
| Zero deformable ERP | 0.847093047 | 0.692458044 |
| Half timestep, same duration | 0.916165440 | 0.668408993 |

**Observation:** Projection improves this rigid/sheet conservation check. Removing face contact preserves momentum by losing the collision entirely, which remains a failed case. Smaller steps and zero damping do not resolve the unequal-sheet failure.

The tolerances were not widened to manufacture success. Do not fix a coupled-world error with an after-step global velocity correction that hides its cause. The rigid/sheet residual's full numerical cause remains unproven; projection bringing it within this tolerance does not establish exact conservation at every contact.

## Unequal-mass impulse diagnosis

The [single-contact probe](world-physics-contact-probe.cpp) invokes the actual face/node repulsion operation with one known contact. There is no world stepping, damping, gravity, spring force or competing contact. A 1 kg incoming node contacts three face nodes of 1 kg each (control), 2 kg each, or 0.1 kg each, at barycentric weights (0.2, 0.3, 0.5). It measures linear/angular momentum against an independent impulse-at-point reference and checks that a frictionless inelastic operation adds no kinetic energy. The reference also computes the effective-mass inelastic result independently.

The pinned implementation's normal update multiplies every face recipient by the **incoming node's** inverse mass. Its tangential update instead uses each face node's own inverse mass. With unequal masses the normal operation cannot balance the node's impulse. This is both [direct source evidence](https://github.com/bulletphysics/bullet3/blob/d3afe76132fc76b0f9b2359cbaef47a3766e7fbb/src/BulletSoftBody/btSoftBody.h#L1367-L1406) and an executed operation, rather than attributing a whole-world symptom from source inspection alone.

| Single normal-contact operation | Original | Experimental correction |
| --- | --- | --- |
| Equal masses | Linear/angular error rounds to zero; PASS | Linear/angular error rounds to zero; PASS |
| Heavy face | 0.724637681 N·s linear, 0.422532746 N·m·s angular error; FAIL | Linear/angular error rounds to zero; PASS |
| Light face | 0.652173913 N·s linear, 0.380279471 N·m·s angular error; FAIL | Linear/angular error rounds to zero, but energy rises from 0.5 J to 1.035601764 J; FAIL |

The [experimental patch](bullet-unequal-contact-experiment.patch) changes only that recipient inverse mass. It preserves upstream notices and is clearly labeled a local experiment; no upstream submission or production dependency update was made. A separate local checkout, `build/world-physics-evaluation/bullet-contact-experiment/`, and separate CMake output, `bullet-contact-build/`, preserve the original source/libraries for comparison. The [world results](2026-11-05-world-physics-patched-results.txt) and [isolated contact results](2026-11-05-world-physics-contact-results.txt) retain exit codes.

With this patch, the unequal-sheet test's linear error rounds to zero, while the baseline rigid/sheet case still fails at 0.908488460 N·s. Combining the patch with projection enabled passes all five initial world cases: the rigid/sheet error is 0.021969265 N·s (about 0.44% of the initial 5 N·s, below the unchanged 0.05 threshold), and unequal-sheet error rounds to zero. This isolates a cause of the asymmetric failure. The stronger light-face case explicitly shows why fixing impulse distribution alone is insufficient: impulse magnitude still reflects an equal-mass approximation, so the contact can inject kinetic energy. **The full contact gate remains failed.** Constrained nodes, friction, self-contact, angular response over time and coupled fracture are also unvalidated.

## Reproduction

From the repository root, obtain the pinned checkout in a fresh scratch directory:

```sh
git clone --depth 1 https://github.com/bulletphysics/bullet3.git build/world-physics-evaluation/bullet3
git -C build/world-physics-evaluation/bullet3 fetch --depth 1 origin d3afe76132fc76b0f9b2359cbaef47a3766e7fbb
git -C build/world-physics-evaluation/bullet3 checkout --detach d3afe76132fc76b0f9b2359cbaef47a3766e7fbb
cmake -S build/world-physics-evaluation/bullet3 \
  -B build/world-physics-evaluation/bullet-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DCMAKE_CXX_STANDARD=17 \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DUSE_DOUBLE_PRECISION=ON \
  -DBUILD_CPU_DEMOS=OFF -DBUILD_BULLET2_DEMOS=OFF \
  -DBUILD_OPENGL3_DEMOS=OFF -DBUILD_EXTRAS=OFF \
  -DBUILD_UNIT_TESTS=OFF -DBUILD_PYBULLET=OFF -DINSTALL_LIBS=OFF
cmake --build build/world-physics-evaluation/bullet-build \
  --target BulletSoftBody BulletDynamics BulletCollision LinearMath --parallel 4
xcrun clang++ -std=c++17 -O2 -Wall -Wextra -arch arm64 \
  -mmacosx-version-min=15.0 -DBT_USE_DOUBLE_PRECISION \
  -isystem build/world-physics-evaluation/bullet3/src \
  docs/research/world-physics-probe.cpp \
  build/world-physics-evaluation/bullet-build/src/BulletSoftBody/libBulletSoftBody.a \
  build/world-physics-evaluation/bullet-build/src/BulletDynamics/libBulletDynamics.a \
  build/world-physics-evaluation/bullet-build/src/BulletCollision/libBulletCollision.a \
  build/world-physics-evaluation/bullet-build/src/LinearMath/libLinearMath.a \
  -o build/world-physics-evaluation/probe-final
build/world-physics-evaluation/probe-final
```

The last command is expected to print physical failures and exit 1. Run each diagnostic by passing `no-split`, `no-damping`, `node-only`, `implicit`, `projection`, `zero-erp` or `half-step`. Do not interpret a wrapper that captures these statuses and itself exits 0 as the fixture passing. Build success and physical acceptance are separate results.

For the isolated impulse diagnostic, substitute `world-physics-contact-probe.cpp` as the compiler input and a different executable output; both original and patched versions exit 1 with the complete three-case fixture. To reproduce the corrected-distribution experiment, clone the pinned checkout into `bullet-contact-experiment/`, apply the retained zero-context patch with `git apply --unidiff-zero`, and build the same four CMake targets in `bullet-contact-build/` with the same options. Compile **both the fixture and all libraries** against that checkout. The patched world probe with argument `projection` exits 0 for its five initial cases. The patched world probe without projection still exits 1. Merely changing the fixture's header include path while linking the original libraries is not an equivalent experiment.

## Decision

Keep Bullet as a provisional coupled-world candidate. Resolve effective-mass/energy consistency across mass ratios, then extend contact tests to friction, angular accounting and structural attachments. Demonstrate actual plasticity/fracture and high-speed thin contact before selection. Compare Chrono if connected shell/beam mechanics remain the decisive missing piece; Jolt can provide a rigid-part comparison but does not by itself meet continuous crumpling. See the [primary-source comparison](2026-11-05-world-physics-prior-art.md) for platform, licensing and feature boundaries.

The host ownership/geometry bridge is a separate feasibility gate even if every headless test passes. No city-scale performance, gameplay, streaming, full energy balance, self-contact, thermal, fluid, ragdoll, projectile or mission compatibility claim follows from these results.

## Verification and review

Final sources compiled without diagnostics using `-Wall -Wextra` with upstream headers under `-isystem`. All eight original world variants, both patched world configurations and both three-case single-contact probes were freshly reproduced with their physical exit codes retained. An intentional no-op mutation of the contact operation fails all three cases, proving the per-recipient impulse checks require actual transfer rather than merely a zero net residual.

A fresh reviewer independently compiled the fixtures against both checkout/library sets and matched the recorded results. The review found and resolved a weak analytical-reference assertion: the final probe compares actual node/face impulses against the prescribed diagnostic shares and requires nonzero response. The reviewer did not independently rebuild the upstream libraries or run the game. No Critical/Important issues remained in the research artifacts; this is not production physics approval.
