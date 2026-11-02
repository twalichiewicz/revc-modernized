# Articulated NPC Contact Solver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Correct shared NPC ragdoll collapse, unresolved overlap, incomplete limb collision and inter-body penetration.

**Architecture:** Retain native damage/fall/get-up adapters. Share measured capsule geometry and couple anatomy to terrain, then resolve contacts between active people after native world processing.

**Tech Stack:** C++17, native arm64 macVC ABI, existing Makefile and offline native fixtures.

**Spec:** `docs/superpowers/specs/2026-11-01-ragdoll-contact-solver-design.md`

## Global Constraints

- C++17, native Apple Silicon macVC 1.0.5217, no new external dependency.
- Keep twelve active bodies and at most four 1/60-second steps per update.
- Preserve accepted native damage and existing localized hit directions/points.
- Preserve player, mission, vehicle-entry/exit, attachment and cutscene owners.
- Keep unrelated uncommitted work intact; do not stage or commit that work.

## Review Focus

- Unknown or stale car contact must never authorize passing through another obstacle.
- World projection must preserve whole-body anatomy on every intermediate frame.
- Bodies trapped between terrain and another person must retain clearance and avoid energy injection.
- Missing support or streamed geometry must not become phantom ground or a floating pose.
- Late crowd publication must respect generation/clump changes and native ownership.

---

### Task 1: Robust native sphere contacts and corner separation

**Files:** `src/reactcontact.cpp`, `src/reactcontact.h`, `tests/test_reactcontact.cpp`, optionally a focused native-adapter test.

**Interfaces:** Preserve `reactcontact::Sweep` overloads and the existing native `World` seam; extend optional query capabilities only when required to express native refinement honestly.

- [x] Add failing clear-car-envelope/real-car-contact/obstacle-behind-envelope and acute-corner escape cases; retain stale-buffer/exhaustion cases.
- [x] Run the tests and capture the expected failures.
- [x] Implement native contact refinement, bounded multi-contact separation and swept escape.
- [x] Run contact adapter tests and actual-hook consumers, including malformed queries and restored native state.

### Task 2: Shared body volumes and coupled anatomy/world solving

**Files:** create `src/reactshape.h` (and `.cpp` if appropriate); modify `src/reactphysics.*`, `src/reactself.*`, `tests/test_reactcontact.cpp`, `tests/test_reactphysics.cpp`, `tests/test_reactself.cpp`, `Makefile` as needed.

**Interfaces:** Preserve `Body::Advance`/impact entry points and `Sweep`. Expose a small measured-shape seam for crowd collision and a certified complete-pose correction method on `Body` only as needed by Task 3.

- [x] Promote review failures into maintained tests: per-frame bone length, gravity-only landing, seeded impacts, whole-shin thin-wall contact, moving geometry and recovery.
- [x] Run each new behavior against the baseline and record its failure.
- [x] Implement conservative capsule coverage, coupled constraints and complete-pose validity; remove independent final point clamping.
- [x] Verify anatomy, self contacts, skull/neck, support/bracing, nonfatal recovery, fatal settling and bounded queries at 30/60 Hz; compare release timings to baseline.

### Task 3: Contacts between bodies and native world integration

**Files:** create `src/reactcrowd.*` and `tests/test_reactcrowd.cpp`; modify `src/reactions.cpp`, `tests/test_reactionhook.cpp`, relevant hook fixtures and `Makefile`.

**Interfaces:** A bounded crowd solve consumes `Body` pointers plus each body's world-query context. Add a native late-world hook that resolves validated active bodies and publishes corrected roots after individual controls, once per world pass.

- [x] Write failing approaching-body, high-speed crossing, coincident body, pinned-against-wall and actual late-hook/root-publication tests.
- [x] Implement stable measured-volume contacts and world-certified shape-preserving correction with closing-velocity response.
- [x] Test actor caps, balanced/native owners, stale generations, replaced clumps, cutscene/load resets, repeated hook visits and death/get-up transitions.

### Task 4: Verification, review and documentation

**Files:** update this plan/spec and `README.md` only for actual resulting behavior/remaining validation limits.

