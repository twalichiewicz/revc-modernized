# macVC runtime feedback probe

This is a standalone diagnostic sidecar for the native arm64 macVC **1.0.5217**
binary. It is excluded from the production plugin build. It calls no save or
settings persistence API. Do not save a game containing a temporary fixture NPC.
This work concerns a GTA: Vice City mod; aircraft names refer to in-game vehicles.

Build from the repository root:

```sh
python3 tools/runtime/build_probe.py
```

The build checks every native symbol name against `--game`, runs the bounded
command parser and native-action boundary tests, emits offsets from `--plugin`'s retained symbols, builds
with warnings as errors, and ad-hoc signs `build/runtime/libVCMMProbe.dylib`.
The generated hidden-global offsets are used only if the loaded plugin UUID
matches the build artifact. Rebuild the sidecar after rebuilding the plugin.
The game UUID is checked separately at runtime.

`player_motion.sources` is a read-only input observer. It reports the selected GLFW
joystick, bounded raw/mapped axes for connected devices, and the native controller
state regions at offsets `0x6c`, `0x96` and `0xc0`. Compare these with `pad_new` and a
concurrent physical-controller observation before attributing movement to automation,
controller drift or the game's merge. It records game action states, not keyboard text.

For an owned Learjet, `player_car.wheels` reports the three physical wheel snapshots:
ground contact, spin radians, compression metres and nose steering radians. This uses the
matching plugin's public snapshot function with its current header ABI; stale wheel sources
also reject the sidecar build. `flight.airport_jets` records model registration, native stream
state, model references and body-COL metadata. These observations alone do not establish
accepted handling, sound or visual appearance.

`player_car.jet_gear_down` reports the owned jet's deployment fraction (0 stowed,
1 extended). `camera_debug` records the native camera mode, zoom, world source and
seated head/eye coordinates relative to a validated vehicle. With a matching plugin
UUID, it also reports first-person activation, cockpit ownership, filtered head
position and look angles. These fields only observe camera state; they do not move
the camera or replace its transforms. Optimized split head-coordinate globals are
resolved separately from the selected binary's symbols.

`player_car.engine_power` observes the owned jet's cached engine spool, health and
engine state through the matching plugin's public function. `exhaust_particles`
reads the native EXHAUST_FUMES list: validated live count, maximum alpha, nearest
camera distance, creation range and up to six particle samples. The bounded walk
checks each node against the native750-particle array and detects cycles. These
fields do not change particle settings or create particles; a live particle count
alone does not establish visible appearance.

`flight.tower_models` observes native streaming state, original-atomic readiness,
references and native COL primitive counts for the four inventoried upper-tower
models. These bounded, read-only metadata snapshots help distinguish missing
original assets from a missing collision surface. Atomic readiness does not
establish that an instance was admitted or drawn, and the original COL counts do
not include the separately scoped supplementary aircraft contacts.

Hidden layouts also require the selected production plugin to be at least as
new as the relevant mirrored ABI sources/headers. For example, changing
`reactphysics::Body` requires rebuilding the production plugin before this
sidecar: a UUID copied from an old artifact does not establish that new headers
describe its layout. This repo currently emits no production source-build stamp,
so the check uses conservative file modification times. Build the selected
plugin from this source tree; a copied/touched binary is not proof of provenance.

For native-only profiling of an older baseline, use:

```sh
python3 tools/runtime/build_probe.py --no-hidden --out build/runtime-native
```

This mode does not inspect the selected plugin artifact, emits no hidden offsets,
and disables **all** plugin-local reads (`hidden_globals_enabled=false`), including
reaction/APB/grade/config/heli crew and wear state. It can report native helicopter
generation/state and frame cadence. In particular, an old baseline's `heli::Wear`
cannot be read using the current source layout even if its UUID was emitted into
a generated header. Native skeleton snapshots remain available; physical and
native-target fields are null without the current hidden reaction state.

Launch the executable directly from GameData, after closing any existing game:

```sh
cd '/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData'
env -u RW_MTL_RT \
  DYLD_INSERT_LIBRARIES=/Users/waliwalu/GitHub/vc-minimap/build/runtime/libVCMMProbe.dylib \
  VCMM_PROBE_COMMAND=/tmp/vcmm-probe.command \
  '/Applications/Grand Theft Auto Vice City.app/Contents/MacOS/macVC'
```

`RW_MTL_RT` must be absent: even a value of `0` enables the native cached
environment check. This sidecar does not edit that native cache. Use the
production plugin already installed in the application; the sidecar does not
install or patch anything. No debugger, executable-memory rewrite, or JIT is used.

The CoreFoundation main-loop observer waits for the patched `CHud::Draw` slot
to point into `libVCModernHUD.dylib`, then atomically wraps its **current** target.
It validates the version 2 hook table, UUID, patched branch, and trampoline.
The observer does not run mutating game commands. Once chained, all commands run
on the main thread before the current plugin HUD handler. A missing or replaced
hook is not blindly reinstalled. Native commands require an initialized player
validated through the ped pool, loaded-game flag, area, time, and frame counter.

Send commands from the repository root, one controller at a time:

```sh
python3 tools/runtime/probe.py status
python3 tools/runtime/probe.py clock 12 0 1
python3 tools/runtime/probe.py weather 0
python3 tools/runtime/probe.py teleport -327 -489 12
python3 tools/runtime/probe.py ground -327 -489 25
python3 tools/runtime/probe.py spawn -324 -489 15
python3 tools/runtime/probe.py damage 17 10 0 0
python3 tools/runtime/probe.py remove
python3 tools/runtime/probe.py restore
```

The command file is claimed by atomic rename before reading. The CLI atomically
publishes a single request with a unique ID and waits for its matching status.
Use `--command /absolute/path` to match a different `VCMM_PROBE_COMMAND`, and
`--timeout 30` when streaming may take longer. A timeout leaves the request in
place if it has not been consumed; inspect the status/log before removing it.

