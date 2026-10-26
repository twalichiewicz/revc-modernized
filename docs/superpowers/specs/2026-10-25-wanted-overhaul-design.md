# Wanted overhaul: witnesses, descriptions, the APB, and police helicopters

Design for the macVC mod in this repo (`vc-minimap`, the "VC Modern HUD" dylib for the
native Apple Silicon port of Vice City, macVC 1.0.5217, built from reVC).

## Why

In Vice City the police always know exactly where Tommy is. Every cop, police car and
helicopter steers at his real position every frame, whether or not anyone has seen him,
like a laser-guided bomb. Escaping is a matter of outrunning a timer, and nothing you do
(changing clothes, swapping cars) matters.

The player wants it to work like Red Dead Redemption 2 (and, as widely reported, GTA VI):
the police only know what someone has actually seen and told them. A crime has to be seen
and called in. Cops know you by your description (your clothes and your car) and where you
were last seen. Get out of their sight and change what they're looking for, and they have
to find you again. Escaping leaves you under an APB until you save at a safehouse.

The police helicopter should also stop "magically shooting bullets from its belly": SWAT
officers sit in its open sides with rifles, and you can shoot them out.

## Goals

- Crimes reach the police through witnesses (or a cop seeing them), not automatically.
- The police know Tommy by a **description** (outfit and vehicle) and a **last known
  position**, both updated only when a cop (or helicopter) actually sees him.
- Every part of the police response works from what they know, not from where Tommy really
  is: dispatch, chasing cars, cops on foot, roadblocks, helicopters.
- Changing clothes or vehicle out of sight breaks the description; in sight, it doesn't.
- Recognition is visible and gradual: an eye with a filling meter over each cop who is
  looking at you.
- Escaping leaves an APB (hollow stars) that only a safehouse save (or wasted/busted) clears.
- The HUD shows all of this: circular status icons to the right of the minimap, and the
  stars filled, dimmed or hollow.
- Police helicopters carry SWAT door gunners; more and heavier air support at higher stars.

## Non-goals

- Changing how many stars a crime is worth. The game's own crime-to-stars rules stay.
- Changing missions. Scripted wanted levels and "lose the cops" objectives keep working
  through the game's own wanted level (see "The game's wanted level").
