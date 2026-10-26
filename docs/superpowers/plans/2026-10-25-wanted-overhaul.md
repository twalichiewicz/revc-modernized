# Wanted Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Vice City's all-knowing police with witnesses, descriptions, a search, an APB with hollow stars, and police helicopters with SWAT door gunners (and the Hunter at six stars).

**Architecture:** A pure, unit-tested model (`heat`) holds what the police know and the state machine. A game bridge (`wanted`) feeds it from the game every frame and keeps the game's own wanted level in step with it. Sight, witnesses, the HUD, the police (cars, units, foot cops) and the helicopters read the bridge (`wanted::Target()`, not the player's real position) and report back to it. Only the police helicopter needs a new hook; everything else runs from hooks the mod already has (`CCamera::Process`, `CPad::Update`, `CPed::PreRender`, `CPed::KillCharOnFootArmed`) or by writing game state.

**Tech Stack:** C++17, clang (xcrun), the mod's own build (`make`, `make test`, `./install.sh`), game functions by `dlsym(RTLD_MAIN_ONLY, …)`, reVC source (`gh api repos/mrxenginner/reVC/contents/src/<path> -H "Accept: application/vnd.github.raw"`) for logic, `otool -tV -p <symbol>` on the game binary for offsets.

**Spec:** `docs/superpowers/specs/2026-10-25-wanted-overhaul-design.md`

## Global Constraints

- Game binary: macVC 1.0.5217 at `/Applications/Grand Theft Auto Vice City.app/Contents/MacOS/macVC` (arm64, exported symbols). Every offset this plan gives was read from that binary; any new one is read the same way before use.
- Hooks: `tools/patch_macvc.py` fits 73 patch sites and 69 are used. This plan adds exactly one (`CHeli::ProcessControl`, Task 8). No other new hooks.
- Never keep a raw game pointer across ticks: store pool handles (`CPools::GetPedRef` / `GetVehicleRef`) and resolve them with `api.PoolsGetPed` / `api.PoolsGetVehicle` each tick (the class of bug behind the pickup crash).
- Settings in `vcmodernhud.ini`: `RealisticWanted=1` (Tasks 1–7) and `HeliGunners=1` (Tasks 8–9), both on by default; off gives the game's behaviour back.
- Log state changes and a 30 s summary to `vcmodernhud.log` (via `Log()`), in the mod's plain-English style.
- Code style: match the surrounding files (namespaces per module, an anonymous namespace for internals, tabs, comments that say what and why, constants named `kName`, globals `gName`).
- `install.sh` refuses while the game runs (`pgrep -f "Vice City.app/Contents/MacOS/macVC"`); queue it with `while pgrep -f "Vice City.app/Contents/MacOS/macVC" >/dev/null; do sleep 5; done; ./install.sh` if it is.
- Commits: the user commits when they ask. Each task ends at a checkpoint (build, tests, install) instead of a commit.
- The agent can't launch the game. Each checkpoint lists what the user should see in play and the log lines that confirm it.

## Review Focus

- A save loaded, or a long time jump, mid-chase: everything clears (no stale APB, description or stars). Pinned in Task 1 (`TestClearForgetsEverything`) and Task 2 (time-jump detection, as `police.cpp` does).
- Mission scripts that set, lower or clear the stars, including under the APB: the mod follows them; a script clear never leaves an APB or brings stars back. Pinned in Task 1 (`TestLoweredFollowsScript`, `TestClearForgetsEverything`) and Task 2 (classification of drops).
- Peds and vehicles the mod tracks (cops with eyes, witnesses, door gunners, the Hunter) removed by the game: handles are resolved every tick and dropped when gone. Pinned in Tasks 3, 7, 8 and 9 (each keeps handles only).
- Cutscenes, NoPolice zones and the "ignored by cops" flags: the model pauses during cutscenes and clears in a NoPolice zone or while ignored. Pinned in Task 2.
- Wasted or busted while under the APB or searching: everything clears and no stars come back after the respawn. Pinned in Task 2 (a drop to 0 with no respray or outfit change is a clear).

---

## File Structure

