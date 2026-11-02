# Driving in first person, the police helicopter, the airliners, the wanted HUD and doors (Plan 7)

**Goal:** The player's list after playing Plan 6:

1. Driving in first person: "aiming to shoot still uses the 3rd person cursor, but it should be the first
   person one"; "the gun being in Tommy's hand while he drives obfuscates a lot of your view. When you're
   not aiming we should just leave the gun on the center console, and when you aim Tommy grabs it and
   starts aiming."
2. Helicopters: "their spotlight should be a physical object on the bottom of the model"; "it should be
   shootable/destroyable"; "shoot the pilot (same as you can shoot people driving); shoot out the rear
   rotor; do more damage if you shoot where the top blades connect to the fuselage."
3. The NPC jets: "the lighting doesn't seem to affect them, they're weirdly lit"; "they're flying way too
   low considering where our new clouds are"; "at night they should have blinking lights."
4. Wanted: "if you get off/out of the car that is IDd, the icon on the HUD should grey itself to about 60%,
   if you get back in it goes back. If the car completely despawns/is destroyed the icon should be
   cleared"; "cops are able to pull you out of the car a little too easily even when you're moving. It
   should be like GTA4 [...] they should do the hold-onto-door handle ragdoll and eventually let go";
   "when an officer spots you, the spotted icon above them shouldn't just vanish, it should stay red as
   they come to intercept, then once things are at regular wanted behavior the icon can go away."

The player asked to skip the questions and start on all four.

## Global Constraints

- Plans 1-6's. The game's own models and functions where it has them; offsets for 1.0.5217 only.
- Every change has its switch (`ConsoleGun`, `CopsGrabDoors`, `Planes`; the helicopter's under
  `HeliDamage`, the HUD under `RealisticWanted`), and falls back to the game's own if a function or hook is
  missing (logged).
- Placements measured from the models (`tools/viewmodel`-style DFF reading in the scratchpad: the cars'
  interiors, chopper.dff, airtrain.dff) and checked by render; rules with no game in them unit-tested.
- Don't commit; install only with the game closed.

## Review Focus

1. The gun is never lost: whatever happens while it's on the console (aiming, getting out, being dragged
   out, the game changing his weapon), it's in his hand again when he's out (`seatgun::Off`).
2. Nobody is left attached: a door-holder or pilot whose car or helicopter is gone, wrecked, or no longer
   his is detached (`doorgrab::Update`, `heli::Disband`); the game clears `m_attachedTo` itself when the
   entity is deleted (`RegisterReference`).
3. Render state, lights and materials are put back after the lamp, the beam, the console gun and the
   planes (`shaders::SavedStates`, `SetAmbientColoursForPedsCarsAndObjects`, `Unfade`).
4. The flight path's runway nodes never move (the game puts every node under 14 m at 14 m as it loads the
   path: the first install looked for nodes under it, found none and raised nothing), so the game's take-off
   and landing points and its timing (by length over the ground) stay as they were
   (`TestRaisedIntoTheClouds`, `TestAsTheGameHasIt`).
5. No line of sight leaves the world's grid. `CWorld::GetIsLineOfSightClear` and `ProcessLineOfSight`
   don't clamp their sector indices (x -2400 to 1600, y -2000 to 2000), `CCoronas::Render` looks from every
   corona on screen to the camera every 2 s whatever its LOScheck, and the jumbos fly off the grid's south
   edge: their lights, seen from 4 km, crashed it twice (reading, then writing, before `ms_aSectors`). Both
   functions are hooked (`gridfix`, `gridclip` with tests/test_gridclip.cpp) and cut every line, the game's
   and the mod's, at the grid's edge.

---

### Task 1: First person in the seat

