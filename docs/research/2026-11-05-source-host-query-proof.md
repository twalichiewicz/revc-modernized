# Reproducible source host and one damaged-wing query surface

2026-11-05. **The distinct source host builds, and the bounded geometry-query proof passes.** Two independent clean build directories produced identical ARM64 executable SHA-256 `19b84dd5f25cdd7230c5e4e36c835bf41fd5b91dc2cf2116220ec93bf27df37e`. Each executes65 successful assertions through compiled engine routines. The shared-model publication control fails18 of those assertions, as expected. This is not a macVC5217 rebuild, a rendered wing tear or an accepted solver-owned aircraft/building simulation.

The later installed interactive follow-on is recorded at the end of this report. It adds a rendered, predetermined wing-tip cut in a separate app mode; the original query proof and dynamic-physics acceptance limits remain distinct.

## Source decision and identity

The [source-availability investigation](2026-11-05-host-source-availability.md) found no matching public macVC engine-source delivery in the checked release, owner repositories, branches/history, workflow/artifact surfaces or archive; bounded local workspace/archive checks found no matching checkout. The public release labelled5217 also hashes differently from the pristine5217 image previously audited. Version labels cannot establish binary identity.

Under the user's request to make a source/build decision and continue, **use the pinned mrxenginner source tree as a separately identified experimental query host**, preserving the installed Metal game and its plugin. Matching macVC rendering, ABI, save and gameplay/mod compatibility remain unestablished; the existing fixed-address plugin must not be loaded into this host. The [source lock](../../tools/host/source-lock.json) records:

| Input | Pin |
| --- | --- |
| Engine | `mrxenginner/reVC` `a26fff4af555a8a425ac9f17d1dd718d00d1f942` |
| Engine's librw submodule | `mrxenginner/librw` `0ad67ee226ea51beb3f465cb8857195e4db16029` |
| Static GLFW | `glfw/glfw`3.4, commit `7b6aead9fb88b3623e3b3725ebb42670cbe4c579` |
| Existing audio libraries | Homebrew openal-soft1.25.2 and mpg1231.33.7, with dylib/header hashes in the build manifest |
| Build | AppleClang21, CMake4.4.4, ARM64 Release, macOS deployment target26.0 |

The original source also built without query changes. Its macOS15 link target exposed newer audio dylib minimums; the reproducible experiment explicitly targets26.0 rather than claiming15 compatibility. No system dependencies were installed. The `.app` is a development artifact linked to the recorded Homebrew audio libraries; it is not a portable distribution bundle. GLFW is statically linked. The renderer is upstream OpenGL, with a hidden64×64 GLFW context for the query test; no game scene is rendered.

## Reproduce the build and controls

```sh
python3 tools/host/build_host.py \
  --work-dir build/host-source/new-reproduction \
  --control
```

Use a new/empty scratch directory. `--game-data` can point to another user-owned original installation; otherwise the installed game data is read read-only. The runner checks the existing audio versions, fetches exact revisions, applies three retained patches, builds the asset exporter and complete host, and executes the queries in `host-a` and `host-b`. `--control` additionally compiles shared-model publication, expected exit1 with18 failed assertions. Unexpected process/build exits are errors, not successful physics rejections. Missing-file invocation must exit2 normally after static destruction.

The runner records commands, source pins/diff, patch/fixture/tool hashes, compiler/SDK/CMake/OS, audio library/header hashes, linked-library paths, actual DFF/wing hashes and executable identities in `manifest.json`. `-ffile-prefix-map` and `-fmacro-prefix-map` normalize scratch paths. Identical binary output was measured for this pair; this is not a claim of bit-identical builds on other toolchains, dependency builds or SDKs.

Retained artifacts:

- `build/host-source/reviewed/manifest.json` and `logs/`.
- `build/host-source/reviewed/host-a/src/reVC.app` and `host-b/src/reVC.app`.
- `build/host-source/reviewed/wing.txt` and its original local `wing.txt.dff` input copy, kept out of Git.

The complete host executable can rerun the proof directly:

```sh
build/host-source/reviewed/host-a/src/reVC.app/Contents/MacOS/reVC \
  --host-wing-query "$PWD/build/host-source/reviewed/wing.txt"
```

## Geometry and actual query paths

[export_wing.cpp](../../tools/host/export_wing.cpp) reads model181's real `deaddodo.dff` from the installed IMG through the repository's existing DFF/frame flattener. The input has1690 vertices/1355 flattened faces. It selects48 source triangles whose vertices have x>2m and whose centroid satisfies |y|<2m: an outboard starboard-wing strip, not an authored structural/material model. Flattened source geometry is not validated as a watertight or deduplicated physical shell.

