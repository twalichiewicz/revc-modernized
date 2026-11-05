# Shared World Physics Feasibility Plan

> **For agentic workers:** Use superpowers:executing-plans inline, as requested by the user; no implementation agents. Track steps with checkboxes. Use one fresh final reviewer for implementation changes.

**Goal:** Establish a conservative, coupled CPU simulation candidate and a feasible macVC ownership/geometry boundary before replacing production physics.

**Architecture:** One physical world owns motion, constraints, materials and topology. The native game adapter translates intent, units, lifecycle and geometry; it must prevent duplicate native response. This plan covers feasibility only, not the entire world migration.

**Tech Stack:** C++17, native macOS ARM64, pinned CPU physics-library source, existing macVC 1.0.5217 plugin/native instruction tooling. Initial Bullet evaluation uses double precision.

**Spec:** `docs/superpowers/specs/2026-11-05-world-physics-design.md`.

## Global constraints

- Use metres, seconds, kilograms, newtons, newton-seconds and joules inside the shared module. Native unit conversion occurs once in the adapter.
- At any instant a body is native-owned, solver-owned or explicitly kinematic. Exactly one owner advances its pose and velocity.
- Hollow space stays empty. Missing parts do not leave hidden absorbers or colliders.
- Apply force/impulse at its actual location. Angular response uses the current mass distribution and inertia tensor.
- Use one fixed-step clock with bounded, measured substepping. Render interpolation and macVC frame generation do not advance physics.
- Existing installed gameplay remains unchanged until the candidate and native bridge pass their gates; keep experimental dependencies out of the normal plugin link.
- No push/merge. Installation, when warranted, occurs only with the game closed and byte/signature verification.

## Review focus

- Unequal masses, off-center contacts and asymmetric velocities must not pass solely because symmetric errors cancel.
- A wing/rotor can cross a thin obstacle by rotation even if a linear swept sphere reports no collision.
- A native callback after solver writeback can overwrite velocity or advance the same entity again.
- A connected island can straddle streamed cells, include a mission actor, or contain a pool address recycled for a different actor.
- Tearing must update collision, mass, inertia, aerodynamic surfaces and visible topology together, including repeated projectiles through the opening.

## Task 1: Retain a reproducible platform/contact baseline

**Files:** `docs/research/2026-11-05-world-physics-{audit,prior-art,evaluation}.md`, `docs/research/world-physics-probe.cpp`, `docs/research/2026-11-05-world-physics-probe-results.txt`.

**Consumes:** Pinned primary sources and the baseline repository audit. **Produces:** A reproducible executable, raw output and capability/unknown ledger; not an accepted dependency.

- [x] Audit existing motion/damage ownership, units, actor coverage and native build constraints.
- [x] Compare existing implementations and papers, including Bullet, Jolt, PhysX/Blast, Chrono, FEMFX, RoR/BeamNG and XPBD/plasticity literature; distinguish feature documentation from local proof.
- [x] Build pinned Bullet CPU libraries on ARM64 and measure wing strike/miss, rigid/sheet and symmetric/asymmetric sheet cases in one world. Preserve the 0.05 N·s deformable momentum tolerance.
- [x] Change split impulse, damping, face contact, implicit/projection settings, ERP and timestep individually; retain physical exit codes and failed results.
- [x] Commit reviewed research artifacts after final reproduction and whitespace/source checks.

**Initial result:** Build passed, while unmodified contact acceptance and the minimal correction failed. The [follow-up](../../research/2026-11-05-world-physics-conservation-followup.md) now passes the bounded contact gate under a corrected explicit spring/damping policy. This remains an experimental candidate; rigid joint separation is not a plasticity/fracture result.

## Task 2: Resolve the conservative-contact gate

**Files:** Extend `docs/research/world-physics-probe.cpp`, `docs/research/world-physics-contact-probe.cpp` and the evaluation note; retain any instrumented upstream changes as explicit experimental patch artifacts, separate from production source.

**Consumes:** Task 1 fixtures/raw results. **Produces:** A documented contact configuration or corrected candidate path with independently measured conservation, or an explicit rejection of that path.

- [x] Add a free rigid/free sheet reference case with no contact, a single prescribed impulse, unequal node masses and off-center barycentric contact. Assert total linear and angular impulse against an analytical impulse-at-point reference before testing sustained contacts.
- [x] Record per-step initial/final mass, linear/angular momentum, contact impulses and external loads. Test at least two geometrically distinct contact locations and reversed participant ordering. Check nonfinite values and distinguish stabilization work from physical impulse.
- [x] Trace face/node and rigid/deformable contact assembly against that reference. Diagnose configuration or implementation causes before changing tolerances. No post-step global momentum correction.
- [x] Re-run baseline and timestep variants with the diagnosis. Require all contact cases to stay within their declared tolerances, retain geometry separation and show convergence rather than contact removal.
- [x] Resolve the conditional alternative-engine decision for this gate: the corrected explicit prototype passes the bounded contact tests, so a second engine is not needed for that result. Retain Chrono as the comparison if the structural/stiffness gate makes this path unsuitable; no Chrono build or final SDK selection is claimed.