- [x] Re-run the original audit plus maintained focused tests with sanitizers.
- [x] Run `make test`, `make test-feedback` (or its actual repository target), and build `build/libVCModernHUD.dylib`; report any pre-existing failure by name.
- [x] Request independent review of exact changed files against the saved baseline/spec; address Important/Critical findings with failing regressions.
- [x] Record evidence and remaining native visual/performance acceptance without implying that an offline pass establishes live behavior.

## Execution ledger

Ruling: Execute the authorized design and implementation continuously, in the current feature checkout. Its required baseline consists of uncommitted and untracked modules; a clean worktree would omit the implementation being fixed. Preserve exact per-file baselines under `build/ragdoll-fix/baseline` and leave git staging/commits untouched.

Ruling: The user explicitly asked to proceed with both design and implementation and delegated comprehensive implementation choices. Do not insert additional design/plan approval rounds.


## Final verification

The implementation is complete in the current checkout. No staging, commits,
application installation or game launch was performed. Exact source deltas and
baselines are retained in `build/ragdoll-fix`.

- `make -j3 test`: passed, including the feedback suite and new native/contact,
  solver, crowd and actual world-hook regressions.
- `make -j2 build/libVCModernHUD.dylib`: passed, no compiler warnings.
- ASan + UBSan: `reactsolver`, `reactself`, `reactcrowd`, `reactworldhook` and
  `reactcontact_native` all pass on the frozen final source.
- All twenty frozen source/test hashes match after verification; evidence is
  `build/ragdoll-fix/verification.json` and the accompanying logs.
- Captured-rig directional falls: maximum bone-length error 0.009526 m,
  zero measured joint-ground penetration, all 25 bodies remain active until
  supported sleep, with no final-second drift. Missing support wakes a corpse.
- Six controlled trajectories produce identical final poses at 30 and 60 Hz.
- Three hundred free-pair response cases have no kinetic-energy gain; maximum
  measured momentum difference is 0.00002906 in fixture units.
- Independent review corrected four Important findings and ends with spec and
  quality PASS, no remaining Important/Critical finding. The final report is
  `.superpowers/sdd/2026-11-01-ragdoll-contact-solver/final-review.md`.

The final twelve-body/600-frame standalone-body benchmark has median 0.324794 ms
per simulated frame on this host (seven samples); all twelve bodies settle.
It uses native-shaped triangle fixtures, not the actual game. The densely
interpenetrating twelve-body corridor remains expensive (about 14 ms in the
separate offline crowd stress fixture). Do not treat either result as native
frame-time acceptance. Native visual checks, including the originally reported
whole-body ground fall-through and real cop car exits, remain required.

## Follow-up installation and crash fix, November 1

The crash report used plugin UUID `0911ADFB-70FB-31E0-BDCE-BFA006310002`,
matching the installed 16:57 library. The 21:10 solver build had not been
installed. Both the prior library and executable, plus map data, were backed
up under `build/ragdoll-followup/` before running the standard installer.

The matched crash instruction at image offset `0xf160` reads a key rotation
through a null key pointer in `body::RestoreAbout`. A same-clump animation
handoff reproduced this through `aimcam::PoseArms` with exit 139. All key
posers now validate fresh native bindings before any write; ordinary and
temporary-owner preparation share guarded measurement and rollback. Tests
cover missing keys, retired frame memory, mixed replaced/missing bindings,
first-use retry, and foreign key replacement during a native rebuild.

A separate same-call vehicle handoff regression initially failed six checks:
an existing falling controller survived native entry/exit completion and
suppressed collision at the new on-foot root. Control now retires that old
controller and releases its held timer before invoking native vehicle control.
The regression passes for both native transition states. This is a reproduced
lifecycle defect, not proof of the exact cause of the reported visual clipping.

Installation verification also exposed the absent `CWorld::Process` patch
entry required by the new crowd solver. A native patch-table test failed
before adding the entry and now verifies the emitted hook and branch.

Fresh verification: `make -j3 test`, the plugin build, seven native patch-table
tests, seven ASan/UBSan targets, and independent review pass. `./install.sh`
completed successfully at 21:49. Installed plugin UUID is
`A76F6906-BE2E-3D43-B1E4-D3AE1CB8E46F`; its bytes match the verified build.
The launched game logged all reaction hooks ready. Evidence and exact hashes
are in `build/ragdoll-followup/verification.json`. Native car-exit and
ground-fall-through visual acceptance still require gameplay observation.