Original DFF SHA-256: `ab127cc1571050fb82304f1235d38bab04df246f38ed32cd3971e8492c853677`. Exported triangle-file SHA-256: `f35ae97d1ed5ff2177100f3ff3a3e10f2ce555f7f13bd703047bafeab4b1724f`. The predetermined damage removes triangles touching the outermost4% of this strip's x extent. It tests geometry publication, **not impact-derived fracture**. Remaining triangles and bounds are rebuilt together. Native COL precision/representation is retained; this does not validate thin-shell mechanics or general remeshing.

[HostWingQuery.cpp](../../tools/host/HostWingQuery.cpp) is linked into the complete host. Its command-line entry runs before normal game startup but initializes RenderWare, its context, pools, texture-store state and collision-plane cache. It constructs real pooled `CPlane` actors with the same model181 and separate transforms. Minimal empty clump/frame resources satisfy native construction; a rendered Dodo model and damaged render mesh are **not** loaded/published. The collision triangles come from the actual DFF export described above.

| Consumer | Actual compiled engine entry exercised | Assertions |
| --- | --- | --- |
| Physical contact | `CPhysical::ProcessEntityCollision`, then native `CCollision::ProcessColModels` | A stationary0.03m sphere contact probe touches the wing triangles. No observer supplies a synthetic contact result. |
| World LOS | `CWorld::ProcessLineOfSight` through real sectors to `CCollision::ProcessLineOfSight` | Hit/target identity before damage; no hit through removed tip; surviving root/control remain. |
| LOS-clear | `CWorld::GetIsLineOfSightClear` and native `TestLineOfSight` | Occlusion agrees with full LOS in every case. |
| Vertical query | `CWorld::ProcessVerticalLine` through real sectors and native vertical narrow phase | Same intact/removed/surviving/control results as the other query kinds. |

The contact probe supplies the native algorithm's sphere-on-A/triangles-on-B ordering. **Triangle-only aircraft-led collision against a triangle building is not proven by this test**; native contacts do not become arbitrary triangle/triangle continuous contact. No building mesh/wing-strike flight step or two-way impact response is simulated. This is the requested source geometry/query seam proof, not completion of the earlier controlled interaction island.

Two poses cover translation and yaw. For each: intact tip, same-model control, published damaged tip, surviving root, untouched control and repaired tip are checked through all four consumers. A separate prescribed60m local geometry relocation forces a newly occupied sector, while the aircraft pose stays fixed. The test asserts old-sector retirement and new-sector registration, then queries the relocated surface through all four consumers. This is a publication stress case, not physical deformation. Pointer isolation is checked separately. Both actors are removed/deleted; the native pool diagnostics report zero remaining actor, entry-node and COL allocations. These are bounded replacement/destruction checks; they do not establish save/load, stream recreation, mission takeover, multithreaded publication or arbitrary pool-wrap behavior.

## Minimal source integration

[instance-geometry.patch](../../tools/host/instance-geometry.patch) adds an entity-owned COL pointer and revision. `CEntity::GetColModel` resolves the private geometry when present. World LOS/LOS-clear/vertical/sphere routes and entity bounds use that resolver; physical contact already obtains both participating entities' models through it. Related physical bounds lookups are redirected too.

`ReplaceInstanceCollision` takes exclusive ownership of a fully prepared COL only on success. It rejects invalid entity/model identity, null/alias input, non-owning volumes, invalid box bounds and revision exhaustion. The caller remains responsible for complete primitive-array validity and exclusive ownership; this is a bounded experimental seam, not a general untrusted-mesh validation API. Publication must occur on the main thread between queries, with no retained/nested query borrowing the old COL.

For a registered actor, replacement removes the old sector entries, publishes the new COL/revision, reinserts using the new bounds, then destroys the previous private COL. Native `CColModel` destruction unlinks cached triangle planes before freeing arrays. Actor destruction releases its private geometry. Shared model storage is not mutated by this route. The source layout changes; this deliberately reinforces the lack of compatibility with the old binary plugin.

This operation does not yet implement the full production prepare/commit/retire contract: allocation failure during sector reinsertion, concurrent readers, model replacement, save/restore and broader lifecycle revocation remain outside its bounded proof. No native motion owner is transferred.

## Bootstrap failures and supplied crash reports

Two direct test launches produced the reports the user supplied; both were experimental host failures, not production macVC crashes:

1. The first fixture could not select a surviving inboard face and returned before renderer initialization. Empty static `CColModel` destructors unconditionally called `RwFree(nullptr)` via uninitialized allocator callbacks. [null-free.patch](../../tools/host/null-free.patch) makes null frees a no-op while preserving non-null frees. A missing-input test now returns2 normally in both reproduced builds, exercising that pre-initialization exit path. The initial inference of post-teardown allocator clearing was corrected by source/input inspection.
2. After selecting a valid wing-tip region, real `CPlane` construction dereferenced a missing clump. Debugger runs also identified required renderer-start and texture-store initialization. The fixture now follows those lifecycle requirements and supplies valid clump/frame/texture-slot resources. It does not bypass a crash by faking a native query result or skipping destructors. Both full proof runs terminate normally.

