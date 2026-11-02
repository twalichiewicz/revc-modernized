# Viewmodel firing, melee ready poses and heavy-gun framing: prior art

Why first person's gun animations read flat, what shipped games do instead, and the numbers behind
`src/vmkick.cpp` (the kick), `src/vmanim.cpp` (slide, pump, flash), `src/fpgun.cpp` (the shot's light on the
hands) and `src/vmmelee.cpp` (melee ready poses and swings).

The GDC talks (Doom, Call of Duty, Titanfall, Halo, Battlefield) had abstracts at most, so the numbers come
from shipped code and data: GoldSrc / CS 1.6, Source / HL2 / TF2, Quake 1 and 2, Doom 3, the CryEngine 3
docs and Apex Legends' weapon scripts. Tags: **[S]** the source states it, **[M]** measured or simulated
from shipped data, **[I]** inferred. Source keys are at the end.

## What we had, and why it read flat

Every gun shared one kick: 1 cm back and 1.5° up, decaying exponentially at 14/s; the slide back for 15 ms
then home in 50 ms; a 50 ms flash turned at random every frame. Against the prior art [I]:

1. **One kick for every gun.** Every source tunes per weapon, and the range is wide: CS's MP5 kicks 2.6°,
   its Deagle 30°.
2. **Far too small.** 1.5° is less than CS's MP5; CS pistols reach 24–30°.
3. **Pure exponential decay**: no overshoot, about 95% settled in ~210 ms. CS pistols overshoot about 25%
   and settle over 0.6–1 s; Source's view punch overshoots about 12%.
4. **One axis.** CS's M4A1 kick is mostly roll; Apex adds random roll to every gun; CS pistols turn about
   the wrist or forearm, not the gun's middle.
5. **No randomness, no first-shot emphasis, no build-up** in sustained fire.
6. **The slide barely shows.** At 30 fps the first frame after the shot is 33 ms in, when a 15 ms hold is
   already over. CS holds 50 ms back, 50 ms forward.

## CS 1.6's shoot animations, measured [M]

From decompiled models ([CSM], [CSQC]) by forward kinematics, ±10%. Units are model units ("u"); gun
lengths are about Deagle 9u, MP5 19u, M4A1 28u, M3 25u.

| Sequence | fps / frames | Peak at | Peak motion of the gun | Return |
|---|---|---|---|---|
| Deagle shoot1 | 40 / 24 | 50–75 ms | ≈30° muzzle up, 2.9u up, 1.2u back | past rest at ~300 ms; ≈7° overshoot at 375 ms; settled by 575 ms |
| USP shoot1 | 30 / 31 | 67 ms | 24° up, 1.9u up, 0.7u back | rest by 167 ms; a wobble at 200–270 ms; tail to 1 s |
| MP5 shoot1 | 30 / 21 | 67–100 ms | 2.6° up, 0.67u back | smooth, ~500 ms |
| M4A1 shoot1 | 20 / 31 | 50 ms | ~5°, mostly roll, 0.73u back | swings 0.6u forward of rest at 250 ms |
| M3 shoot1 | 32 / 38 | 62 ms | 3.3u straight back, under 2° turn | held back to 250 ms; then the pump moves the whole gun (≈75–80° tilt, 3.5u drop), home at 1.16 s |

- Snap, then settle: the kick peaks in 1–2 frames and settles about 10× slower. The Deagle's settle fits
  ζ≈0.41 at ~1.8 Hz, but its rise is twice as fast as that spring alone: an instant part plus a sprung part.
- The pistols turn about a point ~6–7u behind the gun's root: the wrist or forearm.
- The slide is fully back at the gun's peak (50 ms) and home at 100 ms.
- The gun moves far more than the camera: CS punches the camera 2° for the Deagle [RGD] while the gun
  pitches ~30°.
- CS picks one of 2–3 shoot sequences at random each shot [CSC]; HL1 randomises sound pitch (94–109) [HL-ev].

## Springs and build-up

Source's view punch is a spring, k = 65, c = 9 (ζ≈0.56), and Valve's comment says it overshoots
[SRC-pl][SRC-mv]. Apex's springs ([APX]; ζ and f computed):