- `driveby`: in the first-person seat, Counter-Strike's four lines (`fpcross`) instead of the ring, the
  gap from `fpfire::Spread` (the car's speed as his on foot: still, walking, running), and the shots spread
  so in `DoDriveByAutoAiming`. A shot waits for the gun to be in his hand (`seatgun::InHand`).
- `consolespot` (tests/test_consolespot.cpp): the console's top between the seats (within 0.2 m of his
  hips, flat for the gun's length, 10 cm clear over it), else his seat's inner edge, else a guess; the gun
  laid on its side, its box resting on the spot. On ten of the game's cars: seven consoles, three seats.
- `seatgun`: the game's model of the gun (`CSimpleModelInfo::CreateInstance`) drawn on the console from
  `CAutomobile::Render`'s hook; his right hand to it (0.13 s), it up into the drive-by (0.17 s, the gun
  blended from the console to his hand bone); back down (0.15 s), his hand back to the wheel (0.17 s).
  Bikes and boats: out of his hand till he aims.

### Task 2: The police helicopter's parts

- `helizones` (tests/test_helizones.cpp): the lamp (a ball under the chin), the mast (a capsule from the
  roof to the hub), the tail rotor (a thin ellipsoid), the pilot (a capsule in the right-hand seat, hit only
  through the cockpit's glass), the body as before; a part within 0.5 m inside the body's rounder capsules
  is hit first. The lamp aimed at its spot (never above 9 degrees under level) and its mesh.
- `heli`: hits routed by part (mast x3; lamp 3 hits: glass shards, sparks, `PlayOneShotScriptObject` 57,
  dark for good; tail rotor 6 hits: debris, its atomic hidden, shot down; pilot: the game's ped hit, and
  dead it goes down). The game's searchlight corona moved onto the lens, its red light onto the tail fin
  (`CCoronas::UpdateCoronaCoors` after `PreRenderAlways`). The lamp drawn after `CHeli::Render` (new hook).
  The pilot: a civilian in a policeman's model (so the police's and the witnesses' own work leave him be),
  a mission ped while aboard, attached in the seat, `CAR_sit` kept playing.
- `sight`: at night (20:00-06:00) a helicopter with its light out sees 60% as far.

### Task 3: The airliners

- `flightpath` (tests/test_flightpath.cpp): flight.dat's airborne nodes raised to a climb of 14% and a
  descent of 13% to a peak of 1350 m (eased over 600 m); measured on the real path, up to 1299 m at 10 km
  of 20.7, the runway and the first 2 km of climb as they were.
- `clouds::Between`, `sky::Through` / `SunAt`: the frame's clouds on the CPU along a line, and towards the sun.
- `planes`: the path raised once loaded (and again if it's reloaded); drawn with the sun's light (the
  game's directional for the hour) while it's up, a quarter of it in a cloud's shadow, and the sky's
  colours all round; faded by the clouds along the line to it (material alpha, put back after), not drawn
  inside them; seven coronas (beacons top and bottom pulsing 0.4 s in 1.3, wingtip strobes 0.3 s in 1.5,
  navigation lights, the tail), seen from 4 km, faint by day, hidden by cloud.

### Task 4: Wanted

- `heat` (tests/test_heat.cpp): the vehicle's handle on the description; `VehicleLeft` (on foot: greyed to
  60%), `VehicleGone` (off the description). `wanted::CarGone` each frame: the handle dead or the car
  wrecked.
- `sight`: spotters (a full eye, or a cop who sees a crime of his: `witness` calls `sight::Spotted`) keep a
  red, full eye at least 2.5 s and till he's within 10 m, has lost him 1.5 s, or 8 s have passed; it fades
  over 0.6 s. `wantedhud` draws them red.
- `grip` (tests/test_grip.cpp): open under 1.2 m/s, catch hold under 11 m/s; the grip goes at
  0.15 + 0.6 (v / 6)^2 + 1.2 (turn / 1.2)^2 a second, comes back at a crawl, and 0.35 s at a crawl opens it.
- `doorgrab`: `CVehicle::CanPedEnterCar` (for a ped in `CPed::SeekCar` jacking the player's car) as `grip`
  has it; holding, attached at the door tugging (`CAR_doorlocked`), let go with `SetFall` (`KO_skid_back`)
  and 60% of the car's speed; one opening the door as it pulls away (over 3 m/s) quits and hangs on.

### Task 5: After playing it

The player: "the red spotted indicator never seems to clear, even after I have several cops all actively
shooting at me" (it's meant as in Hitman or Metal Gear Solid: someone's making him out, and once he has,
long enough to notice); "fall damage needs a major rework: SWAT falling out of helicopters and surviving
perfectly fine, or being able to jump off the 4th story and barely taking any damage".

- `spotters` (tests/test_spotters.cpp), out of `sight`: every crime of his in a cop's sight called
  `sight::Spotted`, and each one started that cop's red eye over again (a firefight: crimes all the time).
  Now a crime spots only while the police weren't on him (at the last look), and a spotter on foot
  shooting at him (`PedState` attack) ends it after its 2.5 s.
- `falldamage` (tests/test_falldamage.cpp): measured in the game, its gravity is 0.008 a frame (20 m/s/s),
  a ped's drag 0.4 / 70 (`ApplyAirResistance`), and `CPed::ProcessEntityCollision` deals
  150 (v - 0.25) a unit a frame down, 100 along, halved on soft ground (`CSurfaceTable::IsSoftLanding`):
  under 12.5 m/s nothing, 28 off 12 m, 64 off 30 m. And `CPed::InflictDamage` takes it from armour first
  (SWAT have 50, the FBI and army 100). Now nothing under 12.5 m/s still, 100 at 23 m/s (13.5 m), rising
  as (v - 12.5)^1.3, dead from 24 m/s (15 m) whatever the health, soft ground as from 70% the height, and
  never less than the game's.
- `falls`: in `CPed::InflictDamage`'s hook (player.cpp), a `WEAPONTYPE_FALL` hit's damage from the ped's
  speed as he lands (still the fall's: the game zeroes it after), his armour put aside for it. The game's
  own bail-out rule stays (rolling out of a car, `CAR_rollout`, a fall leaves 5 health at least).

### Task 6: After playing it again

The player: "the offhand grip on the rifle looks all messed up: I can see in the fingers and when sprinting
it's clear the fingers aren't properly wrapping around the barrel handguard"; "the seating position in a
lot of cars is way too low, I end up staring at the dash/hood".

- `vmfit` (tests/test_vmfit.cpp `TestLeftFingersWrapTheHandguard`): the game's fingers rest as a hook (80 mm
  knuckle to tip on player.dff); every two-handed gun's left anchors were nearer his knuckles than that, and
  the two-joint reach folded each middle joint shut (the M4's ring finger from 126 to 18 degrees), its mesh
  crushing in on itself. His left digits now close whole about the hand's hinge, from open, till they meet
  the gun (the thumb towards its anchor), and fall back to the anchor only when that fails. On all twelve
  two-handed guns and twelve outfits every finger keeps its shape within 15 degrees, none inside the gun.
  The right hand's folds are left: the Colt the player approved has them, and a fist round a grip folds.
- `seatview` (tests/test_seatview.cpp) and `seateye`: from where the game sits his head (SetPedPositionInCar
  on ped_frontseat; CAR_sit's head 0.53 m over it, his eye 0.05 m over that), the view out straight ahead
  (12 degrees either way) is cut through the car's opaque body (chassis, bonnet, wings, front bumper, front
  doors; no glass) as the pitches each triangle covers; his eye goes up a centimetre at a time till the
  hood is 7 degrees under level, only where he still sees level under the roof's edge (a convertible's
  frame: the first version raised the Stinger over it), at most 0.25 m and 0.06 m short of the roof. On the
  87 car models from his estimated eye: 78 raised (most 0.05-0.2 m), 19 short of 7 degrees but well under
  level (the Cuban's hood from 3 degrees over his eyes to 3 under). Once a model, his eye as he sits
  (0.5 s in the seat), eased in; logged.
