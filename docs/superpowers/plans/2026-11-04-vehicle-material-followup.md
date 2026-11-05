# Vehicle material and physics follow-up

> **For agentic workers:** Use superpowers:executing-plans inline; one fresh final review, no implementer agents.

**Goal:** Address the six new playtest failures: rotor assembly damage, ballooning Learjets, missing car material damage, health-driven entry rejection, inconsistent glazing and physical body parts.

**Spec:** The user's six-item playtest report in this conversation, continuing aircraft-systems.md and vehicle #37. Preserve prior authorization for inline work, tests, local commits and closed-game installation.

**Architecture:** Keep native dynamic physics distinct from kinematic mission routes. Use certified hit receipts and one material/energy model for vehicles. Compose private mesh holes, glass removal and deformation without overwriting one another. Retain native proof/mission/lifecycle boundaries and create actual native physical debris.

**Tech stack:** C++17; native ARM64 macVC5217; RenderWare and native instruction/installed asset fixtures.

## Constraints and review focus

- Exact CPlane/CHeli bounds, actor generation, clump, pose and repair certificates; no stale callback writes.
- Parked/empty/player Learjets retain gravity, collision response and entry; mission route controllers retain their scripted path ownership.
- A hole cannot make an underlying rotor hub/internal component immune to subsequent rounds; already absent parts cannot absorb shots as invisible material.
- Car glass, holes and dents compose across native damaged variants, repair and streaming. Armor/proof vetoes remain authoritative and no LOS prepass mutates state.
- Detached car parts must have native physics, bounded lifetime and model/texture ownership; attached loose parts retain their native hinge behavior.

## Tasks

- [x] 1. Reproduce parked-plane velocity loss in test_airframedamage/native flight ownership; assert damaged physical planes preserve native impulses/gravity and route fallback remains for stock scripted CPlane/CHeli. Implement the smallest ownership correction; focused tests and commit.
- [x] 2. Reproduce continued shooting through a rotor assembly opening using actual model geometry and the owner fixture. Anchor functional hub material to current native rotor geometry rather than only the broad static COL; tie failure to native removal/audio/lift. Test shielding, class bounds, repair and absent rotor; commit.
- [x] 3. Extend test_carglass and shotcollision/native receipt tests for car entrance/exit holes and component damage without global hull-health depletion. Share private mesh material damage across cars/aircraft, composing dents/glass/variants and keeping armor/proof vetoes. Test native entry eligibility at a failed engine, repair, allocation failure, shared geometry and existing holes; commit.
- [x] 4. Reproduce side/rear glass failures through native caller contracts and real stock panes. Correct actual impact receipt/classification/variant behavior; no mutation from read-only LOS. Test side/rear glazing, damage model switches and composition with holes; commit.
- [x] 5. Inspect native door/hood/trunk debris and hinge contracts, reproduce physical collision failure with a native instruction fixture, then correct actual physical objects. Verify velocities, world collision, parent exclusion, bounded lifetime, no duplicate spawn, repair/stream/proof and finite pool pressure; commit.
- [x] 6. Run make all/test/test-game-assets, real RenderWare and focused ASan/UBSan fixtures; one fresh whole-change review, fix material findings with RED→GREEN tests, closed-game install and installed byte/signature checks. Record evidence in build/vehicle-material-followup/ and retain branch locally.

Initial evidence: latest log records aircraft holes128, Police Maverick main rotor reaches25% then central-body component hits continue; no side/rear pane-break logs. Car204 engine reaches0% while normal native hull damage still runs. Physical route wrapper is invoked for every CPlane, including empty registered Learjets.

User clarification: all bullets and impacts must connect through materials and applied forces. Extend task3 to use one conserved energy-deposition solver for contact loads and projectile paths, and one accumulated integrity for each component/connection. Do not add separate bullet/collision health bars. Visual holes, dents, glass loss and physical detachment must compose around that same spatial material damage.

Evidence to date: one shared work solver now handles contact and projectile absorption; car and aircraft component states accumulate both. Car holes/dents/glass composition and native accepted zero-hull-damage receipts are covered. Native bullet proof is bit25, explosion proof bit17; earlier hooks confused these, now tested against actual native damage instructions. Variant propagation, real stock pane tests, shared connection weakening and native local panel impulses are implemented. Final full-suite validation/review/install remain. Panel collision retains native hinge and coarse debris proxies; detailed independent collision for an open attached panel remains future work.

## Final validation and installation

Implementation commits: c46df49, a86c11a, e00d9eb (base d3012dd). Full `make -j4 all test test-game-assets` passed:82 test commands, no warnings/errors. Eight focused ASan/UBSan fixtures passed. Actual librw material/glass/debris fixture passed under sanitizers for authored code; native JIT/prebuilt librw were not instrumented. Actual stock Deluxo5, Hermes5, Benson3 side/rear panes passed. All22 new native bindings resolve in5217.

Fresh whole-change review found no Critical/Important issues. Deferred minor: composed holes/dents interpolate pristine surface normals, so reflections can miss the dent shape although geometry/material loss remain correct (`carglass.cpp` CopySkin).

Rulings: reference sheet densities and calibrated native work retain tunable approximations rather than claim an SI simulation; native hinges/debris collision proxies retain existing collision fidelity, so open attached panels lack independent detailed world collision shapes. Reviewer declined interactive force calibration, perceived debris weight and collision fidelity beyond those proxies; deterministic native force/hinge tests verify behavior, with interactive feel left to playtesting. Costs: further calibration and articulated collision work may be needed.

Closed-game installation succeeded:167 hooks+10 skips, plugin byte match and strict plugin/executable signature checks passed. Installed plugin SHA256 `f95861f18e874ea48d52fcab1ab3af49aa4c326d0a10b5b25cada62c83619fc7`; executable `a1d9e389975295c29ef2887383b21e1bb81da4ac48c224759e65feabf58004ed`. Evidence and archived review/ledger: `build/vehicle-material-followup/`. Branch retained locally; no push or merge.