Result: `bullet-conservative-contact-experiment.patch` and `world-physics-explicit-policy.h` pass 44 contact cases, one force/current-geometry case, 70 whole-step checks and the initial five-case fixture. Three-way contact exposed and corrected a second lost-reaction path. Phase callbacks measure aggregate recipient impulses rather than relying on an overwritten library contact field. These are controlled mass-spring fixtures with uniform coefficients; local stress receipts, heterogeneous material laws, stiff structures and broader engine/host acceptance remain later work. The earlier one-operand patch stays retained as a failed diagnostic.

Run: compile and invoke the fixture using the evaluation note's commands. A successful build is insufficient; accepted fixtures must exit 0 and retain all collision assertions. Record a failed/rejected candidate honestly instead of leaving its acceptance checkbox complete.

## Task 3: Prove structural behavior and geometric contact

**Files:** Add `docs/research/world-physics-structure-probe.cpp` and `docs/research/2026-11-05-world-physics-structure-evaluation.md`; add a small authored physical fixture beside it if data is required.

**Consumes:** Accepted contact path from Task 2. **Produces:** An authored load-bearing structure, topology-change measurements and an SDK selection record with custom work identified.

- [ ] Author chassis, spar, skin and mount structure with mass, thickness, stiffness, yield/plastic state and fracture work. Bind collision and visible/aerodynamic surface revisions to that same structure. Use data with explicit estimated provenance; a bare render mesh does not supply these properties.
- [x] Test load/unload: elastic recovery below yield, nonzero permanent set above yield, and severed connectivity above fracture. Check no negative work or independently duplicated energy budgets. Report what the engine supplies and what the fixture implements.
- [x] Clip a deformable wing against a wall at normal and oblique incidence, then repeat as a clear miss. Assert force/torque transmission, speed loss, attachment loading and surviving/detached mass and inertia. Include eccentric loading and unequal masses from Review Focus.
- [ ] Test thin translating and rotating surfaces, an edge crossing between structural nodes, two deformables and self-contact. Test a high-speed projectile through intact material, empty cabin, existing hole and changed topology. Do not infer angular CCD from linear CCD.
- [ ] Separate and recontact fragments. Verify no phantom hull and matching collision/render/aero revision after repeated tears. Retain mass and linear/angular momentum across the split, within declared tolerances.
- [x] Measure step/contact/constraint count, CPU time, memory and topology-update time on the actual host; choose a benchmark workload and rate only after measurement. Record failed cases before dependency selection.

Progress: The [structural evaluation](../../research/2026-11-05-world-physics-structure-evaluation.md) records 15 material checks, 71 coupled structural checks, repeated topology rebuilds and measured 1/32-structure workloads. This is a custom axial lattice with estimated properties and a headless canonical surface; installed render/aero bindings and calibrated shell behavior are unproven. The geometric gate deliberately fails (42/50), including fast shots, angular sweeps, edge crossings, self-contact and fragment recontact. The current Bullet path is not accepted, so Task 3 remains open. A subsequent [continuous-contact evaluation](../../research/2026-11-05-world-physics-continuous-contact.md) builds IPC Toolkit and passes prescribed trajectory/first-event tests. It does not implement persistent, force-driven multi-contact or close the original geometric gate. No Chrono build or gameplay migration is implied. The [persistent-contact experiment](../../research/2026-11-05-world-physics-persistent-contact.md) subsequently adds an implicit force-driven node-island solve, loaded release/recontact, fixed reactions and force-driven topology/query checks. Its bounded 34 dynamics/12 material assertions do not replace the original gate or establish rigid coupling, angular/edge/intact-folded contact, calibrated shells or game ownership.

Gate: rigid joint breakage, an elastic sheet or a cosmetic hole cannot substitute for these assertions. If a candidate requires implementing constitutive/fracture mechanics, keep that work in its physical state/solve and assess its cost explicitly.

## Task 4: Establish the host integration boundary

**Files:** Extend `docs/research/2026-11-05-world-physics-audit.md`; retain exact native disassembly/caller evidence under ignored `build/world-physics-evaluation/native/`. Inspect `tools/patch_macvc.py`, `src/reactions.cpp`, `src/flightmodel.cpp`, `src/handling.cpp`, `src/airworld.cpp`, `src/panelcollision.cpp` and existing native fixtures.

