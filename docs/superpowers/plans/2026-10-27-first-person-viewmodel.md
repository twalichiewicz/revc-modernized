# First-person viewmodel (Plan 3 of 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In first person on foot, the Colt 45 is drawn as a proper viewmodel: Tommy's own arm, from whichever
outfit he's wearing, cut into articulated fingers that grip a new low-poly, photo-textured Colt through anchors.
It's drawn in a pass of its own (its own FOV, never in a wall, lit like his body), with draw, holster, fire,
reload and empty animations. Every other gun keeps today's viewmodel until Plan 4.

**Architecture:** Pure, unit-tested modules do the work on plain data: `vmmath` (frames), `vmarms` (cut his arm
out of his skinned mesh and give the fingers joints), `vmgun` (the gun asset), `vmpose` (bone frames from the
gun, finger IK), `vmview` (framing, FOV), `vmanim` (animation state), `vmskin` (CPU skinning). An offline tool
(`vcmm-viewmodel`) runs the same code on the game's own DFFs for every outfit and renders the result through
Blender, so the geometry is checked before the game ever runs it. The runtime (`fpgun.cpp`) reads his clump
through RenderWare's skin API, draws through `RwIm3D` with the game's ped lighting after
`CRenderer::RenderFirstPersonVehicle`, and hides his real arms by shrinking their bones.

**Tech Stack:** C++17 (the dylib, tests and tools), Blender 5.2 Python (the gun build, developer only),
RenderWare/librw gl3 through the game's exported symbols.

**Spec:** `docs/superpowers/specs/2026-10-27-first-person-gunplay-design.md` ("The viewmodel", "Weapons covered",
"Settings"). Reference: `docs/research/viewmodel-spike/` (approved look, Colt values).

## Global Constraints

- No Rockstar data in the repo: his arms are built at runtime from the loaded clump; the gun build reads the
  game's `colt45.dff` only to measure it; only numbers derived from it are committed.
- Vice City fidelity: the low-poly gun stays within 250–400 triangles and a 256×256 texture, detail painted in.
- Tommy's own mesh and texture: nothing of his arm is replaced; only the finger slab is cut (and capped).
- It must work for every outfit and fail safe: if a model doesn't split as expected, first person falls back
  to today's viewmodel (`viewmodel.cpp`) and logs why.
- First person on foot only. `FirstPersonGuns = 0` gives today's first person exactly.
- Anchors are positions only, never drawn.
- Commits: don't commit (the user decides when). Install only when the game isn't running
  (`pgrep -f "Grand Theft Auto Vice City.app/Contents/MacOS/macVC"`); never kill the game.
- Approved Colt values (spike): hold = the game's Colt frame, scale `(x1 − x0) / 0.216 × 0.95`, moved 0.04 m down
  and 0.03 m forward in the hand; anchors on the left panel 30 mm behind the front strap (pinky 32 mm), finger
  half-thickness 8 mm; hip framing eye `(−0.150, −0.195, 0.160) × scale` in the gun frame, looking 13° right
  and 3° down; vertical FOV 62°; forearm 28° down and 12° out from the hand's axis.

## Review Focus

1. **An outfit whose hand doesn't split as expected** (`player6.dff` has its bind pose on other axes; five
   outfits have hooked fingers, seven extended ones). Expected: every outfit splits in the hand bone's own
   frame; any that can't is refused with a reason and first person shows today's viewmodel. Pinned by the
   offline tool's all-outfit report (Task 4) and `TestNoSlabIsRefused`.
2. **Changing outfit mid-game** (a clothes pickup swaps his model). Expected: the arms are rebuilt from the new
   model the next frame they're needed, never drawn from a freed one. Pinned by the cache key test in Task 8.
3. **Switching to a gun without a new viewmodel** (anything but the Colt in this plan). Expected: today's viewmodel
   with his real arms, the same frame; no frame with both arms or with none. Pinned by `TestUncoveredGunFallsBack`.
4. **Moments the viewmodel isn't used** (climbing, swimming, the shootdodge's dive and landing, falling, dying,
   getting into a car). Expected: his real arms come back that frame. Pinned by `TestStatesThatFallBack`.
5. **Render state after the pass** (the depth clear, lighting, fog, blending). Expected: nothing leaks into the
   HUD, motion blur or screen droplets drawn after it; checked in game (Task 8, Step 9).

---

### Task 1: Frames and skinning (`vmmath`, `vmskin`)