| Profile (hip) | Pitch k / c → ζ | Roll |
|---|---|---|
| pistol | 80/20 → 1.12 | 15000/20 → 0.08, ~19 Hz |
| wingman (revolver) | 120/30 → 1.37 | 150/30 → 1.22 |
| shotgun | 350/22 → 0.59, 2.4 Hz | 350/25 → 0.67 |
| smg | 55/8 → 0.54 | 16000/20 → 0.08, ~20 Hz |
| lmg | 35/12 → 1.0 | 220/15 → 0.51 |

- Each Apex kick is split by `hardScale` / `softScale` keys (Wingman pitch 0.65 / 0.35) [S]: read as an
  instant offset and a sprung part [I], matching CS's shape.
- Build-up in sustained fire: CS's KickBack grows with the shot count, capped [RGD]; HL2 ramps with how long
  you've fired [SRC-hl2]; Doom 3's `MuzzleRise` adds 0.1 s (chaingun) or 0.2 s (machine gun) per shot to a
  timer capped at 0.6 s, the offset being the full kick × time left ÷ 0.6 — "the guns will incrementally back
  up as they are being fired" [D3]; Quake 2's machine gun adds −1.5° a shot to 9 shots, with ±0.7° jitter [Q2].
- A bigger first shot: CryEngine 3 uses 3.0 for the first shot of a burst and 0.5 after, its hands following
  the kicked weapon bone by IK [CRY]. Apex ramps a kick scale from 1.0–1.5 to 1.0–2.25 over N shots [APX].

## Secondary motion, flash and light

- Apex's per-shot gun jolt for automatics: −0.25…−0.5u back, random roll [APX].
- Brass: Doom 3 ejects 10 ms after the shot, but on the pump (650 ms) for the shotgun [D3]. The pump comes
  after the kick (CS M3: ~250 ms in) and moves the whole gun [M].
- Revolvers are heavy: HL2's .357 punches −8°, HL1's Python −10°; Apex's Wingman springs are overdamped.
- Minigun: TF2 spins up for 0.75 s first [TF2]; Doom 3's chaingun builds to −4° pitch, −10° roll [D3].
- Flash: GoldSrc's sprite dies the next frame, turned 0–359° at random, sized per weapon [XASH]; HL2's
  pistol flash lasts 0.01 s, the SMG's 0.025 s, scaled ×1.25–1.5 [SRC-fx].
- Light: GoldSrc and Source put a 255/192/64 light on the viewmodel for ~0.05 s (radius 24–64) — entity
  lights, lighting only models: in practice "the shot lights the hands" [XASH][SRC-fx][I].
- Smoke: HL2's pistol puffs on 3 of 4 shots for 0.25–0.5 s [SRC-fx].
- Camera vs gun: GoldSrc gives the gun the unpunched angles [HL-view]; Source rides it with the camera
  [SRC-vm]. Eiserloh: shake as trauma², rotation only, Perlin noise rather than random [EIS].

## Melee

- CS knife slash (55 fps, 1.2 s) [M]: wind-up 0–160 ms, strike 160–275 ms (sweeping across in ~110 ms),
  held to ~380 ms, recovered by ~545 ms, a settle bump to ~1 s. Stab: back 0–170 ms, thrust and hold
  340–510 ms, return 600–950 ms.
- CS and HL resolve the hit on input and let the visual strike land 150–250 ms later [RGD][HL-crow][I].
- Chivalry 2 (proportions only; its wind-up is gameplay delay): one-handed wind-up 0.125–0.15 s, release
  0.4–0.475 s, recovery 0.65–0.8 s [CHIV].
- Overwatch keeps hands and props off the reticle [OW]; Destiny keeps the "combat corridor" round it clear [DST].
- Ready poses [I, observed]: one-handed tools in the right hand low right, the tip angled up and in, a
  third in from the right edge; long ones rising diagonally.
