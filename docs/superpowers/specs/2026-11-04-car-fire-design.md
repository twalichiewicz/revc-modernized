# Progressive vehicle fire and response

Continue vehicle systems after the user's latest playtest. The10:57:34–11:06:42
log confirms projectile injury to model220 cooling and model156 engine, with
power96%; collision injuries also reach cooling loss and zero drive. No crash,
assertion or watchdog stall was logged. Model-only logging cannot establish
individual-actor histories or full weapon/handling balance.

Implement the next localized leak/fire step and the behavior requested in
[#44](https://github.com/twalichiewicz/revc-modernized/issues/44): cars burn
progressively and firefighters respond; ordinary damage and rollover do not
trigger an automatic short explosion countdown. Reuse native CFire particles,
occupant fleeing, water suppression, reference ownership, emergency vehicle
streaming/spawning/routing and firefighting. Do not add fictional fire counts
outside the dispatch decision, spawn trucks beside the player, heal damage,
overwrite shared handling, or suppress scripts/bombs/explosive weapons.

Use a separate CarFire switch, default true. Normal automobile models excluding
aircraft, Rhino and RC, with valid pool identity, render object, native damage
permission, finite health, no fire/explosion proof, and not mission-created or
in a mission/cutscene are eligible. Noneligible calls remain native. Keep state
bounded to256 pool slots, keyed by pool/flags/ref/model/vtable; repair/restart
clear it, streaming does not heal, nested controllers and exceptions restore
all temporary native gates. Callback repair/recycling retires ownership.

Pure fire progression: low native health below250 or damaged-system ignition
requires2.5 seconds of continuous exposure before ignition. Thermal ignition
requires running engine, heat>=1.1 and a fuel breach>=0.15; electrical ignition
requires running engine, electrical damage>=0.8 and fuel breach>=0.3. At least
1% fuel is needed for system ignition. This is calibrated module interaction,
not reconstructed fuel chemistry. Native external fires ignite immediately.
Once lit, intensity grows from0.3 to1 over15 seconds and burns out at60 seconds;
the wreck is disabled without a blast. Water extinguishing latches suppression
until a new accepted non-fire hit or native external fire, rather than healing
or instantly relighting from the same low health. Repair restores everything.
Accept ordinary dt<=1 second; larger gaps/rewinds do not fast-forward a fire.

Carmechanics supplies a read-only snapshot of heat, fuel and relevant module
damage. Native owned fire exposure injures engine/cooling/electrical/fuel at
0.018/0.025/0.03/0.01 integrity per second. Its existing drive/leak/light physics
remain authoritative. CarMechanics=0 leaves those extra effects inactive.

Certified native gates (per-call scope, not global permanent behavior): skip
only the native low-health flame/countdown branch, the rollover health drain,
and the lethal ordinary-bullet/fire explosion call. Real collision health,
native ammo/occupant damage and explicit delayed/script/explosive destruction
remain native. Native fire start can borrow engine status<=224 synchronously
because StartFire rejects already-burning225; conditionally restore the loan
only for the same actor and unchanged status. Validate actual CFire pool record,
script flag and car binding before retaining it or extending its expiry.

A single managed registered fire within120m of the player enables native
firetruck dispatch: only the exact dispatch total/nearest-fire calls receive
the minimum count3 and search radius120m. Preserve native35s retry cooldown,
wanted/traffic limits, one-on-duty gate, model requests, real road spawn and
water AI. Do not report arrival from a request or successful spawn alone.

Use actual managed-fire state for combat/police fire awareness; low-health
extinguished cars can smoke but are not still advertised as burning. Unmanaged
cars retain native health-based fallback. No terrain retuning, full internal
meshes, fluid/fire spread, aircraft fire replacement or save format changes.

Prove pure timing/suppression, actual native gates, water/object lifetime,
ordinary versus explosive/script ownership, mission/proof/config/lifecycle
fallbacks, single-fire dispatch and native protections. Full suite, sanitizers,
one independent review/fix pass, then authorized install and byte/signature
checks. Live firetruck navigation and burn presentation remain playtest checks.
