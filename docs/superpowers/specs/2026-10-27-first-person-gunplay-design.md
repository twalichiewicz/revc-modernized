# First person: a shooter's feel, with Tommy's own arms

Design for the macVC mod in this repo (`vc-minimap`, the "VC Modern HUD" dylib for the native Apple
Silicon port of Vice City, macVC 1.0.5217, built from reVC). It changes how first person on foot moves,
looks, shoots and is drawn.

## Why

The player wants first person to have Counter-Strike 1.6's level of polish and responsiveness, played
on a controller. CS 1.6 is the bar for **feel and detail**, not a feature list: its models and features
sit at a fidelity that fits a 2002 game. Modern Warfare-era features would be out of place. And first
person should be close to its own mode, the way GTA V treats it, rather than the third-person game with
the camera moved into Tommy's head.

Today it feels like "driving a car rather than moving a person", "floaty and slow", and the guns look
bad. Reading the code shows why:

- **Movement is the game's root motion.** Pushing the stick first turns Tommy's body to face that way
  at a fixed rate (`CPlayerPed::PlayerControlZelda`; only while aiming does `aimcam` switch him to the
  strafing control). Then the walk and run animations blend in and set his speed, and a stopping
  animation slides him to a halt. In first person you're inside all of that.
- **Shots wait for an animation.** Pulling the trigger starts Tommy's attack animation. The bullet
  leaves at the animation's fire frame (`weapon.dat` column T), and the cadence is the animation's
  loop (columns R–S). From a standing start the Colt fires at frame 14, nearly half a second after the
  pull.
- **The gun is a third-person prop.** `viewmodel.cpp` poses Tommy's real arms in the world, holding the
  game's weapon models. Those models are 230–730 triangles with **64×64** textures, made to be seen from
  across the street. In first person they fill a third of the screen.
- The camera eases after his head (0.28 s), and the right stick tops out at about 200°/s with a soft
  curve.

Prior art, with sources: `docs/research/fps-feel-goldsrc.md` (CS 1.6 and Half-Life movement, recoil,
spread, crosshair, viewmodel) and `docs/research/fps-feel-controller.md` (stick handling and aim assist
in console shooters, game-feel techniques). The look was settled with a throwaway spike; its scripts
and the approved render are in `docs/research/viewmodel-spike/`.

## Goals

1. **Moving responds on the first frame.** In first person the stick sets Tommy's velocity directly,
   relative to the view, with CS's acceleration and friction. He starts moving the frame you push,
   strafes and backpedals without turning his body, and stops without a stopping animation.
2. **The right stick feels like a modern console shooter's**, on the same footing as the movement: fast
   when you want it, precise for small corrections, with light aim slowdown over targets.
3. **A shot leaves on the frame you pull the trigger**, at the gun's own cadence, with CS-style view
   punch, spread that grows with sustained fire and movement, and a crosshair that shows the spread.
4. **The gun in your hands looks like it belongs in Vice City at CS 1.6's level of polish:** Tommy's own
   arms and hands (his model and his texture, in whatever outfit he's wearing), articulated so his
   fingers grip each gun properly, holding new low-poly, photo-textured guns. It's drawn as a proper
   viewmodel that never clips into walls, and it animates: draw, fire, reload, holster.
5. **All of it is first person on foot only**, and each part can be switched off, as every feature of
   the mod can.

## Non-goals

- Third person, vehicles (including the seat view and drive-bys), cutscenes and scripted cameras. They
  keep today's behaviour.
- The game's damage numbers, health, the story and missions, and the gunmen's cover AI.
- Melee weapons and fists in first person. They keep today's behaviour (the game's own animation on his
  real arms) in this round.
- Modern-era features: hit markers, kill cams, weapon attachments, weapon inspect, tactical sprint,
  slide.
- Aim assist that moves your aim (rotational pull, snapping, bullet magnetism). Only slowdown.
- New sounds. The game's own weapon sounds stay.

## Anti-goals (it fails if...)

- The guns or arms look higher-detail than the world: too many polygons, too-sharp textures, or a
  modern rendering look. They must sit at Vice City's fidelity: a few hundred triangles, about 256 px
  textures, and detail painted into the texture rather than modelled or shaded.
- Tommy's hands or arms are replaced by new ones. Earlier attempts that did this were rejected outright
  ("a massive downgrade... I told you to use the Tommy model").