**Consumes:** Exact 5217 binary and current hook chain. **Produces:** A source-backed capability matrix for motion ownership, authoritative queries, geometry updates and lifecycle. This investigation can proceed while numerical candidates are being evaluated.

- [ ] Locate the complete native phases reached by `CWorld::Process`, `CAutomobile::ProcessControl`, `CPlane::ProcessControl`, `CHeli::ProcessControl`, `CBoat::ProcessControl`, `CPed::ProcessControl`, `CPhysical::ProcessCollision` and `CPhysical::ProcessShift`, including direct/inlined paths. Trace gravity, position integration and later overwrites. Presence of a symbol is not proof that all callers are hooked.
- [ ] Inventory collision/LOS/weapon/AI queries, world-sector reindexing, animated/render transforms, model/COL lifetime and streaming callbacks. Mark each as proven, missing or unsupported; preserve exact native address/version evidence.
- [ ] Execute isolated native routines where feasible to verify suppression for transferred bodies and continued behavior for native-owned controls. Test repeated callbacks, callback ordering and nested queries. Assert a transferred body's native impulse/integration count is zero, with one solver advance per fixed interval.
- [ ] Exercise recycled pool addresses, restart/rewind, script teleport, mission takeover, creation/destruction, boarding and repair. Trace ownership transitions before/after every native phase. Do not use an immovable native mirror as a substitute for two-way dynamic contact.
- [ ] Verify physical geometry writeback and query consumers share a revision; a torn wing must be absent from all relevant native and solver queries. Test streams crossing a connected island, including collision ahead of the camera at flight speed.
- [ ] Prefer source integration if actual host source is available. If a binary boundary cannot prove one of these contracts, record the precise missing capability and pursue that integration route; do not call library compilation a successful bridge.

Gate: the public macVC repository is download-only. This plan does not assume the port source exists locally or invent an unverified suppression hook.

Progress: [14-vtable/21-method inventory and isolated native scheduling proof](../../research/2026-11-05-world-physics-native-ownership.md) establish the ordinary control/collision/shift/stuck-recovery paths and a controlled suppression seam. Three guarded scenarios pass and all three unguarded controls fail. Full controller/query/stream/lifecycle closure is still open, so Task 4 is not complete and no game handoff is authorized by these results alone.

## Task 5: Plan and implement one complete game interaction slice

**Files:** Create a separate implementation plan only after Tasks 2–4 establish the concrete solver and native contracts. Expected responsibility boundaries: shared world/physical asset state; game adapter/ABI; physical asset authoring/binding; integration fixtures. Select exact files and interfaces from the demonstrated boundary, rather than defining speculative public APIs now.

**Consumes:** Accepted numerical path and native ownership/query matrix. **Produces:** One installed, reviewable slice that proves the new architecture actually interacts with the native game.

- [ ] Move a complete interacting fixture island into the shared world: aircraft/wing against an ordinary building; person/vehicle against an attached and detached door; explosion applying occluded loads to people, vehicle and debris. Include an NPC and mission entity through the same ownership route.
- [ ] Derive local functional consequences from connectivity: detached wing supplies no parent lift, severed rotor drive supplies no torque/audio spin-up, damaged mechanism responds physically. Demonstrate finite armor and boarding accessibility without a generic hull threshold.
- [ ] Use shared flow/pressure and force-at-point inputs for the slice; verify air-relative rather than world-relative velocity. Apply recoil/projectile impulse and explosion pressure through the same authoritative geometry/load state.
- [ ] Compare 30/60/120 rendered fps with the same simulation input/time. Verify identity, sleeping/waking, pinned streaming cells, teardown, repair and save/restore of physical state. Fault injection must reject nonfinite output before native publication.
- [ ] Run relevant native/ABI and existing regression checks plus fresh final review. Install only with the game closed, verify bytes/signatures, then capture live traces of the actual wing/building, door/person and explosion cases. Distinguish automated evidence from remaining playtest observations.

Task 5 requires a concrete implementation plan because host facts and candidate selection are not yet established. That is an engineering gate, not a request for a new user interview or permission cycle.

## Full target after feasibility

Keep the comprehensive coverage table in the spec as the migration ledger: all cars/trucks/bikes/tanks/RC, fixed-wing/rotorcraft/boats, pedestrians, held/dropped weapons, NPC and player ammunition, structures/props/terrain, water/air/wash, material plasticity/fracture, fuel/cooling/hydraulics/electrical/heat and mission/stream lifecycle. Later plans expand one shared world and authored assets; they must not restart separate damage/force owners.

Do not mark the whole-world request or issue37 complete on the strength of this feasibility plan, the local probe or the first game slice.
