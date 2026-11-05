# Controlled aircraft/building slice: host contracts and integration decision

2026-11-05. Baseline `c7fe3b7`, macVC 1.0.5217 ARM64. **Decision: do not hand the island to the experimental solver through the current binary adapter. Require matching engine source, or a source-implemented host bridge, before expanding the solver.** The missing capability is coherent **per-instance physical geometry and motion ownership at a host phase boundary**, including all native query consumers and resource lifetime. There is no experimental game build or in-app performance result from this investigation.

This follows the user's new milestone: one aircraft/wing against one building; four host contracts first, then an experimental app and measurement. It supersedes the next numerical-optimization work item, without discarding the numerical experiments or the broader design.

## Scope and evidence

The candidate is the existing separately managed **Dodo, model181, CPlane**, and one generation-valid **doontoon22 building instance, model1868**. These are candidate integration identities, not an admitted live island. The native probe uses synthetic isolated objects with those model numbers and synthetic sphere/empty COLs. It does not import the actual Dodo wing or building geometry, render a tear, create a detached wing, or run a contact solve. A second same-model aircraft is an isolation control, not an extra proposed island participant.

Pristine binary: UUID `02983a69-6bd0-33f7-b72b-2aca8bcb3c33`, SHA-256 `a715c2ce363265385b73ac6d2decd386f7d8bbccb61519766907a563a663cc95`. [The runner](../../tools/physics/check_slice_contracts.py) rejects other/patched inputs through the existing identity audit before copied code executes. It retains disassembly, input/harness hashes, build command, executable hash and both run results in `build/world-physics-evaluation/slice-contracts/final/manifest.json`.

REA providers are not connected. Methods here are LLVM disassembly, repository source inspection and isolated execution using the existing ARM64 instruction harness. **Static inventory is not runtime coverage; copied native callers are not live-game acceptance.**

## Four required contracts

| Contract | Demonstrated evidence | Missing proof/capability | Gate |
| --- | --- | --- | --- |
| Exclusive motion ownership | Existing complete native world-scheduler fixture reproduces 3 guarded passes and 3 deliberately unguarded failures. It covers ordinary control/collision retries/shift/stuck recovery with synthetic callbacks. The current owned Dodo adapter bypasses ambient CPlane route control. | Actual game traces proving zero native motion/impulse for the island and one solver advance per interval, including all later plugin/native writers, script/replay transforms and admission/handoff. The existing Dodo still calls native CPhysical control and uses native collision. | **Partial; not accepted.** |
| Authoritative collision/query geometry | The new probe executes native physical-pair, LOS-clear and vertical-sector callers. All select the shared model COL. Real native LOS/box/sphere math hits the original sphere and misses an empty replacement. Shared replacement affects both aircraft. | An instance-local geometry provider, bounds/sector updates and revision-aware queries covering physical contacts, LOS, LOS-clear, vertical probes and sphere/world tests. Current shot-specific render queries and supplemental contacts do not provide this. | **Reproduced gap.** |
| Geometry writeback | Existing carglass mesh cloning/cutting/denting and RW matrix/frame bindings show local rendering capabilities; existing tests remain in place. Native callers show that changing/removing RW state does not redirect COL queries. | A prepared/published solver snapshot mapped to actual wing vertices with collision, culling bounds, render frames and aero using one revision; same-model isolation and detached-body publication. No live solver-to-RW publication has been demonstrated. | **Not proven.** |
| Lifecycle handling | Current modules validate pool entries/generation/model/clump and clear histories on restart or selected repair/delete events. Native COL plane caches retain COL identity. | One island lease and ordered revoke/rebind across deletion, pool reuse/wrap, load/restart, teleport, repair, stream recreation and model/COL release. No live failure-injection or persistence test covers this complete island. | **Not proven.** |

## Exact native and plugin evidence