- Anything smooths, delays or animates input before it takes effect: a body that turns before he moves,
  an easing on the camera, a shot that waits for an animation.
- Aim assist is noticeable as something steering the player.
- First person stops working with the mod's other first-person features: crouch walking, climbing,
  swimming, bullet time, the shootdodge, the over-the-shoulder aiming switch-off when in first person.

## Decided with the player

- **CS 1.6 is the polish and responsiveness bar; Vice City is the fidelity bar.** First person is
  treated as its own mode, as GTA V does.
- **Controller first.** Keyboard and mouse keep working through the game's own mouse look.
- **Guns are made new, by me, in Blender, by script.** Each is Vice City's own weapon remodelled at real
  dimensions and scaled to the game's model. They're low-poly like the game's (the spike's Colt is 284
  triangles against the game's 288), with all the detail in a 256×256 texture. The texture is a "photo
  shoot" of a detailed version of the same gun, projected onto the low-poly one by side, top, back and
  front views, the way Vice City's own gun textures are side-view photos.
- **The arms are Tommy's own model and texture, split apart and given joints.** His finger slab (one
  hooked piece on one bone per hand) is cut into four fingers, and each finger gets two joints. His
  thumb gets two joints of its own. Nothing else of his mesh changes.
- **Fingers are placed by anchors on the gun.** Each gun defines where each fingertip goes (the index on
  the trigger face, the others on the grip, the thumb along the frame), and each finger's two joints are
  solved to reach its anchor. Anchors are only positions. They're never drawn.
- **Approved grip, for the Colt** (from the spike): the gun sits in the hand bone's frame where the game
  puts its own Colt, scaled to the game's Colt length (×1.09), then moved 4 cm down and 3 cm forward in
  the hand so its trigger guard sits over his middle finger. The middle, ring and pinky anchors are on
  the left grip panel about 30 mm behind the front strap, a finger's half-thickness off the surface.
- **Framing:** the gun is lower right, turned in toward the crosshair (about 13°), the way CS frames
  its viewmodels. The forearm runs out of the lower-right corner.
- **His body stays.** With a gun out, Tommy's own body is still drawn (his torso and legs when you look
  down, and his shadow). Only his head and real arms are hidden, with the viewmodel arms drawn in their
  place.

## How it works

### Movement

In first person, on foot, standing or crouch-walking (not swimming, climbing, diving, falling, in a
cutscene, or out of the player's control), Tommy's horizontal velocity comes from the stick instead of
from his animations. After the game's own control has run (`CPlayerPed::ProcessControl`, already
hooked in `player.cpp`; `aimcam::FaceAndStrafe` already rewrites his velocity the same way), it's set
by the GoldSrc model:

- Wish direction is the left stick in the view's frame (forward is where the camera looks, flattened).
  Wish speed is the stick's push times his top speed for the gait.
- **Friction, then acceleration**, every frame, as `PM_Friction` / `PM_Accelerate`:
  `drop = max(speed, stopspeed) · friction · dt`, then
  `v += min(wishspeed − v·wishdir, accel · wishspeed · dt) · wishdir`.
- **Constants start at CS's:** accel 5, friction 4, and stop speed at 0.3 × top speed (CS's 75 of 250).
  That reaches 90% speed in about 0.33 s and full speed in about 0.4 s, and stops in about 0.5 s, with
  motion starting on the first frame. Both are tunable.
- **Top speeds are Vice City's own.** Walk on a light push, run on a full push, sprint on A, crouch walk
  as the mod's crouch has it, so first person covers ground exactly as third person does.
