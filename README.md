# Modernized Grand Theft Auto: Vice City

Vice City on the Mac, playing like a Rockstar game from the last decade.

This mod is for the native Apple Silicon port of Vice City, which is built from reVC. It brings in what
GTA IV, GTA V and Red Dead Redemption 2 made standard:

- a tilted GPS minimap and a modern HUD;
- a GTA V controller layout, with over-the-shoulder aiming, smooth sticks and free-aim drive-bys;
- a true first-person view, on foot and from the driver's seat;
- swimming, climbing and walking crouched;
- gunfights from cover, with bullet time and the shootdodge;
- police who know only what they've seen, RDR2-style: witnesses, your description, a search, an APB;
- volumetric clouds, and traffic that doesn't pop in.

Vice City's story, missions and world stay as they are. Every feature can be switched off.

![Driving along the beach at night, following a GPS route on the minimap](docs/driving.jpg)

## What it changes

### Map and HUD

![The pause menu's map, with its tabs, the legend and the prices of businesses for sale](docs/pause-map.jpg)

- A rectangular minimap, tilted in 3D like GTA VI's. It's drawn from the game's own buildings and roads.
- GPS routes along the road network to your waypoint or the next mission objective. The waypoint clears
  when you arrive. There's no road route while you fly.
- Health, armour and stamina show as bars under the map, and your breath does when you dive. The clock
  sits on the map.
- Money appears only when it changes, or where you can spend it. The weapon is smaller, in the top-right
  corner, with the wanted stars above it.
- When you change station, its logo shows at the bottom of the screen instead of the big coloured name.
- A GTA IV-style pause menu opens on the map, with the other pages in tabs. The map has a compact legend,
  marks hospitals, police stations and businesses you can buy, with their prices, and shows the GPS route
  as soon as you set a waypoint.
- On the pause map, a tooltip names the icon under the cursor, and the controller's cursor snaps onto icons
  as it nears them. A/Cross, or a right click, makes that place your destination: the GPS routes there and
  its icon gets a purple border, on both maps, instead of the waypoint's square. The legend calls the
  waypoint "Destination".
- HUD shapes (the stars, the wanted icons, the eyes) have smooth edges, though the game draws its 2D
  without anti-aliasing.
- Options → Gameplay changes the crosshair style, size, thickness, color, center dot and dynamic
  spread, with a live preview. The same settings apply on foot and in drive-bys in either view.
- Street racing and the supported RC races put an arrow inside the current checkpoint toward the
  next one. Finish markers and races with no defined next checkpoint have no arrow.

### Controls and camera

<p>
  <img src="docs/aiming.jpg" width="49%" alt="Aiming the M4 over the shoulder in a storm">
  <img src="docs/weapon-wheel.jpg" width="49%" alt="The weapon wheel open in the rain">
</p>

- A GTA V controller layout ([below](#controls)). It's read straight from the controller, so it works
  whatever the game's own button mapping is.
- The game's hints and the mod's prompts name the button that does it now ("Press RT to fire", "D-pad up
  to answer"), not the PS2 layout's. They follow the controller you have (PlayStation, Xbox or Switch
  icons) and switch to your keys as soon as you touch the keyboard, a hint already on screen too. A few
  hints whose controls changed, like the drive-by tutorial, are reworded.
- The mod's own controls have keyboard keys too ([below](#keyboard-and-mouse)), and a few actions the new
  layout had left without a button got one: the RC helicopters' bombs, the Predator's cannons, arming a
  car bomb, an armed vehicle's handbrake and a plane's rudder.
- Smooth sticks. The game read each stick only past a third of the way out, then jumped, and each axis on
  its own, so walking snapped to straight lines. Now both sticks have one small round dead zone and
  answer in any direction: a gentle push on the left stick walks, and the right stick turns the camera
  smoothly, with its own sensitivity while aiming.
- Over-the-shoulder aiming on LT, and you can walk while aiming. The M4, Ruger and M60 use it too (the
  game used to aim them in first person). Two-handed guns stay at the shoulder while you walk, pointing
  where the crosshair is.
- Weapon and radio wheels, with time slowed while they're open.
- Free-aim drive-bys: hold LB in a vehicle with an SMG or a pistol, aim anywhere with the right stick, and
  fire with RB.
- The triggers work as analog pedals in cars, and as throttle and climb in helicopters and planes.
- The controller rumbles for crashes, explosions and punches.
- GTA IV-style camera shake: handheld on foot, a gentle sway and jolts in vehicles.

### First person

- The camera button (Select/Back) goes near, mid, far, then into Tommy's eyes. The eye is fixed to his
  body, with Counter-Strike's bob if `HeadBob1stPerson` is on in `macVC.ini`. His steps sway the view
  gently, barely walking and most sprinting, and strafing tips it a little that way (it's the handheld
  camera: `CameraShake` sets how much). His head and neck are hidden so they never clip into the
  camera; looking down, the eye tips forward past his chest, so you see his body and legs. Aiming only
  zooms: the view stays where it points.
- The stick moves him the moment you push it, with Counter-Strike 1.6's acceleration and friction, instead
  of his walk and run animations. He strafes and backpedals without turning, gets up to speed in about
  0.4 s and stops in about 0.5 s, and keeps Vice City's own walking, running and sprinting speeds.
- The right stick turns like a modern console shooter's: fine at the centre, fast at full push, and faster
  still held at the rim. Aiming slows it as much as the view zooms in. Over an enemy it slows a little, so
  you don't overshoot him, but it never pulls your aim.
- Every gun is a proper viewmodel, as Counter-Strike 1.6 draws its guns: Tommy's own arms, from whichever
  outfit he's wearing, their fingers cut apart and jointed so they grip it (his left fingers close round
  a handguard as they rest, till they meet it), holding a new low-poly,
  photo-textured model of Vice City's gun (pistols and SMGs one-handed, the rest two-handed, as the game
  holds them). It's drawn after the world, so it never goes into a wall, through its own field of view,
  and lit as he is. It comes up when you switch to it and kicks as it fires, each kind of gun its own way:
  a pistol flips up and drops back past where it was, the Python higher and heavier, a shotgun shoves
  straight back into him, automatics jolt each shot (the first hardest) and climb and back up as you hold
  the trigger, the minigun buzzes. His forearm goes with the kick and his left elbow takes it; aimed, it
  kicks mostly straight back so the sights stay on. The slide or bolt cycles (held back for a visible
  frame), the pump is racked after the kick with the whole gun rolling in, the Python's cylinder turns and
  the minigun's barrels spin, and the flash at the muzzle (turned and sized afresh each shot) lights his
  hands and the gun warm for an instant; a pistol's slide locks back when it's empty. Reloading lifts the gun and turns it in, and his
  left hand pulls the magazine out, takes it away and brings the new one up and in (on a two-handed gun it
  leaves the handguard for it). The gun bobs with his steps, more as he runs, and not at all aimed;
  sprinting, he carries it as a runner does: a pistol or SMG up by his ear, muzzle up; a long gun up across his
  chest; a heavy one at his hip, all moving in step with his legs. Every barrel points where you aim.
- The sniper rifles have a modern scope: LT raises the rifle to your eye, then you're looking through the
  game's own scope art, sharp, filling the screen. RB steps between 3× and 8×. It sways gently; L3 holds
  your breath to steady it for a few seconds, then he gasps. A shot drops the scope while the bolt cycles,
  then it comes back. You can creep while scoped, less accurately. The rocket launcher fires from the
  game's own sight.
- With the new viewmodel, guns fire as Counter-Strike 1.6's do: the shot leaves the frame you pull RT (no
  waiting for Tommy's attack animation), at the gun's own rate (pistols and shotguns once a pull). Each shot
  kicks the view up, automatics more with each shot of a burst and a little sideways, settling back to
  where you aimed, with a short jolt through him. The first shot standing or walking goes exactly where the crosshair is; holding the
  trigger, running or jumping spreads them, crouching and aiming tighten them. The crosshair is
  Counter-Strike's four lines, crisp on whole pixels, opening and closing with the spread, through a
  Max Payne dive too. Aiming in third person shows the same lines, and a drive-by's ring is as thick.
- Gunshots hit harder: the game's own gunshot sounds are cleaned up and given low end (every shooter's),
  and your own gun has a close, centred body under its shot, with a tail that sounds like a room indoors
  and the city outside. The M4 no longer plays the Ruger's shot sped up. The game's sound is plain stereo
  (no headphone 3D processing, which coloured close shots).
