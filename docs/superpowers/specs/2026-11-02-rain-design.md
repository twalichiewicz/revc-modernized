# GTA IV-style rain (#32) — design

## Intent

Rain in Vice City is a few thin grey lines and a slightly darker screen. The goal is GTA IV's storm: rain
you feel is falling around you, streets that turn dark and mirror-like with the city's lights smeared
across them, water splashing up, and drops on the view. Rainy weather should become a mood the city has,
not a filter.

You chose all four parts:
- denser, lit, wind-angled rain streaks
- wet roads with reflections
- splashes and puddles
- drops on the camera (first person, and in cars)

## Goals

- **Streaks:** many more, lit by the sky and by nearby lights (street lamps and headlights catch them),
  angled by the wind, and absent under roofs and bridges and indoors.
- **Wet roads:** they darken and gain gloss as rain builds and dry slowly after. Light sources (street
  lamps, headlights, neon, the low sun) reflect as long streaks on the road toward the camera, as on wet
  asphalt.
- **Splashes and puddles:** drops splash on roads, pavements and car roofs. Puddles build in low spots as
  rain continues, reflect the sky and lights, and shrink as it stops.
- **Drops on the view:** in first person, and in the seat view in cars, drops land on the "lens" and
  trickle. While the car moves they streak away with speed.
- **Driven by the game's own weather:** the amount from `CWeather::Rain`, wetness building and drying
  from `CWeather::WetRoads`, storms (`bScriptsForceRain`, the hurricane) included.

## Non-goals

- Rain sounds, wiper animation, puddles that splash under tyres, lightning or flooding (possible later).
- Changing weather frequency, or when it rains.
- Wet clothes or skin on characters.

## Anti-goals

- **Rain where none falls:** indoors, under bridges, under the Malibu's awning.
- **A visible frame-rate drop on this machine** in heavy rain.
- **Washed-out or blurry screens:** no drops on the view in third person at all, and subtle ones where
  they appear.
- **Breaking the existing look:** the sky, volumetric clouds and lighting pass already respond to rain
  (`sky.cpp`, `clouds.cpp`, `grade.cpp`), and all of this must sit with them.

## Constraints

- **The renderer:** the game draws through librw's gl3 backend on OpenGL ES (ANGLE). The mod's shader
  layer (`shaders.h`) can build shaders with the game's headers, run full-screen passes through RwIm2D,
  draw world geometry through RwIm3D, render to targets, grab the frame drawn so far (`GrabFrame`), and
  read the camera's matrices. It has **no scene depth texture**, so true screen-space reflections aren't
  available.
- **What the mod knows of the world:**
  - every road segment with its width (`map.bin`, from the game's car paths)
  - building footprints with roof heights (`map.bin`)
  - the game's lights (`CPointLights`, and the ray-traced shadows' light candidates)
  - the sun and sky (`shaders::CurrentSky`)
- **The game's own rain:** `CWeather::RenderRainStreaks`, `AddRain` (drops on the screen) and
  `AddSplashesDuringHurricane` exist and can be replaced or kept, part by part.

## How (decided)

- **Streaks:** drawn by the mod in place of the game's, as a camera-centred volume of lit, wind-angled
  streak quads, more of them the heavier the rain. The wind is the volumetric clouds' fixed (7, 3) m/s,
  so the two agree.
- **Covered places get no streaks.** The data is the building footprints with their roof heights, plus
  what the baker currently drops: the street level they're measured from, and awnings' collision. The
  game's own no-rain zones (`Cullzone.dat`) cover interiors.
- **What gets wet (my call, from GTA IV, where the whole street surface goes wet):** all outdoor paved
  ground, pavements, car parks and forecourts too, not only the traffic lanes. The baker writes the
  game's own ground surfaces (tarmac and pavement, from the collision it already reads, with real
  heights) into `map.bin` as the wet surface. Roads only would be cheaper, but kerbs and pavements would
  stay dry beside dark lanes, and the puddles need pavements.
- **Wet roads:** drawn as geometry over that ground.
  - A darkening, glossy layer that takes the sky's colour at a grazing angle (Fresnel).
  - Light reflections drawn as stretched streaks on the road plane, below each light toward the camera.
  - This stands in for screen-space reflections, which need a depth buffer the mod doesn't have.
- **Puddles:** stable spots on roads and pavements (seeded by place, so the same puddle is there next
  time). They grow and shrink with wetness and reflect like the wet road, only more strongly.
- **Splashes:** small short-lived splash sprites on surfaces near the camera, more with heavier rain.
  They may build on the game's own splash particles.
- **Drops on the view (your decision, 2026-11-02):**
  - **On foot in first person:** on the view, as a lens. A full-screen pass using the frame grab
    refracts the scene through drop shapes that land, merge and trickle.
  - **In a car's seat view:** on the windscreen only. The pass is masked to the car's own windows (the
    glass the mod already recognises by its materials), so the dashboard, pillars, roof and his hands
    stay dry. On the windscreen, speed pushes drops up and aside.
  - **On bikes and boats, where there's no glass:** on the view, as on foot.
  - Aircraft cockpits are treated as cars (their glass). Convertibles with the top down are treated as
    bikes.
- **Settings:** each part can be turned off in `vcmodernhud.ini`.

## Left to the builder

- Counts, sizes, colours, timings, falloffs and the wetness model's curves.
- Whether splashes reuse the game's particle system or the mod's own sprites, and whether any of the
  game's own rain stays.
- Performance budgets per part, measured in heavy rain at night downtown.
- Tests: the pure parts as models (wetness over time, covered columns, puddle placement, streak
  counts); the look and frame rate checked in play.

## Order

Wet roads first (the biggest change in look), then streaks, then drops on the view, then splashes and
puddles.
