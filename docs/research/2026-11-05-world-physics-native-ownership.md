# Native motion ownership: first executable boundary proof

2026-11-05. Static LLVM inventory and isolated execution of supplied macVC 5217 instructions. No REA provider is connected; these observations come from the repository's LLVM/native-fixture tools. The game was not launched or modified.

## Identity and methods

The saved pristine image is `~/Library/Application Support/macVC/vcmodernhud/macVC.vanilla`, UUID `02983a69-6bd0-33f7-b72b-2aca8bcb3c33`, SHA-256 `a715c2ce363265385b73ac6d2decd386f7d8bbccb61519766907a563a663cc95`. The [audit tool](../../tools/physics/audit_native.py) requires this identity and rejects an already-patched image.

The [retained inventory](2026-11-05-world-physics-native-inventory.json) records 14 vtables and 21 methods, including player/NPC pedestrians, automobiles, bikes, boats, helicopters, planes, objects, buildings, dummies and cutscene objects. The image uses `DYLD_CHAINED_PTR_64_OFFSET` pointers: decoding raw vtable entries as absolute addresses produces incorrect targets. The tool checks the advertised pointer format before resolving the targets.

For the concrete physical actor classes, the native world calls virtual slots `0x48` (control), `0x50` (collision) and `0x58` (shift). Control has class-specific implementations; collision/shift generally resolve to `CPhysical`. Static/default `CEntity` slots share merged no-op code. This is a static dispatch inventory, not proof that all actor behaviors have migrated.

| Native evidence | Observation | Integration consequence |
| --- | --- | --- |
| `CWorld::Process`, `0x100137f7c` | Ordinary moving-list control, repeated collision passes, shift passes and final stuck recovery are separate phases. | A control-only handoff does not establish ownership of movement. |
| Control dispatch `0x100138320`, possible second pass `0x10013840c` | Uses the class control vtable slot. | Preserve class gameplay/AI work while identifying and suppressing native motion for transferred instances. |
| Collision dispatch `0x1001384b4` through `0x100138628` | Can retry the same body six times. | A custom solver must not advance once per retry. |
| Shift dispatch `0x1001387cc`, `0x100138880` | Two later passes can still move an unsafe body. | Mark accepted ownership state and guard later motion paths. |
| Stuck recovery `0x1001388c4`–`0x1001388ec` | Inline velocity damping followed by direct move/turn integration. | Neither a self-collision hook nor a single pose writeback covers this path. |
| `CPhysical::ProcessControl`, `0x100155d50` | Gravity, queued movement/turn contributions, drag and bookkeeping are mixed. | Returning early from the wrong routine can drop required native state updates. |
| Native gravity call `0x100155e70` | Calls `CPhysical::ApplyGravity`, `0x100155fd4`. | Imported solver gravity cannot also be applied natively. |
| `CAutomobile::ProcessControl` | Contains several direct move/turn calls and two direct base-control call sites. | Skipping only the world's virtual collision dispatch leaves additional writers. |
| `CWorld` post-motion ped work | Boarding, attached-ped and seat-position routines can rewrite transforms later. | Character/seat/animation ownership is a separate required part of the bridge. |

The configured patch list contains world/class control and collision hooks, but not the common `CPhysical` control, gravity, move-speed or shift entries. "Listed hook" in the inventory means membership in the source patch list, not proof that a runtime plugin hook is active.

## Isolated native execution

The [fixture](world-physics-native-probe.cpp) executes the complete copied `CWorld::Process`, the copied native `CPhysical::ProcessControl`, its ordinary gravity path and the complete native `ApplyMoveSpeed`. Existing native-fixture relocation maps external calls and data to isolated process-owned storage. Rendering, AI and collision outcomes are controlled callbacks; it does not execute full automobile/pedestrian control, actual contact geometry or the live game.

Two synthetic physical actors are separated by 100 native units. One is designated solver-owned; the other remains native. The tested ownership callback marks safe position (`0x4000` at entity `+0x60`), clears stuck state (`0x2000`) and preserves `UsesCollision` (`0x100`). This allows the native scheduler to skip retries for the accepted body. No actual Bullet world is linked into this fixture.

| Scenario | Owned actor | Native actor / handoff |
| --- | --- | --- |
| Ordinary accepted contact | No native gravity, collision retry, shift or move; original velocity/position retained. | Native gravity and one contact movement remain. |
| Unresolved native contact | Still no native movement. | Six collision calls, two shifts, nine total move calls including stuck recovery; native 0.707 velocity damping remains. |
| Ownership release | First frame remains protected. | Released actor resumes native gravity/movement on the following frame; other actor keeps its native behavior. |

All three guarded scenarios pass. Turning suppression off makes all three owned-body assertions fail, while native-body expectations still pass. This negative control demonstrates that the fixture detects duplicated native motion rather than merely observing unchanged empty objects. [Retained results](2026-11-05-world-physics-native-results.txt).

## Reproduction

```sh
python3 tools/physics/check_native_ownership.py \
  --binary "$HOME/Library/Application Support/macVC/vcmodernhud/macVC.vanilla" \
  --output-dir build/world-physics-evaluation/native
```

The checker inventories the target, compiles the isolated fixture and runs guarded and deliberately unguarded configurations. Expected guarded exit is 0; expected negative-control exit is 1; the checker itself exits 0 only when both expectations hold. Full disassembly and working logs remain in ignored scratch storage. The binary is only read.

## What this does not prove

This is a **boundary experiment**, not a complete native bridge. It establishes the ordinary native scheduler's retry/integration behavior and a viable suppression seam under the fixture's controlled conditions. It does not establish all transform writers, replay/cutscene branches, real class controllers, collision/query synchronization, native/solver interactions, mesh publication, streaming, pool-generation identity, mission behavior, repair or live gameplay.

The native and owned actors do not interact in this test. A production handoff still requires migrating a complete interacting island or a proven two-way bridge; presenting a native dynamic actor as an immovable solver mirror is not justified. Safe-position bookkeeping alone is also insufficient if some other native path subsequently changes an owned actor's flags or pose. Source-level integration remains preferable if the actual port source becomes available.
