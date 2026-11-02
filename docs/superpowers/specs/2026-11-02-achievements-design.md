# Achievements (#24)

Status: mechanics agreed 2026-11-02; placement decided (inside STATS, below); the redesigned
Stats page mockups await approval. No code yet.

## Decisions (asked 2026-11-02)

- **List:** the Definitive Edition's console list (gta.wiki / GTABase, tracked in
  [#24](https://github.com/twalichiewicz/revc-modernized/issues/24)), **minus Greasy Palms**, which
  can't be earned while realistic wanted removes every police bribe. Bribes return later as a new
  mechanic ([#31](https://github.com/twalichiewicz/revc-modernized/issues/31)), and Greasy Palms
  with them. So 42 achievements; Kingpin needs the other 41.
- **Thresholds:** the Definitive Edition's wherever known (e.g. Godfather at its patched 100,000,
  not the native 1,000,000 plus $10M; Stuff of Legends at the DE's figure). Where the DE number is
  unconfirmed, the native threshold, flagged as such on the page.
- **Existing saves:** loading a save awards everything it already meets, as if it had been played
  with achievements on. **Cheats block unlocks** (`CStats::CheatedCount` > 0), as in the DE.
- **Visible in game:** a pause-screen page lists every achievement with its state and progress.

## Detection

From `docs/research/2026-11-02-achievements-native-map.md` (read-only research; offsets and script
globals unconfirmed in a running game):

- **Kingpin** unlocks when the other 41 have.
- **33 from saved data** the game already keeps: `CStats` counters (levels, wheelie time, taxi,
  fires, pizzas, property damage and ownership, media attention, unique jumps, stores, stars,
  headshots, 100%, high scores), mission-passed script globals, hidden packages, the import
  garage's bitmasks, the criminal rating, and per-station radio listen times.
- **3 by live polling:** six stars, Deluxo top speed (threshold to be measured in game), outfits
  worn (the script's 12).
- **5 mission conditions**, through hooks the patch already carries (`CPed::InflictDamage`,
  `CPickup::Update`) plus script globals: Iron-y (golf-club kill of Gonzalez in *Four Iron*),
  Salutations (M4 kill of Diaz in *Rub Out*), Chopper'd Up (all *Demolition Man* hostiles by rotor),
  Baked Distractions (tank taken before the army is alerted), Not my First Time (*G-Spotlight*
  without a fall; the script keeps no fall flag, so this one is a height heuristic and the riskiest).

Script globals are specific to this `main.scm`: detection checks the script's bytes (as
`story.cpp` does) and disables script-based entries on a mismatch rather than guessing.

## Records and presentation

- **Per-profile record** in `~/Library/Application Support/macVC/vcmodernhud/achievements`: what is
  unlocked and when, independent of save slots. Polling runs a few times a second, not per frame.
- **Unlock notice:** a short in-game notice in the game's own frontend style (fonts and colours from
  `fonts.txd`, per the project's UI rule), queued so several unlocks don't overlap. Built: top
  centre, "ACHIEVEMENT UNLOCKED" over the name in the heading font, 5 s with a fade. Three or more
  in one poll (a save loaded that already meets them) are one "N from your save" notice rather than
  minutes of them; each is still logged.
- **Pause page (decided 2026-11-02):** achievements share the STATS tab. The Stats page is
  redesigned with the Controls page's pill row under the pause tabs, **STATS | ACHIEVEMENTS**
  (as built: LEFT/RIGHT switch them, since LB/RB already switch the pause tabs). STATS shows the game's own stats lines and values restyled into the same dark
  panels, two scrolling columns (label left, value right in pink). ACHIEVEMENTS shows a summary bar
  (count, progress to Kingpin), a scrolling list (pink dot and date when unlocked, progress when
  not) and a legend-style detail panel for the selected entry. Mockups rendered from the game's own
  `fronten1.txd`/`fonts.txd` at the README screenshot's size; approved 2026-11-02 and built.
- **main.scm check timing:** the script space holds main.scm only once a game is started or loaded,
  so the "is this the main.scm the globals are for" check runs at every poll, not at startup.
  (story.cpp and credit.cpp still check once at startup and so have been off: #36.)

## Checks

Tests per detection family against fake native state (counters, globals, garage masks, mission
kills), the cheat block, retroactive award on load, record persistence and corruption tolerance,
the notice queue, and the page model; then in-game checks of a sample from each family and your
acceptance of the page.