- He faces the view at all times, so strafing and backpedalling never turn him. His legs' animation is
  irrelevant in first person (his body isn't drawn), but it should keep playing the gait that matches
  his speed, for his sounds and for anything else that reads his move state.
- Jumping, climbing, vaulting and the shootdodge keep their own control. In the air he keeps his
  velocity, with CS-style air control (air accel 10, at most about 0.76 m/s added toward the stick).
- Pulling the trigger while sprinting drops him to a run and fires at once.

### Look

The right stick in first person (on foot), replacing today's `StickRates` behaviour there:

- **Dead zone 0.12**, radial and rescaled so output starts from zero at its edge. Counts as full at 0.95.
- **Response curve: power 2.0**, hip and aiming alike.
- **Top turn rate 240°/s yaw, 145°/s pitch** (about 4.2 and 2.5 rad/s).
- **Rim boost:** held at the rim (≥ 0.95), yaw speeds up by up to 50% (pitch by up to 20%), after a
  0.05 s delay and ramping in over 0.35 s. That's for whipping round without making small movements
  twitchy.
- **Aiming (LT)** scales the rate by `tan(FOV_aim/2) / tan(FOV_hip/2)`, so the crosshair moves across
  the world at the same apparent speed when the view zooms. In first person this replaces the
  `AimSensitivity` slow-down at its default value. Moving that slider scales the aimed rate from there
  (aimed rate = hip rate × the zoom ratio × `AimSensitivity` / its default), so the two never stack.
- **Aim slowdown:** while the crosshair is on a target's bounds (peds the game treats as targets:
  hostile, police, and anyone the player is fighting), the turn rate is multiplied by 0.5 at the hip and
  0.4 aiming, fading to none by about 1.5° outside them. It switches in at once and fades out over about
  0.15 s. It's skipped when the stick is pushed past 0.85, so it never fights a deliberate flick. It's
  weaker point-blank (inside about 3 m), full to about 20 m, and gone by about 60 m. There's no pull,
  snap or bullet bending.
- The look sensitivity settings on the Controls page scale all of it.
- The mouse keeps the game's own direct mouse look (it already has no smoothing).

### Camera

- The eye is fixed to his body. The 0.28 s ease after his head goes. Steps up and down are smoothed at
  most the way GoldSrc smooths stairs (a short, fast catch-up), never lagging behind movement.
- **Bob:** CS's `V_CalcBob` (`cl_bob 0.01`, `cl_bobcycle 0.8`, `cl_bobup 0.5`, clamped), scaled to
  his speed. It replaces today's step bob in first person, and the `HeadBob1stPerson` setting still
  turns it off.
- **View punch:** each shot adds a punch angle (up, with a little sideways for automatics) to the
  camera, decaying as CS's `len −= (10 + 0.5·len)·dt` (degrees, per second). Shots go along the punched
  view, as CS's do.
- **Shake on fire:** rotation only, never translation, and very small. The mod's existing handheld
  shake stays, reduced as it is today in first person.

### Shooting

In first person on foot, firing is the mod's, not the attack animation's:

- **On the pull, the shot.** When the trigger is pressed, the gun is ready and has ammo, and he isn't
  reloading or switching, the mod fires that frame through `CWeapon::Fire` (already hooked in
  `camera.cpp`), from his eye along the punched view, as today's first-person shots go. Going through
  `CWeapon::Fire` keeps everything that listens to it: police hearing gunfire, gunmen taking cover, ammo,
  reloading. The game's attack state is never entered in first person, so its animation can't gate or
  delay anything. The sound, muzzle flash and shell effects the attack path would have made must still
  happen; see "Left to the builder".
- **Cadence is the game's own:** each gun's time between shots is its attack loop (`weapon.dat` columns
  R–S, at 30 frames per second), so the Uzi still out-fires the Colt by the same margin. **Pistols, the
  Python and the shotguns fire once per pull** (my call); the SMGs, rifles, M60, minigun and flamethrower
  keep firing while it's held.
- **View punch per gun.** Pistols kick about 2° up per shot (the Python and shotguns more). Automatics
  use CS's `KickBack` shape: a base kick, growth with each shot in the burst, a little sideways that
  switches direction now and then, and a cap. The starting values are CS's M4 and AK
  (`docs/research/fps-feel-goldsrc.md` §2.3), scaled to each gun's cadence.
- **Spread** (my call: Vice City's player guns currently have none). The first shot standing still or
  walking goes where the crosshair is. Spread grows with sustained fire as CS's accuracy does
  (`shots³/200 + 0.35`, capped) and recovers after a short pause. It's larger running and much larger
  in the air. Crouching and aiming (LT) tighten it. Pistols recover between pulls by time, as CS's
  do. Shotgun pellets keep their own pattern on top.
- **Crosshair:** in first person the hip-fire dot becomes CS's four-line crosshair. Its gap opens with
  each shot and with movement, and closes as the spread recovers. It's the game's crosshair colour and
  drawn with the mod's feathered 2D like the rest of the HUD.
- **Aiming (LT)** keeps today's behaviour in first person (the gun comes up under the crosshair, and the
  view zooms slightly), now with the spread and turn-rate effects above.
