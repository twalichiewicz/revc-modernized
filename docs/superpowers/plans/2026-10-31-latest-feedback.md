# Latest feedback implementation plan

> **For agentic workers:** Use the approved spec below. Independent module owners may work in parallel under superpowers:dispatching-parallel-agents; the primary agent integrates common files and runs whole-project verification. Do not repeat design approval: the user said "Proceed".

**Goal:** Address all 33 feedback items, including GTA IV-style physical NPC reactions, with item-specific verification and explicit remaining limitations.

**Architecture:** Keep deterministic rules separate from macVC hooks. Camera, helicopter/APB and map/HUD changes have independent owners; the primary agent owns NPC reactions/population and shared integration. Preserve existing uncommitted work in place on a new branch, using the initial-file snapshot for review.

**Tech Stack:** C++17, arm64 macVC 1.0.5217, RenderWare/OpenGL, Python build tools, Makefile tests.

**Spec:** `docs/superpowers/specs/2026-10-31-feedback-and-npc-reactivity-design.md` (approved in conversation).

## Global Constraints

- Preserve the initial checkout's changes; baseline snapshot: `/tmp/vc-feedback-before-20261031`.
- No commits or GitHub writes. Installation and in-game validation were separately authorized by the user's later "Proceed".
- Preserve a rollback backup before installation or test-setting changes. Do not overwrite user saves.
- Root owns `Makefile`, `src/config.*`, `src/game.*`, `src/minimap.cpp`, `src/player.cpp`, `tools/patch_macvc.py`, `README.md` and this ledger. Other owners report integration requirements.
- Follow root-cause investigation and red/green tests at meaningful seams. Rendering/physics behavior needs game verification; do not claim compilation proves it.
- No replacement of the approved physical reactions with animation-only knockdowns.

## Review Focus

1. Switching first/third person, dying and respawning restores all model/render state.
2. Pool slots can be reused: every persistent NPC/vehicle record must validate its handle/generation.
3. Mission, cutscene and attached-ped controllers retain ownership and are not commandeered.
4. Fixed-step simulation stays bounded under long frames and crowded scenes.
5. Disabled features and missing hooks fall back without partially initialized behavior.

## Task 1: Camera, vehicles, aiming and swimming

**Owner/files:** camera specialist; `src/camera.*`, `src/controls.cpp`, `src/steering.*`, `src/swim.*`, `src/aimcam.*`, `src/driveby.*`, first-person viewmodel code and dedicated new rules/tests. No shared-file edits.

**Coverage:** 4.1–4.2, 5.1–5.10 (coordinate light/material ownership with HUD owner), 8.1–8.2.

**Interfaces:** Preserve existing module entry points. Send root any new sources, hook symbols, feature switches or required API declarations. Reticle drawing should route through the HUD owner's shared renderer once available.

- [x] Reproduce the relevant code paths, using offline geometry/pose tools where possible.
- [x] Add and run red regression tests for drowning progression, camera/pose lifecycle rules and collision/lookup math where real seams exist.
- [x] Implement fixes and run targeted tests green, documenting unverified in-game cases.
- [x] Provide root integration instructions and exact verification commands/results.

## Task 2: APB and helicopter correctness/performance

**Owner/files:** police-aircraft specialist; `src/heli.*`, `src/helizones.*`, `src/heat.*`, `src/sight.*`, `src/wantedhud.*`, dedicated new rules/tests.

**Coverage:** 6.1–6.9.

**Interfaces:** Root integrates hook/Makefile changes. Existing `heat::Model` remains the source of wanted state. Add state-aware recognition so a clean description under APB does not fill recognition; newly witnessed crimes still report normally.

- [x] Add/run failing tests for clean APB recognition, navigation clearance/contact and crash transitions at usable seams.
- [x] Trace aircraft crew, light-source and streaming paths against the exact binary.
- [x] Implement fixes, verify targeted tests and document measured/unmeasured performance.
- [x] Report exact integrations and remaining game checks.

## Task 3: Map/HUD, crosshair Options and estate lighting

