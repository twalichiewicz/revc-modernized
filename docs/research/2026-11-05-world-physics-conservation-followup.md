# Conservative contact follow-up

2026-11-05. Continues the [initial evaluation](2026-11-05-world-physics-evaluation.md) at repository baseline `ad503e7`. This is an isolated CPU mass-spring/contact prototype, not an installed replacement for macVC physics or a final SDK selection.

## Result

The selected experimental configuration passes 44 analytical contact cases, one current-geometry force case, 70 whole-step checks, and the original five-case wing/contact fixture. The native scheduling investigation is [reported separately](2026-11-05-world-physics-native-ownership.md).

These results establish a conservative **bounded prototype** for the tested explicit spring/damping and contact configuration. They do not establish plastic metal, tearing, thin/angular CCD, self-collision, calibrated vehicles, heterogeneous material laws, city performance or the game bridge.

## Changes and evidence

The earlier one-operand experiment balanced an unequal-mass impulse but could increase energy. It remains retained as a failed diagnostic; the new [experimental patch](bullet-conservative-contact-experiment.patch) replaces the relevant mass/contact calculations in a separate checkout of the same pinned Bullet revision, `d3afe76132fc76b0f9b2359cbaef47a3766e7fbb`.

1. **Face/node contact:** Compute inverse effective mass as `W = inverseMassNode + sum(barycentric_i² × inverseMassVertex_i)`. The inelastic normal impulse is `max(0, -relativeNormalSpeed) / W`. Each recipient receives its barycentric share using its own inverse mass; friction is bounded by the normal impulse and the tangential stopping impulse. An optional overlap penalty uses a scalar backward-Euler gap spring with bounded recovery velocity. This is contact stabilization, not an authored metal material law.
2. **Rigid/face contact:** Use the same barycentric Jacobian/effective mass consistently in contact assembly and per-vertex velocity distribution. The previous velocity weights and effective inverse mass did not preserve the transferred momentum in these cases.
3. **Force/solve order:** Temporary phase instrumentation measured balanced contact transfer, followed by a momentum change during the soft-only post-contact solve. For example, one sampled contact had a contact residual around `9e-16 N·s`, followed by `0.00286 N·s` in the later phase. The [explicit policy](world-physics-explicit-policy.h) evaluates spring and damping impulses before the shared contact solve, then retains the solved contact velocities. It does not apply a global velocity correction. Implicit stepping and projected strain damping are rejected for this policy rather than silently treated as supported configurations.
4. **Collision flags do not remove mass:** A three-participant test exposed impulses disappearing when a collision-marked node was treated as immovable in the following contact. Only actual zero inverse mass represents a fixed recipient in the corrected contact operation. The test's maximum momentum residual fell from roughly `3 N·s` to roundoff, while the third body still receives motion and the sphere remains outside the contacted surface.
5. **Current geometry:** The explicit spring policy uses current node positions for spring direction, not a possibly stale predictor. An isolated stale-predictor case initially produced `10.24 N` force error and `10.19 N·m` torque error; the corrected force matches its independent reference with zero reported error.

The source instrumentation used to locate the post-contact change was removed. The whole-step fixture now measures phase transfers through the existing before/after solver callbacks, without modifying the library for logging.

## Verification

| Experiment | Unmodified source / original policy | Corrected source / explicit policy |
| --- | --- | --- |
| Analytical face/node contact | 12/44 pass | 44/44 pass |
| Current-geometry force regression | Initial explicit policy failed | Pass |
| Whole-step suite | Closed-system rigid/sheet and unequal-sheet cases fail | 70/70 pass |
| Original wing miss/strike and contact checks | Original physical acceptance fails | 5/5 pass |

The analytical contact cases vary recipient mass, unequal vertex masses, fixed node/face/vertex supports, two contact locations, orientation, separating motion, sliding friction and sticking friction. They compare actual per-recipient impulses, linear/angular balance including fixed-support reactions, final velocities and kinetic energy against independent point-contact equations. Literal reference impulses cross-check the oracle.

The whole-world fixtures use **zero friction**. Analytical friction cases use coincident contact points, avoiding a couple across a collision margin. Passing them does not establish finite-thickness or finite-margin friction/angular conservation; that remains a separate structural/contact gate.

