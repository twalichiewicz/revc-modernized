# Street Names Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the street you're on, over the area you're in, in the HUD's bottom-right corner, as GTA IV shows it.

**Architecture:**
- **Authoring and baking:** named street outlines are written in `assets/streets.txt`. The baker (`vcmm-bake`) snaps them onto the game's own car-path segments and writes a street index and height for each road into `map.bin`, as version 5.
- **At runtime:** a pure tracker picks the street under Tommy, with hysteresis. A banner model times when it shows. A small HUD module draws both lines and hides the game's own area name while it does.

**Tech Stack:** C++17; the mod's HUD text (`text.h`, the game's fonts); `mapdata` (`map.bin`); the baker in `tools/bake/`; repo tests compiled by the Makefile (`make test`).

**Spec:** `docs/superpowers/specs/2026-11-02-street-names-design.md`

## Global Constraints

- **Canonical names:**
  - Ocean Drive must cover the Ocean View hotel, DBP Security's spots "in"/"off Ocean Drive", the chef's
    hotel, and the "Ocean Drive" stunt race.
  - Washington Street must cover 1102 Washington Street.
  - The bridges carry the game's names.
- **Other names:** altered Miami names (as Washington Avenue → Washington Street), never a real person's
  name. Short cross streets in Ocean Beach and Washington Beach are numbered ("5th Street").
- **Off every road** (parks, beach, interiors, water) there's no street line, only the area name.
- **No game data in the repo:** only names and coordinates. Geometry always comes from the player's
  `paths.ipl` at bake time.
- **Look:** the game's own fonts and colours, matching the HUD's other bottom-right text.
- **Setting:** `StreetNames=1` in `vcmodernhud.ini`, on by default. With 0, or with an old `map.bin`, the
  game's own area name draws as now.
- **Quality bar:** every new test passes under `make test` and the sanitizers, and `make all` has no
  warnings.

## Review Focus

1. **Bridges over streets:** standing on one, the street below must not show. Pinned in Task 5,
   `TestOverpassPicksByHeight`.
2. **Standing at a crossroads on foot:** the name must not flip between the two streets. Pinned in
   Task 5, `TestCrossroadsKeepsCurrent`.
3. **Driving against a segment's direction, or reversing:** the street still matches; the angle test
   ignores direction. Pinned in Task 5, `TestReverseHeadingMatches`.
4. **A short unnamed stretch** (a car park entrance, a few metres of service road) between two pieces of
   the same street: no new banner on the return. Pinned in Task 6, `TestBriefGapNoRebanner`.
5. **An old `map.bin`** (install not re-run): nothing breaks, and the game's area name shows as before.
   Pinned in Task 1, `TestLoadsVersion4`, and Task 7, `TestOldMapLeavesGameAreaName`.

---

### Task 1: `map.bin` version 5: street names, a street per road, road heights

**Files:**
- Modify: `src/mapdata.h` (`Map`), `src/mapdata.cpp` (`Load`, `Save`, `kMagic` → `VCMMMAP5`)
- Test: `tests/test_mapstreets.cpp` (new; a Makefile rule like `test_mapheights`, added to the test list)

**Interfaces:**
- Produces, in `Map`:
  - `std::vector<std::string> streets;` (the names)
  - `std::vector<uint16_t> roadStreet;` (one per road; `kNoStreet = 0xffff`)
  - `std::vector<float> roadZ;` (two per road: the heights at `a` and `b`)
  - `static constexpr uint16_t kNoStreet`
- **Layout:** version 5 appends a section after the roads: a name count, then each name as a `uint8`
  length and its bytes, then `roadStreet`, then `roadZ`. Versions 3 and 4 still load: no streets,
  `roadStreet` all `kNoStreet`, `roadZ` all 0.

- [ ] **Step 1: Write the failing tests:** `TestRoundTrip` (2 roads, 2 names, one road unnamed, its
  heights kept) and `TestLoadsVersion4` (a v4 file loads; `streets` is empty and `roadStreet` is all
  `kNoStreet`).
- [ ] **Step 2: Run them:** `make build/test_mapstreets && ./build/test_mapstreets`. Expected: a FAIL to
  compile (no `streets` member).
- [ ] **Step 3: Implement the section in `Map::Save` and `Map::Load`.**
- [ ] **Step 4: Run them, and the existing map tests:** `make test_mapstreets test_mapheights test_mapassets`.
  Expected: all pass.
- [ ] **Step 5: Commit:** `git commit -m "map.bin v5: street names, a street and heights per road"`.

### Task 2: The baker snaps street outlines onto roads

**Files:**
- Create: `tools/bake/streets.h`, `tools/bake/streets.cpp`
- Modify: `tools/bake/bake.cpp`. `Roads()` also fills `roadZ` from `RoadSegment::a.z`/`b.z`. Main reads
  `assets/streets.txt` when given `--streets <file>` and fills `streets`/`roadStreet`. `install.sh` passes
  the repo's `assets/streets.txt`.
