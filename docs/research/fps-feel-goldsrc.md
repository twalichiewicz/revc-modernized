# FPS feel prior art: Quake / GoldSrc / Counter-Strike 1.6

Reference for making reVC's first-person mode feel like CS 1.6. Every number is traced to source code or a Valve document. Values marked **derived** are computed from those formulas, not quoted.

## Sources and how far to trust them

CS 1.6's own game and client DLLs are closed source. For CS-specific values, the best primary sources are reverse-engineered reimplementations, and each claim says which kind of source it comes from.

| Tag | Repository / document (pinned) | Nature |
|---|---|---|
| HLSDK | [ValveSoftware/halflife @ b1b5cf5](https://github.com/ValveSoftware/halflife/tree/b1b5cf5892918535619b2937bb927e46cb097ba1) | Valve's official Half-Life SDK (game + client DLL code, shared player movement) |
| SDK2013 | [ValveSoftware/source-sdk-2013 @ be57e3d](https://github.com/ValveSoftware/source-sdk-2013/tree/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0) | Valve Source SDK, used only to contrast viewmodel FOV/lag |
| Quake | [id-Software/Quake @ bf4ac42](https://github.com/id-Software/Quake/tree/bf4ac424ce754894ac8f1dae6a3981954bc9852d) | id's GPL release, GoldSrc's ancestor |
| ReGameDLL | [rehlds/ReGameDLL_CS @ 4a50c42](https://github.com/rehlds/ReGameDLL_CS/tree/4a50c42e85fd3778c2b7d24731c7d30c83bbab01) | Reverse-engineered CS 1.6 server game DLL. Code under `#ifdef REGAMEDLL_FIXES` / `REGAMEDLL_ADD` is ReGameDLL's own change. The `#else` branch reproduces the original binary, and this doc quotes the original branch unless it says otherwise |
| ReHLDS | [rehlds/ReHLDS @ 550f2d6](https://github.com/rehlds/ReHLDS/tree/550f2d62f13f4ebeb029c1d9d1c212133202611d) | Reverse-engineered GoldSrc dedicated-server engine (engine cvar defaults) |
| cs16-client | [FWGS/cs16-client @ a238ef1](https://github.com/FWGS/cs16-client/tree/a238ef131803ed7592c4ba701499027c862889ee) | Reverse-engineered CS 1.6 **client** DLL (crosshair, events, input) |
| Xash3D | [FWGS/xash3d-fwgs @ 27092af](https://github.com/FWGS/xash3d-fwgs/tree/27092af50d912432887faa43a1cccddfae569b2d) | Clean-room GoldSrc-compatible engine (renderer and prediction loop; GoldSrc's own are closed) |
| Bernier 2001 | [VDC: Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization](https://developer.valvesoftware.com/wiki/Latency_Compensating_Methods_in_Client/Server_In-game_Protocol_Design_and_Optimization) | Valve engineer's design paper on HL1 prediction |

Units are GoldSrc world units (u). CS's standing hull is 72 u tall (mins z −36) and its eye offset is 17 u above the hull centre, so the eye sits **53 u** above the feet; ducked it is 30 u (duck hull −18, `VEC_DUCK_VIEW` 12). Source: [ReGameDLL `dlls/util.h` L78–88](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/util.h#L78-L88). HL1 uses `VEC_VIEW 28` (64 u eye): [HLSDK `pm_shared.c` L87](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L87). To keep CS proportions in GTA, scale distances and speeds by (GTA eye height / 53 u). That scale is **derived**.

---

## 1. Movement physics

### 1.1 Input to wish velocity

**Keyboard.** `CL_CreateMove` sets `forwardmove = ±cl_forwardspeed` and `sidemove = ±cl_sidespeed` for each key held. In HLSDK it applies the speed key (`*= cl_movespeedkey`) and then scales the whole `(forward, side, up)` vector down so its length is no more than the client maxspeed. Because the cap is on the vector's length, moving diagonally is not faster. Source: [HLSDK `cl_dll/input.cpp` L660–723](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/input.cpp#L660-L723).

The HLSDK defaults are `cl_forwardspeed 400`, `cl_backspeed 400`, `cl_sidespeed 400`, `cl_upspeed 320`, `cl_movespeedkey 0.3`, `cl_yawspeed 210` and `cl_pitchspeed 225`: [HLSDK `input.cpp` L985–991](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/input.cpp#L985-L991).

**CS walk key.** In the reverse-engineered CS client, `cl_movespeedkey` defaults to **0.52** and is clamped to at most 0.52. CS applies it **after** the maxspeed clip, so walking runs at 0.52 × the weapon's max speed. Source: [cs16-client `cl_dll/input.cpp` L712–722, L978](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/input.cpp#L712-L722).

**Controller (GoldSrc joystick).** `IN_JoyMove` maps each axis to −1…1 and ignores it below a threshold of **0.15**. Inputs above the threshold are not rescaled, so the deadzone is a hard cut. The mappings are linear:

- `forwardmove += axis * joyforwardsensitivity * speed * cl_forwardspeed`
- yaw rate = `axis * joyyawsensitivity * cl_yawspeed` °/s (`aspeed = speed * frametime`)
- pitch rate uses `cl_pitchspeed` the same way.

The thresholds and sensitivities are registered at defaults 0.15 and ±1.0. There is no response curve and no aim assist. Source: [HLSDK `cl_dll/inputw32.cpp` IN_JoyMove L918–1100, cvars L1125–1146](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/inputw32.cpp#L1125-L1146).

**Derived consequence:** with `cl_forwardspeed 400` and a 250 maxspeed, any stick deflection of 62.5% or more already gives full speed. Deflection below 35% (140/400) stays under the 140 u/s rifle accuracy threshold in §2.3.

**Inside pmove.** `PM_CheckParamters` scales the command down to `maxspeed = min(sv_maxspeed, clientmaxspeed)` ([HLSDK `pm_shared.c` L2837–2870](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L2837-L2870)). `PM_WalkMove` then builds the wish velocity from the yaw-flattened basis vectors:

```c
wishvel[i] = pmove->forward[i]*fmove + pmove->right[i]*smove;   // z zeroed, forward/right renormalised
wishspeed  = VectorNormalize(wishdir);  if (wishspeed > maxspeed) wishspeed = maxspeed;
```

Source: [HLSDK `pm_shared.c` PM_WalkMove L1034–1084](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L1034-L1084). `PM_WalkMove` also zeroes velocity once speed drops below 1.

**Order per tick on ground** (ReGameDLL `PM_PlayerMove`): jump check → `PM_Friction` → `PM_WalkMove` (which calls `PM_Accelerate`); in the air the last step is `PM_AirMove` instead. Source: [ReGameDLL `pm_shared/pm_shared.cpp` L3330–3370](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L3330-L3370).

### 1.2 Friction, acceleration, air acceleration

**`PM_Friction`** ([HLSDK L1202–1277](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L1202-L1277)). It applies only on the ground. Friction is multiplied by `edgefriction` when a trace 16 u ahead of the feet, along the direction of motion, finds no floor within 34 u.

```c
control  = (speed < stopspeed) ? stopspeed : speed;
drop     = control * friction * frametime;
newspeed = max(speed - drop, 0);  velocity *= newspeed / speed;
```

**`PM_Accelerate`** ([HLSDK L990–1027](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L990-L1027)). This is identical to Quake's `SV_Accelerate` ([Quake `WinQuake/sv_user.c` L190–205](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/sv_user.c#L190-L205)).

```c
currentspeed = DotProduct(velocity, wishdir);
addspeed     = wishspeed - currentspeed;            if (addspeed <= 0) return;
accelspeed   = accel * frametime * wishspeed * pmove->friction;   // pmove->friction = 1 normally
if (accelspeed > addspeed) accelspeed = addspeed;   velocity += accelspeed * wishdir;
```

**`PM_AirAccelerate`** ([HLSDK L1279–1314](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L1279-L1314); same code in [ReGameDLL L1336–1370](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L1336-L1370); same in Quake [`WinQuake/sv_user.c` L207–228](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/sv_user.c#L207-L228) and QuakeWorld [`QW/client/pmove.c` L412](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/QW/client/pmove.c#L412)).

The cap of **30** applies only to the target projection. The acceleration still uses the full wishspeed:

```c
wishspd = min(wishspeed, 30);
addspeed = wishspd - DotProduct(velocity, wishdir);  if (addspeed <= 0) return;
accelspeed = accel * wishspeed * frametime;  accelspeed = min(accelspeed, addspeed);
```

In practice, airborne input can only push velocity along `wishdir` up to 30 u/s. Holding forward while flying forward faster than 30 u/s does nothing; only sideways steering, which is the basis of strafe-jumping, has an effect.

### 1.3 Default values

| Cvar | GoldSrc engine default | CS 1.6 (game DLL) | Quake | Source |
|---|---|---|---|---|
| `sv_accelerate` | 10 | **5 (hard-locked)** | 10 | [ReHLDS `sv_user.cpp` L52](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_user.cpp#L50-L55); [ReGameDLL `multiplay_gamerules.cpp` L1697–1700, L2567–2580](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/multiplay_gamerules.cpp#L1697-L1700); [Quake `sv_user.c` L168](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/sv_user.c#L167-L168) |
| `sv_friction` | 4 | **4 (hard-locked)** | 4 | [ReHLDS `sv_phys.cpp` L52](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_phys.cpp#L49-L53); [Quake `sv_phys.c` L42](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/sv_phys.c#L42-L43) |
| `sv_stopspeed` | 100 | **75 (hard-locked)** | 100 | same as above |
| `sv_airaccelerate` | 10 | not set by the game DLL | (uses `sv_accelerate`) | [ReHLDS `sv_main.cpp` L127](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L127-L128) |
| `sv_maxspeed` | 320 | 320 (per-weapon `maxspeed` is lower, §1.4) | 320 | [ReHLDS `sv_user.cpp` L51](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_user.cpp#L50-L55) |
| `edgefriction` | 2 | — | 2 | [ReHLDS `sv_user.cpp` L50](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_user.cpp#L50-L55); [Quake `sv_user.c` L27](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/sv_user.c#L27) |
| `sv_gravity` / `sv_stepsize` | 800 / 18 | — | — | [ReHLDS `sv_phys.cpp` L49–51](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_phys.cpp#L49-L53) |
| `sv_rollangle` (view roll when strafing) | 0 | — | — | [ReHLDS `sv_user.cpp` L54–55](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_user.cpp#L50-L55) |

ReGameDLL's comment reads: "Hardlock the player accelaration to 5.0". The game rules reset the three locked cvars at round start and re-check them once per second.

### 1.4 CS 1.6 max speeds and multipliers

`CBasePlayer::ResetMaxSpeed` sets `pev->maxspeed` from the active weapon's `GetMaxSpeed()`. It uses 240 with no weapon, 227 for the VIP, and 1 during freeze time. Source: [ReGameDLL `dlls/player.cpp` L8096–8127](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/player.cpp#L8096-L8127).

Per-weapon constants come from [ReGameDLL `dlls/weapons.h`](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.h#L512); for example, AK47 is at L701 and AWP at L807–808. All of the requested examples are verified:

| u/s | Weapons |
|---|---|
| 260 | Scout (220 zoomed) |
| 250 | knife, all pistols (USP, Glock18, P228, Deagle, Elite, FiveSeven), C4, grenades, MP5, MAC10, TMP, UMP45 |
| 245 | P90 |
| 240 | AUG, Galil, Famas, XM1014 |
| 235 | SG552 (200 zoomed) |
| 230 | M4A1, M3 |
| 221 | AK47 |
| 220 | M249 |
| 210 | AWP, G3SG1, SG550 (150 zoomed) |
| 180 | knife or grenade with tactical shield |

**Crouch:** `PLAYER_DUCKING_MULTIPLIER 0.333` multiplies `forwardmove`/`sidemove`/`upmove` as soon as duck is held, including during the transition. It runs after `PM_CheckParameters` has clamped the command to maxspeed, so crouch speed is 0.333 × maxspeed (83 u/s with a knife). The duck transition takes `TIME_TO_DUCK 0.4` s on the ground and is instant in the air. Source: [ReGameDLL `pm_shared.h` L55, L61](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.h#L55-L74); [`pm_shared.cpp` PM_Duck L2141–2164](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L2141-L2164).

**Walk:** 0.52 × maxspeed (§1.1), which is 130 u/s with a knife or pistol and 115 u/s with an AK. **Derived:** both are under the 140 u/s threshold the rifles use for their "moving" spread (§2.3), so walking keeps standing accuracy. Pistols treat any speed above 0 as moving.

**Jump:** vertical velocity is `sqrt(2*800*45)` = 268.3 u/s ([ReGameDLL `PM_JumpHeight` L2614–2633](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L2614-L2633)). **Derived:** airtime is 0.67 s.

**Landing slowdown ("stamina").** `PM_Jump` sets `fuser2 = 1315.789429` ms, which decays by `cmd.msec` each tick. While it is above 0, every `PM_WalkMove` tick multiplies horizontal velocity by `(100 - fuser2*0.001*19)*0.01`. That factor is 0.75 at the moment of the jump. **Derived:** it is about 0.88 per tick on landing after a full jump. It also scales the next jump's height. Because it is applied per tick, the effect depends on frame rate; ReGameDLL adds a `stamina_restore_rate` cvar to correct that. Source: [ReGameDLL `pm_shared.cpp` L1059–1078, L2812–2820](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L1059-L1078).

**Bunny-hop cap.** If speed exceeds `BUNNYJUMP_MAX_SPEED_FACTOR` × maxspeed when you jump, velocity is scaled to 0.8 of that cap. In CS the factor is 1.2, so speed drops to 0.96 × maxspeed; HL1 uses 1.7. Source: [ReGameDLL `pm_shared.h` L74, `pm_shared.cpp` L2587–2612](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L2587-L2612); [HLSDK `pm_shared.c` L2436](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L2436).

**Hit slowdown ("tagging").** Taking damage sets `m_flVelocityModifier` to 0.5 for a normal flinch, or to 0.65 plus 170 u/s of knockback for a large flinch. While on the ground, each `PreThink` adds 0.01 to the modifier and multiplies velocity by it until it reaches 1. Source: [ReGameDLL `player.cpp` L1225–1250, L4768–4781](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/player.cpp#L4768-L4781).

### 1.5 Time to full speed and to stop (derived)

These come from the formulas above. The continuous form, with input held on the ground, is:

- `dv/dt = accel*wishspeed − friction*max(v, stopspeed)`.
- Above stopspeed, v approaches `accel/friction * wishspeed` (1.25 × wishspeed in CS, 2.5 × in HL1) but is clipped at wishspeed.
- Released: `v = v0·e^(−friction·t)` down to stopspeed, then a linear fall at `friction·stopspeed` (300 u/s² in CS).

Simulated per tick with the exact code order (friction, then accelerate), at 30, 60 and 100 Hz:

| Case | 90% speed | Full speed | Stop from full | Slide distance |
|---|---|---|---|---|
| CS, 250 (knife/pistol) | 0.32–0.33 s | 0.40–0.41 s | 0.53–0.55 s | 46–51 u |
| CS, 221 (AK) | 0.33 s | 0.40–0.41 s | 0.50–0.52 s | 40–44 u |
| CS, 210 (AWP) | 0.33 s | 0.40–0.42 s | 0.50 s | 37–41 u |
| CS crouch, 83 u/s | 0.62 s | 0.71 s | 0.28 s | 11 u |
| CS walk, 130 u/s | 0.37 s | 0.45 s | 0.39 s | 22 u |
| HL1/Quake, 320 (`accel 10`, `stopspeed 100`) | 0.12 s | 0.13 s | 0.53–0.54 s | 59–65 u |

Because acceleration is proportional to wishspeed, ramp time is almost the same for every weapon. Heavier weapons only lower the top speed. CS's "weighty" start comes from `sv_accelerate 5` against `friction 4`.

---

## 2. Weapons (CS 1.6, ReGameDLL)

### 2.1 Where firing happens

`CBasePlayerWeapon::ItemPostFrame` runs every player tick. It works through these cases in order:

1. Finish a pending reload.
2. If `IN_ATTACK2` is held and the secondary timer allows it, run the secondary attack.
3. If `IN_ATTACK` is held and `CanAttack(m_flNextPrimaryAttack)`, call `PrimaryAttack()`.
4. If reload is pressed, reload.
5. If no fire button is held, run recovery and auto-reload, then `WeaponIdle()`.

Source: [ReGameDLL `dlls/weapons.cpp` L981–1200](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L981-L1200).

Shots travel along `v_angle + punchangle` (`UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle)`, e.g. [wpn_ak47.cpp L122](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_ak47.cpp#L122)). Recoil therefore moves the bullets by exactly the amount the camera is punched (the scale is 1x).

### 2.2 Punchangle: how it is set and how it decays

**Set.** Rifles call `KickBack(...)` (§2.3). The USP, Deagle and AWP do `punchangle.x -= 2.0` per shot; negative pitch is up. The Glock18 applies **no** punch: its original code has no punchangle write, and ReGameDLL adds only a dummy `KickBack(0,…)`. Sources:

- [wpn_usp.cpp L241–246](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_usp.cpp#L241-L246)
- [wpn_deagle.cpp L179–184](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_deagle.cpp#L179-L184)
- [wpn_awp.cpp L191–196](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_awp.cpp#L191-L196)
- [wpn_glock18.cpp L255–258](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_glock18.cpp#L255-L258)

**Decay.** Decay runs inside player movement, once per usercmd, in `PM_CheckParamters` → `PM_DropPunchAngle`. It acts on the length of the punch vector, so pitch and yaw shrink together:

```c
len = VectorNormalize(punchangle);
len -= (10.0 + len * 0.5) * frametime;
len = max(len, 0.0);  VectorScale(punchangle, len, punchangle);
```

Source: [HLSDK `pm_shared.c` L2821–2830](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/pm_shared/pm_shared.c#L2821-L2830); identical in [ReGameDLL `pm_shared.cpp` L3005–3017](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/pm_shared/pm_shared.cpp#L3005-L3017). The client view runs the same decay on its client-only `ev_punchangle` ([HLSDK `cl_dll/view.cpp` V_DropPunchAngle L1682–1690](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L1682-L1690)).

**Derived:** the closed form is `L(t) = (L0 + 20)·e^(−t/2) − 20`, so the punch is gone at `t = 2·ln(1 + L0/20)`:

| Starting punch | Time to return to 0 |
|---|---|
| 2° (one pistol shot) | 0.19 s |
| 3.5° | 0.32 s |
| 5.75° (AK cap) | 0.51 s |

The return speed is therefore about 10–13 °/s, nearly linear.

### 2.3 KickBack, spread and accuracy

**`KickBack(up_base, lateral_base, up_modifier, lateral_modifier, up_max, lateral_max, direction_change)`**, original branch. Source: [ReGameDLL `weapons.cpp` L733–811 (`#else` part)](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L733-L811).

```c
if (m_iShotsFired == 1) { kickUp = up_base; kickLat = lateral_base; }
else { kickUp = m_iShotsFired*up_modifier + up_base; kickLat = m_iShotsFired*lateral_modifier + lateral_base; }
punchangle.x -= kickUp;            if (punchangle.x < -up_max) punchangle.x = -up_max;      // up, capped
punchangle.y += (m_iDirection ? +kickLat : -kickLat);  clamp to ±lateral_max;          // side, capped
if (!RANDOM_LONG(0, direction_change)) m_iDirection = !m_iDirection;                     // flip with p = 1/(dc+1)
```

What each argument does:

- **base**: the kick of the first shot.
- **modifier**: the extra kick added per shot already fired in the burst.
- **max**: the cap on accumulated punch in that axis.
- **direction_change**: the inverse odds of the side kick switching direction on any shot.

`m_iShotsFired` is incremented before `KickBack` is called, so the first shot sees 1.

The variant is chosen by state. Velocity is tested first, so jumping while moving uses the "moving" row.

| Weapon | Moving (2D speed > 0) | Airborne | Ducking | Standing |
|---|---|---|---|---|
| AK47 ([L156–171](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_ak47.cpp#L156-L171)) | 1.5, 0.45, 0.225, 0.05, 6.5, 2.5, 7 | 2.0, 1.0, 0.5, 0.35, 9.0, 6.0, 5 | 0.9, 0.35, 0.15, 0.025, 5.5, 1.5, 9 | 1.0, 0.375, 0.175, 0.0375, 5.75, 1.75, 8 |
| M4A1 ([L212–227](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_m4a1.cpp#L212-L227)) | 1.0, 0.45, 0.28, 0.045, 3.75, 3.0, 7 | 1.2, 0.5, 0.23, 0.15, 5.5, 3.5, 6 | 0.6, 0.3, 0.2, 0.0125, 3.25, 2.0, 7 | 0.65, 0.35, 0.25, 0.015, 3.5, 2.25, 7 |
| USP / Deagle / AWP | pitch −2.0 per shot, no side kick | | | |
| Glock18 | no punch | | | |

**Spread geometry.** In `FireBullets3`, x and y are each the sum of two shared-seed uniforms on [−0.5, 0.5]. Each therefore has a triangular distribution on [−1, 1]; for players there is no circular rejection, so the pattern is square. The shot direction is:

```c
vecDir = vecDirShooting + x*spread*vecRight + y*spread*vecUp;
```

Source: [ReGameDLL `dlls/cbase.cpp` L1335–1359](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/cbase.cpp#L1335-L1359). Spread is a tangent offset; **derived:** the maximum deviation per axis is atan(spread).

**Rifle accuracy.** Accuracy grows with the cube of shots in the burst:

| | AK47 | M4A1 |
|---|---|---|
| Accuracy after each shot | `shots³/200 + 0.35`, cap 1.25 ([L97–100](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_ak47.cpp#L89-L100)) | `shots³/220 + 0.3`, cap 1.0 ([L140–143](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_m4a1.cpp#L132-L143)) |
| Value after deploy or reload | 0.2 | 0.2 |

In the original binary the division is **integer** division, so the AK stays at 0.35 for shots 1–5 and jumps to the cap on shot 6. ReGameDLL changed the divisor to a float to make the transition smooth ([PR #677](https://github.com/rehlds/ReGameDLL_CS/pull/677)). Each shot's spread uses the accuracy value from **before** that shot's update:

| State | AK47 spread ([L73–87](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_ak47.cpp#L73-L87)) | M4A1 spread ([L98–130](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_m4a1.cpp#L98-L130)) |
|---|---|---|
| Airborne | 0.04 + 0.4·acc | 0.035 + 0.4·acc |
| 2D speed > 140 | 0.04 + 0.07·acc | 0.035 + 0.07·acc |
| Otherwise (standing, walking or crouched) | 0.0275·acc | 0.02·acc (0.025·acc silenced) |

Crouching does not reduce rifle spread; it only reduces recoil.

**Derived, AK, standing, full auto at 100 Hz** (original integer division, ignoring random side flips):

| Shot | Time | Punch up | Punch side | Spread used (max per axis) |
|---|---|---|---|---|
| 1 | 0.0 s | 1.0° | 0.38° | 0.0055 (0.32°) |
| 2–6 | 0.1–0.5 s | 1.39 → 4.36° | 0.46 → 1.31° | 0.0096 (0.55°) |
| 7 | 0.6 s | 5.43° | 1.60° | 0.0344 (1.97°) |
| 8 and later | 0.7 s on | sawtooth at the 5.75° cap | 1.75° cap | 0.0344 (1.97°) |

**Burst recovery (rifles/SMGs)**, from the `ItemPostFrame` no-button branch, [L1115–1158](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1115-L1158):

- On release, `m_iShotsFired` is capped at 15.
- After 0.4 s, it decreases by 1 every 0.0225 s.
- **Derived:** a full recovery from 15 takes about 0.74 s.

`m_flAccuracy` itself is only recomputed on the next shot. In the original game it is not reset when the count reaches 0, so the first shot after a long pause still uses the last shot's accuracy. ReGameDLL fixes this by resetting to the base accuracy, via `GetBaseAccuracy` [L53–75](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L53-L75) ([PR #662](https://github.com/rehlds/ReGameDLL_CS/pull/662)).

**Pistol accuracy** recovers with the time since the last shot:

| | USP ([L157–184](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_usp.cpp#L157-L184)) | Glock18 | Deagle |
|---|---|---|---|
| Update per shot | `acc -= (0.3 − Δt)·0.275` | `acc -= (0.325 − Δt)·0.275` | `acc -= (0.4 − Δt)·0.35` |
| Clamp | [0.6, 0.92] | [0.6, 0.9] | [0.55, 0.9] |
| Value at deploy | 0.92 | 0.9 | 0.9 |

Firing faster than the reference interval lowers accuracy; waiting longer raises it. Spread is `k·(1 − acc)`, with k chosen by state:

| Weapon | Air | Moving (speed > 0) | Duck | Stand |
|---|---|---|---|---|
| USP | 1.2 | 0.225 | 0.08 | 0.1 |
| USP silenced | 1.3 | 0.25 | 0.125 | 0.15 |
| Glock18 semi | 1.0 | 0.165 | 0.075 | 0.1 |
| Deagle | 1.5 | 0.25 | 0.115 | 0.13 |

Sources: [wpn_usp.cpp L115–155](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_usp.cpp#L115-L155), [wpn_glock18.cpp L114–150](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_glock18.cpp#L114-L150), [wpn_deagle.cpp L74–93](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_deagle.cpp#L74-L93).

**AWP** uses a fixed spread by state ([wpn_awp.cpp L95–137](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_awp.cpp#L95-L137)):

| Airborne | Speed > 140 | Speed > 10 | Ducking | Standing |
|---|---|---|---|---|
| 0.85 | 0.25 | 0.1 | 0 | 0.001 |

Firing unscoped adds 0.08. Firing scoped drops the zoom and restores it once the bolt cycle has finished (`m_bResumeZoom`).

### 2.4 Fire rate, semi-auto, deploy, reload

**Fire cycle.** Each weapon sets `m_flNextPrimaryAttack = GetNextAttackDelay(cycle)` ([weapons.cpp L1755–1795](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1755-L1795)).

**Semi-auto.** Pistols do `if (++m_iShotsFired > 1) return;`, so holding the trigger fires once. When the attack button is released, `m_iShotsFired` resets to 0 for secondary weapons ([weapons.cpp L1135–1142](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1135-L1142)). Their cycle argument is shortened in code: USP and Deagle by 0.075, Glock and P228 by 0.05.

| Weapon | Cycle (s) | Rate | Reload time (s) | Idle anim delay after shot (s) |
|---|---|---|---|---|
| AK47 | 0.0955 | 628 RPM | 2.45 | 1.9 |
| M4A1 | 0.0875 | 686 RPM | 3.05 | 1.5 |
| MP5 / MAC10 / TMP / P90 | 0.075 / 0.07 / 0.07 / 0.066 | — | 2.63 / 3.15 / 2.12 / 3.4 | — |
| USP (semi) | 0.225 − 0.075 = **0.15** | 6.7 /s max | 2.7 | 2.0 |
| Glock18 (semi) | 0.2 − 0.05 = **0.15** | — | 2.2 | 2.5 |
| Glock18 (burst) | 0.5 | 3 rounds 0.1 s apart, follow-up rounds spread 0.05 | — | — |
| Deagle (semi) | 0.3 − 0.075 = **0.225** | — | 2.2 | — |
| AWP | 1.45 | — | 2.5 | 2.0 |
| Scout | 1.25 | — | 2.0 | — |

Sources: reload constants in [weapons.h](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.h#L516) (`*_RELOAD_TIME`); the Glock burst in [weapons.cpp FireRemaining L813–877](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L813-L877).

**Deploy.** `DefaultDeploy` sets `m_pPlayer->m_flNextAttack = 0.75` s (no firing), sets the idle timer to 1.5 s, resets FOV to 90 and plays the DRAW sequence. The AWP overrides the lockout to 1.45 s. Source: [weapons.cpp L1492–1520](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1492-L1520); [wpn_awp.cpp L61–70](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_awp.cpp#L61-L70).

**Reload.** `DefaultReload` sets `m_flNextAttack = reload time`, plays RELOAD and sets the idle timer to reload time + 0.5. Ammo moves into the magazine only when the timer expires. Switching weapons cancels the reload: `Holster` clears `m_fInReload` and the viewmodel instantly, with no holster animation. Source: [weapons.cpp L1552–1574, L1680–1690](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1552-L1574).

**Silencer toggle.** M4A1 locks both attacks for 2.0 s ([wpn_m4a1.cpp L79–96](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_m4a1.cpp#L79-L96)); USP takes 3.0 s ([weapons.h L516–521](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.h#L516-L521)).

### 2.5 Dynamic crosshair (reverse-engineered client)

`CHudAmmo::DrawCrosshair` runs once per HUD frame. Source: [cs16-client `cl_dll/ammo.cpp` L1199–1231, L1243–1282, L1598–1700](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/ammo.cpp#L1598-L1700).

**Base gap and growth step.** Both come from the `Distances[weaponId−1]` table:

| Weapon | Base gap | Growth per shot |
|---|---|---|
| AK47 | 4 | 4 |
| M4A1 | 4 | 3 |
| USP | 8 | 3 |
| Glock18 | 8 | 3 |
| Deagle | 8 | 3 |
| MP5 | 6 | 3 |

**State scaling of the base gap**, applied when `cl_dynamiccrosshair 1` and the weapon has the relevant flags:

| Condition | Factor |
|---|---|
| Airborne | 2 |
| Ducking | 0.5 (pistols only) |
| Speed ≥ threshold (140 for rifles, 170 for P90, 0 for pistols) | 1.5 |
| Silenced USP or M4, or burst Glock or Famas | 1.4 (twice for burst) |

**Each frame:**

```c
if (no new local shot since last frame) { gap -= gap*0.013 + 0.1;  alpha += 2; }
else                                     { gap = min(gap + delta, 15); alpha = max(alpha - 40, 120); }
gap = max(gap, baseGap);   lineLength = (gap - baseGap)*0.5 + 5;   // pixels at a 640/800/1024-wide base, scaled to screen width
```

The shot counter `g_iShotsFired` is incremented by the local fire event, for example [events/event_ak47.cpp L52–54](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/events/event_ak47.cpp#L52-L54).

**Derived:** recovery runs per frame, not per second, so the gap shrinks from 15 to 4 px in 51 frames: 0.51 s at 100 fps, 0.85 s at 60 fps. The crosshair is drawn from the shot counter, not from the actual spread value, so it is a visual approximation.

---

## 3. First-shot timing: why the click feels instant

**Design (Valve).** In Bernier 2001, section "Client-Side Prediction of Weapon Firing": "In Half-Life, we chose to avoid this complication by moving the implementation of a weapon's firing logic into 'shared code' just like the player movement code…" The paper adds that predicting firing, switching, deployment and holstering means "the user feels that the game is 100% responsive to his or her movement and weapon activation activities."

**Pipeline in code:**

1. `CL_CreateMove` packs the buttons held this frame into a usercmd.
2. The engine predicts that command right away. `CL_RunUsercmd` calls the client DLL's `PM_Move`, then `HUD_PostRunCmd`. Source: Xash3D reimplementation, [`engine/client/dll_int/cl_pmove.c` L900–938](https://github.com/FWGS/xash3d-fwgs/blob/27092af50d912432887faa43a1cccddfae569b2d/engine/client/dll_int/cl_pmove.c#L900-L938).
3. `HUD_PostRunCmd` calls `HUD_WeaponsPostThink` if `cl_lw` is non-zero. That copies the predicted weapon state in, runs `ItemPostFrame()`, and copies the results out. `runfuncs` is 1 only on the first prediction of a command, so sounds and effects play once. Sources: [HLSDK `cl_dll/hl/hl_weapons.cpp` L1047–1077, L845–870](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/hl/hl_weapons.cpp#L1047-L1077); CS: [cs16-client `cl_dll/cs_wpn/cs_weapons.cpp` L1006, L1226, L1296, L1337, L1468](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/cs_wpn/cs_weapons.cpp#L1468). The punchangle is part of the predicted state.
4. `PrimaryAttack` calls `PLAYBACK_EVENT_FULL(FEV_NOTHOST, …)`. On the client this runs the fire event locally in the same frame: viewmodel SHOOT anim, muzzle flash, sound, shell, and tracer/decal using the same shared random seed as the server. The punch is applied to the view before the frame renders. Source: [cs16-client `events/event_ak47.cpp`](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/events/event_ak47.cpp#L40-L80).
5. When the server runs the same command, it does not send the event back to the shooter. ReHLDS skips the invoking client `if ((flags & FEV_NOTHOST) && cl->lw && cl == host_client)` ([`pr_cmds.cpp` L1297](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/pr_cmds.cpp#L1297)). It also skips the viewmodel animation message: `SendWeaponAnim(…, skiplocal)` returns early `if (skiplocal && ENGINE_CANSKIP(…))` ([ReGameDLL `weapons.cpp` L1401–1414](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L1401-L1414); `CanSkipPlayer` returns `client->lw` ([ReHLDS `pr_cmds.cpp` L2739–2750](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/pr_cmds.cpp#L2739-L2750))). The local animation is never restarted by a late server message. HLSDK forces the animation only when the playing viewmodel animation differs from the `weaponanim` in the predicted state (comment: "fixes some animation glitches") ([hl_weapons.cpp L924–940](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/hl/hl_weapons.cpp#L924-L940)).
6. `cl_lw` is sent as userinfo and read by the server ([ReHLDS `sv_main.cpp` L5405–5406](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L5405-L5406)). Its default is 1 in Xash3D ([`cl_main.c` L78](https://github.com/FWGS/xash3d-fwgs/blob/27092af50d912432887faa43a1cccddfae569b2d/engine/client/cl_main.c#L78)).

**How fire rate is enforced.**

- With `CLIENT_WEAPONS`, `UTIL_WeaponTimeBase()` returns 0 ([HLSDK `dlls/util.cpp` L34–41](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/util.cpp#L34-L41)). Weapon timers are therefore relative countdowns.
- The countdowns are decremented by `cmd->msec/1000` each command, on both client ([hl_weapons.cpp L966–976](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/hl/hl_weapons.cpp#L966-L976)) and server (`Q_max(t − frametime, −1)`, [ReGameDLL `player.cpp` L5485–5499](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/player.cpp#L5485-L5499)).
- A shot is allowed when the timer is ≤ 0 (`CanAttack`, [weapons.cpp L879–893](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.cpp#L879-L893)).
- `GetNextAttackDelay` returns `0 + delay`; the "creep" compensation code is marked as removed after build 6153. The overshoot below 0 is discarded, so each cycle rounds up to a whole number of command frames. **Derived:** at 100 Hz the AK fires every 0.10 s (600 RPM) and the M4 every 0.09 s.

**Derived lesson for a single-player mod:** the instant feel comes from doing everything in the tick that reads the button:

- the fire decision
- ammo
- punch
- viewmodel SHOOT animation (restarted from frame 0)
- muzzle flash
- sound

All of it happens before that frame renders, with nothing deferred to a later message.

---

## 4. Viewmodel conventions

### 4.1 Bob (`V_CalcBob`)

Defaults: `cl_bob 0.01`, `cl_bobcycle 0.8`, `cl_bobup 0.5` ([HLSDK `cl_dll/view.cpp` L1720–1722](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L1713-L1724); identical in [cs16-client `view.cpp` L1885–1887](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/view.cpp#L1885-L1892)).

The formula, from [HLSDK view.cpp L164–206](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L164-L206):

```c
if (onground == -1) return bob;                         // frozen in the air
bobtime += frametime;
cycle = fmod(bobtime, cl_bobcycle) / cl_bobcycle;
cycle = (cycle < cl_bobup) ? M_PI*cycle/cl_bobup : M_PI + M_PI*(cycle - cl_bobup)/(1 - cl_bobup);
bob = len2D(simvel) * cl_bob;
bob = bob*0.3 + bob*0.7*sin(cycle);   bob = clamp(bob, -7, 4);
```

Where the bob is applied ([view.cpp L522, L638–676](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L638-L676)):

- Camera: `vieworg.z += bob`.
- Gun origin: `+= 0.4·bob·forward` and `z += bob`.
- Gun angles: yaw `−= 0.5·bob`, roll `−= bob`, pitch `−= 0.3·bob`.
- Gun origin is also lowered 1 u, with a small `viewsize`-dependent fudge.

**Derived:** at 250 u/s the bob ranges from −1.0 to +2.5 u, with a 0.8 s period that does not depend on speed.

### 4.2 Idle sway, lag and punch on the gun

- **Idle sway.** `V_AddIdle` and `V_CalcGunAngle` multiply sine sway by `v_idlescale`, which is 0 unless TFC's concussion effect sets it. The rates are `v_iyaw_cycle 2`, `v_iroll_cycle 0.5`, `v_ipitch_cycle 1`, and the levels are 0.3, 0.1 and 0.3. HL and CS therefore have **no idle sway**. Source: [view.cpp L96–103, L362–391](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L362-L391).
- **Viewmodel lag.** HLSDK has no view-model lag. cs16-client adds an optional `cl_weaponlag`, default 0, which is not part of the original game ([view.cpp L625–630](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/view.cpp#L625-L630)).
  - For reference, Source's `CalcViewModelLag` eases the last-facing vector toward the current one at 5/s. It speeds up past a difference of 1.5 and offsets the origin by `−5 × (forward − lastFacing)`, plus pitch-based shifts of `−pitch·(0.035 fwd, 0.03 right, 0.02 up)`. Source: [SDK2013 `src/game/shared/baseviewmodel_shared.cpp` L459–520](https://github.com/ValveSoftware/source-sdk-2013/blob/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0/src/game/shared/baseviewmodel_shared.cpp#L459-L520).
- **Punch moves the camera, not the gun.** The gun angles are copied from `cl_viewangles` and set in `V_CalcGunAngle` **before** `punchangle` and `ev_punchangle` are added to the camera angles. Source: [view.cpp L638–692](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L638-L692); same order in [cs16-client view.cpp `V_CalcNormalRefdef` L685+](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/view.cpp#L685). **Derived:** on screen the gun stays aimed at the un-punched direction while the camera kicks up.
- **Stair smoothing.** When the predicted origin steps up on the ground, the camera and gun z catch up at 150 u/s, lagging at most 18 u. Source: [view.cpp L694–716](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/view.cpp#L694-L716).

### 4.3 FOV and depth hack

- **FOV.** `default_fov` is 90 ([HLSDK `cl_dll/hud.cpp` L332](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/cl_dll/hud.cpp#L332)), and CS's `DEFAULT_FOV` is 90 ([ReGameDLL `cdll_dll.h` L36](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/cdll_dll.h#L36)). Scope FOVs are 40 and 10 for the AWP and Scout ([wpn_awp.cpp L77–81](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_awp.cpp#L77-L81)).
  - GoldSrc has **no separate viewmodel FOV** in the client code. The Xash3D renderer draws the viewmodel with the scene projection; its scene FOV is `scr_fov`, adjusted for widescreen ([`engine/client/cl_view.c` L307–315](https://github.com/FWGS/xash3d-fwgs/blob/27092af50d912432887faa43a1cccddfae569b2d/engine/client/cl_view.c#L307-L315)).
  - A separate viewmodel FOV is a Source feature: `viewmodel_fov` defaults to 54 ([SDK2013 `src/game/client/view.cpp` L111–113](https://github.com/ValveSoftware/source-sdk-2013/blob/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0/src/game/client/view.cpp#L105-L114)) and is aspect-scaled separately from the world FOV (L1084).
- **Depth range hack.** The gun is drawn with a compressed depth range so it is always in front of world geometry.
  - Quake: `glDepthRange(min, min + 0.3*(max−min))` around the viewmodel draw ([`WinQuake/gl_rmain.c` L706–709](https://github.com/id-Software/Quake/blob/bf4ac424ce754894ac8f1dae6a3981954bc9852d/WinQuake/gl_rmain.c#L706-L709)).
  - Xash3D: `VIEWMODEL_DEPTH_RANGE 0.3` ([`ref/gl/gl_studio.c` L3322–3364](https://github.com/FWGS/xash3d-fwgs/blob/27092af50d912432887faa43a1cccddfae569b2d/ref/gl/gl_studio.c#L3322-L3364), [`gl_local.h` L185](https://github.com/FWGS/xash3d-fwgs/blob/27092af50d912432887faa43a1cccddfae569b2d/ref/gl/gl_local.h#L185)).
  - Source: `DepthRange(0, 0.1)` ([SDK2013 `viewrender.cpp` L1102–1110](https://github.com/ValveSoftware/source-sdk-2013/blob/be57e3d01ac7d7aa6a2cacbfef187c0b46616db0/src/game/client/viewrender.cpp#L1102-L1110)).
  - The viewmodel origin is the eye position (`simorg + viewheight`); the gun's offset is authored into the model.

### 4.4 Animation conventions (CS 1.6)

**Sequence order** is given by the enums in [ReGameDLL `weapons.h`](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/weapons.h#L707-L715):

| Weapon | Sequences |
|---|---|
| AK47 | `IDLE1, RELOAD, DRAW, SHOOT1, SHOOT2, SHOOT3` |
| M4A1 | `IDLE, SHOOT1–3, RELOAD, DRAW, ATTACH_SILENCER`, plus `UNSIL_*` variants and `DETACH_SILENCER` |
| USP | `IDLE, SHOOT1–3, SHOOT_EMPTY, RELOAD, DRAW, ATTACH_SILENCER`, plus `UNSIL_*` |
| Glock18 | `IDLE1–3, SHOOT, SHOOT2, SHOOT3, SHOOT_EMPTY, RELOAD, DRAW, HOLSTER, ADD_SILENCER, DRAW2, RELOAD2` |
| Deagle | `IDLE1, SHOOT1, SHOOT2, SHOOT_EMPTY, RELOAD, DRAW` |
| Knife | `IDLE, ATTACK1HIT, ATTACK2HIT, DRAW, STABHIT, STABMISS, MIDATTACK1HIT, MIDATTACK2HIT` |

**When each sequence plays:**

- **Fire:** the client event picks `RandomLong(SHOOT1, SHOOT3)` on each shot. Pistols play `SHOOT_EMPTY` on the shot that empties the magazine (`bparam1 = m_iClip == 0`). Source: [cs16-client `events/event_usp.cpp` L57–80](https://github.com/FWGS/cs16-client/blob/a238ef131803ed7592c4ba701499027c862889ee/cl_dll/events/event_usp.cpp#L57-L80).
- **Draw:** plays on `Deploy`, with a 0.75 s fire lockout. The Glock picks DRAW or DRAW2 at random.
- **Holster:** there is no holster animation in the switch path; `Holster` blanks the viewmodel.
- **Reload:** RELOAD plays for `*_RELOAD_TIME`.
- **Idle:** after a shot, `m_flTimeWeaponIdle` is set (AK 1.9 s, M4 1.5 s, USP 2.0 s, Glock 2.5 s). When it expires, IDLE plays and the timer is pushed out 20 s ([wpn_ak47.cpp L186–197](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_ak47.cpp#L186-L197)).
- **Knife:** a slash re-arms in 0.35 s after a miss and 0.4 s after a hit. A stab re-arms in 1.0 s after a miss and 1.1 s after a hit. Source: [wpn_knife.cpp Swing L315–380, Stab L515–555](https://github.com/rehlds/ReGameDLL_CS/blob/4a50c42e85fd3778c2b7d24731c7d30c83bbab01/regamedll/dlls/wpn_shared/wpn_knife.cpp#L315-L380).

---

## Numbers to start from

| Parameter | Value | From |
|---|---|---|
| Ground accel / friction / stopspeed | 5 / 4 / 75 | §1.3 (CS hard-lock) |
| Edge friction multiplier | 2 (probe 16 u ahead, 34 u down) | §1.2 |
| Air accel / air wish cap | 10 / 30 u/s | §1.2–1.3 |
| Gravity / jump velocity | 800 / 268.3 u/s | §1.4 |
| Max speed (knife, pistol / M4 / AK / AWP) | 250 / 230 / 221 / 210 (AWP 150 scoped) | §1.4 |
| Walk / crouch multipliers | 0.52 / 0.333 of max speed | §1.1, §1.4 |
| Time to full speed / stop from full | ≈0.40 s / ≈0.50–0.55 s (slide 40–50 u) | §1.5, derived |
| Landing slowdown | `fuser2 = 1315.79 ms`; v ×= `1 − fuser2·0.00019` per tick | §1.4 |
| Move input scale; stick deadzone | 400 per axis, clipped to max speed; 0.15 hard cut, linear | §1.1 |
| Stick look speed at full deflection | yaw 210 °/s, pitch 225 °/s, linear | §1.1 |
| Punch decay | `len −= (10 + 0.5·len)·dt` (2° gone in 0.19 s) | §2.2 |
| AK KickBack, standing | 1.0, 0.375, 0.175, 0.0375, 5.75, 1.75, 8 | §2.3 |
| M4 KickBack, standing | 0.65, 0.35, 0.25, 0.015, 3.5, 2.25, 7 | §2.3 |
| Pistol / AWP punch | 2.0° up per shot (Glock 0) | §2.2 |
| Rifle spread (still / >140 u/s / air) | 0.0275·acc / 0.04 + 0.07·acc / 0.04 + 0.4·acc (AK) | §2.3 |
| Rifle accuracy growth / recovery | `n³/200 + 0.35`, cap 1.25; −1 shot per 22.5 ms after a 0.4 s pause | §2.3 |
| Pistol accuracy | `acc −= (0.3 − Δt)·0.275`, clamp [0.6, 0.92] (USP) | §2.3 |
| Fire cycle (AK / M4 / USP / Glock / Deagle / AWP) | 0.0955 / 0.0875 / 0.15 / 0.15 / 0.225 / 1.45 s | §2.4 |
| Deploy lockout | 0.75 s (AWP 1.45) | §2.4 |
| Reload (AK / M4 / USP / Glock / Deagle / AWP) | 2.45 / 3.05 / 2.7 / 2.2 / 2.2 / 2.5 s | §2.4 |
| Crosshair | gap +delta per shot (max 15 px), recovers `gap·1.3% + 0.1` px per frame; alpha −40 per shot (min 120), +2 per frame | §2.5 |
| Bob | `cl_bob 0.01`, `cl_bobcycle 0.8`, `cl_bobup 0.5`; clamp [−7, 4]; gun +0.4·bob fwd, +bob up, yaw −0.5·bob, roll −bob, pitch −0.3·bob | §4.1 |
| Idle sway / viewmodel lag | none / none (Source lag for reference: 5/s, cap 1.5) | §4.2 |
| Stair-step camera smoothing | 150 u/s, max 18 u | §4.2 |
| FOV / viewmodel depth range | 90° horizontal, gun uses the scene FOV / 0–0.3 of depth (Source: 0–0.1, `viewmodel_fov` 54) | §4.3 |

**Derived porting notes:**

- Several CS effects run once per tick or once per frame and so depend on frame rate: the landing slowdown, the crosshair recovery, and fire-cycle rounding. The decays themselves are already `dt`-scaled (punch, friction).
- Run friction before acceleration, and apply punch and decay before the frame's camera is built.

## Not verified

- **CS binary.** All CS-specific numbers come from ReGameDLL_CS and cs16-client, which are reverse-engineered. None were checked against the CS 1.6 binaries or the Steam build.
- **Walk key.** `cl_movespeedkey 0.52`, applied after the maxspeed clip, appears only in cs16-client. The default in CS 1.6's shipped `config.cfg` was not inspected.
- **Crosshair.** The algorithm and `Distances` table are from cs16-client, not Valve code.
- **GoldSrc renderer.** It is closed source. The 0.3 viewmodel depth range, the lack of a separate viewmodel FOV, and Hor+ widescreen FOV are inferred from Quake GL and Xash3D, not confirmed in GoldSrc.
- **`cl_lw` default.** The GoldSrc client default of 1 is taken from Xash3D; the HLSDK only reads the cvar.
- **CS client view code.** That CS 1.6's `client.dll` matches HLSDK `view.cpp` (bob, punch order, stair smoothing) is confirmed only through cs16-client's reimplementation.
- **Animation lengths.** Sequence frame counts and fps live in the `.mdl` files, which were not read. Only code-side timers are given.
- **Air acceleration on real servers.** The value used on typical CS servers varies; the engine default is 10 and the game DLL does not lock it.
- **Client frame rate.** The GoldSrc client's default `fps_max` was not confirmed. ReHLDS's `fps_max 100` is the server host's value.
- **Unit size.** Treating 1 u as about 1 inch is a community convention with no primary source; use the eye-height ratio instead.
