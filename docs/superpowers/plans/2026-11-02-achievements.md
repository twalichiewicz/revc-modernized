# Achievements (#24) — implementation plan

Design: `docs/superpowers/specs/2026-11-02-achievements-design.md` (mechanics and the Stats page
placement approved 2026-11-02). Native sources: `docs/research/2026-11-02-achievements-native-map.md`.

**Goal:** the Definitive Edition's console achievements (42; Greasy Palms deferred to #31), awarded
from what the save already meets and from live play, blocked by cheats, with an unlock notice and a
STATS | ACHIEVEMENTS pill row on the pause Stats page.

**Decisions taken from the user's answers (2026-11-02):** DE thresholds where known — Godfather at a
criminal rating of 100,000 (DE patch; no $10M condition); Stuff of Legends as this game ranks it
(1800; the guide's 1600 is unconfirmed). "All music stations": the nine radio stations (0–8, talk
included, as the DE guide counts), each heard for at least 10 s so tuning past doesn't count. Deluxo
"max speed": 95% of its handling top speed (to calibrate in play). Retro Lewk: the script's 12
outfits, worn while the mod runs.

## Phase 1 — model (pure, `src/achievementsmodel.*`) — done
- [x] The 42 definitions: key, name, description, target, progress text.
- [x] `Facts` (a snapshot of native values) → progress per achievement; unlock when met; Kingpin
      when the other 41 are; nothing unlocks while cheated or before the facts are valid.
- [x] `Record`: unlocked keys with dates, outfits seen; text format that tolerates corruption and
      unknown keys. (The mission-condition latches live in Phase 2: they're per mission run, not saved.)
- [x] Tests (`test_achievementsmodel`): every family at, below and above its threshold; cheat block;
      retroactive award; Kingpin; record round-trip, corruption, unknown keys.

## Phase 2 — native (`src/achievements.cpp`) — done
- [x] Bindings: CStats statics, `CWorld::Players`, `CGarages::CarTypesCollected`, criminal rating,
      radio listen times, script globals behind `story::ScriptKnown()` (story.cpp's main.scm check).
- [x] Live facts: six stars, Deluxo speed, outfit worn; mission conditions through the existing
      `CPed::InflictDamage` path (Iron-y, Salutations, Chopper'd Up) and script-global polling
      (Not my First Time, Baked Distractions); load detection (game time jumping back, or on by more
      than 10 s, waits 3 s to settle); polling every 250 ms. `Achievements=0` turns it all off.
- [x] Record file `~/Library/Application Support/macVC/vcmodernhud/achievements`; unlock log lines.
- [x] Tests (`test_achievements`) against fake native memory for each binding family and each
      mission condition, with its spoilers (wrong weapon, unset handle, mission restarted, alerted).

## Phase 3 — presentation — built; awaiting in-game checks
- [x] Unlock notice (`statspage::DrawNotice`): top centre, the heading font, fades in and out; three or
      more at once (a save that already meets them) are one "N from your save" notice.
- [x] Stats page (`src/statspage.*`, `src/statsmodel.*`): the Controls page's pill row; STATS is the
      game's own lines (`CStats::ConstructStatLine`) in two scrolling columns; ACHIEVEMENTS has the
      summary, list and detail panel. Left / right switch STATS and ACHIEVEMENTS (LB / RB keep switching
      the pause tabs); up / down scroll; the wheel scrolls; Back resumes, as on every tab.
      `CMenuManager::PrintStats` is hooked off under it; without that hook the vanilla page stays.
- [x] Page-model tests (`test_statsmodel`); full suite, sanitizers, zero-warning build.
- [x] Install and startup check of the pre-review build: a save's five unlocks after the load settled;
      it showed the main.scm check ran too early (fixed: per poll). Review: G-Spotlight's pass ends its
      script in the same tick (fixed: checked at the script's end; loads reset the mission watches).
- [x] Final build installed (61A5432B); startup: main.scm recognised once the save loaded, eight
      mission-flag achievements unlocked from it (13 of 42).
- [ ] In-game checks of a sample from each family and the user's acceptance.
