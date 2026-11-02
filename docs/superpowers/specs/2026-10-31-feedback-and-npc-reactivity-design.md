# Latest-version feedback and GTA IV-style NPC reactivity

Status: approved in conversation on 2026-10-31. Implementation and remaining acceptance work are tracked
in `../plans/2026-10-31-feedback-verification.md`.

## Intent and sources

Address all 33 requests in the seven open feedback issues, preserving Vice City's world, missions,
art style and the mod's existing controls. The player's clarification for "Euphoria-lite" is
**"GTA4-style reactivity."** This means physical responses that depend on what happened to the NPC:
local hit reactions, loss of balance, attempts to recover, and falls that interact with the world.

Sources read on 2026-10-31:

- [#1 HUD](https://github.com/twalichiewicz/revc-modernized/issues/1): 3 requests.
- [#4 Third-person mode](https://github.com/twalichiewicz/revc-modernized/issues/4): 2 requests.
- [#5 First-person mode](https://github.com/twalichiewicz/revc-modernized/issues/5): 10 requests.
- [#6 Police](https://github.com/twalichiewicz/revc-modernized/issues/6): 11 requests.
- [#7 Environments](https://github.com/twalichiewicz/revc-modernized/issues/7): 1 request.
- [#8 Swimming](https://github.com/twalichiewicz/revc-modernized/issues/8): 2 requests.
- [#9 NPCs](https://github.com/twalichiewicz/revc-modernized/issues/9): 4 requests.

Each issue had no comments when read. Item IDs below follow its bullet order. An item remains open until
its observed behavior passes verification; the presence of relevant code is not evidence it works.

## Existing implementation and constraints

This is an injected C++17/RenderWare plugin for the native arm64 macVC **1.0.5217** executable. Its
patcher and symbol bindings target that build. Keep that compatibility, the current build/install
workflow, existing game assets and feature-switch conventions.

The checkout already contains extensive uncommitted work, including helicopter hit regions and crew,
driver eye height, a console gun, stronger fall damage and recognition indicators. Preserve it. Do not
commit, reset, replace the executable or install into a running game as part of design work.

Relevant seams found in the current code:

- `src/player.cpp` hooks damage for all peds and `CPed::PreRender`, but movement control only for Tommy.
- `src/body.cpp` measures and poses one cached skeleton. NPC reactivity needs per-ped state and rig
  handling that cannot overwrite the player's cache or another NPC's pose.
- `src/swim.cpp` drains 8 health/second after breath runs out and constrains vertical movement against
  a sampled bottom. Its terrain report needs an actual underwater collision reproduction.
- `src/interact.cpp`'s greeting branch asks the NPC to speak, without an explicit Tommy line.
- `src/heat.cpp` matches descriptions by outfit or vehicle; `src/sight.cpp` applies recognition.
- `src/police.cpp` already distributes virtual car patrols and station reserves over road nodes;
  `src/traffic.cpp` still delegates pedestrian population generation to the game.
- `src/mapdata.h` describes collision-derived roofs with extruded walls. `BuildingScale` defaults to
  0.6; closer map fidelity needs a geometry comparison, not only a height multiplier change.
- `src/rtshadows.cpp` records the port's ray-tracing diagnostics. Estate lighting must be isolated
  between the original renderer, mod lighting, material state and post-processing before a fix.

All numerical defaults and implementation recommendations below are the assistant's proposals, not
values supplied or approved by the player. The builder may tune them using the acceptance scenarios.

## Approaches

1. **Animation overlays and existing knockdowns.** Least integration work, but limited contact and
   balance behavior. Extra flinch clips alone do not satisfy GTA IV-style physical reactivity.
2. **Hybrid physical reactions with animation recovery — recommended.** Use the existing damage,
   animation and entity systems, adding an articulated simulation for affected NPCs, active balance
   while alive, world contact during falls, and controlled transitions back to the game's animation.
   This is a substantial feature, with a feasibility checkpoint before extending it across the game.
3. **Replace pedestrian animation and physics wholesale.** Greater control, but requires replacing
   unrelated locomotion and mission behavior. It adds scope the feedback does not require.

Approach 2 was recommended in this design; the player approved proceeding with the design.

## Coverage and observable acceptance

### HUD — #1

| ID | Requested outcome and acceptance |
| --- | --- |
| 1.1 | Buildings on the tilted map more closely match their real footprints, roof shapes and relative heights. Compare the same representative blocks, including Vercetti Estate and high-rises, in game and in the offline preview. Keep roads, icons and routes readable. |
| 1.2 | Crosshairs are customizable from Options > Gameplay using both controller and mouse (user correction during installed-game testing). Controls: style, size, thickness, color, center dot and dynamic spread. Show a preview, persist changes, restore defaults, and use the setting in first person, third-person aiming and drive-bys; preserve specialized scope/rocket art. |
| 1.3 | A directional arrow inside the current racing checkpoint points toward the following checkpoint. It updates when the checkpoint advances and behaves sensibly at the finish or when no following checkpoint exists. Mission checkpoint detection must be verified before choosing hook sites. |

### Third person — #4

| ID | Requested outcome and acceptance |
| --- | --- |
| 4.1 | Aiming raises/lowers and moves the gun without the reported abrupt or twisted repositioning. Check pistols, shotguns, rifles and heavy weapons while standing, walking, crouching and switching views. |
| 4.2 | Third-person drive-bys use the chosen modern crosshair, consistent with first-person drive-bys. Remove the obsolete ring without drawing two reticles or changing the intended aim point. |

### First person — #5

| ID | Requested outcome and acceptance |
| --- | --- |
| 5.1 | Brake-light glow remains on the external lenses and does not shine across the cabin. Check braking at night in a sedan, coupe and convertible, including reversing. |
| 5.2 | Rolling or resting upside down does not place the eye through terrain or produce the reported bloom flash. Preserve a usable camera through recovery and exit. |
| 5.3 | Tommy's own articulated fingers wrap the steering-wheel rim. Reuse gun-grip techniques with wheel-specific contacts; maintain thumb placement, wrist alignment and hand contact while steering, honking, signaling and retrieving the console gun. |
| 5.4 | Opening the radio wheel freezes camera look. Selection input cannot also rotate the view; releasing it resumes look without a jump. Cover controller and mouse. |
| 5.5 | Intact and damaged windows look transparent from inside, with reasonable tint and lighting. Preserve broken/missing panes and external appearance; shared materials and render state must be restored after the interior pass. |
| 5.6 | On foot the player can look practically straight up without a singularity, a flipped view or wrong-direction shooting. Proposed limit: 89 degrees; handle looking down and switching scope/view consistently. |
| 5.7 | The pump shotgun's right arm stays outside the visible camera near plane while aiming, firing, pumping and reloading. Test supported outfits and viewmodel FOV settings. |
| 5.8 | Actual player damage produces a clear brief blood/impact overlay. Proposed behavior: scale with health damage, add impact direction when available, fade quickly and cap opacity so aiming remains readable. Do not trigger it for an ignored/invulnerable hit; reset on load, death and respawn. |
| 5.9 | Before a death camera pulls out, Tommy's ordinary third-person mesh, head, arms and bone scales are restored. No misshapen first-person body is visible during the transition or on respawn. |
| 5.10 | While swimming, the eye follows the swimming head/pose rather than sitting inside the upper back. Check treading, crawling, diving, surfacing, turning and climbing out. |

### Police — #6

| ID | Requested outcome and acceptance |
| --- | --- |
| 6.1 | Under an APB, new clothes and a clean car prevent recognition when neither matches the recorded description. Recommended: retain the APB history but do not accumulate recognition from a wholly clean description; returning to a matching description can expose the player again. This applies to APB recognition, not immunity for a newly witnessed crime or an active pursuit. |
| 6.2 | Replace the ambiguous APB badge with legible `APB` letters, consistent with hollow stars and the description indicators. Check at supported HUD scales. |
| 6.3 | Helicopter arrival has no repeatable new hitch attributable to synchronous mod work. Profile arrival and warm-up separately; preload needed assets asynchronously and cache mesh/crew setup where the measurement supports it. Record before/after frame times. |
| 6.4 | Police helicopters avoid building sides as well as roofs, and can escape a blocked altitude/path without getting permanently stuck. Use swept motion/body clearance rather than only a roof-height sample. |
| 6.5 | Two police helicopters keep body and rotor clearance during approach, circling and altitude changes. If contact occurs, resolve overlap/motion consistently instead of passing through each other. |
| 6.6 | The spotlight's lens glow, beam and actual emitted light share the moving physical lamp's position and direction. Moving only a corona is insufficient. |
| 6.7 | Spotlight visibility responds to ambient daylight. It is subdued or off at noon, visible at night, and transitions smoothly at dusk/dawn. |
| 6.8 | The pilot stays seated inside the cockpit while alive. Killing the pilot transitions the helicopter into a loss-of-control descent once; the dead pilot must not clip prone through the fuselage. |
| 6.9 | A disabled helicopter visibly loses altitude and hits the world before breaking up as appropriate. Preserve momentum and distinguish fatal impact from a rotor/pilot failure; do not simply spawn airborne wreck pieces for every disable event. |
| 6.10 | Foot patrols have virtual positions distributed over suitable pedestrian routes across the city, independently of Tommy. Materialize near the player only when eligible and out of sight; officers cannot originate in inaccessible lots or arbitrary remote terrain. |
| 6.11 | Stations produce replacement officers and vehicles over time, with queues/capacity, dispatch and travel. Reserve generation and actual in-world departures share one accounting system; no limitless instantaneous reinforcement. |

### Environments — #7

| ID | Requested outcome and acceptance |
| --- | --- |
| 7.1 | Vercetti Estate no longer has the reported excessive shadow/lighting contrast. The user clarified that the problem is **inside**, specifically very dark walls or floor shadows. Compare identical interior camera/time/weather with mod lighting and port ray tracing independently enabled/disabled. Fix the isolated source and compare another interior/exterior to avoid flattening the whole city. |

### Swimming — #8

| ID | Requested outcome and acceptance |
| --- | --- |
| 8.1 | Once breath is exhausted, drowning kills rapidly instead of slowly draining health. Proposed full-health duration: about 2 seconds, independent of armour and consistent across frame rates, both swimming and trapped in a submerged vehicle. Breath duration remains unchanged. Surfacing in time stops drowning. |
| 8.2 | Swimming cannot cross the seabed, submerged slopes, walls or underwater structures. Sweep the intended movement, including horizontal movement and body extent, against solid geometry; slide or stop on contact. Check shallow transitions and escape from a submerged car. |

### NPCs — #9

| ID | Requested outcome and acceptance |
| --- | --- |
| 9.1 | Drivers who can perceive a gun pointed at them visibly react: brake, flee or surrender according to circumstances and temperament. Prevent oscillating decisions, through-wall perception and control of mission-scripted drivers. Shooting remains a separate event. |
| 9.2 | Greeting audibly plays an appropriate Tommy line and elicits the existing NPC response. Verify an actual supported player speech event/asset rather than assuming generic pedestrian chat IDs work for Tommy. |
| 9.3 | GTA IV-style physical reactivity, as detailed below: directional and local impacts, balance/recovery, falls and world contact. Repeated hits affect an ongoing reaction; no universal canned collapse. |
| 9.4 | Civilian population follows zone demand, paths, time and bounded local density, rather than creating crowds wherever Tommy goes. Use the same population accounting/streaming boundary as foot police, with distinct civilian demand and police dispatch rules. |

## NPC reactivity: required behavior

The reference is the *feel* of GTA IV, within Vice City's visual fidelity. The deliverable must show
characters responding to physical circumstances rather than selecting only a stronger fall animation.

- **Local response:** shoulder/arm hits recoil that side; leg hits compromise the corresponding support;
  torso impacts turn/bend the torso in the impact direction. Damage remains the game's decision.
- **Small contacts:** the user's later check found no bumps/trips for smaller interactions. Actual
  nondamaging contacts must also produce local recoil/stagger when appropriate, with low obstructions
  at speed able to trip an NPC. Ordinary ground support and mere proximity must not trigger reactions.
- **Body volume:** the user reports full ragdolls collapsing inward like a black hole. Limb contacts,
  torso/neck constraints and skinned bone orientations must preserve anatomical volume throughout
  complete falls, including the upper chest between the measured spine and shoulders.
- **Balance:** a living NPC displaced by a nonfatal hit tries to stay upright, takes a corrective step
  where there is supporting ground and room, and braces with the arms when falling. Repeated impacts,
  lost ground support and momentum can defeat recovery.
- **Contact and momentum:** vehicle knocks, explosions and falls preserve relevant momentum. The body
  contacts floors, slopes, nearby walls and vehicles instead of visibly continuing through them. A
  blocked recovery step must not teleport the NPC through an obstacle.
- **Transitions:** a survivor can settle, get up and resume an appropriate AI state. A dead NPC cannot
  recover or resume combat. The visual/physical pose remains continuous when entering or leaving a fall.
- **Variation from circumstances:** different hit locations/directions, movement and surroundings give
  different outcomes without requiring exaggerated random impulses. A single ordinary pistol hit
  should not routinely throw a healthy NPC off their feet.

### Follow-up playtest requirements

The user's latest playtest reports intermittent ground penetration, head folding into neck/chest,
and small car/foot contacts causing collapse too readily. Living bodies must resist, brace and
recover support before surrendering to a fall; reducing every input to a fall threshold does not
satisfy this requirement. Preserve native health and mission decisions.

Tommy also needs contact flinches and protective reaching while running into obstacles. Keep his
locomotion/camera/weapon/swim/climb/death pose owners intact; this is a dedicated player response,
not permission to take over the player with the ordinary NPC ragdoll controller.

The user subsequently confirmed that Tommy raises his arms, but reported backward shoulder
hyperextension. Protective hand targets and elbow bends must respect the current torso frame,
including rear/side contacts and turns after contact. Preserve measured arm lengths and aimed hands;
first-person unarmed shoulders must be mirrored and armed shoulders must remain anchored.

NPCs running against walls and the sides of vehicles must separate or stop/slide without getting
trapped in overlapping geometry. Validate native collision ownership and articulated contacts
together, including officers immediately after leaving their car.

Weapon impulses must use the real accepted impact position and incoming ray direction whenever
the native event supplies them. A foot hit must act locally at the foot in the incoming direction.
Coarse body-piece/attacker-position fallbacks cannot stand in for captured bullet data. Preserve
native damage acceptance and helicopter hit processing, including with helicopter features disabled.

The same playtest adds window-mode persistence: remember whether the last session used a window or
fullscreen. The Mac fullscreen Space and GLFW's exclusive fullscreen video mode are separate native
states. Persist the Mac choice separately, preserve existing video preferences, and ignore temporary
fullscreen exits during renderer recreation or shutdown. Do not interrupt the user's active play
session to install or verify these changes.

### Integration recommendation (continued)

Add a dedicated reaction subsystem rather than inserting a solver into the cover AI or `body.cpp`.
Keep a deterministic balance/impulse model separate from game hooks. Store active physical states by
validated pool handle, with generation/lifetime checks; cache reusable rig measurements separately.
Keep player pose state independent, including when the player and multiple NPCs render in one frame.

Capture accepted damage at `player::Hook_InflictDamage`. Determine impact direction from a verified
hit event or attacker/impact positions; do not reinterpret the existing direction byte as a world
vector without checking it. Damage amount is not a physical impulse: tune weapon/contact responses
separately and never add duplicate health damage. Collision and explosion inputs need their own
verified integration points; they are not assumed to be covered by bullet damage alone.

Use the current animation as the target for an articulated body while upright, relax that target as
balance is lost, and blend into get-up animation after a stable recovery opportunity. During physical
falls, keep the entity's collision/root position aligned with the simulated pelvis so the skeleton,
damage hit locations and world position do not separate. Avoid solving the same contact twice through
both the game and the new layer. The builder owns solver choice after the feasibility checkpoint,
provided it supports constraints, joint limits, contacts and bounded fixed-step simulation.

Mission/cutscene control, attached crew, people entering/exiting vehicles and scripted animations must
retain ownership. The initial feature covers ordinary on-foot civilian and combat NPCs, including cops;
driver intimidation is item 9.1, and seated helicopter pilot behavior is item 6.8. Do not seize an
attached pilot or door-holder into an unrelated world-space reaction. Ordinary on-foot mission NPCs
may react only where their scripts remain functional; otherwise preserve their scripted controller.

### Feasibility checkpoint and evidence

Before general integration, build a bounded in-game prototype on one ordinary NPC. Demonstrate two
nonfatal directional impacts with recovery, repeated hits causing a fall, and a falling body contacting
a slope and a wall. Check alive/dead transitions and the rendered skeleton against the physical root.
Compare these scenarios to the existing game side by side and record them for review.

If the engine cannot support the required contacts and animation ownership through the available
hooks, report the precise limitation and the additional work needed. Do not silently substitute
animation-only knockdowns or call a force applied to the ped's single collision body an articulated
physical reaction. Exact middleware parity is not promised; the observable behaviors above are the bar.

Use a bounded active set, proposed initially at 12 nearby reacting NPCs, deterministic fixed-step
integration, limits on catch-up steps, and cheaper/frozen treatment at distance. Saturation must fall
back smoothly to the game's behavior without freezing AI or losing entities. Profile the actual target
Mac, report frame-time cost in the crowded scenario, and tune the cap from measurements.

## Shared design decisions

- **Population:** virtual agents persist across the city; streamed entities are their local
  representation. Distribution is weighted by suitable paths/zones, with capacities and replenishment
  rules. Tommy controls relevance for rendering/materialization, not the origin or number of patrols.
  Do not attempt to keep a fully rendered city population resident.
- **Helicopters:** keep navigation/avoidance, crew attachment, damage and crash transitions distinct.
  An aircraft's disabled state must not be overwritten by its pursuit navigation on the next update.
- **Rendering:** changes to shared meshes/materials, bone visibility, lights and render state must be
  scoped and restored. A first-person effect cannot leave a later third-person or NPC draw corrupted.
- **Settings:** new optional behavior follows the existing INI conventions and has a reliable disabled
  path. Crosshair settings are additionally exposed in Options. Defaults match today's appearance
  except where a feedback item explicitly changes it.
- **Lifecycle:** clear transient state on game load, new game, ped/entity replacement and feature
  disable. Do not reuse stale entity pointers. Keep hook failure behavior explicit and logged.

## Proposed delivery order

1. Camera, swimming, aiming, greetings and player damage feedback: items 4.1–4.2, 5.1–5.10,
   8.1–8.2 and 9.2. Related rendering work may be split into independent verified changes.
2. APB readability/recognition and helicopter correctness/performance: items 6.1–6.9.
3. Shared population model, station replenishment and driver intimidation: items 6.10–6.11,
   9.1 and 9.4. This depends on a stable separation between physical and virtual agents.
4. NPC reactivity feasibility checkpoint, then the complete physical reaction feature: item 9.3.
5. Map geometry, crosshair Options, race arrows and estate lighting: items 1.1–1.3 and 7.1.

This order is the assistant's recommendation. Keep every issue/item traceable through implementation;
neither an earlier pass nor a prototype completes the whole backlog. Each subsystem gets its own
implementation plan after this design is accepted.

## Verification and completion

For rules that can run independently of the game, write regression tests that exercise the real rule
at its integration seam: APB recognition, breath/drowning progression, collision motion, reaction
balance/impulse evolution, population budgets and replenishment, and settings persistence. Avoid tests
that merely duplicate implementation constants or assert the existence of hook names.

Run the repository's `make test` suite and `make` for implementation changes. Rendering, animation,
physics, NPC speech and helicopter arrival also require targeted in-game verification on macVC
1.0.5217; compilation or isolated unit tests do not establish those behaviors. Use the existing map
preview and viewmodel render tools for geometry/pose comparisons where applicable.

For each item, record what reproduced the original symptom, the changed behavior, the check performed
and any remaining limitation. Profile representative helicopter arrival and crowded reactive-NPC
scenarios before/after. Check first/third-person transitions, changed outfits, active missions, a save
reload and feature-disable paths. Keep unresolved items open and explicitly listed. GitHub edits,
comments, commits and installation are separate actions, not part of approving this design.