| Evidence | Direct observation | Consequence |
| --- | --- | --- |
| `CPlane::ProcessControl` `0x10027643c`; pose stores `0x100277440`–`0x100277460`; velocity stores `0x10027748c`–`0x1002774a8` | Ambient route control writes matrix/linear/angular motion directly, then calls `CMatrix::UpdateRW` at `0x1002774ec` and `UpdateRwFrame` at `0x1002774f4`. | Suppressing generic gravity or only collision does not suppress this controller. |
| [planeflight.cpp](../../src/planeflight.cpp), `Hook_Control` | Owned CPlane bypasses that ambient routine, then calls `roster.physical(v)` and submits flight impulses through native methods. | Existing ownership means ownership of the flight controller, not ownership by the experimental world. |
| `CWorld::Process` `0x100137f7c` | Retry, shift and inline stuck-recovery paths are separate from control. See the [previous executable ownership evidence](2026-11-05-world-physics-native-ownership.md). | Safe flags work in the controlled fixture; they do not prove immunity to other writers. |
| `CPhysical::ProcessEntityCollision` `0x100155af0`; COL loads `0x100155b1c`, `0x100155b28`; call `0x100155b3c` | Loads both models' `CBaseModelInfo +0x20` COL pointers and passes them to `ProcessColModels`. | A private RW mesh or solver triangle set is not automatically the contact geometry. |
| `CWorld::ProcessLineOfSightSectorList` `0x10012d794`; nonped COL load `0x10012d9e0`; call `0x10012d9fc` | Regular nonped LOS uses model COL, separately from optional wheel/ped cases. Static evidence in this investigation. | Weapons' current precise mesh replacement is a scoped hook, not universal world authority. |
| `GetIsLineOfSightSectorListClear` `0x10012f5c8`; COL loads `0x10012f610`, `0x10012f6c8`; calls `0x10012f624`, `0x10012f6dc` | Calls `CCollision::TestLineOfSight`, a different routine from `ProcessLineOfSight`. Full copied caller executed on its ordinary branch. | Hooking only the existing narrow LOS entry cannot cover this route. |
| `ProcessVerticalLineSectorList` `0x10012e38c`; COL load `0x10012e414`; call `0x10012e434` | Full copied caller passes model COL to `CCollision::ProcessVerticalLine`. Narrow phase is an observer here. | A render tear does not change ground/vertical queries. |
| `TestSphereAgainstSectorList` `0x1001314e4`; `ProcessColModels` call `0x100131688` | Separate native sphere/world route. Disassembled, not executed by the new probe. | It also needs a proven instance/bounds route; the existing shot scope supplies none. |
| [shotcollision.cpp](../../src/shotcollision.cpp), `Hook_World` / `Hook_Narrow` | Precise render-mesh replacement requires an active shot and nested query context; otherwise it delegates to the native routines. | Ordinary LOS and the other query kinds remain outside that replacement. |
| [towercollision.cpp](../../src/towercollision.cpp), `ColModels`; [panelcollision.cpp](../../src/panelcollision.cpp), `ColModels` | Both evaluate original native geometry first and add scoped contacts; the panel route excludes aircraft. | These are native contact supplements, not removal/replacement of the island's physical surface. |
| `CPlane::DeleteRwObject` `0x1002763d0` | The atomic branch detaches/destroys its RW object at `0x1002763f8`–`0x100276424`, then tail-calls base deletion at `0x100276438`. | A base-delete hook alone is not a universal before-destruction notification for a CPlane atomic. The current Dodo's actual clump path still needs separate live verification. |
| `CEntity::DeleteRwObject` `0x100152ccc`; `RemoveTrianglePlanes` `0x10004d330` | Base deletion releases model references; plane removal unlinks COL cache entries before freeing planes. | COL/mesh publication needs lifetime ownership, cache invalidation and safe retirement, not only a pointer swap. |

The source hook list is an inventory, not proof of installed active hooks. The new probe does not execute the plugin. These facts, combined with the inspected current hook behavior, establish the current bridge's gap; they do **not** prove that arbitrary future binary patching is mathematically impossible.

## Executable query controls

[world-physics-slice-native-probe.cpp](world-physics-slice-native-probe.cpp) copies complete native physical-pair, LOS-clear, vertical-sector and `TestLineOfSight` routines, along with native `TestLineBox`, matrix inversion and matrix/vector math. The harness relocates PC-relative data/calls. Two LOS-clear call targets are explicitly instrumented to observe the selected pointer before executing the unchanged native narrow routine.

Physical contact and vertical narrow phases are controlled observers: they prove **argument selection**, not physical hits, impulses or vertical intersection correctness. The sphere-only triangle-plane callback asserts zero triangles; it does not test caches. No scripts, renderer, streaming or solver run here.

Twelve fact assertions pass:

- Real native LOS hits an intact synthetic sphere and misses its empty replacement.
- Both same-model aircraft pass the shared model COL to physical contact, LOS-clear and vertical query consumers.
- The first aircraft still occludes native LOS after its entire synthetic RW pointer is removed. This is a routing control, not a rendered wing tear.
- Changing the shared model COL makes both aircraft clear the ray, demonstrating cross-instance effects.
- All expected native caller observers actually execute.

Three per-instance authority assertions deliberately fail for the first aircraft: contact, LOS-clear and vertical queries do not receive its private replacement. The native comparison aircraft retains the intact model as expected. [Retained output](2026-11-05-aircraft-building-contract-results.txt).

```sh
python3 tools/physics/check_slice_contracts.py \
  --binary "$HOME/Library/Application Support/macVC/vcmodernhud/macVC.vanilla" \
  --output-dir build/world-physics-evaluation/slice-contracts/final
# exit 1: fact reproduction succeeded; query authority is still missing.
```

`--facts-only` returns 0 for fact reproduction while the manifest still records failed native authority and unproven in-app contracts. Unsupported images, abnormal execution, build failures and changed inputs return 2; those are not physical rejections. Neither mode accepts an in-app bridge.

## Source-access and architecture decision