- **Scoped weapons** (sniper rifles, rocket launcher) keep the game's rule: they fire only from the
  game's own scope view. At the hip they show their viewmodel and don't fire, so they need no hip
  recoil or spread.
- **The shootdodge and bullet time.** Diving and lying on the floor after a dive count as standing still
  for spread, so dive shots are as accurate as Max Payne's. In bullet time the guns run on the game's
  clock, slowed with the world: cadence, punch decay, spread recovery, crosshair recovery and the
  viewmodel's animations. Only your aim (the look above) runs at full speed, as bullet time already
  works.
- **Throwing** (grenades, tear gas, molotovs, remote grenades) works like CS's. Pressing the trigger
  starts the wind-up on the viewmodel. Releasing it throws that frame, with the distance set by how long
  it was held, as the game sets throw power today, through the game's own projectile code. After a
  remote grenade is thrown, his hand holds the game's detonator model, and the trigger sets it off.
- **Reloading** takes the game's own reload time and plays the viewmodel's reload. B (or R) reloads,
  as now. Firing an empty gun reloads it.
- **Switching** plays the holster and draw, about half a second in all. The new gun can't fire until
  its draw is done.

### The viewmodel

**Tommy's arms, built at runtime.** No Rockstar data goes in the repo. The arms are made, when needed,
from the player's current model (the game's loaded clump, whichever of his outfits he's wearing) and its
texture:

- Take his arms from the shoulder joint out: the upper arm (with the sleeve as that outfit has it), the
  forearm, the hand and fingers, with his skin weights.
- **Split each hand's finger slab** (the vertices on bone 25 / 35) into four fingers across its width at
  quarter points, separate the pieces, cap the cuts (a flat face each, plain skin), and part them
  slightly. Cut them as fingers along the hook the game modelled: a base segment down from the knuckles
  and a tip segment along the underside back toward the palm.
- **Add bones under his hand bone:** a base and a tip bone per finger, bending at the hook's own corner,
  and two thumb bones along the thumb (his hand bone's vertices below the palm). Move his finger and
  thumb skin weights onto them.
- It must work for every outfit's model, and fail safe: if a model doesn't split as expected, first
  person falls back to today's viewmodel and logs why.

**Guns, made in Blender, shipped as built assets.** For each gun, a script in `tools/` builds a detailed
model at real dimensions, photographs it into a 256×256 atlas (side, top, back, front), and builds the
low-poly model wearing it. The low-poly mesh, atlas and its data are committed to the repo in a form
the mod loads, so players don't need Blender. Each gun's data also has:

- its **hold**: where it sits in the hand bone's frame, matching the game's own weapon model of that
  gun (scale and offset), as the spike found for the Colt;
- its **anchors**: fingertip positions for the right hand, and for two-handed guns the left (on the
  handguard, pump or foregrip), with the thumbs. They move during animations (the trigger anchor as it's
  pulled, the left hand's to the magazine and back during a reload);
- its **muzzle** point (for the flash) and its **moving parts** (slide, bolt, pump, magazine, minigun
  barrels) as separate pieces the animations can move.

**Handedness follows Vice City:** guns he holds one-handed in third person (pistols, Python, Tec-9,
Uzi, Ingram) are one-handed. The rest are two-handed, with his left arm articulated the same way.

**Posing, every frame:**

- The hand bone's frame comes from the gun (gun pose × the gun's hold, inverted).
- The forearm carries on from the hand's axis, angled down about 28° and out about 12°, so the wrist
  stays nearly straight. The upper arm runs back past the camera. These are set directly, not by IK: a
  viewmodel arm only has to look right from one eye. For each bone, the matrix that puts its bind frame
  `F` in the world is `F · B⁻¹ · rest` (the spike found Blender's rest axes differ from the game's).
- Each finger's two joints are solved (two-bone IK) to put its tip on its anchor.

**Drawing.** Tommy's real body stays drawn in first person, as GTA V's does. Look down and his torso and
legs are there, and his shadow stays. Only his head (as today) and his real arms are hidden, by shrinking
their bones' skin away the way `camera::AfterPreRender` hides his head now. The viewmodel arms are drawn
instead. Wherever the viewmodel isn't used (fists and melee, climbing, swimming, the shootdodge's dive
and landing, getting in and out of vehicles), his real arms show again as the game poses them, so his
body never pops in and out. The viewmodel is drawn in a pass of its own after the world:

