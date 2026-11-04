# Vehicle Damage Follow-up Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans inline; one fresh final reviewer.

**Goal:** Fix reported fire/glass behavior and deepen localized, armored, mission and aircraft damage.
**Architecture:** Native owners retain certification/lifecycles. Correct service ownership and ignition, add material-limited part/armor interactions, and give airframes separate components/private skin instead of unsafe automobile offsets.
**Tech Stack:** C++17, exact macVC5217 ARM64, existing native/RW/asset fixtures.
**Spec:** docs/superpowers/specs/2026-11-04-vehicle-damage-followup-design.md

## Global Constraints

User overrides earlier blanket mission/aircraft exclusions. Preserve explicit script/proof/config/lifecycle authority.
No global invulnerability armor, no whole-health-to-all-module conversion, no cosmetic-only hole claim.
Cold hull health alone never ignites; localized source dwell8s, external native fires immediate.
Native fire pool40 x0x38, 256 bounded actor slots; original geometry and unrelated actors remain intact.

## Review Focus

1. Healthy firefighting crew stays through suppression while direct danger/script/player exits still work; service counters clean up.
2. Cold bullet-riddled skin stays cold; actual heat/leak/short and native fire sources still work in missions.
3. Normal/deferred/drive-by window/armor/part receipts use real native boundaries without read-only effects or occupant protection bypass.
4. CPlane accesses stay within its exact layout, and independent airframe failures reach actual flight consumers rather than only visual/global health changes.
5. Private skin mutation preserves materials/normals, holes affect later rays, bounded memory/repair/recycling and nested callbacks remain correct.

### Task 1: Reported regressions and mission policy

**Files:** firemodel, carfire, drivers, shotcollision, tests, native hooks as needed.
- [x] RED cold low-health ignition and empty-car side window tests; native arrival exit/cannon regression.
- [x] Fix certified glass boundary, localized ignition, crew service ownership/release, mission gating; focused/native GREEN.
- [x] Record causes and commit.

### Task 2: Armor and ballistic part loss

**Files:** projectile/shot/parts owners, new material policy as needed, real stock/native fixtures.
- [x] RED Enforcer/Securicar body/glazing protection versus exposed wheels/penetration/blast; repeated localized bullet part detachment.
- [x] Implement layer budget and native component progression without invincibility/shared writes; GREEN and commit.

### Task 3: Aircraft component and skin model

**Files:** separate airframe/skin models and owner, shot tracing, flight consumers, RW/native/stock fixtures.
- [x] RED thin-skin penetration/perforation and localized propulsion/control/aero failures including guarded CPlane and missions.
- [x] Implement separate safe layouts, bounded private skin and flight effects; native/stock/RW consumers GREEN, document fidelity and commit.

### Task 4: Completion

- [x] Full all/test/test-game-assets, focused sanitizers, one fresh review/one tested fix pass.
- [ ] Authorized closed-game install, byte/signature checks, verification record and commit; clean only this workspace.

## Execution record

- Base0a5dc24, local vehicle-damage-followup branch. Latest log snapshot retained in build/vehicle-damage-followup/playtest.log. Explicit continued execution/install preferences persist; no new approval interview.
- RED reproductions: normal side glass receives zero-position native damage without windscreen-only prepass; cold aggregate health100 ignites without any leaking/hot/electrical system. Native arrival exit at50810 confirmed, service-cycle regression next.

- Task1: native regression proves crew/cannon through extinction and native cleanup; added scoped0.5m cannon minimum. Cold global health no longer ignites; real localized interaction requires8s. Empty side impacts certify the actual zero-position damage call. All focused suites/assets/capacity checks passed.

- Independent whole-branch review: four Important findings, no Critical/Minor. One tested fix pass corrected native pose-controller consumers (full pristine CPlane and CHeli execution),65-layer armor fallback, disabled/uninstalled progressive-fire fallback and independent aircraft rearming after water. Complete all/test/test-game-assets and final four ASAN/UBSAN binaries passed; real librw skin builder passed. Native controllers retain their mission state while calibrated component losses affect actual progress, altitude and attitude. Exact coefficients/component locations and live presentation/navigation remain refinements, not claims of full War Thunder fidelity.