**Files:**
- Create: `src/vmmath.h` (header-only), `src/vmskin.h`, `src/vmskin.cpp`
- Test: `tests/test_vmskin.cpp`
- Modify: `Makefile` (add `src/vmskin.cpp` to PLUGIN, a `test_vmskin` target, and run it in `test`)

**Interfaces:**
- Produces (`vmmath.h`, namespace `vm`):
  - `struct Vec3 { float x, y, z; }` with `Add`, `Sub`, `Mul(Vec3, float)`, `Dot`, `Cross`, `Length`, `Normal`.
  - `struct Frame { Vec3 x, y, z, pos; }`: an affine matrix as RenderWare keeps one (axes and origin are
    columns; RW's 16-float layout is `x, flags, y, pad, z, pad, pos, pad`).
  - `Frame Identity()`, `Frame Compose(const Frame &a, const Frame &b)` (a·b), `Frame Inverse(const Frame &f)`
    (general affine, for skin-to-bone matrices that carry scale), `Vec3 Apply(const Frame &, Vec3)`,
    `Vec3 Rotate(const Frame &, Vec3)` (no translation), `Frame FromRw(const float *m)`, `void ToRw(const Frame &, float *m)`.
  - `Frame TurnAbout(Vec3 axis, float angle, Vec3 point)` (rotation about a line),
    `Frame Swing(Vec3 from, Vec3 to, Vec3 point)` (the least rotation taking direction `from` to `to`, about `point`),
    `Frame Look(Vec3 eye, Vec3 forward, Vec3 up)` (x right, y forward, z up, origin at the eye).
- Produces (`vmskin.h`):
  - `struct SkinVertex { Vec3 pos, normal; float u, v; uint8_t bone[4]; float weight[4]; }`
  - `struct Triangle { uint16_t v[3]; uint16_t material; }`
  - `void Skin(const std::vector<SkinVertex> &in, const Frame *matrices, std::vector<Vec3> &pos, std::vector<Vec3> &normal)`:
    `pos = Σ w·(M_b · p)`, normals rotated the same way and normalised. `matrices[b]` is already `W_b · Bind_b⁻¹`.

- [ ] **Step 1: Write the failing tests** in `tests/test_vmskin.cpp` (the repo's `CHECK` macro style):
  - `TestInverseUndoesCompose`: for a frame with rotation 30° about (1,2,3), scale 1.2 and origin (4,5,6),
    `Compose(f, Inverse(f))` is the identity within 1e-5.
  - `TestRwLayoutRoundTrips`: `FromRw(ToRw(f))` equals `f`; `FromRw` reads the origin from floats 12–14.
  - `TestSwingTakesDirection`: `Rotate(Swing(a, b, p), Normal(a))` equals `Normal(b)`, and `p` is fixed.
  - `TestRestPoseIsIdentity`: with every matrix the identity, `Skin` returns the input positions and normals.
  - `TestWeightsBlend`: a vertex 50/50 on two bones translated by (0,0,1) and (0,0,3) lands at +2 in z.
  - `TestLookFrame`: `Look(eye, (0,1,0), (0,0,1))` maps (1,0,0) to eye + (1,0,0) and (0,1,0) to eye + (0,1,0).
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmskin`. Expected: compile error (no `vmskin.h`).
- [ ] **Step 3: Implement** `vmmath.h` and `vmskin.cpp` as specified.
- [ ] **Step 4: Run them.** Run: `make build/test_vmskin && build/test_vmskin`. Expected: `all vmskin tests passed`.

### Task 2: His arms, articulated (`vmarms`)

**Files:**
- Create: `src/vmarms.h`, `src/vmarms.cpp`
- Test: `tests/test_vmarms.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `vmmath.h`, `vmskin.h` (Task 1).
- Produces (`vmarms.h`, namespace `vm`):
  - `struct SkinnedMesh { std::vector<SkinVertex> vertices; std::vector<Triangle> triangles; std::vector<int> tags; std::vector<Frame> bind; }`
    (`tags[i]` and `bind[i]`: skin bone i's HAnim tag and its bind frame in skin space, the inverse of its
    skin-to-bone matrix).
  - `enum ArmBone { kUpper, kFore, kHand, kIndex, kIndexTip, kMiddle, kMiddleTip, kRing, kRingTip, kPinky, kPinkyTip, kThumb, kThumbTip, kArmBones }`
  - `enum Digit { kDigitIndex, kDigitMiddle, kDigitRing, kDigitPinky, kDigitThumb, kDigits }`
  - `struct Arm { std::vector<SkinVertex> vertices; std::vector<Triangle> triangles; Frame bind[kArmBones]; Vec3 tip[kDigits]; Vec3 pole[kDigits]; }`
    (vertices' `bone[]` are `ArmBone`s; `tip[d]`: digit d's fingertip, the end of its tip bone, in skin space;
    `pole[d]`: the side its middle joint bends out to, in skin space, for IK).
  - `struct Arms { bool ok = false; std::string why; Arm right, left; }`
  - `Arms Articulate(const SkinnedMesh &mesh)`.

The cut, done in the **hand bone's own frame** (`Bind_hand⁻¹ · v`). Measured on all 12 outfits, that frame is the
same for every one: x runs along the hand to the fingers (knuckle at x 0.088), +y is the palm side the fingers
curl to, z runs across the hand, and the thumb is at +z on the right hand and −z on the left. (In bind space
`player6.dff` has its axes permuted, so nothing may assume bind-space axes.)

1. **His arm:** triangles whose three vertices' strongest bones are all in {upper arm, forearm, hand, fingers}
   (tags 22–25 right, 32–35 left), re-indexed. Weight on any other bone moves to the upper arm.
2. **The slab:** vertices whose strongest bone is the finger bone (25 / 35). Its z range `[z0, z1]` is cut at the
   quarter points by bisecting each slab-only triangle that crosses a cut (new vertices interpolate position,
   normal, UV and weights). Vertices on a cut are duplicated per side so the four pieces separate, each cut's
   boundary loops are capped (a fan, flat normal, UV `(64/256, 60/256)`: plain skin), and each finger is
   narrowed about its own middle to 0.86 of its width.
3. **Index on the thumb's side:** the finger nearest the thumb's z is the index, then middle, ring, pinky.
4. **Finger joints, by the finger's own shape** (hooked and extended fingers both): geodesic distance over the
   slab's edges from its vertices nearest the knuckle. Per finger, the tip point is the centroid of its
   vertices beyond 0.9 of its longest distance, the bend is the centroid of those at 0.45–0.55, the base
   bone runs knuckle → bend and the tip bone bend → tip. Each finger vertex's weight goes to tip by
   `smoothstep(0.4, 0.6, g / gMax)`, the rest to base. Bone frames: x along the bone, z the hand's z.
   `pole[d]` is the bend's offset from the knuckle–tip line (the side the joint bends out to).
5. **The thumb:** hand-bone vertices with local y > 0.032, z on the thumb's side beyond 0.01, and x > 0. Its axis is
   the principal axis of those points (pointing towards the fingers), its base bone from 0.2 to 0.6 of its
   extent and its tip bone from 0.6 to the end. Weights move onto them by `smoothstep(0.1, 0.3, t)` (on the
   thumb) and `smoothstep(0.5, 0.65, t)` (the tip), as the spike did.
6. **Refusals** (`ok = false`, `why` says which): no hierarchy tag for 22–25 / 32–35, fewer than 12 slab
   vertices on a hand, a cut that leaves an open boundary it can't cap, or fewer than 8 thumb vertices.

- [ ] **Step 1: Write the failing tests** in `tests/test_vmarms.cpp`, on a synthetic arm built in the test
  (upper arm and forearm boxes on bones 22 and 23, a palm box on 24 with a thumb box below it at +z, and a
  finger slab on 25: a strip 0.09 wide in z, bent at a corner like `player.dff`'s hook; the same mirrored for
  32–35; a torso box on bone 3 that must not be kept). Each bone's bind frame matches the measured hand frame.
  - `TestKeepsOnlyHisArm`: no output vertex sits in the torso box; triangle count is the arm's.
  - `TestFourSeparateFingers`: the slab's triangles form exactly four connected pieces (by shared vertices),
    ordered index → pinky from the thumb's side.
  - `TestCutsAreCapped`: within each finger, every edge on a cut plane is used by exactly two triangles.
  - `TestFingerWeights`: every finger vertex is weighted only to its own finger's base and tip bones, weights
    summing to 1 within 1e-5; the vertex furthest along the hook is on the tip bone.
  - `TestThumbWeights`: the thumb box's end vertices are on `kThumbTip`, its root on `kHand`.
  - `TestOtherBonesFoldIntoUpperArm`: a vertex 30% on the clavicle (21) and 70% on 22 ends 100% on `kUpper`.
  - `TestBothHandsMirror`: the left arm has the same counts as the right, its index at −z.
  - `TestPermutedBindAxes`: the same synthetic mesh with every vertex and bind frame rotated 90° about two
    axes (as `player6.dff` is) gives the same counts and weights.
  - `TestNoSlabIsRefused`: without bone-25 vertices, `ok` is false and `why` names the right hand's slab.
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmarms`. Expected: compile error (no `vmarms.h`).
- [ ] **Step 3: Implement** `Articulate` as described.
- [ ] **Step 4: Run them.** Run: `make build/test_vmarms && build/test_vmarms`. Expected: `all vmarms tests passed`.

### Task 3: The gun asset (`vmgun`) and the Colt's build

**Files:**
- Create: `src/vmgun.h`, `src/vmgun.cpp`, `tests/test_vmgun.cpp`
- Create: `tools/guns/lib.py` (the spike's `lib.py`), `tools/guns/gamefiles.py` (IMG and DFF reading without
  bpy, from the spike's `vcextract.Img` and `vcdff.parse/geometry`), `tools/guns/kit.py` (the photo shoot, UV
  projection, palette, `.vmg` writer), `tools/guns/colt45.py` (the detailed and low Colt from the spike's
  `colt.py` and `v2_colt_low.py`, its parts, anchors, sight, muzzle and hold), `tools/guns/README.md`
- Create: `assets/guns/colt45.vmg` (built, committed), `src/gunassets.S`
- Modify: `Makefile` (a `guns` target for developers: `GAMEDATA=… make guns`; `src/gunassets.S` in PLUGIN,
  depending on `assets/guns/*.vmg`; `test_vmgun`)

**Interfaces:**
- Consumes: `vmmath.h` (Task 1).
- Produces (`vmgun.h`, namespace `vm`):
  - `struct GunVertex { Vec3 pos, normal; float u, v; uint32_t part; }`
  - `struct Anchor { char name[16]; Vec3 pos; uint32_t part; }` (part `0xFFFFFFFF`: on the body)
  - `struct Part { char name[16]; Vec3 pivot; }` (part 0 is always `body`)
  - `struct Gun { float scale; Frame hold; Vec3 muzzle, sightRear, sightFront; std::vector<Part> parts; std::vector<Anchor> anchors; std::vector<GunVertex> vertices; std::vector<uint16_t> indices; int atlasWidth, atlasHeight; std::vector<uint8_t> atlasRgba; }`
  - `bool ParseGun(const uint8_t *data, size_t size, Gun &out, std::string &why)`
  - `const Anchor *FindAnchor(const Gun &, const char *name)`; `int FindPart(const Gun &, const char *name)` (−1 if none)
  - `gunassets.S` exports `_vmgun_colt45` and `_vmgun_colt45_end` (`.incbin "assets/guns/colt45.vmg"` in `__DATA,__const`).

The `.vmg` format (little-endian), written by `kit.py`:

```
char  magic[4] = "VMG1"
u32   vertexCount, triangleCount, partCount, anchorCount
f32   scale
f32   hold[12]                    gun frame -> hand frame: x axis, y axis, z axis, origin
f32   muzzle[3], sightRear[3], sightFront[3]
Part  parts[partCount]            char name[16]; f32 pivot[3]
Anchor anchors[anchorCount]       char name[16]; f32 pos[3]; u32 part
Vtx   vertices[vertexCount]       f32 pos[3], normal[3], uv[2]; u32 part
u16   indices[3 * triangleCount]  then 0 or 2 bytes of padding to a 4-byte boundary
u32   atlasWidth, atlasHeight
u8    palette[256 * 4]            RGBA
u8    pixels[atlasWidth * atlasHeight]
```

The gun frame is the spike's (x right, y forward along the barrel, z up, origin at the rear of the tang at the
grip's foot), in metres **already scaled** to the game's model (`scale`), so `hold` is rigid. The Colt's
values (in real mm before scaling, from the spike): `hold` rotation maps gun y → hand x, gun x → hand −y,
gun z → hand z, origin `(x0 + 0.03, yc, z1 − 0.1355·scale − 0.04)` from the game's `colt45.dff` extents;
parts `body`, `slide` (slide, both sights, bushing; pivot irrelevant, it translates), `trigger`, `magazine` (the
base plate plus a body inside the grip, drawn only when it drops); anchors `r_index` (−1.0, 101.3, 85.0,
part `trigger`), `r_middle`/`r_ring` on the left panel at z 60 / 43 (`x = −22.8`, `y = front_strap_y(z) − 30`),
`r_pinky` at z 27 (32 mm back), `r_thumb` (−19.0, 62.0, 97.0); muzzle (0, 217.5, 117); rear sight
notch (0, 26, 134); front sight top (0, 203.5, 136).

- [ ] **Step 1: Write the failing tests** in `tests/test_vmgun.cpp`:
  - `TestParsesAHandMadeGun`: a two-triangle gun written by the test into a byte vector (one part, one anchor,
    a 2×2 atlas) parses back exactly, palette expanded to RGBA.
  - `TestRefusesBadData`: wrong magic, a truncated vertex block and a triangle index past the vertex count
    each return false with a reason.
  - `TestTheColt`: the embedded `_vmgun_colt45` parses; 250–400 triangles; a 256×256 atlas; parts `body`,
    `slide`, `trigger`, `magazine`; anchors `r_index`, `r_middle`, `r_ring`, `r_pinky`, `r_thumb`; `hold`'s axes
    orthonormal within 1e-4; `scale` between 1.0 and 1.2.
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmgun`. Expected: compile error (no `vmgun.h`).
- [ ] **Step 3: Write the Blender build** (`tools/guns/*.py`), port the spike's Colt into it with the parts and
  anchors above, and run it: `GAMEDATA="/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData" make guns`.
  Expected: `assets/guns/colt45.vmg` written, the script prints the triangle count (about 290) and the
  hold, and `build/guns/colt45-atlas.png` and `build/guns/colt45-preview.png` look like the spike's.
- [ ] **Step 4: Implement** `vmgun.cpp` and `gunassets.S`.
- [ ] **Step 5: Run them.** Run: `make build/test_vmgun && build/test_vmgun`. Expected: `all vmgun tests passed`.

### Task 4: The offline check (`vcmm-viewmodel`)

**Files:**
- Create: `tools/viewmodel/dff.h`, `tools/viewmodel/dff.cpp` (a skinned DFF into `vm::SkinnedMesh`: the
  spike's `vcdff.py` in C++, RenderWare 3.3's 12 lighting bytes and `0xDEADDEAD` markers included),
  `tools/viewmodel/viewmodel.cpp`, `tools/viewmodel/render.py` (Blender: an OBJ over a background, as the spike did)
- Modify: `Makefile` (a `viewmodel` target building `build/vcmm-viewmodel` from the tool, `tools/bake/gamefiles.cpp`
  and the `vm*.cpp` modules)

**Interfaces:**
- Consumes: `vm::Articulate` (Task 2), `vm::ParseGun` (Task 3); later `vm::Pose`, `vm::Framing`, `vm::Skin`
  (Tasks 5–6) when they exist.
- Produces: `build/vcmm-viewmodel <GameData> <outdir> [outfit…]`: for each outfit (`player.dff`, `player2–9.dff`,
  `play10–12.dff`) a report line (`<name>: ok, right 4 fingers, N slab vertices, M thumb vertices` or
  `<name>: refused: <why>`), and per outfit `<outdir>/<name>.obj` (+ `.mtl`, the outfit's texture as PNG),
  posed holding the Colt once Tasks 5–6 exist.

- [ ] **Step 1: Write the tool** (its own check is the report: no unit test).
- [ ] **Step 2: Run it.** Run: `make viewmodel && build/vcmm-viewmodel "$GAMEDATA" build/vm`.
  Expected: all 12 outfits `ok`. Any refusal is a finding for Task 2 (fix the rule there, with a test).

### Task 5: Posing (`vmpose`)

**Files:**
- Create: `src/vmpose.h`, `src/vmpose.cpp`, `tests/test_vmpose.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `vm::Arm` (Task 2), `vm::Gun`, `vm::Anchor` (Task 3), `vmmath.h`.
- Produces (`vmpose.h`):
  - `struct ArmAngles { float down = 28.0f, out = 12.0f; }` (degrees, the forearm off the hand's axis)
  - `void Pose(const Arm &arm, const Frame &handWorld, const Vec3 anchors[kDigits], const bool reach[kDigits], const ArmAngles &, bool left, Frame out[kArmBones])`:
    the skinning matrices `W_k · Bind_k⁻¹` for every arm bone.
  - `Frame HandFor(const Gun &gun, const Frame &gunWorld)` = `gunWorld · gun.hold⁻¹`.

1. **Hand:** `W_hand = handWorld`.
2. **Forearm:** first carried rigidly with the hand as in his bind pose (`W_hand · Bind_hand⁻¹ · Bind_fore`, a
   straight wrist), then swung about the wrist (`Swing`) so it runs from the wrist back along
   `normalize(−handX − handZ·tan(down) ± handY·tan(out))`, outward being away from the body (−hand y for the
   right arm, as in the spike; mirrored for the left).
3. **Upper arm:** carried rigidly from the forearm, then swung about the elbow towards `back + (0.35 out, 0.55 up)` as the spike did.
4. **Fingers:** each digit's base and tip carried with the hand, then two-bone IK: base swung about the
   knuckle and tip about the bend so the tip point reaches the anchor, the bend kept on the `pole` side.
   Reach is clamped to `[|L1 − L2| + 1 mm, L1 + L2 − 1 mm]`. A digit with `reach[d]` false stays as carried.

- [ ] **Step 1: Write the failing tests** in `tests/test_vmpose.cpp` (the synthetic arm from Task 2's test
  helpers, moved to `tests/vmtestarm.h` so both use it):
  - `TestBindPoseIsRest`: with `handWorld = Bind_hand`, angles 0 and no reach, every output is the identity.
  - `TestHandFollowsGun`: `Apply(out[kHand], Bind_hand.pos)` equals `HandFor(gun, gunWorld).pos`.
  - `TestForearmAngles`: the elbow lies along the direction above from the wrist, its distance the bind
    forearm's length, within 1e-4.
  - `TestFingertipsReachAnchors`: anchors placed 85% of each digit's reach away land within 1 mm.
  - `TestFingersBendTheRightWay`: the bend lies on the `pole` side of the knuckle–anchor line.
  - `TestUnreachableAnchor`: an anchor 1 m away gives a straight finger pointing at it, no NaN.
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmpose`. Expected: compile error.
- [ ] **Step 3: Implement** `vmpose.cpp`.
- [ ] **Step 4: Run them.** Run: `make build/test_vmpose && build/test_vmpose`. Expected: `all vmpose tests passed`.

### Task 6: Framing (`vmview`) and the offline render

**Files:**
- Create: `src/vmview.h`, `src/vmview.cpp`, `tests/test_vmview.cpp`
- Modify: `tools/viewmodel/viewmodel.cpp` (pose, skin and write the first-person OBJ), `Makefile`

**Interfaces:**
- Consumes: Tasks 1–5.
- Produces (`vmview.h`):
  - `struct Framing { Vec3 hipEye; float hipYaw, hipPitch; float aimBack, aimAbove; }` (hip eye in real metres
    in the gun frame, degrees; aiming: the eye `aimBack` behind the rear sight and `aimAbove` over the sight line)
  - `constexpr Framing kColtFraming{{−0.150f, −0.195f, 0.160f}, 13.0f, −3.0f, 0.22f, 0.02f}`
  - `Frame GunInView(const Gun &, const Framing &, float aim)`: the gun frame in view space (x right, y
    forward, z up, eye at the origin), hip at `aim = 0`, sights on the crosshair line at `aim = 1`.
  - `float FovSquash(float gameTanHalfV, float viewmodelFovV)` = `gameTanHalfV / tan(viewmodelFovV / 2)`:
    view-space x and z are multiplied by it so the viewmodel looks as through its own FOV.
  - `constexpr float kPushOut = 4.0f`: the whole viewmodel is scaled about the eye by this, so the game's near
    plane (0.15 m on foot) never clips it; it looks the same and depth is cleared before it's drawn.
  - `Vec3 ToWorld(const Frame &view, Vec3 viewSpace, float squash)`: `view · (x·squash·kPushOut, y·kPushOut, z·squash·kPushOut)`.

- [ ] **Step 1: Write the failing tests** in `tests/test_vmview.cpp`:
  - `TestHipEyeSitsWhereFramed`: `Apply(Inverse(GunInView(colt, kColtFraming, 0)), origin)` is the hip eye × scale.
  - `TestAimPutsSightsOnTheLine`: at `aim = 1` the rear and front sights have x = 0 and both lie
    `aimAbove × scale` below the view axis within 1e-4.
  - `TestSquashMatchesFov`: a point at 62°/2 up through `FovSquash(tan(55.4°/2), 62°)` projects at the game's
    vertical edge (tan 27.7°).
  - `TestPushOutKeepsTheImage`: a point and its pushed-out version project to the same screen point.
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmview`. Expected: compile error.
- [ ] **Step 3: Implement** `vmview.cpp`; then extend the tool to pose (Task 5), skin (Task 1) and write each
  outfit's arm and the Colt in view space, and `render.py` to render them with a 62° vertical camera at
  1600×900 over the spike's background.
- [ ] **Step 4: Run them.** Run: `make build/test_vmview && build/test_vmview`. Expected: `all vmview tests passed`.
- [ ] **Step 5: Render and look.** Run: `build/vcmm-viewmodel "$GAMEDATA" build/vm && blender -b -P tools/viewmodel/render.py -- build/vm`.
  Expected: `build/vm/player.png` matches `docs/research/viewmodel-spike/first-person-colt45.png`'s top frame
  (gun lower right, turned in, fingers on the anchors, no clipping); every other outfit grips the Colt
  without fingers through the grip. Fix anything wrong in Tasks 2/5 with a test before moving on.

### Task 7: Animation (`vmanim`)

**Files:**
- Create: `src/vmanim.h`, `src/vmanim.cpp`, `tests/test_vmanim.cpp`
- Modify: `Makefile`

**Interfaces:**
- Produces (`vmanim.h`):
  - `struct AnimInput { int weapon; bool covered; int clip; bool reloading; float reloadProgress; bool sprinting; bool shot; bool triggerHeld; float dt; }`
  - `struct AnimOutput { bool visible; int weapon; Vec3 offset; float pitch, roll; float slideBack; float triggerBack; float magazineDrop; bool magazineShown; float flash; }`
    (`offset` metres in view space, angles in degrees, part moves in metres along the part's own axis, `flash` 0..1)
  - `class Anim { public: AnimOutput Update(const AnimInput &); }`

Values (tunable by play later): **draw** 0.35 s, rising 0.25 m and turning up from 35° down, smoothstep;
**holster** 0.2 s, the reverse; switching plays the holster of the old gun, then the draw of the new one, and
`weapon` in the output stays the old gun until its holster ends. **Fire:** each `shot` kicks 0.025 m back and
4° up, decaying at 14/s; the slide goes 0.022 m back in 0.025 s and returns in 0.045 s; the trigger anchor 4 mm
back while held; flash 1 for 0.05 s. **Empty:** at `clip == 0` the slide stays back. **Reload** over
`reloadProgress`: 0–0.15 tilt in (roll 25°, pitch 10° up, 0.03 m down), 0.15–0.3 the magazine drops 0.12 m and
hides, 0.45–0.65 a new one rises in from 0.12 m down, 0.75–0.85 the slide goes forward, 0.85–1 tilt out.
**Sprint:** 0.04 m down and 25° down, eased at 6/s.

- [ ] **Step 1: Write the failing tests** in `tests/test_vmanim.cpp`:
  - `TestDrawRises`: after a switch to a covered gun, `offset.z` starts at −0.25 and reaches 0 at 0.35 s.
  - `TestSwitchHolstersFirst`: switching guns shows the old one until 0.2 s, then draws the new one.
  - `TestShotKicksAndSettles`: `slideBack` peaks at 0.022 by 0.025 s and is 0 by 0.08 s; the kick is under
    5% of its peak by 0.25 s.
  - `TestEmptyHoldsTheSlide`: with `clip == 0` after a shot, `slideBack` stays 0.022.
  - `TestReloadPhases`: at progress 0.2 the magazine is dropping, at 0.5 hidden, at 0.7 shown and seated, at
    0.8 the slide moving forward, at 1 everything at rest.
  - `TestUncoveredGunFallsBack`: an uncovered weapon makes `visible` false the same frame.
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_vmanim`. Expected: compile error.
- [ ] **Step 3: Implement** `vmanim.cpp`.
- [ ] **Step 4: Run them.** Run: `make build/test_vmanim && build/test_vmanim`. Expected: `all vmanim tests passed`.

### Task 8: In the game (`fpgun`)

**Files:**
- Create: `src/fpgun.h`, `src/fpgun.cpp`, `tests/test_fpgun.cpp` (the decisions, which are pure)
- Modify: `src/camera.cpp` (`AfterPreRender`: the new viewmodel or today's), `src/minimap.cpp` (install),
  `src/config.h/.cpp` (`FirstPersonGuns = 1`, `ViewmodelFov = 62`), `src/shaders.h/.cpp` (`View` and `Projection`:
  librw's matrices as it set them, beside `ViewProjection`), `tools/patch_macvc.py` (hook
  `_ZN9CRenderer24RenderFirstPersonVehicleEv`), `README.md`, `Makefile`

**Interfaces:**
- Consumes: everything above; `camera::FirstPersonOnFoot()`, `camera::AimBlend()`, `climb::Active()`,
  `swim::Swimming()`, `shootdodge::Posing()` (the dive and landing), `game.h` (`PedState`, weapons).
- Produces (`fpgun.h`):
  - `bool Install()`
  - `bool Covered(int weaponType)` (only the Colt, 17, in this plan)
  - `enum class Use { kNone, kOld, kNew }` and the pure
    `Use Choose(bool firstPersonOnFoot, bool enabled, bool covered, bool armsOk, int pedState, bool swimming, bool climbing, bool diving)`
  - `bool Prepare(uint8_t *ped)` (from `camera::AfterPreRender`: decides `Use`, poses this frame and, for
    `kNew`, shrinks his real arms; returns false when today's viewmodel should run instead)

At runtime:

1. **His mesh:** from `ped + 0x58` (the clump), its atomic's geometry; vertices, normals, UVs and triangles
   through the RW accessors `steering.cpp` binds (plus `RpMorphTargetGetVertexNormals`); skin through
   `RpSkinGeometryGetSkin`, `RpSkinGetNumBones`, `RpSkinGetVertexBoneIndices`, `RpSkinGetVertexBoneWeights`,
   `RpSkinGetSkinToBoneMatrices`; tags by asking `RpHAnimIDGetIndex` for tags 0–60. Built with `Articulate` once
   per geometry pointer and model index (a clothes change rebuilds), logged once: `fpgun: arms from <model>
   (N vertices, 4 fingers a hand)` or `fpgun: <why>; first person keeps the old viewmodel`.
2. **Hiding his real arms:** bones 22–25 and 32–35 shrunk to their upper arm's origin (`camera.cpp`'s `shrink`),
   which also hides the game's weapon in his hand.
3. **Drawing,** in the `RenderFirstPersonVehicle` hook after the original: save states (`shaders::SavedStates`);
   `CPed::SetupLighting` on him; ZTEST and ZWRITE on, `glad_glDepthMask(1)` and `glad_glClear(GL_DEPTH_BUFFER_BIT)`;
   fog off, cull none; the arm skinned and the gun's parts moved by the animation, all through `ToWorld` with
   the view from librw's view matrix (`shaders` exposes it) and `FovSquash` from its projection; arm triangles
   per material with his material's texture raster, the gun with its atlas (made once with `CreateRaster`);
   `RwIm3DTransform` with `kIm3DVertexUV | kIm3DLighting` (as `steering.cpp`), white vertex colour; the flash
   as an additive camera-facing quad at the muzzle with `*gpGunFlashRaster`, unlit, ZWRITE off;
   `CPed::RemoveLighting`; states restored.
4. **Fallback:** `Choose` gives `kOld` when not covered, arms not ok, or `FirstPersonGuns = 0`, and `kNone`
   off foot; states fall (0x2a), get up (0x2b), die (0x36), dead (0x37), swimming, climbing and diving give `kOld`.

- [ ] **Step 1: Write the failing tests** in `tests/test_fpgun.cpp`:
  - `TestCoveredGunUsesNew`: on foot, enabled, Colt, arms ok, idle → `kNew`.
  - `TestUncoveredGunFallsBack` (Review Focus 3): the Uzi → `kOld`.
  - `TestStatesThatFallBack` (Review Focus 4): fall, get up, die, dead, swimming, climbing, diving → `kOld`.
  - `TestSwitchedOff`: `enabled` false → `kOld`; `firstPersonOnFoot` false → `kNone`.
  - `TestArmsCacheKey` (Review Focus 2): the cache rebuilds when the geometry pointer or model index changes,
    not otherwise (the key comparison is a pure function `SameModel`).
- [ ] **Step 2: Run them to see them fail.** Run: `make build/test_fpgun`. Expected: compile error.
- [ ] **Step 3: Implement** `fpgun.cpp` and the changes listed; add the hook to `DEFAULT_HOOKS`.
- [ ] **Step 4: Run them, and the whole suite.** Run: `make all && make test`. Expected: no warnings; every suite passes.
- [ ] **Step 5: Install** (only if the game isn't running): `./install.sh`. Expected: `Installed`, the hook
  count one higher than before.
- [ ] **Step 6: README:** the first-person section says the Colt has the new viewmodel, and the settings table
  gains `FirstPersonGuns` and `ViewmodelFov`.
- [ ] **Step 7: Hand to the user to check in game** (the runtime pass can't be run here): the log line from
  step 1 for his outfit; the Colt drawn lower right with his own arm and fingers; never inside a wall; lit
  like his body by day, at night and indoors; draw on switching to it, kick and slide on firing, the reload;
  other guns and fists as before; his real arms back when climbing, swimming and diving; the HUD, motion
  blur and rain drops unchanged (Review Focus 5).