The whole-step cases independently vary registration order, contact location, uniform/nonuniform node masses and timestep. They include rigid/sheet contact, two sheets, a rigid body contacting two sheets, and a real miss. Mass, linear/angular momentum and kinetic plus link-strain energy are measured after every step. Per-recipient momentum changes are also collected around the primary solver and between that solver and the end of the step, separating primary contact response from later soft/soft response. These are aggregate recipient impulses in fixed fixtures, not yet a production per-contact stress-event API.

The supported configuration's largest sampled closed-system linear residual is on the order of `1e-14 N·s`; the largest sampled angular residual is about `0.00108 N·m·s`. Primary/secondary phase linear residuals are near roundoff. No sampled kinetic-plus-link-strain gain above the initial value was observed. Damping/contact losses are permitted; this is not a complete accounting of dissipated heat or penalty-spring energy.

Declared whole-step tolerances remain explicit in the fixture: linear residual below `1e-6 N·s + 1e-6 × |P_initial|`; angular residual below `0.01 N·m·s + 0.01 × |L_initial|`; energy gain below `0.01 J + 0.01 × E_initial`; mass error below `1e-9 kg`. The sphere's signed clearance is checked against current local triangles throughout the trajectory, with a 0.02 m penetration allowance. A sheet's mean height is not a valid substitute when it bows. Soft/soft separation still uses the final mean-plane check and needs stronger local intersection coverage in the structural gate.

The timestep study uses 480/960/1920 Hz; the three-participant refinement additionally uses 3840/7680 Hz. For the latter pair, final sphere position changes by about 0.00336 m and velocity by 0.00561 m/s, within the declared 0.01 m / 0.05 m/s refinement bounds. These are test rates, not a chosen default for the city.

## Reproduction and provenance

Retained [results](2026-11-05-world-physics-conservative-results.txt) include physical exit codes. All build/run commands use C++17, native ARM64, double precision and the same CPU build options as the initial evaluation. No system dependency install, game patch or game launch is required.

Starting with the pinned pristine checkout from the initial evaluation:

```sh
git clone --shared --no-hardlinks build/world-physics-evaluation/bullet3 build/world-physics-evaluation/bullet-conservative-experiment
git -C build/world-physics-evaluation/bullet-conservative-experiment apply --unidiff-zero "$PWD/docs/research/bullet-conservative-contact-experiment.patch"
python3 tools/physics/check_conservation.py \
  --source build/world-physics-evaluation/bullet-conservative-experiment \
  --build-dir build/world-physics-evaluation/bullet-conservative-build \
  --output-dir build/world-physics-evaluation/conservative-final \
  --explicit-forces
```

Expected checker exit: **0**. It always rebuilds the library before compiling fixtures, records the actual source diff against `HEAD` (including staged and working edits) and its SHA-256, fixture/header hashes, compiler identity, options and physical exit codes. Untracked nonignored source inputs are rejected until staged so they can be reconstructed. This prevents a changed inline header being tested only in the fixture while the world library remains stale. Original-source comparison uses `--source .../bullet3 --build-dir .../bullet-build` without `--explicit-forces`; physical failures and exit 1 are expected.

Source-record regression tests run with `python3 tests/test_physics_probe_provenance.py` after the pinned checkout exists. They use real local clones and replay the generated patch into an independent pristine checkout; only the unrelated CMake boundary is stopped. This checks staged, working, mixed and newly indexed source files plus untracked-input rejection without rebuilding the SDK for each metadata test.

The current prototype remains deliberately narrow: all fixture spring coefficients are 100 N/m and damping is 0.1 kg/s. Bullet groups force objects by force type, so assigning a different force object to each body does not establish different material parameters per body. The next structural implementation needs one force owner with explicit per-body/member material state. Real thin metal can also require much smaller steps or a different constitutive/integration path; the tests do not establish a suitable stiffness or performance budget.

The game, native binary, installed plugin and user settings are unchanged. The next gate is an authored load-bearing structure with real persistent plastic state, fracture, synchronized collision/render/aerodynamic topology and measured numerical cost.
