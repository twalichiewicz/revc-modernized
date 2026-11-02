# Ragdoll behavior follow-up

- [x] Preserve dirty workspace baseline in `build/ragdoll-behavior/baseline`.
- [x] Reproduce native navigation falls (36 failing assertions), recovery
  rollback freezes and contact-reset stalls, and prone bullet misses.
- [x] Separate native contact bracing from physical impact ownership.
- [x] Repair rejected get-up constraint candidates without weakening certificates.
- [x] Prevent ordinary contacts from restarting recovery.
- [x] Project validated native hit volumes independently of rendering; preserve
  common native filtering, damage and nearest-hit semantics.
- [x] Merge posed sector bounds before existing grid clipping.
- [x] Run focused regressions, full suite, sanitizer targets and native patch checks.
- [x] Complete independent review and resolve material findings.
- [x] Build, install and verify matching library/native hooks when the running
  game has exited; preserve the installed library/executable for rollback.
- [x] Record exact native visual acceptance coverage and remaining observations.

The old runtime foot-obstruction test expected a healthy AI navigation contact
to force a procedural trip. That expectation conflicts with this reported defect
and is replaced by native locomotion retention. The pure physical obstruction
test remains and still requires a freely moving body to be able to trip.

## Contact-hook expectations (2026-11-02)

Four `test_reactionhook` assertions assumed ordinary native contact injects
velocity. They now test the native contract: no injected velocity (every joint
keeps native's accepted motion, including a 1.5m/s slide after 3m/s of rejected
normal speed), a protective brace measured against an unbraced control body
(hands reach toward the obstacle and lift, the far foot steps with the push),
the brace turning with a later opposite contact and with a reversed
player-owned contact, `Revision()`-based duplicate suppression for retries and
for the opposite collision ordering, and the accepted trajectory continuing to
carry a foot-obstructed body. Mutants for physical-mode contact, no duplicate
gate, no player reversal, flipped direction, no brace and replayed incoming
speed each fail the corresponding assertions. The pure physical trip in
`test_reactcontact` is unchanged. The implementation review predates this
test-only rewrite; the rewrite was validated by those mutants, not re-reviewed.

## Verification (2026-11-02)

- `make -k test`: exit 0, no failures (`build/ragdoll-behavior/full-suite.log`).
- Sanitizers (ASan+UBSan), now including `reactionhook`: 10/10 pass, zero
  sanitizer reports (`sanitizers.log`, `*-san.log`).
- `make test-game-assets` against the installed game files: exit 0
  (`game-assets.log`). `make all`: zero warnings (`build-all.log`).
- Installed with `install.sh` (`install.log`): 131 hooks and 6 skips. The
  installed dylib matches the build (`25e59368…`); the patched executable is
  byte-identical to the previous install (`f204fe17…`). The previous pair is
  kept in `rollback-pre-install-20261102/` with `SHA256SUMS`.
- Startup at 00:06:16: 137-entry hook table found, reactions ready, no
  reaction-hit hook failure; the menu-level startup log matches the previous
  launch apart from timings. `credit: hooks missing` predates this work (present
  in all three earlier logs). No crash report; quit cleanly from the menu.
- Not covered: no save was loaded and no live ambulance exit, car navigation,
  get-up or prone-combat playtest was performed. Visual acceptance remains
  offline replay plus unit/native-executable fixtures only.

## Killed NPCs vanishing (2026-11-02)

Reported after the install above: NPCs tend to vanish when killed.

Root cause, each link verified:
- While a ragdoll owns the root, `Place` writes it at pelvis height (probe:
  0.24-0.41m above the floor).
- Native `CPed::SetDead` clears `UsesCollision` (bit 8 of `+0x60`, except while
  drowning) and stores PED_DEAD (vanilla arm64, `0x10019efe4..0x10019f018`).
- `Eligible` required `UsesCollision`, so `Frame` dropped the reaction and
  restored native bones with the root left at pelvis height.
- Native corpse poses lie 0.76-1.07m below the root (`ped.ifp` ko_*/kd_* final
  frames, against -0.02m for idle_stance), so the corpse was drawn roughly 0.6m
  underground. Peds that die untracked stay fully native and look fine, hence
  "tend to".
Every earlier death fixture kept `UsesCollision` set with PED_DEAD, which native
never does.

Fix (`src/reactions.cpp`), test-first in `tests/test_reactcorpse.cpp` through
the real control/collision/frame/render hooks and the real contact sweep:
- A: a body already articulated keeps its reaction after native SetDead;
  untracked native corpses stay native (side-effect-free `Articulated`).
- B: one `Release` handoff for `Frame`, the `Hook_Control` retire path and
  eviction lifts a dead body's root to support + 1.04m (the `StartGetUp`/`Read`
  convention), including after the solver itself drops an uncertifiable pose.
- C: with a full set, `Track` releases the least recently hit settled corpse
  (never a moving one), only after the new body validates.
`test_reactworldhook` modelled "collision disabled transition" with a PED_DEAD
actor, which is native's corpse state; it now uses a living state for the
transition (still fails without the collision gate) and adds the corpse case.
Mutants for each of A/B/C and their selection rules fail the corresponding
checks. (Superseded 2026-11-02, #25: every root-owning body released for a
reason other than the game placing it is lifted, dead or alive, since native
fall and get-up poses expect a standing-height root too; a root the game has
moved since, a respawn/teleport, or the swim taking over is left as placed.)

Verification: `make -k test` exit 0; sanitizers 11/11 (now including
`reactcorpse`), zero reports; `make all` zero warnings; `make test-game-assets`
exit 0. An independent review found no high-confidence defect. Its one
medium-confidence case (a solver-deactivated corpse skipped the lift) was
reproduced and fixed. A settled corpse costs 53 world sphere queries per 60 Hz
frame (12 corpses, about 640), inside the budget the module already allows for
12 falling bodies.

## Tommy's ragdoll (#25, 2026-11-02)

Design: `docs/superpowers/specs/2026-11-02-player-ragdoll-design.md`. Every native
`CPed::SetFall` on Tommy (when the game actually enters PED_FALL) articulates him
with the game's velocity; native get-up returns control; ordinary hits don't;
wasted keeps the ragdoll; respawn/teleport/swim release it without a lift. First
person rides his head fully attached (captured head-to-view rotation from the
composed pose), easing back over 0.4 s. Aim, first-person shooting and walking
stand down while he's down; the player's PreRender now applies the ragdoll before
the camera hides his head.

Independent review before install found the game's `CREATE_PLAYER` marks Tommy
MISSION_CHAR (`0x10008d8a4`), which `Eligible`'s mission gate would have used to
switch the feature off in play; fixed and modelled in the fixture. Also fixed
from it: native SetFall refusals, swimming mid-fall, a stale first-person
ease-back, and the lift for living releases (above). Tests:
`tests/test_playerragdoll.cpp`, the camera block of
`tests/test_damageoverlayhook.cpp`, `tests/test_aimfire.cpp`; every rule
mutation-checked. Full suite, 12/12 sanitizers, zero-warning build and game-file
checks pass; installed A0A57E76 (2026-11-02), startup verified. Not yet seen in
play.