- Test: `tests/test_streetsnap.cpp` (new; links `tools/bake/streets.cpp`)

**Interfaces:**
- Produces:
  - `struct Outline { std::string name; std::vector<Vec2> points; };`
  - `bool ParseStreets(const std::string &text, std::vector<Outline> &out, std::string &error);`
    - Format: one street per line, `"Name" x,y x,y ...`
    - `#` comments and blank lines are skipped.
    - The error names the line number.
  - `std::vector<uint16_t> Assign(const std::vector<Outline> &, const std::vector<mapdata::Road> &);`
    - A road gets the outline whose nearest stretch is within `kSnapDistance = 12` m of the road's
      midpoint and within `kSnapAngle = 30°` of the road's direction (ignoring which way each runs).
    - The nearest such outline wins. With none, the road gets `kNoStreet`.

- [ ] **Step 1: Write the failing tests:**
  - `TestParse`: a comment, two streets, a quoted name with an apostrophe.
  - `TestParseError`: a missing quote gives an error naming line 3.
  - `TestParallelRoadSnaps`
  - `TestCrossStreetNotSnapped`: a perpendicular road 5 m away stays unnamed.
  - `TestNearestOutlineWins`
  - `TestFarRoadUnnamed`: 20 m away.
  - `TestDirectionIgnored`: an outline drawn against the road's direction still snaps.
- [ ] **Step 2: Run them.** Expected: a FAIL to compile.
- [ ] **Step 3: Implement `ParseStreets` and `Assign`.** Use a uniform grid over the roads so assigning
  ~3,900 roads against ~200 outlines stays fast.
- [ ] **Step 4: Run them.** Expected: all pass. Then bake from the real game files with an empty streets
  file: `make all && ./build/vcmm-bake "<GameData>" /tmp/out.bin --streets assets/streets.txt`. Expected:
  "0 of 3878 roads named".
- [ ] **Step 5: Commit.**

### Task 3: Coverage pictures and report

**Files:**
- Modify: `tools/bake/bake.cpp`. A `--streets-debug <dir>` mode writes one PNG per navigation area,
  reusing the `--area` renderer: roads coloured by street, unnamed ones red, each street's name at its
  middle. It prints, per area, named road length against total road length.
- Modify: `tools/bake/streets.h/.cpp`
- Test: `tests/test_streetsnap.cpp`

**Interfaces:**
- Produces: `struct Coverage { float named, total; }; Coverage Measure(const std::vector<mapdata::Road> &, const std::vector<uint16_t> &street, Vec2 lo, Vec2 hi);`

- [ ] **Step 1: Write the failing test:** `TestCoverageCountsLength` (two roads, 10 m named and 30 m
  unnamed, gives 10 of 40).
- [ ] **Step 2: Run it.** Expected: FAIL.
- [ ] **Step 3: Implement `Measure` and the debug mode.**
- [ ] **Step 4: Run it.** Expected: PASS. Then run the debug mode on the real files: 12 PNGs written, and
  a report printed.
- [ ] **Step 5: Commit.**

### Task 4: Author the street names (content), then the user reviews them

**Files:**
- Create: `assets/streets.txt`

- [ ] **Step 1: Place the canonical names** from Global Constraints. Find where those places are:
  - from the mission script (the Ocean View hotel; the DBP Security and chef targets in their missions'
    coordinates)
  - from the 1102 Washington Street safehouse pickup
- [ ] **Step 2: Name every other drivable street** in altered-Miami style, numbering Ocean Beach's and
  Washington Beach's cross streets. Use the Task 3 pictures. Target: at least 95% of road length named
  on the main islands, car parks and alleys excepted.
- [ ] **Step 3: Rebake with `--streets-debug`,** and fix the red stretches that are real streets.
- [ ] **Step 4: Review gate.** Give the user the list (each name, its area, and the 12 pictures). Do not
  go on to Task 7's install until the user approves it, edits included.
- [ ] **Step 5: Commit the approved list.**

### Task 5: The street tracker (pure)

**Files:**
- Create: `src/streetmodel.h`, `src/streetmodel.cpp`
- Test: `tests/test_streetmodel.cpp` (new)

**Interfaces:**
- Consumes: Task 1's `Map::roads`, `roadStreet`, `roadZ`, `kNoStreet`.
- Produces:
  - `class Index { public: explicit Index(const mapdata::Map &); };` (a grid of 64 m cells)
  - `int StreetAt(const Index &, const mapdata::Map &, Vec3 pos, Vec2 heading, bool driving);`
    - On foot: the nearest named road within its half-width plus 6 m.
    - Driving: as on foot, but only roads within 35° of the heading, either way.
    - Either way, only roads whose height at the nearest point is within 4 m of `pos.z`.
    - Returns -1 for none.
  - `class Tracker { public: int Update(int seen, Vec3 pos, float dt); int Current() const; };`
    - A new street replaces the current one after 1 s or 15 m on it.
    - -1 (no road) replaces it after 2 s.

