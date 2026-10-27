# Sniper scope (Plan 8) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** The sniper rifles' scope redone the modern way on the game's own sniper camera: the gun raised to the
eye, then the scope; two zoom stops on RB; a gentle sway, L3 to hold his breath (then a gasp); a shot drops the
scope while the bolt cycles, then it comes back; he can creep while scoped, less accurate; the game's own scope
art drawn crisp, filling the screen with black beyond it and a soft edge; no green smear.

**Research:** `docs/research/2026-10-27-sniper-scope.md` (why full-screen, not picture-in-picture; the hooks).
**Spec:** the player's feedback (2026-10-27): "The scope for the sniper rifle should be redone — either as PiP or
other modern ways of handling it"; their choices: two stops on RB; sway with L3 holding breath; walk slowly while
scoped, less accurate.

## Global Constraints

- Plans 1–7's. The game's firing (`FireSniper`, along the camera) and laser dot stay the game's.
- The rocket launcher (no zoom) is left as it is.

## Review Focus

1. Shots go where the reticle is, sway and spread included (the camera's `Front` is what fires).
2. Leaving the scope (LT let go, a gun switch, a cutscene, death) restores everything: the button, the blur,
   breath, zoom, the FOV.
3. The bolt drop never leaves him stuck out of the scope with LT held, nor in it with LT let go.
4. Holding breath can't be held forever; a gasp follows; it recovers.
5. The overlay at every aspect (4:3, 16:9, 21:9): the art round, black beyond, the centre on the screen's.

---

### Task 1: The scope's rules (`scope`, pure)

- `scope::Scope::Update(const Input &) -> Output`. Input: `scoped`, `zoomPressed` (RB's press), `breathHeld`
  (L3), `shot`, `moving` (m/s), `dt`. Output: `fov` (eased: 26.3° at 3×, 10° at 8×, of a 70° view), `swayYaw`,
  `swayPitch` (degrees: two sines, 0.35° standing, 1.5× moving, eased out holding breath), `breath` (0..1 left),
  `gasping`, `bolting` (0.9 s after a shot: the scope drops), `spread` (radians: moving 0.04 at a walk, 0 still).
  Breath: 4.5 s held, then a 1 s gasp at 4.5× sway easing back; it refills over 3 s.
- Tests: `TestStopsCycleOnRb`, `TestFovEases`, `TestBreathSteadiesThenGasps`, `TestBreathRefills`,
  `TestShotDropsForTheBolt`, `TestMovingSpreads`, `TestLeavingResets`.

### Task 2: In and out of the scope (controls, camera)

- `controls.cpp`: for the sniper rifles in first person, the game's target button only once the gun's aim blend
  is 1 (the raise), and not while `bolting`; RB's press and L3 go to `scope` (L3 kept from the game's duck while
  scoped). The left stick kept for walking while scoped.
- `camera.cpp`: a hook on `CCam::Process_Sniper` (new): after the original, the eye where first person's is, the
  sway turned into `Front`, the FOV the scope's. `fpwalk`: while scoped he creeps (40% of a walk).
- `Hook_Fire`: a sniper shot of his tells `scope` (the bolt), and goes out spread by `scope`'s `spread` (the
  camera's `Front` turned for the shot, then put back).

### Task 3: The scope drawn (hud, grade)

- `hud.cpp`: the game's four scope quarters (`CHud::Sprites` 63 `sitesniper`, 65 `sitelaser`) drawn as a
  circle the screen's height, centred, faded in as the scope comes up; black to every edge; a soft dark edge
  inside the ring.
- `grade.cpp`: while scoped, the sniper motion blur (the green smear) is swapped out.