- Melee weapons are held up ready where you can see them, in the same arms: the game's own model of each
  in his right hand, a baton, hammer or blade up on the right leaning in, a knife point-in, a bat cocked over
  his shoulder, brass knuckles in a raised fist, the chainsaw low at his hip with its bar running in. Each
  swing follows the game's own attack, so it lands when the hit does: drawn back, quickest through the
  hit, followed through and back to ready; a knife's combo comes back the other way and then over the
  top, a club's second blow comes down on someone on the ground, the knuckles jab, hook and uppercut. The
  chainsaw shakes, harder as it cuts. Fists and things you throw are held as before (his own arms, low on
  the right, with the game's models).
- Look far enough round and he turns to face that way.
- In a vehicle you sit in the driver's seat, looking out through the car with his hands on the wheel:
  - In a car whose hood or dash comes up to your eyes where the game sits you (most of them), your eye
    is raised until the hood is 7 degrees under level, as in a real car. It stops short of the roof over
    you and of the roof's front edge coming down over the road.
  - The right stick looks around the cabin, and the view recentres when you let go. R3 looks back.
  - Getting in and out stays in first person.
  - In a car the view leans with the body as it rolls and dives, and Tommy's head comes back a little
    towards level against it (`HeadBalance`).
  - On a motorcycle the view leans with you.
  - Drive-bys aim from the seat, through first person's crosshair: Counter-Strike's four lines, opening
    as you fire and when the car's moving fast, as the shots spread.
  - Your gun lies on the console between the front seats while you drive, so your hand on the wheel
    holds nothing in front of the view (in a car with nothing between the seats, it's on your own seat
    by your thigh). Aim and Tommy's right hand goes down to it and brings it up; let go and he puts it
    back and his hand goes back to the wheel. On a bike or a boat it's put away until you aim. In third
    person it's the same.
  - Flying, the pilot's hands are empty: you can't shoot from the controls.
- Cars' steering wheels are round and turn as you steer, with Tommy's hands on them. Sounding the horn
  (clicking the left stick), he presses its middle; when the blinkers come on, he flicks the stalk behind
  it.

### On foot

![Tommy swimming under the surface of the bay](docs/swimming.jpg)

- Tommy swims instead of drowning: front crawl, a sprint, and treading water. Jump dives, with a breath
  meter. A sinking car gives you time to get out.
- Jump also climbs. He vaults low walls, steps up and pulls up onto ledges (even mid-jump), and climbs out
  of the water onto docks, piers and boats.
- Tommy walks crouched. Crouch, then push the stick: he creeps along low with his gun ready, and strafes
  while aiming. Sprinting or jumping stands him up.
- Falls hurt as they would. A storey's drop is nothing and two storeys take a fifth of your health. A
  fourth-floor window leaves you barely alive, and nobody lives through five storeys, cops and SWAT
  included. Armour doesn't help, and soft ground (sand, grass) helps a little. The game only took a third
  of your health for four storeys, and armour took that first.
- RDR2-style responses: greet or insult people up close, and pick up or wave off hookers.
- Civilian drivers who see you aiming at them brake, flee or surrender according to their temperament
  and speed. Mission drivers keep their scripted behavior.
- An experimental physical reaction layer for ordinary on-foot NPCs: local hit responses, balance,
  arm bracing, articulated falls and recovery. Cops and civilians share measured body volumes for
  anatomy, full-limb world contact and separation between falling bodies; supported corpses sleep
  and wake when support changes. Native damage and protected actors retain their existing owners.
  Final in-game visual and performance checks are still pending; see the verification record below.

### Gunfights

- Bullet time, as in Max Payne. While aiming, click the right stick to slow everything but your aim. It
  uses up stamina and ends when you click again, let go of LT or run out.
- The shootdodge. While aiming, jump with the stick pushed and Tommy dives that way through the air,
  still shooting. He lands, then gets up off his back or front with the game's own animations, turning to
  your aim as he comes up. The dive is in bullet time if you have the stamina.
- Gunmen fight from cover. They shoot from range instead of running up to you, then make for the nearest
  solid cover, like a wall, a car or a crate. They crouch behind it and stand or lean out to shoot.
- They keep their heads down while you're looking at them, and come up to shoot when you look away.
  Shoot at them, or hit someone next to them, and they run for cover or stay down in it.
- From further off they close in cover to cover. Only a few shoot at once, from cover; the rest stay
  down or move up while those cover them, and take a turn as soon as one's free. So they spread out
  around you instead of standing in the open waiting to be shot.
- They use cars as cover: a cop out of his car gets behind it. They go round a car or a corner to reach
  cover behind it, and duck down to reload.
- Shoot the car they're behind and they leave it once it's smoking. If it catches fire, anyone near it
  runs clear of the blast before it blows, to other cover if there is some.
- Coming from out of sight, they wait round the corner for the others and come at you together.
- Their aim is off when they first see you and tightens the longer they have you in sight, so standing
  about in the open gets you hit.
- A gunman who can't see you comes round to where he can, following the walkways round walls, instead of
  shooting at the wall. No one shoots you through a wall any more.

### Police

- The police are a force spread over the city: patrol cars on the roads everywhere, more where there's
  more traffic, and a few waiting at each station. When you're wanted, the nearest are sent and drive by
  road to where they think you are, so how soon the first one arrives depends on where you are. They come
  in out of your sight: through the gate of a walled lot, never inside it, and on a nearby road you can't
  see if they can't arrive where they are.
- Foot patrols also have positions and routes across the city. Stations replenish lost foot and car
  units through shared queues; reinforcements travel from their station instead of all reappearing at once.
- More are sent the longer it goes on, and more at once for more stars. Destroy a car and kill its cops,
  and the next nearest is sent from further off.
- From two stars they set up roadblocks on the roads out of a ring round where they think you are. As time
  goes on there are more of them, and heavier: cars across part of the road, then all of it, then a cop
  with a spike strip in front, then a second line. Break out and they set up again.
- Once you've fired a gun, or while you aim one, cops draw their pistols instead of running up with the
  nightstick.
- The game lets only ten cops chase you at once. Now every cop near you on foot joins the fight from two
  stars. Roadblock cops leave a car of theirs that's caught fire.
- Cars coming for you on foot pull up short, side-on, round a corner if there is one, and the cops get
  out behind them. Wanted on foot, police arrive in cars, instead of appearing on foot wherever you can't
  see, even inside walls with you.
- Cops can only pull you out of a car that's barely moving. The game let them open your door up to
  36 km/h and then held them to it however fast you went. Now, as in GTA IV, one who reaches your door
  while you're moving catches hold of the handle and hangs on, dragged along tugging at it. The faster
  you go and the harder you turn, the sooner he lets go and falls; slow to a crawl and he gets it open.
  Pull away while one's opening it and he's left hanging on. Too fast and he can't catch hold at all.

### Wanted: the police know only what they've seen

The game's police always knew exactly where Tommy was. Now a crime has to be seen and reported, the police
know you by your description, and getting away leaves you under an APB until you save.

**How a crime reaches them**

- A cop who sees a crime of yours files it there and then, from as far as he can see it, with your
  description.
- Cops hear things too: gunfire within about 150 m, an explosion within 250 m, screams within 50 m. They
  search round where it seemed to come from, and nothing stops that call.
- A crime no cop sees has to be called in. People who saw it, if they're the sort to (the game's own
  temperaments), run off to call it, marked on the minimap as they go. One who gets away calls within
  about 20 seconds, and so does one who runs for one of the city's few payphones. Rich people downtown,
  on Starfish Island and in Vice Point often have a brick phone and call from where they stand. Only
  killing or knocking a witness down stops the call.