- with its own field of view, chosen for the framing above, independent of the game's FOV setting;
- in the nearest slice of the depth range (or after clearing depth), so it never sinks into a wall;
- lit by the light the game gives the player's ped at his position (its ambient and directional
  light), so it matches the scene in any weather and at night;
- textured from his current outfit's texture and the gun's atlas, with the game's texture filtering.

**Animations** are code-driven, as the current viewmodel's kicks and dips are, but moving the gun and
its parts, with the hands following through anchors:

- **Draw** (rises in from below), **holster** (drops away), **fire** (the gun kicks back and up a few
  centimetres and settles, with the slide or bolt cycling and the muzzle flash at the muzzle point),
  **reload** (the gun tilts, the magazine drops out, the left hand fetches a new one, slide or pump
  as the gun has it), and **empty** (a pistol's slide stays back).
- The gun lowers while sprinting, as now.
- There's no idle sway or movement lag. CS has none, and it keeps the gun crisp. Bob follows the camera
  (above).

### Weapons covered

First person gets the new viewmodel for every gun: Colt 45, Python, the three shotguns, Tec-9, Uzi,
silenced Ingram, MP5, M4, Ruger, both sniper rifles, the rocket launcher, the flamethrower, the M60 and
the minigun. Thrown weapons (grenades, tear gas, molotovs, remote grenades, and the remote's detonator)
get Tommy's hand holding the item, using the game's own small models (fine at that size), and the
throw described under "Shooting". Melee weapons and fists keep today's behaviour (non-goal).

### Settings

Every part has a switch in `vcmodernhud.ini`, on by default, written into the file's explanations like
the others:

- `FirstPersonMovement`: the stick sets his velocity in first person.
- `FirstPersonLook`: the new right-stick handling in first person.
- `AimSlowdown`: aim slowdown in first person.
- `FirstPersonGuns`: the new viewmodel, instant firing, punch, spread and crosshair. Off gives today's
  first person.

The tunables (movement constants, turn rates, slowdown strength, viewmodel FOV) go in the ini too, with
the defaults above.

## My calls (to check)

- Movement uses CS's own constants (about 0.4 s to full speed), not Half-Life's snappier 0.13 s. CS is
  the reference, and "responsive" comes from motion starting on the first frame, not from instant top
  speed.
- Pistols, the Python and shotguns fire once per pull. Today in Vice City holding the trigger keeps a
  pistol firing.
- First person gets spread, which Vice City's player guns don't have today. That makes first person
  harder than third person at range, so the first shot standing or walking is always exact.
- Aim slowdown only, no pull.
- Melee and fists are left as they are in this round.
- One- or two-handed as Vice City holds each gun, so pistols are one-handed, unlike CS's two-handed
  pistols.
- The spike's scripts and render are kept in `docs/research/viewmodel-spike/` as reference, not as
  product code.
- Throwing is CS-style: wind up on press, throw on release, with the game's power and projectiles.
- Dives count as standing for spread. In bullet time the guns slow with the world, and aim doesn't.
- In first person, aiming's slow-down follows the zoom, with `AimSensitivity` scaling it from its default.
- Scoped weapons keep firing only from the scope. No new hip-fire.

## Left to the builder

- **The firing path.** How to keep the game out of its attack state in first person while firing through
  `CWeapon::Fire`. And where the shot sound, muzzle flash, gunshell particles and the "ped fired" events
  the attack path would have made come from (check reVC's `CPed::Attack` / `CWeapon::FireInstantHit`).
  New hooks go in `tools/patch_macvc.py`'s list, and `install.sh` must be re-run.
- **How the viewmodel is drawn:** through RenderWare (a clump built at runtime, the game's skinning)
  or the mod's own GL path (`shaders.cpp`), and how its depth range and lighting are done there.
- **Asset format and build:** how the Blender scripts' output is stored and loaded, and how they're run
  (Blender is a developer tool, not a player requirement).
- **Exact numbers:** the defaults above are starting points from the research. Tune by playing:
  per-gun punch, spread and crosshair constants, the viewmodel FOV and framing, animation timings.
- **Each gun's hold, anchors and parts**, using the Colt as the worked example
  (`docs/research/viewmodel-spike/`).
- **The order of work**, and what can ship first (movement and look don't depend on the new art).
