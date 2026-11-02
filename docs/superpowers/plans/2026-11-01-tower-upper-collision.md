# Upper tower collision implementation plan

> For agentic workers: execute inline with the existing shared checkout. Root owns integration and independent review; no commits or game operations.

**Goal:** Owned player aircraft hit actual opaque upper building surfaces omitted by shipped collision primitives.

**Architecture:** Bake clipped original triangles for the four inventoried solid building models. Context-chain the native CPhysical entity collision plus the Skimmer's direct CAutomobile caller, and supplement a zero native ProcessColModels body result with a stable private native COL, retaining the original native physics/contact consumer.

**Tech stack:** C++17, arm64 macVC1.0.5217, original DFF/COL/IDE assets.

**Spec:** Root-approved design in the current task; evidence `build/native-tower-ray-evidence.txt` and complete inventory `build/towercollision-inventory.txt`.

## Constraints and review focus

- Models1868,1882,1924,1857 only; no LOD proxies, light overlays or blimps.
- Native original COL always runs first. Positive native contacts and every ordinary/foreign/ground/scripted view remain unchanged.
- Admission revalidates airworld ownership, vehicle/building pool generation, model/MI/COL and exact native matrix arguments.
- Four persistent private COLs own native triangle-plane LRU lifetimes; no stack cache pointers, global model swaps, render RW dependency or collision-path disk access.
- Native zero-body-contact branch only; original CPhysical tail owns impulse, proof/status and damage. Other collision consumers remain native.
- Parent handles Makefile/minimap/patcher/app integration. This worker edits new module/tests/private bake helper only.

## Task 1: Exact asset data

- [x] Write real-asset regression with the two high facade rays and lower native-positive controls; baseline must fail upper contact.
- [x] Add private bake helper/generator and compact generated static data. Independently compare regeneration to original opaque clipped faces for all four models; retain exact float coordinates and native scratch limits.
- [x] Verify generated upper surface coverage, no geometry below the primitive-top seam, and no extra candidate model.

## Task 2: Native contact seam

- [x] Write hook fixture that runs native-shaped original contact -> real installed hook -> native contact consumer, failing for missing upper contact.
- [x] Implement Install/context guards, original-first zero-result supplementation and four persistent native COLs.
- [x] Cover ordinary contact preservation, wrong context/matrix/COL/model, actor/building generation changes, streaming replacement, nested calls, exception restoration, partial installation retry, restart/shutdown plane-cache cleanup.
- [x] Verify focused normal and sanitizers plus actual native ABI/asset tests; preserve parent-owned full suite results.
- [x] Freeze source and request parent's independent review. Fix concrete review issues, then rerun affected checks.

## Review correction

The independent review found that the stock Skimmer's CAutomobile caller directly invokes ProcessColModels rather than CPhysical. A narrow model190/kind1 context now covers that exact native caller. Its original body and four wheel outputs run first; the supplementary solver retains the actual A COL and uses aligned four-point/four-fraction scratch outputs, leaving original vehicle wheel arrays byte-identical. Exact matrices, MI COL and body/wheel output arguments remain mandatory. The missing-caller regression was observed failing before the change, then passes along with nested Skimmer, generation refusal, native caller/line-consumer ABI and fresh sanitizer checks.

## Acceptance boundary

The standalone fixture executes the installed production hooks with native-shaped original callbacks. The exact-build test reads original arm64 instructions and layouts; it does not execute the game's solver. Real installed aircraft impulse/damage acceptance remains pending root's controlled checks. Only owned aircraft body contacts gain the missing triangles. Native vertical wheel queries, pedestrians, helicopters, line-of-sight and bullets retain their existing collision data.