- Saving the APB in the save file (saving clears it, so there's nothing to save).
- Faces, masks, CCTV, licence plates (GTA VI ideas that don't fit Vice City's data).

## Anti-goals (it fails if...)

- Cops still home in on Tommy with no one seeing him. That's the core complaint.
- Changing clothes or cars right in front of cops helps.
- A recognition happens with no visible warning (no eye, no meter).
- Missions break: a mission's scripted stars are ignored, or "lose the cops" can't pass.
- SWAT abseiling from helicopters (removed: it puts them in a weaker position than shooting
  from the air).

## The loop

```
Clear
  └─ crime seen by a cop ─────────────────────────────────► Wanted (ID'd)
  └─ crime seen by a civilian ─► Reported (call going through)
        ├─ witness stopped ───────────────────────────────► Clear
        └─ call gets through
              ├─ witness saw Tommy ────────────────────────► Wanted (ID'd)
              └─ witness didn't see him (car alarm, etc.) ─► Searching (location only)

Wanted (filled stars, cops can see him or just lost him)
  └─ no cop has seen him for a while ──────────────────────► Searching (stars dim)

Searching
  └─ a cop recognises him (eye fills) ─────────────────────► Wanted, same stars
  └─ search runs out, he's outside the search area
        ├─ they had a description ─────────────────────────► APB (hollow stars)
        └─ they never had one ─────────────────────────────► Clear

APB (hollow stars)
  └─ a cop recognises him ─────────────────────────────────► Wanted, at the level he escaped at
  └─ save at a safehouse, wasted, busted ──────────────────► Clear
```

Wasted or busted clears everything from any state, as the game does now.

## Reporting crimes

- **A cop who sees a crime** needs no call: the stars come at once and he IDs Tommy on the
  spot (description and position).
- **A civilian who sees a crime** may become a witness. Who reports and who just flees follows
  the game's own ped temperaments (`m_pedStats` fear, lawfulness, temper), as
  `CCivilianPed::ProcessControl` already decides.
- **Most witnesses run to the nearest free payphone** and call it in. Vice City already has
  this, switched off before release: `CPed::RunToReportCrime` (the ped runs to
  `gPhoneInfo.FindNearestFreePhone`), then `PED_FACE_PHONE` and `PED_MAKE_CALL`, which reports
  the crime. In reVC the comment reads: "They changed true into false to make this function
  unusable. So running to phone actually starts but first frame after that cancels it."
  It's re-enabled.
- **Rich NPCs sometimes carry a brick phone** and call from where they stand after a short
  delay. This makes crimes in rich and business areas riskier. *(My call: which peds count as
  rich is by ped model (suits and the like); they're more common downtown, on Starfish Island
  and in Vice Point; a random share of them carry a phone.)*
- **Stopping a witness** before the call gets through means nothing is reported.
- **A witness with no phone in reach** just flees, and reports only if they run into a cop.
  *(My call.)*
- **A car alarm** set off while stealing a car reports the location only. *(My call.)*
- **Stars for a report:** a call that gets through counts the game's own wanted points for
  that crime, as if a cop had seen it (`CWanted`'s crime rules, unchanged). A car alarm counts
  as the game's own alarm rule does when a cop hears one: 1 star. So a location-only report
  always has at least one star to search with. *(My call.)*
- **What the call tells the police:** if the witness saw Tommy, his description (outfit, and
  vehicle if he was in one) and where. If not, only where; the police start searching there.

## What the police know

- **The description:** Tommy's outfit (his current player model/clothes) and the vehicle he
  was last seen in. *(My call: a vehicle matches by model and colour, so a Pay 'n' Spray
  respray breaks it; another car of the same model and colour still matches.)*
- **The last known position**, and when.
- Both update only when a cop or a police helicopter sees Tommy. If a cop watches him change
  cars or clothes, the description updates to the new ones. *(My call.)*

## No homing

When no cop or helicopter can see Tommy, every part of the police works from the last known
position, not from where he really is:

- Units are dispatched to the last known position (or the crime scene for a location-only
  report) and spread out along the roads from there.
- The roadblock perimeter forms around that spot, not around Tommy.
- Police cars in pursuit drive to where he was last seen, then search.
- Cops on foot go to where they last saw him (the gunfight AI's "go round to see him" uses
  his last known position, not his real one).
- Helicopters search the area (searchlight at night) rather than flying straight to him.

## Recognition: the eye

- A cop who can see Tommy (line of sight, within his view) gets an eye icon with a ring
  meter. The meter fills the longer he looks, faster the closer he is. *(My call: faster when
  closer.)*
- **Matching the description** (the described outfit, or the described vehicle) fills the eye
  from a distance, quickly.
- **A close look always works:** a cop who gets close to Tommy fills the eye even when nothing
  on the description matches (or there's no description at all, after a location-only
  report), more slowly. This holds anywhere: changing clothes and car means they have to get
  close to re-ID him, as the player put it ("they need to reID (getting close to you)"), and
  as the agreed APB mockup shows ("only a close look gives you away").
- **Committing a crime in a cop's view** IDs him at once.
- **Full meter = recognised:** the police have him again (description and position updated).
- On screen, the eye is over the cop. Off screen, it sits at the screen edge, pointing his way.
- **The police helicopter is the big spotter:** it sees much further than cops on the ground,
  and its searchlight sweeps the search area at night.
- *(My call: the police uniform outfit counts as just another outfit, and doesn't fool cops.)*

## Escaping and the APB

- **Losing them:** once no cop has seen Tommy for a while, longer at more stars, and he's
  outside the area they're searching, the stars go hollow (APB) if they had a description,
  or clear if they never had one.
- **The APB** stays until Tommy saves at a safehouse. Wasted or busted also clears it.
- **Spotted under the APB:** a full eye brings the stars back, filled, at the level he
  escaped at, and the chase picks up from there.
- **Pay 'n' Spray** no longer wipes the stars. It changes the car's colour, which breaks the
  car part of the description. With filled stars it only helps if no cop sees him drive out.
- **Changing clothes no longer lowers the stars** (in the game, a clothes change clears up to
  two). Like the respray, it only breaks the outfit part of the description, and only out of
  sight. *(Follows from Pay 'n' Spray, as agreed.)*
- **Police bribe pickups are removed from the map.**

### The game's wanted level

The mod keeps its own state (reported, wanted, searching, APB) on top of the game's
`CWanted`. *(My call, to keep missions working:)*

- While wanted or searching, the game's wanted level is the star count, so scripts that read
  or set it behave as before.
- Under the APB (hollow stars), the game's wanted level is 0. Mission "lose the cops"
  objectives pass, and the safehouse save works as normal (the game won't save while
  wanted). Saving clears the APB. The APB isn't stored in the save file; quitting without
  saving drops it, and the last save had none.
- When a mission script sets a wanted level, that counts as the police knowing where he is
  (a cop-seen ID).
- When a mission script lowers the wanted level, the mod follows it. When a script clears
  it, that clears everything, including the APB: the story says he's clear, and missions that
  clear stars for a cutscene or an ending must keep working. The game's "lower wanted level"
  cheat clears everything too. *(My call.)* Telling a script's clear apart from the game's
  own (the clothes change, Pay 'n' Spray) is left to the builder.
- Passing or failing a mission doesn't clear the APB on its own; only a save does, as agreed.

## The HUD

Agreed with mockups (`.superpowers/brainstorm/.../wanted-states.html`):

- **Stars, top right (where the mod's HUD draws them now):** filled while wanted, dimmed while
  searching, hollow under the APB.
- **Circular icons to the right of the minimap** (RDR2 / GTA VI style), white glyphs on dark
  circles:
  - a red phone with a ring meter: a witness's call going through;
  - an outfit icon and a car icon: on the description; struck through when broken;
  - a magnifier: searching;
  - a badge: the APB.
- **The minimap** shows the area they're searching (where they last saw him) while searching.
  *(My call: witnesses running for a phone are marked on the minimap too.)*
- **The eye meters** over cops (or at the screen edge) as above.

## Police helicopters

- **No belly gun.** The game's `CHeli` firing from its underside is replaced.
- **Two SWAT officers with rifles sit in the open sides** of each police helicopter. Each
  fires only when his side faces Tommy. *(My call: the pilot circles to give them a line of
  fire; an officer who's shot falls out.)*
- **With both officers down,** the helicopter keeps tracking Tommy and reporting to the
  police (the long-range spotter), but doesn't shoot.
- **Shot down,** the next helicopter to arrive has fresh officers. The mod's existing
  helicopter damage (`heli.cpp`: hits where they land, smoke, coming down sooner) stays.
- **No SWAT on ropes:** the game's abseiling SWAT (`CHeli` dropping `COP_HELI_SWAT` peds) is
  removed.
- **How many:** one police helicopter from 3 stars, two from 4 stars *(4 is my call for "a
  level higher")*. Today the game sends one at 3 to 6 stars (`CWanted::NumOfHelisRequired`).
- **At 6 stars the army's Hunter joins** (model 155, the attack helicopter at Fort Baxter),
  with its cannon and rockets, and takes more hits to bring down. The game can fly a
  helicopter with AI (`CCarCtrl::SteerAIHeliTowardsTargetCoors`, `MISSION_HELI_FLYTOCOORS`),
  but the Hunter's weapons only fire for the player (`GetStatus() == STATUS_PLAYER` in
  `CAutomobile::ProcessControl`), so the mod fires them for the AI pilot.

## Constraints

- **The game binary:** macVC 1.0.5217 (arm64, exported symbols, no hardened runtime). The mod
  is a dylib; game functions are patched in by `tools/patch_macvc.py`. The header padding fits
  73 patch sites and 69 are used, so **only 4 new hooks are left**. Prefer calling exported
  functions (`dlsym(RTLD_MAIN_ONLY)`), per-frame work from existing hooks (`CCamera::Process`,
  `CPad::Update`, `CPed::PreRender`, `CPlayerPed::ProcessControl`), and writing game state,
  over new hooks. reVC source (`github.com/mrxenginner/reVC`) is the reference for logic;
  offsets must be checked against the binary.
- **Existing police and gunfight work to build on:** `src/police.cpp` (units across the city
  dispatched by road, roadblock perimeter with spike strips, cars pulling up short, cops
  joining past the game's 10-cop cap, roadblock cops leaving burning cars), `src/combat.cpp`
  (cover-based gunfight AI), `src/heli.cpp` (police helicopter damage), `src/hud.cpp` (stars),
  `src/minimap.cpp` (minimap). These currently target Tommy's real position and need to work
  from the police's knowledge instead.
- **Settings:** each part should be switchable in `vcmodernhud.ini` like the mod's other
  features, with the game's behaviour back when off.
- **Logging:** the mod logs to `vcmodernhud.log`; the new system should log state changes
  (reported, ID'd, searching, APB, cleared, recognised, description broken) and a periodic
  summary like the police log, so play sessions can be reviewed.
- **The player tests by playing;** the agent can't launch or control the game.

## Left to the builder

- All tuning numbers: how long until the search ends at each star level, eye fill rates and
  distances, the search area's size, the rich-phone share and call delay, helicopter
  sight range, the gunners' accuracy.
- How the "no homing" redirection is done in the game: which reads of the player's position
  (car AI chase missions, cop ped objectives, the helicopter's target, roadblocks) to redirect
  to the last known position, and how, within the hook budget.
- How the outfit is identified (player model index or clothes state) and how the HUD icons and
  eye meters are drawn (the mod has its own 2D drawing in `draw.cpp`/`hud.cpp`).
- How door gunners are attached to and posed in the helicopter. Risk: the game's police
  helicopter model (`MI_CHOPPER`) may not have open sides, so gunners might clip through the
  doors; seat them where they look right.
- The order of building and how to split it into reviewable steps.
- **If the hooks don't fit** in the 4 that are left: make room in the patcher first, as its
  own step, checked on its own (for example code caves in game functions that are never
  called; the stubs have to be within branch range of the game's code, which is why they sit
  in the header padding now). If more room can't be made, cut in this order, last first:
  the Hunter at 6 stars; the second police helicopter; the door gunners (the belly gun then
  stays); rich NPCs' phones; the payphone witnesses (crimes then reach the police as the game
  has it). The core (description, recognition, no homing, searching, the APB, the HUD) stays.
  *(My call.)*

## Research notes

- GTA VI, as far as could be confirmed: six wanted stars and a "Criminal Profile" that judges
  crimes differently were shown by Rockstar. Witness- and alarm-gated police, identification
  by face/clothes/vehicle, and description-based searching are widely reported (fan guides,
  2022 leak footage), but not confirmed by Rockstar.