| Command | Arguments and behavior |
| --- | --- |
| `status` | Current player/NPC generation, health, state, wanted level, held weapon slot/type/ammo, clock/freeze, slow flag/time scale, area, weather interpolation/rain/clouds, reaction count, and helicopter state. Lighting includes native RT enabled/active, sun vector/screen position, shadow strength, ambient and directional colors. Read-only `point_lights` reports the current bounded native 64-entry light list, and `player_motion` reports native movement state and velocity in metres/second; neither requires hidden plugin globals. `player_motion.pad_new` contains the 21 merged native controller fields. `pad_mode` and `pad_affected_by_controller` report the native mapping mode and input-source flag; do not infer a previous process's input mode from a later launch. Generation-validated `player_car` reports model/class/status/origin, actual vehicle velocity, native mass/drag/damage flags and health; rotor speed is read only for the six full automobile helicopter models. `flight` reports current mission/cutscene ownership, UUID-guarded force/roster readiness and request timestamps, Dodo streaming/COL metadata, and at most 32 native aircraft-pool entries, including their pose, velocity and UUID-validated wheel snapshots. `player_car` also reports force multiplier, centre of mass and physical wheel spin/compression/steer to distinguish a spawn/contact problem from input or aerodynamic behavior. A not-loaded snapshot outside roster eligibility is not evidence of a failed model request. These are read-only diagnostics, available after rebuilding/relaunching the sidecar. A light list sampled outside the estate does not establish its room lighting. |
| `npcpose` | Status acknowledgment including owned NPC reaction mode/weight/balance, physical/native-target/rendered positions for all 15 physical joints, root owner/error, separate pelvis offset/error, physical neck/head axis, native flags, get-up association clocks/flags, and native foot ground/normal snapshots. Read only. |
| `window status` | Read the actual Cocoa window's fullscreen style bit and the separate native renderer `Windowed` preference. The same fields appear in every status acknowledgment under `window`. |
| `window native 0/1` | On the main-thread HUD boundary, request the actual Cocoa `toggleFullScreen:` transition only when the current style differs. Requires the windowed renderer for a change; does not select a renderer mode or restore/change the camera. Repeated pending requests do not toggle twice. The acknowledgment is asynchronous: inspect `window status` after completion. Calls no INI/settings/health API; production OS-notification handling persists the completed mode. |
| `clock H M FREEZE` | Native `CClock::SetGameClock`, hour 0–23, minute 0–59, native `gbFreezeTime` 0/1. Does not freeze physics. |
| `weather N` | Native `ForceWeatherNow`, 0–5; `-1` releases forced weather. Rain/cloud/interpolation still evolve; record their status when comparing captures. |
| `rt 0/1` | Runtime frontend raytracing byte, refused if `RW_MTL_RT` is present. |
| `grade 0/1` | Runtime plugin grade readiness byte, guarded by the plugin build UUID and nm-derived offset. Enabling also requires grading to have initialized at plugin startup (`grade_can_enable=true`); otherwise restart with `ModernLighting=1`. Disabling is always allowed when the byte is available. |
| `clouds 0/1` | Runtime plugin cloud flag, using nm-derived `gConfig` and the compiled `offsetof(Config, clouds)`, guarded by the plugin UUID. |
| `lightmap 0/1 [MULT]` | Native live lightmap enable switch and optional multiplier 0–8. |
| `gloss 0/1 [MULT]` | Native live gloss enable switch and optional multiplier 0–8. |
| `rimlight 0/1 [MULT]` | Native live rimlight enable switch and optional multiplier 0–8. |
| `filter MODE [INTENSITY]` | Native live color-filter mode: 0 None, 1 Simple, 2 Normal, 3 Mobile; optional native intensity 0–2. Intensity is nonlinear and zero differs from mode None. Status records both; `restore` returns both to their original values. |
| `teleport X Y Z` | On-foot player only, ordinarily outdoors. Native collision/scene streaming followed by `CPed::Teleport(CVector)`, with player revalidation after streaming. Uses the exact supplied height, including water fixtures. X−2500..1700 and Y−2100..2100 allow bounded physics checks just outside the sector grid; Z−100..1200. NPC, camera and shot fixture limits remain narrower. One bounded estate-door exception permits area2 source X−430..−330, Y−625..−530, Z15..40 to destination X−380..−376, Y−546..−540, Z16..19. This exception skips exterior streaming and never writes the current area; the normal doorway performs the transition. |
| `camera SX SY SZ TX TY TZ` | Native fixed camera source and look-at target, jump cut, script controller. |
| `camera restore` | Native `RestoreWithJumpCut`. |
| `ground X Y Z` | Native static-world vertical query from Z to Z−100, reporting hit point and normal. |
| `line SX SY SZ TX TY TZ` | Native static-world line query, reporting hit point and normal for walls/slopes. Queries buildings, objects, and dummies; excludes vehicles and peds. |
| `spawn X Y Z` | One normal civilian, model 7/type 4, within 150 m of the player. Uses native ground +1 m. Requests the model and returns a retry message if not loaded. Requires at least four free native ped-pool slots before calling `AddPed`, and refuses a second owned NPC. |
| `npcmove X Y Z` | Native teleport of the owned eligible NPC, with native ground +1 m and the same distance bound. |
| `npcrun X Y Z` | Give only the owned, living eligible NPC the native RUN_TO_AREA objective. Destination must remain within 50 m of that NPC and 150 m of the player; normal native navigation/contact remains authoritative. |
| `weapon TYPE` | Select only an already-owned weapon type 0–36 for the living on-foot player. Calls verified native `SetCurrentWeapon(int slot)` after searching all 10 inventory slots; grants no weapon/ammo. An unloaded owned model is requested and requires retrying. |
| `npcshot BONE SX SY SZ` | Target the owned NPC's current `foot_l`, `foot_r`, `hand_l`, `chest`, or `head` bone using the player's actual held firearm with ammo. Source must be within 30 m of the NPC. Native line of sight includes buildings, vehicles, pedestrians, objects, and dummies; fire only when its first hit is the owned generation. Pass that real contact point/piece unchanged to patched `DoBulletImpact`, exercising scoped accepted-damage handling. A miss, wall, or other actor refuses the shot. |
| `damage WEAPON AMOUNT PIECE DIRECTION` | Calls the currently hooked exported `CPed::InflictDamage` only on the owned eligible NPC. Reports the untouched native bool plus before/after health and state. Native bool means fatal transition, not damage acceptance. |
| `remove` | Native removal of the owned generation-referenced eligible NPC only. |
| `wanted N` | Native player wanted setter, 0–6; normal game/plugin wanted logic continues afterward. |
| `pilotdamage SLOT WEAPON AMOUNT PIECE DIRECTION` | Native hooked ped damage only on a generation-validated, mission-created civilian/model-1 pilot attached to the selected random police helicopter in slot 0/1. Reports native result and health. |
| `helishot SLOT PART` | One normal hooked native `CWeapon::DoBulletImpact` ray toward `lamp`, `tail`, `mast`, `hull`, or `pilot` in a validated random police helicopter slot 0/1. Requires the player's actual held firearm; cockpit rays additionally validate the attached pilot. Native wall visibility and part selection still decide the hit. |
| `frames start LABEL` | Begin a bounded frame capture; labels contain only letters, digits, `_` and `-`. |
| `frames stop` | Stop and atomically write the frame CSV. |
| `restore` | Restore runtime clock/freeze, weather types, RT, grade, clouds, native custom-pipe switches/multipliers and color-filter mode/intensity as captured before the first mutation; restore native camera if owned; remove the owned NPC if still eligible. Restore the initial weapon slot only for the same player generation and unchanged owned slot/type. An unloaded original model requires retrying after streaming. Teleport, wanted and damage changes are not rolled back. |