- In a built-up spot, someone at a window may call it in too, just the place, and that can't be stopped.
- A car alarm you set off gets called in by someone nearby, just the place.
- A call counts what the crime is worth, by the game's own rules, and gives the police the place, and your
  description if they saw you. Gun violence is always at least two stars. Something worth no stars on its
  own (taking a parked scooter, say) isn't called in at all.

**What they know**

- Your description is your outfit and the vehicle you were last seen in, by model and colour. It changes
  only when a cop or a police helicopter sees you, so changing clothes or cars in front of them doesn't
  help.
- Out of their sight they work from where you were last seen: units are sent there, the roadblocks go up
  round it, and the cars that were chasing you drive about the area, road to road, looking for you. Cops
  on foot fan out round it too, and a police helicopter circles it.
- A cop who's lost you has to recognise you again. An eye over him fills while he looks at you: fast from
  far off if you match the description, only up close if you don't. Off towards the edge of the screen,
  or behind you, the eye sits on a ring round the middle pointing his way, like a damage indicator. While
  an eye fills you hear it, a low swell rising as it goes, Hitman-style, and a sting when it's full: they
  have you again.
- While the police don't know where you are, no cops appear next to you out of nowhere (the game's spike
  strip cops at three stars included).

**Getting away**

- Once nobody's seen you for a while, the stars dim and the police search the area. Stay out of it till
  the search runs out (longer for more stars) and the stars go hollow: an APB, if they had your
  description or it was two stars or more. Otherwise you're clear.