**Choose engine-source integration, or a versioned bridge implemented in engine source by the port maintainer. Do not expand the custom solver to compensate for this host gap.** The public [macVC README](https://github.com/gtamac/macVC/blob/main/README.md), checked again for this investigation, describes a download-only repository without source. No matching engine checkout was identified in the immediate workspace. Connected GitHub installation repository-name searches for `macVC` and `reVC` also returned no repositories; this is limited to the connection's accessible installations and those names. Private source availability elsewhere is unknown; no one has been contacted.

The exact required capability is an **entity-identity-aware geometry/ownership transaction at a defined world-step boundary**, with these source-level responsibilities:

1. **Motion lease:** admission/revocation keyed by entity generation and world epoch; common native motion/force/contact/shift/controller writers respect it. Controls become inputs. Native script teleport/replay/mission overrides revoke or explicitly update the lease. Trace before/after every native phase; never advance on collision retries.
2. **Query lease:** all relevant native contact, LOS/LOS-clear, vertical and sphere/world consumers resolve the entity's current immutable physical geometry revision, preserving native masks/ignore/nearest-hit semantics. Broad-phase bounds and sector membership follow the same revision. A detached wing is removed from the parent and registered as its own dynamic participant; another Dodo keeps its intact surface. Nested queries cannot observe temporary global model substitutions.
3. **Prepared geometry publication:** actual asset binding plus prepare/commit/retire for solver geometry, COL/cache data, render vertices/frames/bounds and aerodynamic surface state. Prepare allocations first; publish one revision at the barrier. The renderer's retained consumers must not use retired mesh storage. A failed solve/publication preserves the last valid physical state and reports the fault.
4. **Lifecycle lease:** notifications before object/model/COL destruction and after valid recreation; pin the building/island's required collision data independently of camera visibility. Revoke on restart/load/teleport/repair; test reused addresses, finite native generation wrap, teardown during callbacks and stream recreation. Preserve physical state or explicitly terminate the isolated experiment; do not resurrect an intact wing silently.

These are required semantics, **not existing exported API names**. A matching source delivery must include the 5217 game/world/entity/physical/plane/query/streaming code and the renderer/resource-lifetime changes needed to reproduce the installed port, or a newer reproducible host build implementing those semantics. An unrelated upstream reVC fork is not assumed ABI/render/save compatible. Starting a separate engine port would be a separate architecture/scope decision, not an invisible fallback.

Reopen the binary route only if a bounded implementation proves all four contracts through the same acceptance traces, including every listed query route and lifecycle case. Merely adding the missing LOS hook or mutating the shared COL does not reopen the gate.

## Build, performance and numerical status

The [slice plan](../superpowers/plans/2026-11-05-aircraft-building-slice.md) leaves the experimental-app step gated. **No test app was produced and no in-app slice performance was measured**, because host contract acceptance failed first. Building an app around the current split geometry would test a different architecture. The production executable/plugin remain unchanged.

Existing numerical work is retained unchanged. It still has bounded path/shape coverage, synthetic stiffness/material data, numerical impact dissipation and nonzero angular integration error; friction, calibrated aircraft shells/fracture and dense scheduling are unaccepted. A retained fast-contact run cost about54ms for4.17ms simulated; identical-input adaptive counts have varied. Those are **offline research limitations**, not measurements of this proposed island or reasons to continue solver optimization before the host contract exists. See [directed advancement](2026-11-05-world-physics-directed-advancement.md).

Verification on this change:

- `make test` and `make test-game-assets`: exit0, including existing native aircraft/query/geometry/lifecycle regressions.
- Existing native ownership checker: exit0, with 3 guarded passes and all3 unguarded assertions failing as intended.
- Existing release IPC/Bullet runner `--rigid`: exit0; 79 geometry,3 grazes,167 events,8 query workloads,12 material,34 persistent,63 coupling and4 fast-rotation checks pass, with both benchmark modes completing. This is preservation of bounded tests, not acceptance of the slice.
- Original Bullet controls rebuilt unchanged: 44 contacts,70 whole-step,15 material and71 structural checks pass; the retained original geometry control still fails42/50, aggregate exit1 as intended.
- New contract checker: facts exit0; native authority exit1; a tampered-image control returns2 before native execution. Python compile and `git diff --check` pass.
- Existing solver/fixture/test files are unchanged from `c7fe3b7`. Installed app executable SHA-256 remains `a1d9e389975295c29ef2887383b21e1bb81da4ac48c224759e65feabf58004ed`, plugin remains `8e9d2306c68debd261508fcc790ddc07069006087b7bbf84bcdac0aba7fd1069`.

Detailed logs/provenance remain in the ignored `slice-contracts` scratch directory. No sanitizer rerun is claimed for the unchanged solver in this audit. None of these offline checks substitutes for required in-app hit/miss/tear, lifecycle and frame-cost measurements.

One fresh reviewer checked `c7fe3b7..73fcdb6`, relevant current adapter source and retained disassembly, and independently reproduced12 passing facts/3 failed authority assertions/exit1. No Critical, Important or Minor findings. The reviewer confirmed the distinction between rejecting the current adapter and proving every future binary implementation impossible; they did not repeat the broader author regression suites. The evidence/decision milestone is reviewed; the in-app milestone is still pending the host contracts.