- Create `src/heat.h`, `src/heat.cpp`: the model (no game dependencies). Tested by `tests/test_heat.cpp`.
- Create `src/wanted.h`, `src/wanted.cpp`: the game bridge (wanted level sync, Tommy's looks, saves, respray and clothes, bribes, settings, log).
- Create `src/sight.h`, `src/sight.cpp`: who can see Tommy, eye meters, recognition.
- Create `src/wantedhud.h`, `src/wantedhud.cpp`: status icons right of the minimap, eyes over cops, the search area on the minimap.
- Create `src/witness.h`, `src/witness.cpp`: crime interception, payphone runs, rich NPCs' phones, car alarms.
- Create `src/hunter.h`, `src/hunter.cpp`: the Hunter at six stars.
- Modify `src/hud.cpp` (star style), `src/minimap.cpp` (install, draw calls), `src/camera.cpp` (per-frame calls), `src/police.cpp` (target and search), `src/combat.cpp` (blind gunmen, attached peds), `src/heli.cpp` (hook, gunners, second helicopter), `src/config.h`, `src/config.cpp`, `tools/patch_macvc.py`, `Makefile`, `README.md`.

---

### Task 1: The heat model

**Files:**
- Create: `src/heat.h`, `src/heat.cpp`
- Create: `tests/test_heat.cpp`
- Modify: `Makefile` (a `$(BUILD)/test_heat` target added to `test`, like `test_turns`)

**Interfaces:**
- Consumes: `Vec3` from `src/types.h`.
- Produces:

```cpp
namespace heat {
enum class State : uint8_t { Clear, Reported, Wanted, Searching, Apb };
struct Looks { std::string outfit; int vehicleModel = -1; int colour1 = -1, colour2 = -1; }; // -1: on foot
struct Description {
	std::string outfit; int vehicleModel = -1, colour1 = -1, colour2 = -1;
	bool haveOutfit = false, haveVehicle = false;
};
constexpr int kMaxStars = 6;
constexpr uint32_t kLostAfterMs = 4000;
constexpr uint32_t kSearchMs[kMaxStars + 1] = {0, 20000, 30000, 40000, 50000, 60000, 75000};
constexpr float kEyeDecayPerSecond = 0.35f;
float SearchRadius(int stars);                                       // 60 + 25 * stars (m)
float FillRate(float distance, bool matches, bool helicopter);       // eye fill per second
class Model {
public:
	State state() const;
	int stars() const;          // shown: filled, dimmed while searching, hollow under the APB
	int gameStars() const;      // the game's wanted level should be: stars while Wanted/Searching, else 0
	int escapedStars() const;
	const Description &description() const;
	Vec3 lastSeen() const;  uint32_t lastSeenAt() const;
	Vec3 searchCentre() const;  float searchRadius() const;  uint32_t searchSince() const;
	bool Matches(const Looks &looks) const;         // outfit or vehicle on the description and the same now
	bool OutfitStruck(const Looks &looks) const;    // on the description but he's changed it
	bool VehicleStruck(const Looks &looks) const;   // in another vehicle, or the same one resprayed
	void CopSees(const Looks &looks, Vec3 at, uint32_t now, int gameStars);
	void Reported(const Looks *looks, Vec3 at, uint32_t now, int stars); // looks null: location only
	void Calling(bool anyone);
	void Tick(Vec3 him, uint32_t now);
	void Lowered(int stars);
	void Clear();
};
}
```

Rules the implementation follows (from the spec; the numbers above are this plan's tuning):
- `CopSees`: state becomes Wanted; `stars = max(stars, gameStars, state was Apb ? escapedStars : 0)`; the description becomes `looks` (`haveOutfit = true`; the vehicle part is replaced only when `looks.vehicleModel >= 0`, so on foot the last car seen stays on it); `lastSeen = at`, `lastSeenAt = now`.
- `Reported` with looks: as `CopSees` but `stars = max(stars, stars arg)`. Without looks: state becomes Searching (unless already Wanted) with `searchCentre = at`, `searchSince = now`, radius `SearchRadius(stars)`, and no description added.
- `Tick`: Wanted and `now - lastSeenAt > kLostAfterMs` → Searching (centre `lastSeen`, since `now`). Searching and `now - searchSince > kSearchMs[stars]` and Tommy further than the radius from the centre → Apb (`escapedStars = stars`) if the description has anything, else Clear.
- `VehicleStruck(looks)`: `haveVehicle` and `looks.vehicleModel >= 0` and (model or either colour differs). On foot it's not struck (back in the same car, it matches).
- `Calling(true)` makes `state()` report Reported only while Clear.
- `Lowered(n)`: `stars = min(stars, n)`; 0 → `Clear()`.
- `FillRate(d, matches, heli)`: with `heli`, `d /= 2.5`. Matching: `1/1.2` per second at `d <= 15`, falling linearly to `1/5` at `d = 70`, `0` beyond 70. Not matching: `1/3` at `d <= 5`, falling linearly to `0` at `d = 10`; a helicopter never recognises without a match.

- [ ] **Step 1: Write the failing tests** in `tests/test_heat.cpp` (the `CHECK` macro and `main` as in `tests/test_turns.cpp`; "player"/"play2" outfits, car model 145 colours 1/2; `near(a, b)` = within 0.001):

```cpp
TestCopSeesMakesWanted:   m.CopSees({"player"}, {0,0,0}, 1000, 2); CHECK(m.state()==State::Wanted && m.stars()==2 && m.gameStars()==2 && m.description().haveOutfit && m.description().outfit=="player");
TestLostAfterFourSeconds: after the above, m.Tick({0,0,0}, 5000) → Wanted; m.Tick({0,0,0}, 5001) → Searching; near(m.searchRadius(), 110);
TestSearchEndsIntoApb:    ... Searching since 5001 at 2 stars; m.Tick({200,0,0}, 35002) → Apb, escapedStars()==2, gameStars()==0, stars()==2;
TestSearchHoldsInsideArea:same, but m.Tick({50,0,0}, 35002) → Searching;
TestLocationOnlyEndsClear:m.Reported(nullptr, {0,0,0}, 0, 1) → Searching, !description().haveOutfit; m.Tick({200,0,0}, 20001) → Clear;
TestWitnessWithLooks:     Looks l{"player"}; m.Reported(&l, {0,0,0}, 0, 1) → Wanted, description().outfit=="player";
TestRecognisedUnderApb:   (Apb with escapedStars 3) m.CopSees({"play2"}, {0,0,0}, t, 0) → Wanted, stars()==3, gameStars()==3, description().outfit=="play2";
TestNewCrimeUnderApb:     (Apb with escapedStars 2) m.CopSees({"player"}, {0,0,0}, t, 4) → stars()==4;
TestOutfitStruck:         description outfit "player": !m.Matches({"play2"}) && m.Matches({"player"}) && m.OutfitStruck({"play2"});
TestResprayedCarStruck:   description car 145 1/2: m.VehicleStruck({"play2",145,3,2}) && !m.VehicleStruck({"play2",-1,-1,-1}) && m.Matches({"play2",145,1,2});
TestOnFootKeepsCar:       m.CopSees({"player",145,1,2},…); m.CopSees({"player",-1,-1,-1},…); CHECK(m.description().haveVehicle && m.description().vehicleModel==145);
TestLoweredFollowsScript: Wanted at 3; m.Lowered(1) → stars()==1; m.Lowered(0) → Clear;
TestClearForgetsEverything: from Apb, m.Clear() → Clear, stars()==0, escapedStars()==0, !description().haveOutfit && !haveVehicle;
TestCallingShowsReported: m.Calling(true) → Reported; m.CopSees(…,1) → Wanted (Calling doesn't override); m.Clear(); m.Calling(false) → Clear;
TestFillRates:            near(FillRate(10,true,false), 1/1.2f); near(FillRate(70,true,false), 0.2f); FillRate(71,true,false)==0; near(FillRate(4,false,false), 1/3.f); FillRate(10,false,false)==0; near(FillRate(25,true,true), 1/1.2f); FillRate(4,false,true)==0;
```

- [ ] **Step 2: Run them to see them fail**

Run: `make test`
Expected: the build of `test_heat` fails (`heat.h` not found).

- [ ] **Step 3: Implement `heat::Model`, `SearchRadius`, `FillRate`** in `src/heat.cpp` by the rules above.

- [ ] **Step 4: Run the tests**

Run: `make test`
Expected: `all projection tests passed`, `all turn tests passed`, `all heat tests passed` (print that line from `test_heat` on success, as the others do).

- [ ] **Step 5: Checkpoint** — `make` builds with no warnings (heat isn't in the dylib yet; add `src/heat.cpp` to `PLUGIN` in this task so Task 2 links). No commit.

---

### Task 2: The game bridge, hollow stars and the game's star wipes

**Files:**
- Create: `src/wanted.h`, `src/wanted.cpp`
- Modify: `src/hud.cpp` (`DrawWanted`), `src/camera.cpp` (`Hook_Process`), `src/minimap.cpp` (install), `src/config.h`, `src/config.cpp`, `Makefile`, `README.md`

**Interfaces:**
- Consumes: `heat::Model` (Task 1).
- Produces:

```cpp
namespace wanted {
bool Install();                     // dlsym only; logs what's missing; false leaves the game's wanted system alone
void Update();                      // each frame, first thing after orig in camera.cpp's Hook_Process (before police::Update)
bool Active();                      // installed, RealisticWanted on
const heat::Model &Model();
heat::Looks Looks();                // Tommy now
CVector Target();                   // where the police think he is (Task 3 refines it)
void CopSees(const CVector &at);    // sight.cpp: a full eye, or a cop sees him while Wanted
void Reported(bool sawHim, const CVector &at, int stars); // witness.cpp
enum class StarLook : uint8_t { Filled, Dim, Hollow };
int ShownStars();  StarLook Look();
}
```

Decisions:
- `Looks()`: the outfit is the player model's name, `reinterpret_cast<const char *>(api.modelInfoPtrs[0] + 8)` (`CPed::Undress` loads a special model into model 0; `game.cpp`'s `WeaponIcon` reads names at `+8` the same way). In a vehicle: its model (`EntityModel`) and colours at `CVehicle+0x210`/`+0x211` (`CAutomobile` constructor: `add x1, x19, #0x210; add x2, x19, #0x211` into `ChooseVehicleColour`).
- The game's level: `CWanted` at `player+0x778`, level `+0x20`, ignored flags `+0x1e` bits 0–1. Set it with `CWanted::SetWantedLevel(int)` (`_ZN7CWanted14SetWantedLevelEi`, called as `void(*)(uint8_t *wanted, int)`). Keep `gExpected`, the level last seen or set.
- Each frame, the game's level `g` against `gExpected`:
  - up → `Model.CopSees(looks, him, now, g)` (a cop within 14 m of a crime: `CEventList::ReportCrimeForEvent` registers it immediately; a mission script setting stars counts the same).
  - down, and (the player's vehicle colours changed since last frame, or `CGarages::IsPointWithinAnyGarage(him)`) → a respray: `SetWantedLevel(gExpected)` back. The car no longer matches, which the model works out from `Looks()`.
  - down, and the outfit name changed within the last 2 s → a clothes change: `SetWantedLevel(gExpected)` back.
  - down otherwise → a script, the cheat, wasted or busted: `g == 0` → `Model.Clear()`, else `Model.Lowered(g)`.
  - after `Model.Tick`, whenever `Model.gameStars() != g` (into the APB: 0; recognised under it: the escaped stars) → `SetWantedLevel(Model.gameStars())`, `gExpected` = that.
- Load or time jump (`now < gLastTime || now - gLastTime > 10000`, as `police::Update` does) → `Model.Clear()`, `gExpected = g`. During a cutscene (`CCutsceneMgr::ms_running`) → skip the frame. In a NoPolice zone (`CCullZones::CurrentFlags_Player & 0x10`) or while ignored → `Model.Clear()`.
- A save: while the state is Apb, once a second, `stat` `GTAVCsf1.b`…`GTAVCsf8.b` in the support directory (`minimap.cpp`'s `SupportDir()`); a modification time newer than when the APB began → `Model.Clear()`.
- Bribes off the map: every 2 s, for each pickup `p` (`Pickup(i)`, `kNumPickups`) with type (`p[0x36]`) non-zero, not removed (`!p[0x37]`), and model (`int16` at `+0x2a`) equal to `*MI_PICKUP_BRIBE` (`dlsym "MI_PICKUP_BRIBE"`, `int16`): `CPickups::RemovePickUp(i | (uint16 at +0x2c) << 16)` (`_ZN8CPickups12RemovePickUpEi`). Log how many on the first removal.
- `hud.cpp` `DrawWanted`: when `wanted::Active()`, draw `wanted::ShownStars()` stars instead of the game's level, and keep them up (not faded) under the APB. Filled as now. Dim: alpha × 0.45. Hollow: only the outline pass, drawn in the star colour instead of black, no fill.
- Settings: `bool realisticWanted = true;` in `config.h`; `RealisticWanted=1` in `config.cpp`'s default ini text and parser, with the comment "police know you by what they've seen: witnesses, your clothes and car, a search, an APB until you save".
- Log: "wanted: …" on every state change (`Clear → Wanted (3 stars, seen at x,y)`, `→ Searching (centre x,y, radius r)`, `→ APB (escaped at n stars)`, `respray: stars kept`, `clothes: stars kept`, `cleared by the game (script, cheat, wasted or busted)`, `saved: APB cleared`), and once 30 s while not Clear a line with the state, stars, description and what matches now.

- [ ] **Step 1: Implement `wanted.cpp`** as above; `Target()` returns Tommy's real position for now.
- [ ] **Step 2: Wire it** — `wanted::Install()` in `minimap.cpp` after `police::Install()`; `wanted::Update()` in `camera.cpp`'s `Hook_Process` right after `orig_Process`; the star look in `hud.cpp`; `src/wanted.cpp` in `PLUGIN`.
- [ ] **Step 3: Build and test**

Run: `make 2>&1 | grep -E "error|warning"; make test`
Expected: no output from the grep; the three "all … tests passed" lines.

- [ ] **Step 4: Install** (`./install.sh`, or queued if the game runs). Expected: `==> Installed.`
- [ ] **Step 5: README** — under "### Police": stars go hollow when you escape (an APB) until you save at a safehouse; changing clothes and Pay 'n' Spray no longer wipe stars; police bribes are gone.
- [ ] **Step 6: Checkpoint for the user to play** — get 2 stars, escape: the stars dim after 4 s out of sight and go hollow after the search (log `wanted: … → APB`); a Pay 'n' Spray with stars keeps them (log `respray: stars kept`); saving clears the hollow stars (log `saved: APB cleared`); no bribe stars on the map (log the count removed). Police still home in at this stage. No commit.

---

### Task 3: Sight and the eye

**Files:**
- Create: `src/sight.h`, `src/sight.cpp`
- Modify: `src/wanted.cpp` (call sight, `Target()`), `Makefile`

**Interfaces:**
- Consumes: `wanted::Model()`, `wanted::Looks()`, `wanted::CopSees` (Task 2); `heat::FillRate`, `heat::kEyeDecayPerSecond` (Task 1).
- Produces:

```cpp
namespace sight {
struct Eye { int handle; bool helicopter; CVector head; float fill; }; // handle: a ped's, or -1 - heli slot
void Update(float dt);                // from wanted::Update, every frame
bool Seen();                          // a cop or helicopter sees him this frame
const std::vector<Eye> &Eyes();       // those filling (fill > 0), for the HUD
}
```

Decisions:
- Lookers: peds of type cop (`PedType == 6`), alive, on foot or in a law vehicle (from the vehicle use its driver's eyes at the vehicle's position + 1.2 m); and the police helicopters (`CHeli::pHelis`, 4 slots, `dlsym _ZN5CHeli6pHelisE`), their position.
- A ped sees Tommy if he's within 90 m, within 75° of its facing (`EntityForward`; a driver: the vehicle's forward, 100°), and `api.GetIsLineOfSightClear(eyes, his middle, buildings=true, vehicles=false, peds=false, objects=true, …)`. A helicopter: within 225 m, any direction, same line test.
- While `Model().state()` is Wanted, anyone who sees him calls `wanted::CopSees(him)` at once (they're on him; no meter).
- Otherwise (Searching, Apb, or Clear after a location-only report — any state but Clear-with-nothing), each looker's eye fills by `FillRate(distance, Model().Matches(looks), helicopter) * dt` and decays by `kEyeDecayPerSecond * dt` while it can't see him; full (≥ 1) → `wanted::CopSees(him)`, all eyes reset. No eyes while Clear and nothing reported.
- Committing a crime in a cop's view is already immediate (Task 2's "up" path).
- Eyes are kept by handle (`gPedRef`), resolved every frame, dropped when the ped is gone.
- `wanted::Target()`: his real position while `Seen()` or within `kLostAfterMs` of `lastSeenAt` while Wanted; otherwise `lastSeen` (Wanted) or `searchCentre` (Searching).
- Log the first recognition of each chase: "sight: recognised by a cop at d m (matched outfit/car | a close look)".

- [ ] **Step 1: Implement `sight.cpp` and `Target()`.**
- [ ] **Step 2: Build and test** — `make 2>&1 | grep -E "error|warning"; make test` → no grep output, three passed lines.
- [ ] **Step 3: Install.** Expected `==> Installed.`
- [ ] **Step 4: Checkpoint** — with hollow stars, walking past a cop fills recognition (no HUD yet: log `sight: recognised …`) and the stars return filled. No commit.

---

### Task 4: The HUD

**Files:**
- Create: `src/wantedhud.h`, `src/wantedhud.cpp`
- Modify: `src/minimap.cpp` (draw calls), `src/hud.cpp` (`DrawPills` raised above the icon row), `Makefile`, `README.md`

**Interfaces:**
- Consumes: `wanted::Model()`, `wanted::Looks()` (Task 2), `sight::Eyes()` (Task 3), `witness::Calling()` and `witness::Runners()` (Task 7; until then, stubs returning `0.0f` and an empty list, declared in `witness.h` in this task).
- Produces:

```cpp
namespace wantedhud {
float RowHeight(float mapH);   // the icon row's height (0 when no icons), for DrawPills
void Draw(Canvas &canvas, float mapX, float mapY, float mapW, float mapH); // icons right of the map
void DrawEyes(Canvas &canvas); // after the HUD, before the font flush
void DrawSearchArea(Canvas &canvas); // on the minimap, from minimap.cpp's map drawing (gProj.GroundToScreen)
}
```

Decisions (from the approved mockup, `.superpowers/brainstorm/90508-1792992096/content/wanted-states.html`):
- Row of 34-px-at-1080p circles (scale with `ScreenHeight()/1080`), white glyphs on `{12,14,20,184}` discs with a white 1.5-px ring, left to right from the map's right edge + 8 px, bottom aligned with the map's bottom: phone (red ring `{226,75,75}`, its arc = `witness::Calling()` 0..1) while calling; magnifier while Searching; badge while Apb; outfit and car while either is on the description, each at 38% alpha with a red diagonal when struck (`OutfitStruck`/`VehicleStruck`).
- Glyphs drawn from triangles in `wantedhud.cpp`, as `places.cpp` draws its icons (`Disc`, `Rect`, fans): shirt, car, phone, magnifier, badge (12-point star), eye.
- Eyes: for each `sight::Eyes()` entry, `CSprite::CalcScreenCoors` (`_ZN7CSprite15CalcScreenCoorsERKN2rw4V3dEPS1_PfS4_b`) of `head + 0.6 m`; on screen → a 34-px disc with the eye glyph and a yellow `{240,192,64}` arc for `fill`; off screen or behind the camera → the same at the screen edge along the direction to it, 24 px in.
- Search area: while Searching, a translucent blue `{80,130,255,46}` disc with a dashed `{120,170,255,242}` rim of `searchRadius()` around `searchCentre()` on the minimap.
- `hud::DrawPills` lifts its stack by `wantedhud::RowHeight(mapH) + 6 px`.

- [ ] **Step 1: Implement and wire** (`minimap.cpp`: `wantedhud::Draw` beside `hud::DrawPills`, `DrawSearchArea` with the map's other overlays, `DrawEyes` after `hud::DrawWanted`).
- [ ] **Step 2: Build and test** — no warnings; three passed lines.
- [ ] **Step 3: Install.**
- [ ] **Step 4: README** — the icons, the eye.
- [ ] **Step 5: Checkpoint** — the user sees the four states as in the mockup, eyes over cops filling, the search area on the minimap. No commit.

---

### Task 5: No homing — police cars and units

**Files:**
- Modify: `src/police.cpp`, `README.md`

**Interfaces:**
- Consumes: `wanted::Active()`, `wanted::Target()`, `wanted::Model()`, `sight::Seen()`.
- Produces: `CVector police::SearchPoint(int slot)` (internal) — a road node within the search radius of `searchCentre()`, different per slot.

Decisions:
- In `police::Update`, every use of the player's position (`him`) for police decisions takes `wanted::Target()` when `wanted::Active()`: `gToHim` regrowth, `Dispatch`, `Units`, `Arrive`, `Perimeter` (its centre), `PullUp`. The real position stays only for what isn't police knowledge: despawn distances and "unseen from the camera" spawning tests.
- While Searching and not `sight::Seen()`: every law car with a cop driver in a chase mission (`kRamFar`..`kBlockHandbrakeStop`) is sent to a search point: `car[kMission] = 8` (`MISSION_GOTOCOORDS`; `CCarAI::UpdateCarAI` has its own case for it and doesn't put it back on the chase), destination (`CVector` at `AutoPilot+0x44`, i.e. `car + 0x1b4`) = the point, then `CCarCtrl::JoinCarWithRoadSystemGotoCoors(car, point, false)` (`_ZN8CCarCtrl30JoinCarWithRoadSystemGotoCoorsEP8CVehicle7CVectorb`). Within 15 m of its point it gets the next one. Cruise speed: `gChaseSpeed(car)`.
- Search points: the car path nodes inside the circle, chosen at random per car, re-chosen on arrival; at most 3 cars share one 40-m cell.
- When he's seen again, the game's own `UpdateCarAI` puts them back on the chase the next frame (GOTOCOORDS ends at the point; Wanted gives the chase).
- The roadblock ring's `SetUp` uses `searchCentre()` while Searching.
- Log once per search: "police: searching n cars round x,y (r m)".

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build and test** — no warnings; three passed lines.
- [ ] **Step 3: Install.**
- [ ] **Step 4: Checkpoint** — out of sight, police cars drive to where they last saw him and spread round the area instead of following him; roadblocks form round that spot. No commit.

---

### Task 6: No homing — cops on foot

**Files:**
- Modify: `src/combat.cpp`, `src/police.cpp`

**Interfaces:**
- Consumes: `wanted::Active()`, `wanted::Target()`, `sight::Seen()`; `police::SearchPoint` (Task 5, made non-static and declared in `police.h`).

Decisions:
- `combat.cpp`: when `wanted::Active()` and the gunman can't see Tommy, `Round()` seeks `wanted::Target()` instead of his position, and `Plan`'s distances use it as "him" (a blind gunman closes on where he was, not where he is).
- `combat.cpp` `Tactical()`: false for a ped attached to something (`*(void **)(ped + 0x720)`, `CPed::m_attachedTo`, from `CPed::AttachPedToEntity`: `str x1, [x19, #0x720]`) — Task 8's door gunners can't move.
- `police.cpp`: while Searching and not `sight::Seen()`, cops on foot within 150 m that aren't in a fight hook (no `combat` fighter slot) get `CPed::SetObjective(eObjective, CVector)` (`_ZN4CPed12SetObjectiveE10eObjective7CVector`) with `OBJECTIVE_RUN_TO_AREA` (25, counted from `enum eObjective` in reVC `src/peds/Ped.h`; `KILL_CHAR_ON_FOOT` is 8 and `LEAVE_CAR` 16 by the same count, matching values the mod already uses) to a search point, renewed on arrival; when he's seen again, `CopSees` → back to `kKillOnFoot` with `api.SetObjective(cop, 8, player)` for those within 60 m.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build and test** — no warnings; three passed lines.
- [ ] **Step 3: Install.**
- [ ] **Step 4: Checkpoint** — behind a wall out of sight, gunmen go to where they last saw him; others fan out round the search area. No commit.

---

### Task 7: Witnesses

**Files:**
- Create: `src/witness.h` (from Task 4's stubs), `src/witness.cpp`
- Modify: `src/wanted.cpp` (call `witness::Update`), `src/wantedhud.cpp` (runner markers on the minimap), `Makefile`, `README.md`

**Interfaces:**
- Consumes: `wanted::Reported`, `wanted::Looks()`, `wanted::Model()`.
- Produces:

```cpp
namespace witness {
void Update(uint32_t now);                 // from wanted::Update
float Calling();                           // the furthest-on call's progress, 0..1 (0: none)
std::vector<CVector> Runners();            // witnesses on their way to a phone, for the minimap
}
```

Decisions:
- Crimes: the game's queue is `CWanted+0x28`, 16 entries of `0x1c` (type `+0`, id `+4`, time `+8`, position `+0xc`, reported `+0x18`, police-doesn't-care `+0x19`; read from `CWanted::UpdateCrimesQ`). Each frame, every entry with a type and `reported == 0` is marked reported (so `UpdateCrimesQ` never files it after its 500 ms) and becomes a crime here (type, position, time). Its stars: what `CWanted::ReportCrimeNow` would give — reproduce reVC's points table (`src/core/Wanted.cpp`, `ReportCrimeNow`) as a crime-type → stars table in `witness.cpp`, at least 1.
- Witnesses for a crime: civilians (not cop, not gang, not mission, alive, on foot) within 40 m with a clear line to the crime, chosen by temperament: lawfulness ≥ fear (`m_pedStats`: read the field offsets from reVC `PedStats.h` and `CPed::m_pedStats` from the binary). At most 3 per crime. A witness "saw Tommy" if it had a clear line to him at the crime.
- Rich phones: peds whose model is one of the business/suit models (`default.ide` ped names containing "bus", "wfyst", "wmyst", "wmybu", "wfybu" — list the ids in a `kRich[]` table from the IDE) have a phone with probability 0.35 (rolled once per ped handle). They stop, play the game's phone-to-ear animations (the ids `phone.cpp` uses for Tommy), and the call goes through after 4 s.
- Others run to the nearest free payphone (`CPhoneInfo::FindNearestFreePhone(&pos)` on `_gPhoneInfo`, then the phone's position from `CPhoneInfo::m_aPhones[id]`: read its layout from reVC `src/core/Phones.h`), with `CPed::SetSeek(CVector, float)` and `SetMoveState` run, renewed each second; within 1.5 m they call for 6 s (`PED_FACE_PHONE` via the game's `FacePhone`), then it goes through. No free phone within 250 m: they flee (`SetFindPathAndFlee`) and report only if they come within 20 m of a cop.
- Stopped: dead, or knocked down (`PED_FALL`/`PED_DIE`) → no report. Through → `wanted::Reported(sawHim, crimePos, stars)`.
- Car alarms: when Tommy enters a vehicle whose alarm (`int16` at `CVehicle+0x214`, non-zero and not -1) goes off, a location-only report after 8 s if any civilian within 60 m.
- Handles only; resolve each tick.
- Log each witness ("witness: a ped saw it (ran for a payphone 80 m off | a rich one with a phone)"), each stop and each report.

- [ ] **Step 1: Read the offsets** named above (PedStats fields, `m_pedStats`, `m_aPhones`) and the points table; write them into `witness.cpp` with their sources.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build and test** — no warnings; three passed lines.
- [ ] **Step 4: Install.**
- [ ] **Step 5: README** — witnesses and payphones, rich NPCs' phones.
- [ ] **Step 6: Checkpoint** — a crime with no cop near: a witness runs for a payphone (minimap marker, phone ring filling); stopping them means no stars; downtown, some call at once. No commit.

---

### Task 8: Police helicopters — the hook, door gunners, two helicopters

**Files:**
- Modify: `tools/patch_macvc.py` (one hook), `src/heli.cpp`, `src/heli.h`, `src/combat.cpp` (already: attached peds aren't tactical), `src/config.h`, `src/config.cpp`, `README.md`

**Interfaces:**
- Consumes: `wanted::Active()`, `wanted::Target()`, `wanted::Model()`, `sight::Seen()`.
- Produces: `bool heli::Gunners(int slot, int &alive)` for the log; nothing else depends on it.

Decisions:
- Hook `_ZN5CHeli14ProcessControlEv`: add the symbol to the hook list in `tools/patch_macvc.py` (it then reports 64 hooks), install it in `heli::Install` with `hooks::Install`.
- In the hook, when `wanted::Active()` and not `sight::Seen()`: swap the position `FindPlayerCoors` reads (the player's vehicle's matrix position `+0x38` if he's in one, else the ped's) with a sweep point (`Target()` + 30 m on a circle turning 0.3 rad/s), call the original, swap back. No other code runs between.
- No belly gun: after the original, the shoot timer (heli.cpp's `kShootTimer`) = now, always.
- No SWAT on ropes: before the original, `m_numSwat` = 0 (read its offset from `CHeli::SendDownSwat` in the binary).
- Door gunners: for each police helicopter in `pHelis` without them, two `CCopPed(COP_SWAT)` (`CPed::operator new(unsigned long)`, `_ZN7CCopPedC1E8eCopTypei`, `CWorld::Add`), attached with `CPed::AttachPedToEntity(heli, offset, type, 1.4f, WEAPONTYPE_M4)` (`_ZN4CPed17AttachPedToEntityEP7CEntity7CVectortf11eWeaponType`, called as `void(*)(uint8_t*, uint8_t*, CVector, uint16_t, float, int32_t)`): left offset `{-1.0f, 0.3f, -0.5f}` type 3, right `{1.0f, 0.3f, -0.5f}` type 1 (tune in play so they sit in the doors); objective `api.SetObjective(ped, 8, player)`; accuracy (`ped[0x64f]`) 45. Dead → `CPed::DettachPedFromEntity` so they fall. Handles only.
- With both gunners dead the helicopter carries on (it's still a looker in `sight`). Shot down → the next has fresh gunners (new helicopters get them).
- Two from 4 stars: when the game's level is ≥ 4, `CHeli::NumRandomHelis == 1` and one of `pHelis[0]`, `pHelis[1]` is empty, every 15 s: `CHeli::GenerateHeli(false)` (`_ZN5CHeli12GenerateHeliEb`), set its type to random (read `m_heliType`'s offset from `CHeli::UpdateHelis`), put it in the empty slot, `NumRandomHelis = 2`. The game retires both when it sends them away.
- Setting `HeliGunners=1` ("police helicopters with SWAT door gunners instead of a belly gun, two from 4 stars, the Hunter at 6"); off: the hook passes straight through.
- Log: "heli: gunners aboard", "heli: a gunner down (n left)", "heli: a second helicopter up".

- [ ] **Step 1: Read the offsets** (`m_numSwat`, `m_heliType`) and confirm the hook is patchable (`./install.sh` prints `patched 64 hooks and 6 skips`).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build and test** — no warnings; three passed lines.
- [ ] **Step 4: Install** — expected `patched 64 hooks and 6 skips` and `==> Installed.`
- [ ] **Step 5: README** — the gunners, no rope drop, two helicopters.
- [ ] **Step 6: Checkpoint** — at 3 stars the helicopter has two SWAT in the doors shooting; shooting them stops the fire; out of sight it sweeps where he was; at 4 stars a second comes. No commit.

---

### Task 9: The Hunter at six stars

**Files:**
- Create: `src/hunter.h`, `src/hunter.cpp`
- Modify: `src/wanted.cpp` (call `hunter::Update`), `src/heli.cpp` or `src/minimap.cpp` (install), `Makefile`, `README.md`

**Interfaces:**
- Consumes: `wanted::Target()`, `sight::Seen()`, the game's level.
- Produces: `void hunter::Update(uint32_t now)`, `bool hunter::Install()`.

Decisions:
- At game level 6 (and `HeliGunners`), one Hunter (model 155, requested with `api.RequestModel(155, 1)`) spawns 180 m from `Target()` at 60 m up, out of the camera's view, the way `police.cpp`'s `Spawn` makes cars (`CVehicle::operator new`, `CAutomobile(155, 1)`, matrix, `CWorld::Add`), with `CVehicle::SetUpDriver()` (`_ZN8CVehicle11SetUpDriverEv`) for a pilot.
- Flying: AutoPilot mission `MISSION_HELI_FLYTOCOORS` (read its value from reVC `src/vehicles/AutoPilot.h`), destination = `Target()` + 25 m up, status physics; `CCarCtrl::SteerAIHeliTowardsTargetCoors` does the rest.
- Weapons (the game fires them only for the player): when it sees him (`sight` line test from the Hunter) within 80 m, `CWeapon(WEAPONTYPE_HELICANNON, 5000)` `FireInstantHit` from its gun point every 90 ms; within 150 m, every 4 s, two `CWeapon(WEAPONTYPE_ROCKETLAUNCHER, 100)` `FireProjectile`s from the rocket points (reVC `vecHunterGunPos`, `vecHunterRocketPos` in `src/vehicles/Automobile.cpp`).
- Tougher: health (`CVehicle+0x2a0`) set to 3000 at spawn.
- Leaves (MISSION_HELI_FLYTOCOORS to 300 m up, then removed) when the level drops below 6. Handles only.
- Log "hunter: up", "hunter: down", "hunter: away".

- [ ] **Step 1: Read** `MISSION_HELI_FLYTOCOORS` and the gun and rocket points.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build and test** — no warnings; three passed lines.
- [ ] **Step 4: Install.**
- [ ] **Step 5: README** — the Hunter at six stars.
- [ ] **Step 6: Checkpoint** — at 6 stars the Hunter arrives, shoots its cannon and rockets, takes about three times the hits to bring down, and leaves when the stars drop. No commit.