**Owner/files:** HUD/world specialist; `src/fpcross.*`, new `src/reticle.*` or equivalent, `src/scene.*`, `src/mapdata.*`, `src/menu.*`, `src/controlspage.*`, `src/grade.*`, `src/carlights.*`, `tools/bake/*`, new checkpoint code and dedicated tests.

**Coverage:** 1.1–1.3, 7.1, 5.1/5.5 rendering support coordinated with camera owner.

**Interfaces:** Provide a shared reticle renderer callable by first-person, third-person and drive-by paths. Root owns config integration. No shared-file edits. Preserve special scope/launcher sights.

- [x] Identify root causes and exact checkpoint/lighting hook interfaces before changing them.
- [x] Write/run failing tests for meaningful crosshair configuration, geometry and checkpoint rules.
- [x] Implement Options persistence/preview and integrate all reticle contexts with their owner.
- [x] Verify offline map/geometry rendering and report actual versus remaining estate reproduction.

## Task 4: NPC greetings and driver intimidation

**Owner/files:** primary; `src/interact.cpp`, new driver-perception rules/integration, `src/game.*` as needed.

**Coverage:** 9.1–9.2.

**Interfaces:** Use a verified original Tommy speech event/asset; preserve civilian reply logic. Driver intimidation uses player aim, line of sight and NPC temperament, skips mission/attached controllers and throttles decisions.

- [x] Verify original Tommy speech events/assets and exercise the offline decode/phrase selection path.
- [x] Add/run failing deterministic perception/reaction tests for facing, visibility, mission ownership and cooldown.
- [x] Implement greeting and driver responses; verify targeted tests.

## Task 5: Shared population and station replenishment

**Owner/files:** primary; new `src/populationmodel.*`, `src/population.*`, `src/traffic.*`, `src/police.*`, dedicated tests.

**Coverage:** 6.10–6.11, 9.4.

**Interfaces:** Deterministic virtual-agent state is separate from local ped materialization. Reuse verified pedestrian paths, zone demand and station locations. Root hooks per-frame updates and integrates feature switches. Never create peds on arbitrary ground without a suitable path.

- [x] Add/run failing tests for zone capacity, seeded distribution, reservation/replenishment, stale handle release and no population growth from moving Tommy.
- [x] Implement virtual patrol/civilian records and bounded path-based materialization.
- [x] Connect station queues and on-foot patrol dispatch to existing police accounting.
- [x] Verify deterministic tests, pool-lifecycle behavior and local caps.

## Task 6: Physical NPC-reaction feasibility and integration

**Owner/files:** primary; new deterministic reaction simulation and game integration modules, dedicated tests, `src/player.cpp` and verified collision/damage hooks.

**Coverage:** 9.3.

**Interfaces:** Capture accepted hits using validated ped handles. Simulate an articulated skeleton with constraints, contacts and active balance; keep physical pelvis, entity root and rendered skeleton consistent. Do not reuse `body.cpp`'s singleton player skeleton for simultaneous NPCs. Fixed timestep and bounded active count are required.

- [x] Add/run failing tests for local directional impulses, limb constraints, balance recovery, repeated-hit collapse and world contacts.
- [x] Implement the deterministic solver and binding to measured NPC skeletons.
- [ ] Construct the approved single-NPC feasibility scenarios, including slope/wall contact and live/dead transition.
- [ ] Integrate bounded active-set lifecycle, ownership exclusions and recovery after the feasibility evidence supports it.
- [x] Record exact limitations if in-game validation or engine hooks prevent the required evidence; do not mark those checks complete.

## Task 7: Shared integration and full verification

**Owner/files:** primary; `Makefile`, `src/config.*`, `src/game.*`, `src/minimap.cpp`, `src/player.cpp`, patcher, README and this ledger.

**Interfaces:** Integrate only module-owner-declared interfaces; preserve the pre-existing hooks and initial changes. Use snapshot-relative diffs for independent review.