Exact backtraces and red/build/debug logs remain under ignored `build/host-source/`. The retained patches contain only the observed null-free compatibility fix, explicit test entry and instance geometry/bounds/query seam; there is no custom solver expansion.

## Remaining acceptance and verification

`make test` and `make test-game-assets` pass. Existing production sources, tests, native/physics runners and numerical solver files are unchanged from `24805bc`; their prior numerical limitations and deliberately failing controls are preserved. The installed executable/plugin hashes remain unchanged. Compiler-derived local exporter dependencies, including `src/types.h`, are hashed. Source-host input hashes match the retained reproduction manifest; `git diff --check` passes. One fresh reviewer found an Important gap in sector-reinsertion coverage and a Minor missing header in provenance. Both were addressed in one tested pass. Removing sector removal/reinsertion now produces65 checks/8 failures; restoring it produces65/65 passes. Final clean control/host-a/host-b builds produce65/18,65/0,65/0 respectively, and both positive binaries are identical. Missing-input exits remain normal2. No second review round was performed. The reviewer independently reran the earlier53-check host; broader regression and final fix-pass evidence remain author verification.

The earlier [in-app slice plan](../superpowers/plans/2026-11-05-aircraft-building-slice.md) remains open: actual solver-exclusive motion, coherent render/aero publication, comprehensive lifecycle handling, a wing/building strike and measured in-app frame/step performance are still required. No offline query timing is presented as in-app performance, and this source-host result does not accept the experimental solver.

## Installed interactive follow-on, 2026-11-05

The user subsequently authorized development through an installed test. `/Applications/Grand Theft Auto Vice City.app` now contains a native mode selector, the existing normal macVC game/mod, and a separate `vcPhysicsHost`. Normal launch starts the existing Metal game. Option-launch offers the wing test; `open "/Applications/Grand Theft Auto Vice City.app" --args --physics-test` launches it directly. [Run, controls, reproduction and rollback instructions](../../tools/host/README.md) describe the exact scope.

The scene displays the flattened Dodo, selected wing, fixed wall/contact marker and an untouched control aircraft. D removes the tip; R repairs; Space resets. The same triangle selection publishes rendered and native collision geometry (48→28→48 wing faces). Contact, LOS and vertical indicators respond to that revision. The wall marks the existing stationary sphere contact probe; no authoritative building collider, moving aircraft, force-derived fracture or custom solver step is introduced.

Installed verification, retained in `build/installed-wing/installed-verification.json` and accompanying logs/captures:

- Strict deep signature verification, exact installed payload hashes and original backup fingerprints pass. The source-host executable uses bundled/system dependencies, without the old plugin or Homebrew/repository runtime paths.
- Direct and LaunchServices installed smoke launches each exit0. The 120-frame script damages, repairs, damages and resets, with zero assertion failures. Damage changes25446 framebuffer bytes; repaired capture is byte-identical to intact. The installed framebuffer image was inspected.
- Native host query proof remains65/65. All14 launcher/installation tests, `make test` and `make test-game-assets` pass. Existing solver tests/limitations remain intact.
- Installed normal launch initialized the Metal renderer and the existing177-hook mod; its owned test process stayed alive25seconds and was terminated. The earlier staged normal launch stayed alive80seconds. This proves startup compatibility, not a new driving/flight acceptance run. No test process remains running.

The latest installed hidden/unpaced scene measured CPU frame submission/presentation p50 **0.3668ms**, p95 **1.0768ms**, and four-probe query p95 **0.0078ms**, over120frames. These are isolated scene measurements including initialization transients, not full-game frame rate, GPU completion or solver cost.

The complete original app is preserved at `/Applications/Grand Theft Auto Vice City.before-physics-20261105-112121.app`. Normal game SHA-256 remains `a1d9e389975295c29ef2887383b21e1bb81da4ac48c224759e65feabf58004ed`; mod SHA-256 remains `8e9d2306c68debd261508fcc790ddc07069006087b7bbf84bcdac0aba7fd1069`. Copied dependency signatures and the top-level signature/entry metadata changed for the selector. `build/installed-wing/package-manifest.json` records the rollback path before either directory rename. Atomic journal failure and interrupted-swap recovery tests address the fresh review's sole Important finding. No second review was requested; there were no deferred Minor findings.

The separate mode is an architecture choice necessitated by the unavailable matching Metal-host source; it costs an additional launch mode and does not complete full-world physics. Dynamic flight/building collision, material/solver coupling and save/streaming/mission lifecycle remain open. Cross-filesystem installation is explicitly unsupported; this install verified both directories use the same filesystem. The reviewer independently verified staging, tests and dependencies; installed-path and visual/normal startup verification were performed by the author.