Damage accepts native weapon IDs 0–36 and 39–44, amount 0.01–1000, body piece
0–6 and direction 0–3. Useful IDs are Colt45 **17**, explosion **41**, drowning
**43**, fall **44**; torso **0**, head **6**. Positions are bounded to the game
map (X/Y −2000…2000, Z −100…1200). Spawned NPC handles include the native pool
generation and are revalidated before every use. A readiness/timeline change
discards ownership; no existing world ped is selected as a replacement.
The RT/grade/cloud/custom-pipe baseline survives these world changes so
`restore` still returns process-global switches to their original values.
The weapon baseline is discarded on a world/timeline change. The slot overload
was verified in native disassembly at `0x1001a18c0`: it writes its argument to
`ped+0x64c` and indexes `ped+0x550` with stride `0x18`; the enum overload at
`0x1001a19cc` reads the slot from weapon-info `+0x60` and calls that overload.

`npcshot` calls the native impact stage, rather than granting a gun, constructing
a synthetic collision point, or calling damage directly. It does not invoke the
weapon's firing/ammo-consumption stage. Native damage acceptance remains decisive.
NPC and player references and the held weapon are checked again after the query.

Window commands run before the world/camera command gates, including with a fixed
camera, while retaining the main-thread HUD requirement. `window.native_fullscreen`
is the actual `NSWindow` style bit 14; `renderer_windowed` is the independent
32-bit `FrontEndMenuManager+0x718` value. `pending_native` records a requested
transition for at most three seconds. `restore` does not reverse window mode.
The probe uses typed Objective-C runtime messages and links `libobjc`; it does
not require an Objective-C++ source file or write native preference fields.

`player_bones` supplies read-only rendered chest, neck, shoulders, elbows and wrists
plus the player's native right/forward vectors. Native hierarchy/tag lookups are
bounded, and the player is pool-generation validated. This telemetry also works
in `--no-hidden` builds and reads no private player-controller layout.

`apb` reports the read-only wanted model: state 0 Clear, 1 Reported, 2 Wanted,
3 Searching, 4 APB; stars/escaped stars, saved/current outfit and vehicle details,
last-seen/search timing, and spotted/unspotted eye fill summaries. Each helicopter
includes generation-validated pilot/gunner status, actual plugin lamp brightness,
lamp/rotor hit counters, crash phase/cause/velocity, setup timings, and matching
emitted native spotlights near its optical lens. These fields use nm-derived
local offsets and are disabled on UUID mismatch. The build also derives the
wear-array stride; older layouts are not interpreted as the current crash layout.
Native `light` is the unattenuated searchlight field; use `wear.actual_light`
and `emitted_spots` for daylight suppression checks.

