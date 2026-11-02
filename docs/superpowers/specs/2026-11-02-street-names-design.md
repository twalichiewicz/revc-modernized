# Street names on the HUD (#35) — design

## Intent

Vice City's only sense of place on screen is its 12 big areas (`navig.zon`: Downtown, Ocean Beach, Vice
Point, Little Havana…), shown huge in the bottom-right corner. The goal is GTA IV's usefulness: a glance
at the corner tells you **which street you're on**, under the area you're in. You can follow directions,
recognise a route you drove before, and say "I'm on Ocean Drive near Washington Street".

Already done (ac28bf1): the area name is drawn at 0.6x the game's size.

## Goals

- **The street you're on:** shown in the bottom-right corner, over the area name, in the same family of
  fonts.
- **When it shows:** as you turn onto a new street, on foot or driving, then fading after a few seconds,
  as GTA IV's does. It shows again when the area changes, when you get into a vehicle, and on the expanded
  minimap view (D-pad down).
- **Coverage:** every street a car normally drives on in the main city has a name, bridges and
  causeways included. Car parks, alleys and service roads may stay unnamed.
- **Stable and believable names:** the same stretch of road always has the same name. Names come from the
  game itself wherever it has them, and are in its Miami-derived style elsewhere.

## Non-goals

- Street labels on the pause map or the minimap. A possible later phase; not part of this.
- Spoken directions, or street names in GPS instructions.
- House numbers, or naming footpaths, beaches or interiors.

## Anti-goals (a failure even if it works)

- **Contradicting the game's own text:** "the Ocean View hotel on Ocean Drive" and "1102 Washington
  Street" must be on streets with those names.
- **Flicker:** names jittering at intersections, or a street name shown while you're clearly in a park,
  on the beach or indoors.
- **Joke or real-world-sensitive names:** Vice City's satire lives in its radio and ads, not its street
  signs.

## Constraints

- **No street data exists.** The game data has no street names. `info.zon`'s 167 small zones have
  internal names (`Shop3`, `street3`), and no names fit for display.
- **Canonical names from the game's text:**
  - **Ocean Drive:** "the ~h~Ocean View~g~ hotel on Ocean Drive", the "Ocean Drive" stunt race, DBP
    Security "in Ocean Drive" and "off Ocean Drive", and the chef "in the kitchen of a hotel on Ocean
    Drive". The builder finds where these are from the mission script, and Ocean Drive must reach them.
  - **Washington Street:** the 1102 Washington Street safehouse.
  - **The bridges:** "the bridge to Star Island", "the bridges to the mainland".
- **Road geometry exists:** the mod's baker (`vcmm-bake`) already turns the game's car paths
  (`data/maps/paths.ipl`) into 3,878 road segments with real widths, in `map.bin`. It does this from the
  player's own game files, so no game data ships with the mod.
- **Runtime:** the HUD frame (`CHud::Draw` hook, `hud.cpp`). The game's area name comes from
  `CHud::m_ZoneToPrint`, which already scales through the zone `SetScale` site. The mod's text goes
  through `text::Draw` in the game's fonts.

## How (decided)

- **Authored outlines, snapped to the real roads.** A names file in the repo (`assets/streets.txt`) lists
  each street as a name plus a few rough waypoints along its centreline, in world coordinates.
  - The baker snaps each outline onto the car-path segments: segments near the outline and running
    along it get that street's index, and `map.bin` gains a street index per segment (a format version
    bump).
  - Only names and rough lines are authored. The exact geometry is always the game's own roads.
  - Unmatched segments stay unnamed.
- **An authoring aid.** The baker can write a picture per area with segments coloured by street and
  unnamed ones highlighted, so coverage can be checked and fixed.
- **Which street you're on:**
  - On foot: the nearest named segment within its half-width plus a pavement margin.
  - Driving: the nearest segment whose direction agrees with your heading, which sorts out
    intersections.
  - Hysteresis: a new street wins only once you've been on it about a second, or 15 m.
  - Off every road (parks, beach, interiors, the water) there's no street line, only the area.
- **The names list (your decisions, 2026-11-02):**
  - The canonical names above go on the streets the game puts them on: Ocean Drive along Ocean Beach's
    front, Washington Street where the safehouse is.
  - **Altered Miami names** for the rest, as the game did with Washington Avenue → Washington Street:
    real Miami street names, lightly changed, never a real person's name.
  - **Numbered cross streets:** the short cross streets are numbered, as in Miami Beach (5th Street,
    12th Street). The most precise answer to where you are, especially in Ocean Beach and Washington
    Beach.
  - The builder proposes the full list in that style, and **you review it before it's baked**.

## Settled in review (the builder's calls)

- **Roads have no height in `map.bin`:** where roads cross over each other (bridges over streets,
  ramps), the street under him is decided with a ground or height test, so a bridge doesn't name the
  street below.
- **Boat paths aren't road segments,** so water never gets a street name.
- **The expanded minimap view** (D-pad down) already exists in `src/minimap.cpp`.

## Left to the builder

- **The exact look:** sizes, spacing, fade timings, and how the street line sits over the existing
  (now smaller) area name, matching the HUD's other bottom-right text.
- **The snapping thresholds:** the distance and angle limits, and the hysteresis values.
- **The file format of `assets/streets.txt`.**
- **Tests:** snapping in the baker, the runtime selection logic as a pure model (heading, hysteresis,
  off-road), and the `map.bin` round trip. The look is checked in play.
