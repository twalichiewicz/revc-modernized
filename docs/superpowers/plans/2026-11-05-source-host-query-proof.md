# Source host and shared wing queries implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. Preserve the previous plans and physics tests. Use one fresh final reviewer for this milestone.

**Goal:** Decide source availability, reproduce an ARM64 source-host build, then prove contacts, LOS and vertical queries consume the same instance-local damaged wing geometry.

**Architecture:** Matching macVC5217 source has not been found in the checked release/workspace. Build a separately identified source host from pinned `mrxenginner/reVC` and its pinned librw dependency, with existing local audio libraries and a pinned GLFW build. This is an explicit experimental-host choice under the user's source/build decision request, not a replacement for the installed Metal port or a claim of plugin ABI compatibility. Retain source changes as a reviewable patch plus a reproducible runner in this repository.

**Tech Stack:** CMake, AppleClang21, ARM64 macOS, reVC `a26fff4af555a8a425ac9f17d1dd718d00d1f942`, librw `0ad67ee226ea51beb3f465cb8857195e4db16029`, GLFW3.4 commit `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`.

**Spec:** `docs/superpowers/specs/2026-11-05-world-physics-design.md`; user continuation requests source availability, reproducible host, then identical damaged-wing geometry for contacts/LOS/vertical queries.

## Global constraints

- No solver changes or production app replacement. Preserve existing tests and numerical limitations.
- The source-host renderer/ABI differ from macVC5217. Keep all artifacts and runtime state isolated.
- Match proof strength to evidence: a host self-test calling compiled engine routines is stronger than copied instructions but is still not an interactive gameplay/ownership/performance acceptance test.
- Record source/dependency revisions and local changes, toolchain, commands, linked libraries, asset hashes, exits and executable hashes. Rebuild in a second build directory.
- Never fetch game assets from third parties; use the installed user-owned data read-only and keep generated geometry under ignored build storage.

## Review focus

- Same-model aircraft sharing altered COL data or retaining stale cached triangle planes.
- World LOS/vertical/contact consumers bypassing the entity-specific resolver.
- Broad-phase bounds/sector membership disagreeing after geometry replacement.
- Replacement/destruction leaving dangling per-instance geometry or changing another aircraft.
- A build or synthetic query test being reported as compatibility with the installed app or live flight acceptance.

### Task 1: Source decision and reproducible baseline host

**Files:** `docs/research/2026-11-05-host-source-availability.md`, source lock/build runner under `tools/host/`, ignored source/build artifacts under `build/host-source/`.

- [x] Verify exact macVC release contents, available public source candidates and bounded local source search. Record matching-source absence as checked scope, not universal nonexistence.
- [x] Build the pinned alternative host and dependency without modifying production files. Resolve only observed build blockers; retain any compatibility patch.
- [x] Record a reproducible source/dependency/build manifest and executable/app artifact. Repeat the build in a separate build directory.

Result: public same-label5217 differs by hash; matching source not found in checked surfaces. Explicit choice is a distinct pinned source host. The source host built; two clean patched builds have identical executable SHA-256 `19b84dd5f25cdd7230c5e4e36c835bf41fd5b91dc2cf2116220ec93bf27df37e` under the recorded toolchain/dependencies. Development apps link recorded Homebrew audio libraries; they are not portable distributable bundles.

### Task 2: One physical geometry per entity

**Files:** Reviewable engine patch plus query self-test fixture and local wing-geometry exporter under `tools/host/`; result note under `docs/research/`.

- [x] Add a host self-test that queries intact, damaged and untouched same-model control geometry using actual engine physical contacts, full world LOS and vertical APIs. Use installed Dodo wing triangles where import support permits; label any earlier synthetic fixture separately.
- [x] Observe the failure before adding the entity-local geometry seam. Keep source tests separate from macVC native compatibility tests.
- [x] Introduce the minimal owned per-entity collision geometry/revision seam and route the named world/contact consumers through it. Update bounds/sector membership and retire native triangle-plane caches safely.
- [x] Verify all named consumers hit intact wing, miss the removed patch, retain surviving geometry and leave the same-model control unchanged. Include replacement/destruction and translated/rotated/boundary cases justified by the seam.
- [x] Reproduce the patched host build and query test with provenance in a second build directory; preserve existing repository/native/physics tests.

Result: initial53 checks,10 failures with shared-model publication →53/53 with instance publication. Review added discriminating sector-membership/growth cases: final65/65 pass in both clean builds; shared-model control65/18 fails. Removing reinsertion independently produces65/8 failures. Compiler-derived exporter headers are now in provenance.48 actual DFF wing triangles become28 after a predetermined tip removal. Contact proof is stationary sphere-on-A/wing-triangles-on-B, not triangle-only wing/building dynamics. Render/aero publication, flight ownership, full lifecycle and performance remain the earlier slice's gates. Two reported bootstrap crashes were diagnosed and fixed; normal early exit before RenderWare initialization is a retained control.

### Task 3: Review and report limits

- [x] Fresh final review of the source decision/build/query proof; fix material findings once with tests.
- [x] Report exact source-host/app artifact and measured proof results, distinguish remaining live ownership/render/lifecycle/performance gates, and keep the controlled in-app slice plan open.

Fresh review found one Important coverage gap and one Minor provenance gap; both addressed once with red/green controls and clean reproduction. No second reviewer round. This plan's source/build/query deliverable is complete; the original in-app physics slice remains open.