Files are derived from the explicit command path:

* `/tmp/vcmm-probe.command.status.json`: atomic latest status and acknowledgment.
* `/tmp/vcmm-probe.command.log`: bounded 1 MiB log plus one previous log.
* `/tmp/vcmm-probe.command.frames.csv`: at most 36,000 frame samples; no per-frame
  disk writes. Hitting the bound automatically stops and writes the capture.

`interval_ns` is the wall-clock interval between consecutive calls at the same
HUD frame boundary. It includes the previous frame's remaining render/present
and the next frame's simulation/render work before HUD. It is a whole-frame
cadence measurement, not isolated helicopter CPU time. `hud_chain_ns` is only
the chained HUD handler duration. CSV rows also contain game frame/time,
reaction-vector count, and random-helicopter count. The first interval is zero.
Additional columns retain both random helicopter generation refs/states and
attached crew counts, loaded-model mask bits (165/cop/SWAT/current M4 model), and
the owned NPC's native state, reaction mode, weight/balance, root owner/error, pelvis offset error, physical
pelvis/head/foot trajectories. A missing reaction uses mode `-1`; modes 0–3 are
Balanced, Falling, Recovering, Settled. Pose columns are physical simulation
positions; use `npcpose` to compare against the actual rendered skeleton.
Balanced reactions retain native root ownership. `root_error` is JSON `null`
(CSV `null`) whenever `root_owner` is `native`; the separate `pelvis_offset_error`
continues to measure mesh displacement from its captured root-relative offset.
Physical neck/head-axis methods use offsets from the exact matched plugin UUID.
`valid=1` requires a ready player, native foreground state, no menu/pause/cutscene,
and a nonzero interval at most 250 ms. Larger intervals remain in the CSV with
`valid=0` so focus/loading gaps cannot silently become gameplay hitch evidence.
`flags` retains those six checks in bits 0–5 respectively. Report excluded
intervals separately: a 250 ms cutoff also excludes genuine very large hitches.
The one-second status heartbeat is suspended during capture. Avoid extra status
commands while measuring, and discard samples around setup/streaming commands,
menus, pauses and loading. Polling checks the command path at most 20 Hz.

Example capture:

```sh
python3 tools/runtime/probe.py frames start estate_rt_on
# Leave the game actively rendering for the desired interval.
python3 tools/runtime/probe.py frames stop
cp /tmp/vcmm-probe.command.frames.csv /tmp/estate_rt_on.csv
```

Build/parser/symbol verification establishes the sidecar's construction and
input bounds. Native operation and visual/physics correctness still require
the real game session and recorded observations. The sidecar calls no settings
save API, but the native settings UI or game shutdown can serialize live native
pipe values. Run `restore` before using those paths; sidecar mutations alone do
not guarantee the game cannot persist a value. The game can naturally react to
damage, wanted level, and streaming.

`playerhit AMOUNT` accepts finite0<AMOUNT<=10 and calls the patched native
CPed::InflictDamage entry on the current generation-validated player with no
attacker, Colt17,piece3,direction0. Requires foreground outdoor ordinary
on-foot gameplay,health>50,player camera,controls enabled,and no mission.
Native armour/health acceptance is reported; no heal or direct health write occurs.

`carroll 0|1` requests upright/inverted sign for only the current generation-
validated player's parked healthy automobile while seated as its living driver.
It refuses mission cars,non-automobiles,speed>1m/s,skewed/mid-roll bases,and scripted
or paused gameplay. If the current up.z sign differs,it negates right/up axes,
clears native matrix cache bits,then calls CMatrix::UpdateRW and
CEntity::UpdateRwFrame once. Forward,translation,velocity and physics ownership
remain native. Repeating the same sign does nothing. Use carroll0 promptly to
recover; restore does not reverse a roll or damage. `player_car` status reports
current native pointer/ref validation,driver ownership,and actual up vector.
These commands are diagnostic setup; ordinary native damage/camera/physics still
must be observed in the game. Builds/tests do not establish live visual acceptance.