- [x] Integrate feature switches/defaults, source/test targets and hooks with verified signatures.
- [x] Run `make test`, `make`, relevant offline render tools and `git diff --check`.
- [x] Request one fresh final review of this session's changes against the approved spec.
- [x] Address substantive findings with red/green checks; report deferred or unverified items explicitly.
- [x] Present built, reviewable changes and obtain the separately required installation/game validation authorization (user: "Proceed").

## Progress ledger

- Baseline: `make test` passed on 2026-10-31 before implementation; full log `/tmp/vc-feedback-baseline.log`.
- Ruling: work on `codex/latest-feedback` in the existing checkout with an initial-file snapshot, preserving the extensive dirty state rather than creating a clean worktree that omits it.
- Ruling: the user's "Proceed" authorizes execution of the approved design; choose parallel specialist ownership for independent domains, with root integration, without another approval round.
- Pre-flight: camera/HUD share reticle calls and interior rendering; coordinate those interfaces directly. All shared build/config/hook files have a single owner.
- Tasks 1–5: source/offline implementation complete; installed-game acceptance remains pending per `2026-10-31-feedback-verification.md`.
- Task 6: solver, native contact adapter, rig binding and ownership prototype implemented and regression-tested; in-game feasibility/recovery/feel/performance checkpoint remains open.
- Task 7: all shared integration complete; 62 executable tests pass, forced production rebuild passes, dry patch succeeds (85 hooks/6 skips), targeted sanitizers pass, independent final review has no unresolved Critical/Important code findings.
- Ruling: generic chat event157 has no neutral Tommy mapping; reuse the first original `drug_1` "Hello?" asset through AudioToolbox/OpenAL instead of guessing a speech event. Cost: an additional framework/asset dependency and in-game listening check.
- Ruling: use a custom articulated constrained solver with active balance and native sphere contacts, gated by the planned in-game feasibility checkpoint. Cost: real recovery/contact appearance and frame-time remain unproven until installed-game testing.
- Ruling: race arrows apply only to verified RACE/RCRACE1/RCPLNE1 sequential layouts; missing/unsupported next markers remain untouched. Cost: other race layouts can still need coverage.
- Ruling: keep existing INI values, including the old0.6 building height, while new defaults use1. Cost: existing users must explicitly change that value to obtain full relative heights.
- Final review declined runtime feel/contact quality, target-machine performance, installed-game rendering/input/audio acceptance, estate A/B diagnosis and untouched initial snapshot work; these remain explicit pending work rather than inferred passes.
- No deferred code-review minors were raised. Installation and launch are authorized and completed; item-specific in-game acceptance continues.
- Installed-game correction complete: crosshair settings moved to a new Options > Gameplay menu; Controls has two input tabs. Main/pause keyboard navigation, all six settings, preview, Back, Restore Defaults and restart persistence pass in the installed game. Label layering/footer and cursor deferral fixes are included. All64 tests pass, new menu/label tests pass sanitizers, independent menu review is clear. Mouse/controller acceptance and broader runtime scenarios remain recorded in the verification report.
- Latest follow-up: the user confirms estate darkness is indoors, on walls/floor. The installed interior grade removes additional contrast/vignette; matched estate and hotel/exterior checks support the correction. Native lighting/filtering remain intact.
- Ragdoll follow-up: real captures exposed missing upper torso volume, wrong skin rotation/head pivot, unrestricted hip/knee folding, contact-generated bounce, a native get-up floor gate, and lowered standing targets during fading. These have source fixes and meaningful captured-rig/native-hook regressions. Actual nondamaging bumps and running-obstruction trips now occur with health unchanged. Final anatomy/fade visual replay remains open.
- Latest final checks: all66 test executables, production build/install (85 hooks/6 skips), targeted ASan/UBSan and independent source reviews pass. Evidence and limits are in the verification report. Original INI/save preservation is checked against the session backup.
- Runtime continuation currently requires the Mac unlocked: macOS reports the screen locked and prevents PID-specific activation. The user has been asked to unlock; do not infer that final ragdoll visuals, crowd performance or the other pending native scenarios passed.
