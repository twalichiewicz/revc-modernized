# Controlled aircraft/building slice implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline. Preserve the existing feasibility workspace and physics tests. One fresh final reviewer checks the completed evidence/decision milestone; no implementer delegation.

**Goal:** Prove the host contracts for one aircraft/wing and one building before producing and profiling an experimental in-app physics build.

**Architecture:** The complete island has one motion owner and one geometry revision consumed by contacts, world queries, rendering and aerodynamic loads. First test the existing macVC 1.0.5217 binary boundary. If it cannot establish those contracts, choose source integration and state the required engine capabilities before doing more solver work.

**Tech stack:** Existing C++17 ARM64 plugin, pristine macVC 1.0.5217, repository native-instruction harness and existing isolated Bullet/IPC experiments.

**Spec:** `docs/superpowers/specs/2026-11-05-world-physics-design.md`, narrowed by the user's controlled-slice instruction on 2026-11-05.

## Global constraints

- No solver development unless an observed slice failure demonstrates the need.
- Preserve all existing physics tests, including retained failing controls and numerical limits.
- No production app replacement or broader world migration under this milestone.
- Copied native instructions with controlled callbacks are offline compatibility evidence; they are not a live-game ownership or performance measurement.
- Failure to prove a contract prevents experimental solver handoff; do not silently substitute cosmetic deformation, shared-model edits, kinematic dynamic mirrors or another health branch.

## Review focus

- Direct/inlined motion writes after solver publication, including native route controllers and stuck recovery.
- A torn wing remaining in LOS, vertical probes, collision or broad-phase bounds while it disappears visually.
- Two instances sharing one model COL, including reentrant queries and cached triangle planes.
- Recycled pool identities, restart, repair, teardown and stream recreation invalidating island bindings.
- Profiling solver-only research or failed intervals as if they measured an accepted in-app slice.

### Task 1: Reproduce the host boundary

**Files:** Create `docs/research/world-physics-slice-native-probe.cpp`, `tools/physics/check_slice_contracts.py` and `docs/research/2026-11-05-aircraft-building-contracts.md`. Reuse the existing native harness without weakening tests.

**Interfaces:** Read the pristine binary from `--binary`; write raw disassembly, fixture build/run logs and identity/provenance to `--output-dir`. Fact reproduction and contract acceptance have separate results. Failed host requirements must remain failed requirements.

- [x] Rerun the existing guarded/unguarded native world ownership control.
- [x] Execute native per-entity contact and sector query callers against two same-model aircraft and a building. Observe actual geometry selection; compare with an instance-local replacement. Preserve a deliberate missing-authority failure.
- [x] Inventory actual CPlane pose/velocity writers, query paths, rendering publication and lifecycle hooks with exact addresses and scope limits. Do not infer full coverage from symbol presence.
- [x] Run existing relevant native/assets/geometry tests and the normal regression suite. Retain the existing solver source/test hashes.

Result: twelve fact assertions pass; three native per-instance authority assertions fail. Motion/publication/lifecycle remain unaccepted. See `docs/research/2026-11-05-aircraft-building-contracts.md`; these results do not constitute an in-app proof.

### Task 2: Make the integration decision

**Files:** Complete the contracts note and update the feasibility plan/progress ledger.

**Interfaces:** Produce a four-contract capability matrix with reproduced facts, missing capabilities and an explicit go/no-go decision.

- [x] Decide whether current binary integration proves exclusive motion, authoritative query geometry, coherent geometry writeback and lifecycle ownership for this island.
- [x] If not, choose source integration or a source-implemented host API and name the exact missing facilities, matching-build/source requirements and reopen criteria. Preserve the working plugin and freeze custom solver expansion.

Decision: require matching engine source or a source-implemented host bridge. Task3 is pending that dependency, not complete. No claim that all possible future binary patches are impossible.

### Task 3: Experimental app and measurement (conditional on all four gates)

This task stays pending if Task 2 rejects the host boundary; a research executable is not an experimental game build.

- [ ] Bind one generation-valid Dodo (model181, existing CPlane path) and one identified building instance. Confirm actual asset geometry and contact location before ownership transfer; no additional native moving participant may be treated as fixed.
- [ ] Add scoped phase traces: actor/island identity, owner, native integration/impulse counts, fixed-step count, pose/velocity checksum and render/COL/query/aero revision. Cover hit, miss, wing separation, repeat contact and failed initialization.
- [ ] Exercise deletion, pool reuse, restart, teleport, repair and stream-out/recreation. Reject stale/nonfinite publication before host reads it. Explicitly account for any wing debris as a new dynamic participant.
- [ ] Produce a separately named experimental app with a reproducible build manifest, leaving the installed app intact. Run real in-app cases and capture per-phase traces.
- [ ] Measure warm/cold p50/p95/p99/max CPU frame and fixed-step costs, query and publication costs, allocations, contact/triangle counts and rejected/late steps. Compare an otherwise identical native baseline and 30/60/120 render pacing where supported; state host/tool settings and numerical limitations. Do not report an offline solver benchmark as app performance.

### Evidence/decision review

- [x] Use one fresh final reviewer for the evidence/decision milestone, address material findings once with verification, then report the actual gate status. Keep this plan and the feasibility workspace because Task3 and the in-app milestone remain pending.

Fresh review of `c7fe3b7..73fcdb6`: no findings; native query reproduction confirmed. No fix pass or re-review needed. This closes the evidence/decision work, not the four host gates or the requested in-app milestone.