- Under the APB nobody's after you, but a cop who recognises you brings the stars back where they were.
  A witness who saw someone in other clothes and another vehicle saw a stranger. Saving at a safehouse
  clears the APB, and so does being wasted or busted. Missions that set or clear your stars still do.
- Pay 'n' Spray and changing clothes no longer wipe the stars. A respray changes the car they're looking
  for and new clothes the outfit, so both help only if no cop sees you do it.
- Police bribes are gone from the map.

**On screen**

- The stars look like GTA IV and V's: white while they're on you, pulsing grey while they search, hollow
  under the APB.
- Round icons right of the minimap show what they know: a phone with a ring while a call's going through,
  a magnifier while they search, a badge under the APB, and your outfit and vehicle while they're on your
  description, struck through once you've changed them. The vehicle greys while you're out of it and
  comes back when you get in again; once it's destroyed or gone from the streets it's off the
  description and the icon goes.
- When a cop spots you (his eye full, or he sees a crime of yours while the police aren't after you), his
  eye turns red. It stays over him for a few seconds at least, as he comes for you, and fades once he
  reaches you, loses you for a moment or starts shooting, or after a few seconds more. Once they're after
  you, nobody else turns red: what they see then is the chase.
- In a police car you see what they see: the area they're searching, and the other police on the minimap
  (cars, cops on foot, helicopters, units out in the city). But once they're after you, they always know
  where their car is.

**From the air**

- The police helicopter no longer fires from its belly, and no SWAT rope down from it. Two SWAT officers
  kneel in its open doors with rifles, each shooting out of their own side, and only once the police know
  it's you. Shoot one and he falls out; with both down it keeps following you, telling the police where
  you are. The next helicopter has a fresh pair.
- Police helicopters fly some 35 m up instead of skimming the rooftops. Bullets hit them where you see
  them: the cabin, the tail boom, or a gunner through his open door. They take hits where they land,
  smoke and then burn as they're damaged, and go down after about a quarter as many hits as before.
- Its searchlight is a lamp under its chin, turning to follow its spot on the ground, its lens lit at night
  with a faint beam down to the spot. (The game's light came from a glow 7 m out in front of it, and its
  red beacon floated 9 m behind it; that's on its tail fin now.) One hit on the can or lens shoots the lamp out: its glass
  breaks and it stays dark, and at night it can't see as far.
- A pilot sits at the controls behind the cockpit's glass. Shoot him through it and the helicopter goes
  down, as you can shoot a driver. Six hits on the tail rotor shoot it away and it spins down. Hits on the
  mast, where the main rotor meets the roof, do three times the damage.
- From four stars a second helicopter comes. At six stars the army's Hunter joins in: it hovers off to one
  side with an Army soldier at the controls, turns to face you, and fires its cannon close in and its rockets from further off. It takes about
  three times the hits of a car to bring down, and leaves when the stars drop.

**Flying**

- Full-size playable helicopters use collective, cyclic and pedal forces with momentum and damping.
  Their climb tapers above 3,200 m, beyond the cloud layer. Rotor strikes and targeted shots can break
  the visible main or tail rotor, affecting control; repair restores the damaged parts.
- The Skimmer has lift, angle-of-attack stalls, thrust, drag and aerodynamic steering while retaining
  its native water handling. A flyable Dodo is parked on the airport apron; approach it and use the
  normal vehicle-entry control. Entry and exit require a stopped aircraft with clear ground access.
  Persistent spawn points replace all seven airport scenery Learjets with usable aircraft as you
  approach. Returning after distant aircraft unloads creates usable replacements; obstructed spots
  wait until clear. Board near the left side of the cockpit, ahead of the wing. Tommy reaches for the
  door, steps inside, crouch-walks through the cabin and sits before taking control. The treads are
  integrated into the hinged door's inner face.
  The lined cabin has two cockpit seats, six rear seats and a level floor, with Police Maverick
  instruments, clear windows, rolling suspension, animated wing and tail controls, and engine sounds
  from the airport Airtrain. The cockpit uses the Police Maverick's panel and radio artwork. Gear
  retracts after takeoff and extends on approach, retaining the original attached covers. Flush seals
  with the original paint close the gaps as the gear finishes folding away. The configured horn
  control (normally L3 or Shift) toggles it manually. Small
  navigation and beacon lights sit on modeled lenses. Their original exterior textures are retained. The `Planes`
  setting controls the additional aircraft and plane-flight changes.
- Clean Learjet cruise reaches approximately112m/s in sustained flight simulations, above the
  Infernus/Cheetah handling maximum of83m/s; runway acceleration keeps its existing spool curve.
  Twin engine exhaust follows the real outlets. Strong thrust pushes exposed people and cars
  behind the jet, with distance, obstruction and vehicle mass affecting the response.
- The left stick pitches and rolls; LB/RB operate the rudder without turning the camera sideways.
  The right stick remains the camera. Configured keyboard flight bindings remain available.
- Free-roam Skimmer and helicopter bodies use native collision damage instead of their stock zero
  damage coefficient. Existing nonzero handling and native damage-proof states are preserved.
- Airborne wheel brakes add no extra drag: normal lift, drag and engine power determine the glide.
  Above the city, terrain streaming and the ocean/horizon adapt to flight altitude with bounded LOD requests.
  Nearby original tower models and rooftop details stay loaded along the flight path. Player-controlled
  fixed-wing bodies also receive the missing upper-surface collisions on the four affected towers.
- Native takeoff, landing, water behavior and handling feel are still under playtest. Current
  implementation and acceptance evidence are tracked in
  [the aircraft follow-up plan](docs/superpowers/plans/2026-10-31-aircraft-flight-followup.md).

### The world and the story

- Cars and people appear far enough out that you don't see them pop in, and fade in when they do.
  There are more of them, so the streets near you aren't emptier.
- Traffic isn't the same car over and over. More models stay loaded, and the rarest one on the road is
  picked first.
- The airliners from Escobar International climb above the clouds over the middle of their circuit,
  instead of levelling off far under them (they flew at 345-450 m; the clouds start at 900 m), and come
  down to land as before. They're lit by the sky they fly in: the sun on them, less in a cloud's shadow,
  dark at night (the game lit them like cars on the street, and the far one plain grey). In or behind
  cloud they fade into it. Their lights show: red beacons pulsing on top and underneath, white strobes
  and red and green navigation lights on the wingtips, white on the tail, bright at night and faint by
  day. (The game's only showed within 120 m.)
- Story phone calls ring anywhere, even while driving. A 1980s brick phone slides up with the caller's
  name, D-pad up answers, and Tommy keeps moving.
- Cars have GTA IV's weight. Trucks and buses weigh what such vehicles weigh, so a car that hits one
  gives way. Cars accelerate more gently and top out a little lower, turn in heavier, slide gradually
  instead of snapping, and take longer to stop. Softer springs roll the body in corners, dive it under
  braking and squat it under power, at the same ride height. Each car keeps its place among the others
  (`CarHandling`).
- Side and rear car windows shatter from bullets and severe native door or rear-bumper damage
  (`CarGlass`), with the game's falling glass and sound. Damage stays on the car that was hit, and
  repairs restore its glass. The front windscreen keeps the game's cracked and shattered stages.
- Hoods, trunks and doors retain their damaged and swinging stages before a stronger later hit tears
  them off (`CarParts`). Bumpers retain their damaged model before detaching. Continuous contact with
  a wall counts as one impact for each part. The game's own loose-part physics and repairs handle it.
- Body metal dents near the collision (`CarDeformation`), with repeated hits deepening it up to a cap.
  Dents stay on that car, preserve broken glass and restore on repair. Glass and wheels remain rigid.
- Car radios can be heard from the street, muffled through the bodywork and coming from where the car is.
  Your own car keeps playing after you get out, and so do stopped cars nearby. Getting into a car, you
  hear only that car's radio, and a police car plays the police scanner, not the station you just heard.
- Once the story waits on your businesses, Ken texts you what it needs, and those businesses get gold
  rings on the map. You can buy them on credit: a quarter down, and the rest is paid from their income.

### Graphics

- Volumetric clouds like a flight simulator's: billowing cumulus with rounded undersides and domed tops,
  over the curve of the earth, lit by the sun or moon in the colours of the time of day. There are more
  clouds, and greyer ones, in bad weather. The sun's glare dims behind thick cloud.
- A modern camera look: bloom, shafts of sunlight, softer highlights, a touch more contrast and a
  vignette. Vice City's lighting is baked into its models, so this is the look of the picture, not new
  lights.
- Cars' lights glow in the model itself, where they're painted on: tail lights brighten red when
  braking (and while a car waits with someone at the wheel), reversing lights go white, and headlights
  light up at night. A smashed light stays dark.
- With a GPS route, your car's blinkers come on as the next turn comes up, with a green arrow on the
  dash in the driver's seat.
- Together they cost about 4 ms a frame at 3440×1440 on an M4. Each frame, the clouds work out a
  quarter of their pixels at half resolution and carry the rest over from the last frame.

### Fixes

- reVC's own menu options show their names instead of "FED_VPL missing", including the Graphics Setup
  page, and the police car's name shows when you get in (its text key was in the wrong case).
- The game no longer crashes when someone gets off a bus whose animations had been unloaded.
- A line of sight with an end off the edge of the world's grid (the airliners fly past its south edge)
  no longer reads and writes past the game's sectors, which could crash it.
- Brake, reverse and headlights no longer draw see-through boxes sticking out of the bodywork (a GTA III
  leftover).
- If the car radio gets stuck looping one moment of a song (this can happen after the pause menu), it
  now restarts on its own after a few seconds.
- You can get out of a car that's in the water, even one floating on its roof, and there are no footstep
  sounds while swimming.

## Controls

|                       | On foot                                                     | In a vehicle                                   |
| --------------------- | ----------------------------------------------------------- | ---------------------------------------------- |
| RT / LT               | Fire / aim                                                  | Accelerate / brake and reverse                 |
| LB                    | Hold for the weapon wheel                                   | Hold to aim a drive-by, RB fires               |
| D-pad left / right    | Switch weapon                                               | Change station; hold for the radio wheel       |
| D-pad down            | Zoom the map out, show your money                           | Zoom the map out, show your money              |
| D-pad up              | Answer the phone                                            | Answer the phone, start the side mission       |
| B / Circle            | Melee, reload                                               | Hold for the cinematic camera                  |
| A / Cross             | Sprint                                                      | The vehicle's gun (a car bomb, a bike's guns); an armed vehicle's handbrake, its RB fires |
| X, B / Square, Circle | Answer a prompt (greet, insult; pick up, wave off)          |                                                |
| Jump                  | Jump, vault, climb; dive in the water. Aiming, with the stick pushed: shootdodge |           |
| L3                    | Crouch (push the stick to walk crouched)                    | Horn                                           |
| R3                    | Aiming: bullet time                                         | Look behind                                    |
| Select / Back         | Camera distance, then first person                          | Camera distance, including the driver's seat   |

Flying a helicopter: RT climbs and LT descends, as far as they're pulled. In a plane, RT controls
throttle and LT brakes the wheels on the ground; releasing power leaves the plane gliding. Learjet gear
operates automatically, with L3 (the configured horn control) available for a manual toggle.
LB and RB turn the tail (a helicopter's, a plane's rudder), and the left stick pitches and rolls. A fires
the gun and drops the bombs (the Hunter's cannon, the Sea Sparrow's gun, the RC aircraft's bombs and
detonation), X fires the Hunter's rockets, and B holds the cinematic camera.

In the pause menu, LB/RB (or Q/E) switch tabs and B/Circle goes back. On the map, the left stick moves
the cursor (gently for small pushes; the D-pad nudges it), the right stick moves the map, LT/RT zoom in and
out about the cursor, and A/Cross sets or clears a waypoint, or on an icon makes it the destination.

### The Controls page

Options > Controls replaces the game's Controller Setup pages with one page and two tabs:

- **Controller:** a dot by the pad's name says how it stands: green when the game reads it, amber when
  something needs fixing (the reason is beside it, and when the game reads another pad, Y / USE THIS ONE
  switches to this one), and an empty ring when there's none. Below it, a picture of your pad (PlayStation,
  Xbox or Nintendo) with what each button does on foot, driving, flying or on the pause map; press a button
  and its line lights up. Vibration, inverted look and look sensitivity sit under it.
- **Keyboard & Mouse:** every action's keys, the game's and the mod's, on foot and driving. Pick a key, press
  the new one (Esc cancels); a key another action had is taken from it. The mouse's sensitivity, inverted
  look and mouse steering are at the end.

Everything saves as you change it (`macVC.ini`, and the settings file for the mod's keys and look
sensitivity), and the hints on screen name the new keys at once. Head bob in first person isn't a control:
it stays in `macVC.ini` (`HeadBob1stPerson`) until the game's own features get an Options section.

The Mac window's fullscreen button also remembers its last choice across launches. This saves as
`[VideoMode] NativeFullscreen` in `macVC.ini`, separately from the graphics menu's `Windowed` video mode.

### Keyboard and mouse

The game's own keys are as you set them on the Controls page. The mod's own actions get these, unless a game action
already has the key (then that one has none until you set it on the Controls page, or in the settings file's `[Keys]`):

| Key | Does |
| --- | --- |
| Q (hold) | On foot, the weapon wheel; the mouse picks |
| G (hold) | In a vehicle, the radio wheel |
| R | Reload |
| Y / N | Answer a prompt: greet or insult, pick up or wave off |
| B | Aiming: bullet time |
| X (hold) | In a vehicle, aim a drive-by with the mouse; the vehicle fire key shoots |
| C (hold) | In a vehicle, the cinematic camera |
| M | The expanded minimap |

The phone is answered with the game's own action key (Tab). Reload and the cinematic camera come with the
GTA V layout (`ModernTriggers` and `ModernButtons` on), as B does on the controller.

## Install

You need:
- an Apple Silicon Mac;
- the macOS port of Vice City, version 1.0.5217, set up with your own copy of the game;
- the Xcode Command Line Tools (`xcode-select --install`).

```sh
git clone https://github.com/twalichiewicz/revc-modernized.git
cd revc-modernized
./install.sh    # quit the game first; add the app's path if it isn't in /Applications
```

The installer builds the mod, keeps a copy of the original game and patches it. It only patches the
exact 1.0.5217 build. Run it again whenever the port updates itself. If macOS blocks it, allow your
terminal under System Settings → Privacy & Security → App Management.

`./uninstall.sh` puts the original game back. Your saves are never touched.

## Settings

Settings live in `~/Library/Application Support/macVC/vcmodernhud.ini`, and every feature has a switch
there (1 on, 0 off). The file explains each setting. It's written the first time the game runs, and isn't
updated after that. To pick up settings added by a newer version, delete it and a fresh one is written.

| Setting | What it does |
| --- | --- |
| **Map and HUD** | |
| `Enabled` | The minimap (0 gives the original round radar) |
| `GPS`, `AutoClearWaypoint` | The route to your waypoint or objective; the waypoint clears when you arrive |
| `Buildings`, `BuildingHeight` | 3D buildings on the minimap, and how tall (1 is their true height) |
| `Bars`, `Hud`, `MoneyPill` | The bars under the map; the clock on it and the smaller weapon and stars; money where you can spend it |
| `PauseTabs`, `CompactLegend`, `CircleBack` | The pause menu's tabs and legend; B/Circle goes back (0: Y/Triangle, as on the PS2) |
| `MapIcons` | On the pause map: tooltips, the cursor snapping to icons, and picking one as the destination |
| `Places`, `RadioLogo` | Map icons for hospitals, police stations and businesses; the station logo |
| `RaceArrows` | Direction arrows in supported racing checkpoints |
| `[Crosshair] Style`, `Color`, `Size`, `Thickness`, `CenterDot`, `DynamicSpread` | The crosshair options, also editable in Options → Gameplay |
| `Tilt`, `Zoom`, `Size`, `Aspect`, `Margin` | The minimap's shape and place |
| **Controls and camera** | |
| `ModernTriggers`, `ModernButtons`, `DpadActions`, `Wheels` | The GTA V layout, the D-pad's actions and the wheels |
| `AimCamera`, `DriveByAim` | Over-the-shoulder aiming; free-aim drive-bys |
| `LookSensitivity`, `AimSensitivity` | Right stick speed, and while aiming |
| `FirstPerson`, `SteeringWheel`, `ConsoleGun`, `SeatView` | First person in the camera cycle; round, turning steering wheels; the gun on the console while you drive; your eye raised over the hood |
| `FirstPersonLook`, `FirstPersonLookYaw`, `FirstPersonLookPitch`, `AimSlowdown` | First person: the right stick's handling and its top speeds (degrees a second); aim slowdown over enemies |
| `CameraShake` | 0 is none, 1 is the default, 2 is double |
| `Rumble` | Controller vibration |
| `MOD_WEAPON_WHEEL` … in `[Keys]` | The keyboard keys of the mod's own actions, written as macVC.ini's `[Bindings]` (`kbd:Q,2ndKbd:F1`, `mouse:MIDDLE`) |
| **On foot and gunfights** | |
| `Swimming`, `Climbing`, `CrouchWalk` | Swimming, climbing and walking crouched |
| `FallDamage` | Falls hurt as they would, armour or not |
| `FirstPersonMovement`, `FirstPersonAccel`, `FirstPersonFriction` | First person: the stick moves him at once; how fast he gets up to speed and stops (Counter-Strike's 5 and 4) |
| `FirstPersonGuns`, `ViewmodelFov` | First person: his own arms holding the new guns and melee weapons, drawn as a viewmodel; its field of view (degrees, top to bottom) |
| `Interactions` | Greeting and insulting people, hookers |
| `DriverReactions`, `NPCReactions` | Pedestrian/driver gun-threat responses; the experimental physical NPC reaction layer |
| `SmarterEnemies` | Gunmen fighting from cover |
| `BulletTime` | Bullet time and the shootdodge |
| **Police and wanted** | |
| `SmarterPolice`, `Roadblocks`, `CopsByCar` | Police sent by road from across the city; the roadblocks; arriving by car |
| `CopsGrabDoors` | Your door opens only at a crawl; faster, cops hang on to it and let go |
| `RealisticWanted` | Witnesses, hearing, your description, the search and the APB |
| `HeliDamage`, `HeliGunners` | Shooting the police helicopter properly (its searchlight, pilot, tail rotor and rotor mast too); its SWAT gunners, the second helicopter and the Hunter |
| **The world** | |
| `Phone`, `CarRadios`, `StoryHints`, `Credit` | Calls anywhere; car radios from outside; Ken's texts; buying businesses on credit |
| `SpawnDistance`, `CarVariety` | How much further out cars and people appear (1 is as the game had it); more car models |
| `WorldPopulation` | Path/zone-based civilians and foot patrols, with shared station replenishment |
| `Clouds`, `ModernLighting`, `LightGlow` | The new sky, the camera look, and cars' lights glowing on the model |
| `Planes` | The airliners up in the clouds, lit by the sky, with their lights |
| `CarGlass` | Side and rear windows shatter from bullets and severe component damage |
| `CarParts` | Damaged and swinging stages before native parts detach on stronger later hits |
| `CarDeformation` | Localized persistent body dents from collisions, restored on repair |
| `CarHandling` | GTA IV-style weight: mass by type, gentler acceleration, softer springs that lean, longer braking |
| `HeadBalance` | First person in a car: the view leans with the car, his head levelling a little against it |

If something looks wrong, the log next to the settings file (`vcmodernhud.log`) usually says why. The
last three sessions are kept as `vcmodernhud.1.log` to `.3.log`, so a session that crashed isn't lost when
you start the game again. The log notes deaths, arrests, missions and cutscenes, what the police know and
do (calls, searches, recognitions, the APB), and how the helicopters fare. If the game freezes for 8
seconds, the log records where the game was stuck, which a force quit otherwise loses.

## How it works

The port is a native arm64 build of reVC with its symbols intact. At install time, `tools/patch_macvc.py`
writes a jump at the start of each game function the mod changes, pointing into a plugin built from
`src/`. The plugin works inside the game's own frame:

- It draws the HUD, menus and minimap through the game's 2D primitives, feathering every edge itself.
- It reads the controller through GLFW before the game merges it into its pad state.
- It renders the clouds and lighting with new OpenGL shaders inside the game's renderer.
- It poses Tommy bone by bone wherever the game has no animation: swimming, climbing, walking crouched,
  diving, and the arms holding a gun in first person and while walking aimed.
- It drives the police through the game's own functions and data: what the police know is a small model
  of its own (`src/heat.cpp`, with unit tests), kept in step with the game's wanted level, so missions
  that read or set your stars still work.
- It makes the spotted sound itself and plays it, and the car radios from outside, through the game's
  OpenAL.

The minimap is built from your own game files at install time, so nothing from the game is in this
repository.

## Development

- `make` builds the plugin.
- `make test` runs the unit tests (the minimap's projection, GPS turns, and the police's knowledge).
- `make preview` builds an offline renderer for the minimap and phone.
- `make viewmodel` cuts Tommy's arms from every outfit in your game files, as the mod does, fits his hand
  to each gun, reports how much of each finger ends up inside it, and writes OBJs to render with
  `blender -b -P tools/viewmodel/render.py -- build/vm fp player`. `VM_SHOT=0.05` renders the gun that long
  after a shot; `build/vcmm-viewmodel <GameData> build/vm --melee nitestick` holds a melee weapon instead
  (`VM_SWING=which,time,strike` mid-swing).
- `make guns` rebuilds the viewmodel guns in Blender (`tools/guns/`; the built `assets/guns/*.vmg` are
  committed, so players never need Blender).

Each feature is its own module in `src/`, for example `camera.cpp` (first person), `swim.cpp`,
`clouds.cpp`, `police.cpp` and `wanted.cpp`. Each module's header says what it does and which game
functions it hooks. Offsets and call sites are for the 1.0.5217 binary only. The wanted system's design
and build plan are in `docs/superpowers/`.

## Known limits

- The latest feedback build has passed code, offline geometry and unit checks, but its installed-game
  acceptance checks are pending. Estate lighting remains unresolved. The exact coverage and remaining
  scenarios are in [the feedback verification record](docs/superpowers/plans/2026-10-31-feedback-verification.md).
- In first person, things you throw (grenades, molotovs) still use the game's own models and throw, and the
  keyboard and mouse fire through the game's attack, not the instant firing. Melee weapons are the game's own
  low-detail models (64 px textures, unlike the remade guns), all held in his right hand (the bat and the
  chainsaw too), his fingers as his model has them round a handle. Some outfits (the ones with open, straight fingers) grip
  the guns less neatly than his usual clothes do. Tommy turns on the spot without a stepping animation.
- The shadows are the port's own ray-traced ones (`MetalRayTracing=1` in the port's `macVC.ini`). They
  can cost 5–28 ms a frame, and the mod's log shows their timings.
- Cops hear gunfire, so missions with street shootouts can draw a police search where the game never did.
- The helicopter door gunners' seats, its pilot's, its searchlight and its parts to shoot, the gun on each
  car's console, and the Hunter's flying were all set from the models' measurements and may need tuning.
- Only the 1.0.5217 build of the port is supported. The installer refuses anything else.

## Credits

Built on reVC, the reverse-engineered Vice City source, and the native macOS port made from it.

## Disclaimer

A fan-made mod, not affiliated with or endorsed by Rockstar Games or Take-Two Interactive. It contains no
game code or assets: you need your own copy of the game and the macOS port. Use at your own risk;
`./uninstall.sh` restores the original game.

## License

[MIT](LICENSE)