- Heavy guns [I, observed: TF2's Heavy, Doom's chaingun, GTA V's minigun]: two-handed at hip height, large
  from centre-right, the barrels converging up and in toward the reticle.

## What we did with it

- **The kick** (`vmkick`): per class (pistol, revolver, shotgun, one-handed SMG, MP5, rifle, M60, minigun,
  flamethrower). Pistol 14° up and 1.2 cm back, ζ 0.45 at 2.8 Hz (it drops back past where it was); revolver
  22°, ζ 0.9 (heavy, no wobble); shotgun 3.5 cm straight back and 6° up; automatics 2.5–4° with ±2–2.5° roll.
  Half of each kick is there the frame it fires, the rest a spring's push. Each shot's size ±15–30%, its
  roll's side and yaw random; a burst's first shot ×1.3–1.5; automatics climb on Doom 3's timer (0.05–0.1 s a
  shot, full at 0.6 s, held 0.15 s, then running down). The minigun buzzes (~20 Hz roll) firing, a little
  spinning; the flamethrower shudders. Aimed, rotations ×0.35 and moves ×0.6, so the sights stay on.
- **The gun turns about a point 10 cm back along his forearm**, his right arm going with it; his left hand
  rides the gun while its shoulder stays put (two-bone IK), so the kick never brings his sleeve into view.
- **Slide** held back 35 ms (a 30 fps frame), home in 50 ms, both shortened for a quick gun so it's home before
  the next shot. **Pump** after the kick (0.22 s), the whole gun rolling in 8° and tipping up 4° with it.
- **Flash** one frame at full, gone by the third, turned and sized per shot (not per frame), sized per class;
  none for the flamethrower. **The shot's light** (255/192/64, out to 0.55 m from the muzzle) added over his
  hands and the gun; the flamethrower's flickers while it burns.
- **Melee** (`vmmelee`): see its header. The game's own attack sets the timing (its animation's clock and
  weapon.dat's fire frame), so nothing waits on the viewmodel; where the animation loops or a combo follows
  on, the jump is faded out (inertialization, as Gears of War 4 blends its animations) rather than smoothing
  the swing itself.

Left for later: brass and smoke in the viewmodel; per-shot sound pitch; FOV kick (none of the sources do it).

## Source keys

- [HL-view] https://github.com/ValveSoftware/halflife/blob/master/cl_dll/view.cpp
- [HL-ev] https://github.com/ValveSoftware/halflife/blob/master/cl_dll/ev_hldm.cpp
- [HL-crow] https://github.com/ValveSoftware/halflife/blob/master/dlls/crowbar.cpp
- [RGD] https://github.com/s1lentq/ReGameDLL_CS/tree/master/regamedll/dlls (weapons.cpp, wpn_shared/)
- [CSC] https://github.com/Velaron/cs16-client/tree/main/cl_dll
- [CSM] https://github.com/schwittlick/cs_models/tree/main/models
- [CSQC] https://github.com/Pinsplash/ValveQCs/tree/main/goldsrc/cstrike
- [XASH] https://github.com/FWGS/xash3d-fwgs/blob/master/engine/client/cl_tent.c
- [SRC-mv] https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp
- [SRC-pl] https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/baseplayer_shared.cpp
- [SRC-vm] https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/baseviewmodel_shared.cpp
- [SRC-hl2] https://github.com/ValveSoftware/source-sdk-2013/tree/master/src/game/server/hl2
- [SRC-fx] https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/client/c_te_legacytempents.cpp
- [TF2] https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/tf/tf_weapon_minigun.cpp
- [Q2] https://github.com/id-Software/Quake-2/blob/master/game/p_weapon.c
- [D3] https://github.com/TTimo/doom3.gpl/blob/master/neo/game/Weapon.cpp
- [APX] https://github.com/Mauler125/scripts_r5/blob/S3_N1094/weapons/springs.txt (and the mp_weapon_*.txt beside it)
- [CRY] https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1605638
- [EIS] http://www.mathforgameprogrammers.com/gdc2016/GDC2016_Eiserloh_Squirrel_JuicingYourCameras.pdf
- [OW] https://www.invenglobal.com/articles/1187/how-overwatchs-first-person-animation-breathed-life-into-heroes
- [DST] https://gdcvault.com/play/1022297/The-Art-of-First-Person
- [CHIV] https://chivalry.wiki.gg/wiki/Category:Weapons_(Chivalry_II)
- Inertialization: David Bollo, "Inertialization: High-Performance Animation Transitions in Gears of War", GDC 2018.