- [ ] **Step 1: Write the failing tests:**
  - `TestOnFootNearest`
  - `TestOffRoadNone`: 20 m off the road in a park.
  - `TestDrivingUsesHeading`: at a T-junction, heading east picks the east–west street.
  - `TestReverseHeadingMatches`
  - `TestOverpassPicksByHeight`: a bridge 8 m above a street; at the street's height, the street; on
    the bridge, the bridge.
  - `TestCrossroadsKeepsCurrent`: alternating between two equidistant streets for 3 s keeps the first.
  - `TestSwitchAfterSecond`
  - `TestSwitchAfter15m`
- [ ] **Step 2: Run them.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: all pass.
- [ ] **Step 5: Commit.**

### Task 6: The banner's timing (pure)

**Files:**
- Modify: `src/streetmodel.h/.cpp`
- Test: `tests/test_streetmodel.cpp`

**Interfaces:**
- Produces: `class Banner { public: void Update(int street, int area, bool gotIntoVehicle, bool expandedMap, float dt); float Alpha() const; };`
  - `area` is any value that changes when the area does. Task 7 passes a hash of the area name's text.
  - It shows (alpha rising to 1 over 0.25 s) when the street or the area changes, or he gets into a
    vehicle.
  - It holds for 4 s, then fades over 0.6 s.
  - It stays at 1 while the expanded map is up.
  - Returning to the street it last showed, within 3 s of leaving it, doesn't re-show it.

- [ ] **Step 1: Write the failing tests:**
  - `TestShowsOnNewStreet`
  - `TestFadesAfterHold` (alpha is 1 at 3.9 s and 0 at 4.7 s)
  - `TestShowsOnAreaChange`
  - `TestShowsOnVehicleEntry`
  - `TestExpandedHolds`
  - `TestBriefGapNoRebanner`
- [ ] **Step 2: Run them.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them.** Expected: all pass.
- [ ] **Step 5: Commit.**

### Task 7: Drawing it in the HUD

**Files:**
- Create: `src/streethud.h`, `src/streethud.cpp`
- Modify:
  - `src/minimap.h/.cpp`: expose `const mapdata::Map *BakedMap()` (null without a v5 map) and
    `bool Expanded()`; call `streethud::Draw()` in the HUD frame after `hud`'s.
  - `src/hud.h/.cpp`: `void SuppressAreaName(bool)`. While it's set, the `kZone` scale case uses 0, so
    the game's own text is drawn invisibly.
  - `src/config.h/.cpp`: `streetNames`, INI key `StreetNames`.
  - The Makefile (plugin sources and the new test).
- Test: `tests/test_streethud.cpp` (new; a fixture with fake game memory, as in `test_scanner.cpp`)

**Interfaces:**
- Consumes:
  - Task 5's `Index`, `StreetAt` and `Tracker`; Task 6's `Banner`.
  - `CHud::m_pZoneName` (bound with `dlsym "_ZN4CHud11m_pZoneNameE"`; a pointer to the area name's
    16-bit text).
- Produces: `bool streethud::Install(); void streethud::Draw();`
- **How it draws:**
  - Bottom right, right-justified, where the game's area name sits.
  - The street on top, in the game's font 1, at the size of the HUD's other bottom-right text. The area
    name under it, at 0.8x of that.
  - Both at the banner's alpha. Nothing drawn at alpha 0.
  - While `StreetNames=1` and a v5 map is loaded, `SuppressAreaName(true)`.

- [ ] **Step 1: Write the failing tests:**
  - `TestSuppressesWhileActive`
  - `TestOldMapLeavesGameAreaName`: no v5 map means no suppression and nothing drawn.
  - `TestSettingOffLeavesGameAreaName`
  - `TestNoStreetShowsAreaOnly`: off-road, the banner shows only the area line.
- [ ] **Step 2: Run them.** Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them, then the full suite and the build:** `make -j8 -k test` passes, and `make all`
  has 0 warnings.
- [ ] **Step 5: Commit.**

### Task 8: Install and check in play

- [ ] **Step 1: Install and check startup:**
  - `./install.sh` bakes with the approved `assets/streets.txt`.
  - The startup log shows "map data: … N streets" and no failure lines.
- [ ] **Step 2: In play (the user):**
  - Ocean Drive along the beach front, and outside the Ocean View hotel.
  - Washington Street at 1102.
  - A crossroads on foot and in a car.
  - A bridge over a street.
  - A park and the beach (area name only).
  - The expanded map (D-pad down).
- [ ] **Step 3: Update #35.** Close it once the user accepts.
