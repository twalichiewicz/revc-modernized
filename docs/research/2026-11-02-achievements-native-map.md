> Research for [#24](https://github.com/twalichiewicz/revc-modernized/issues/24), 2026-11-02. Read-only: taken from the binary, main.scm and game data; offsets, thresholds and script globals are unconfirmed in a running game.

# VC Definitive Edition achievements: how to detect each one in macVC 1.0.5217

Read-only research. Sources:
- the pristine binary `macVC.vanilla` (nm, plus a full `llvm-objdump` with adrp/add/ldr resolved to symbols);
- `GameData/data/main.scm`, disassembled with a small VC SCM disassembler and the Sanny Builder VC opcode table;
- `TEXT/american.gxt`, parsed for mission and rank names;
- `default.ide`, `weapon.dat` and `handling.cfg`;
- the mod's own code (`src/game.h`, `story.cpp`, `wanted.cpp`, `player.cpp`, `flightmodel.cpp`, `tools/patch_macvc.py`).

Notation:
- `$N` is a main.scm global. Read it with `game::ScriptGlobal(N)`, which is `CTheScripts::ScriptSpace + 4*N`, the same indexing `story.cpp` uses. For example, the mod's Cop Land var 268 is `$268` here.
- `N@` is a mission-script local. Read it at `CRunningScript + 0x38 + 4*N`; `GetPointerToScriptVariable` does `add x0, x9, #0x38`.
- Script names (`SCRIPT_NAME`, opcode 03A4) are lower-cased by the game: `ProcessCommands900To999` calls `tolower`. So the names are `serg1`, `baron5`, `col_4` and so on, at `CRunningScript+0x10`. Walk `CTheScripts::pActiveScripts` (0x100532968) for them.

## Shared plumbing (applies to many rows)

| Thing | Where (macVC.vanilla) | Notes |
|---|---|---|
| CStats statics | `__ZN6CStats…E` symbols at 0x10054ef00–0x10054f16c (each listed below) | Saved/loaded by `CStats::SaveStats`/`LoadStats` (0x1001196c4 / 0x100119b94); reset by `CStats::Init` on a new game |
| Script globals | `CTheScripts::ScriptSpace` 0x1004e1a50 (`api.scriptSpace`) | Saved with the script block. Mission locals above about $2000 are re-used by other missions, so only read them while that mission's script is running |
| Running mission | `CTheScripts::pActiveScripts` list. Name at +0x10 (lower case), locals at +0x38 | `CTheScripts::IsPlayerOnAMission` is already bound in watch.cpp and story.cpp |
| Any mission pass (live) | `CStats::MissionsPassed` 0x10054f01c (int) and `CStats::LastMissionPassedName` 0x10054f020 (char[8], GXT key such as `LAW_4`) | Written by REGISTER_MISSION_PASSED (`ProcessCommands700To799`). `MissionsPassed` is also bumped by REGISTER_ODDJOB_MISSION_PASSED (1400-1499). Use it as an event trigger; for persisted state use the per-mission flags |
| CPlayerInfo | `CWorld::Players` 0x10060b360, stride 0x1a0, index `CWorld::PlayerInFocus` 0x10060b35a (`game::PlayerInfo()`) | `SavePlayerInfo` order: money +0xc8, visible money +0xcc, **collected packages +0xd0**, **total packages +0xd4** |
| Wanted level | `CPlayerPed+0x778` (CWanted*), then `CWanted+0x20` (int32 m_nWantedLevel) | Confirmed in `GivePlayerGoodiesWithPickUpMI`. The mod already uses it (hud.cpp, wanted.cpp) |
| Ped damage event | `CPed::InflictDamage(ped, by, weaponType, dmg, piece, dir)`, already hooked in `player.cpp` (DEFAULT_HOOKS) | Chain it with `hooks::Chain` rather than adding a patch. Fatal when health ≤ 0 afterwards, or the native result is true |
| Pickup collection event | `CPickup::Update(pickup, player, vehicle, id)`, already in DEFAULT_HOOKS ("businesses on credit") | Pickup model is int16 at +0x2a (see wanted.cpp); `MI_PICKUP_BRIBE` symbol 0x10036d738 |
| main.scm guard | `story.cpp` checks 7 bytes at 0x2621c | Every `$N` below is for this exact main.scm (1,277,288 bytes). Re-use or extend that signature check |
| Cheat gate | `CStats::CheatedCount` 0x10054f0ec (int, saved) | Cheat functions write it (`MayhemCheat`, `VehicleCheat`, …). DE disables trophies once cheats are used, so a matching gate is `CheatedCount == 0` |
| eWeaponType | weapon.dat order: 3 = GolfClub, 7 = Hammer, 17 = Colt45, 26 = M4, 40 = RUNOVERBYCAR (rotor blades) | 40 is what `CVehicle::BladeColSectorList` passes to `InflictDamage` (0x10027c578: w2=0x28, dmg 1000.0, `by` = the heli) |

## Per-achievement map

Difficulty codes:
- **E**: easy. The game already keeps the counter or flag, and the mod polls it.
- **H**: needs an event hook or live polling, because nothing is persisted.
- **M**: needs a mission-script condition.
- **X**: not feasible as-is.

| # | Name | Native source | Saved? | Approach | Diff | Evidence |
|---|---|---|---|---|---|---|
| 1 | Born in the 80's | Live: `MusicManager+0x39a0` float[10] listen-time in ms, indexed by station (`_MusicManager` 0x100382cb0; or `cDMAudio::GetListenTimeArray(&DMAudio)` 0x1000342dc). Saved copy: `CStats::FavoriteRadioStationList` 0x10054f110 float[10] | Yes. `SaveStats` saves the live array; `cMusicManager::ResetMusicAfterReload` restores it from the stats on load | Poll stations 0–8 > threshold. 0 Wildstyle, 1 Flash, 2 K-Chat, 3 Fever, 4 V-Rock, 5 VCPR, 6 Espantoso, 7 Emotion, 8 Wave (9 = MP3, excluded). PowerPyx says DE needs **all 9 including the 2 talk stations**. Use a minimum like ≥30–60 s each: the native counter credits any fraction of a second | E | `cMusicManager::RecordRadioStats` 0x100035270: `aListenTime[m_nPlayingTrack(+0x3988)] += now − last(+0x39c8)` when the track < 10. GXT FEA_FM0..8. `ServiceFrontEndMode` also records, so the menu's radio preview counts too |
| 2 | Running Rampant | `CStats::HighestLevelVigilanteMission` 0x10054f038 int32 | Yes | `≥ 12` | E | `RegisterLevelVigilanteMission` keeps the max. COPCAR (M078) registers `$6940` (= completed + 1) only after a level's criminals are all down, so 12 means level 12 is done |
| 3 | Iron-y | Mission `serg1` ("Four Iron", TEX_1). Gonzalez = `$3166` (CREATE_CHAR 4 62 WMYGO, given GolfClub 3). Pass flag `$244` | No | While `serg1` runs: in the InflictDamage chain, if `ped == PoolsGetPed($3166)` and the hit is fatal, `by == player` and `weapon == 3` (GolfClub), unlock | M | M018 @0x79d61 `CREATE_CHAR 4 62 … $3166`, @0x79d73 `GIVE_WEAPON_TO_CHAR $3166 3`. The mission ends on his death |
| 4 | Not my First Time | Mission `porno4` ("G-Spotlight", PORN_4). Course stage `$4525`: 6 = window, 7..25 = rooftop checkpoints, 26 = final roof. Pass flags `$260`, `$633` | No | While `porno4` runs and `7 ≤ $4525 ≤ 25`, latch "fell" if Tommy or his bike gets near street level (z < ~13: start ground z 10, lowest roof checkpoint z 16.6), or enters the "go back up" flow (the office lift and ladder, POR4_06/POR4_11). On `$260` 0→1 with no latch, unlock. **The pass sets `$260` and the script terminates in the same tick** (pass block @0xac050 returns to @0xa9408: death/arrest check, cleanup @0xac147, `TERMINATE_THIS_SCRIPT`, no `WAIT`), so a poll sees it only as `porno4` gone with `$260` = 1; a detected load resets the watch (a save may read 1) | M | M030 LOCATE checkpoints @0xaa345..0xaac5f (`$4525` 7→25). Pass @0xac050. **Heuristic: the script keeps no "fell" flag** |
| 5 | One is Better Than Two | `CStats::LongestWheelie` 0x10054f158 int32, in **seconds** | Yes | `≥ 30`. For a live unlock, the current wheelie in ms is at `CPlayerInfo+0x128` (≥30000) | E | `CPlayerInfo::Process` @0x10010af98: when a wheelie ends with ms ≥ 5000, `LongestWheelie = max(.., ms/1000)` |
| 6 | Point A to Point B | `CStats::PassengersDroppedOffWithTaxi` 0x10054f010 int32 | Yes | `≥ 25`. Alt: `$369` (lifetime fares in the TAXI script) | E | Only REGISTER_PASSENGER_DROPPED_OFF_TAXI in TAXI (M075 @0x10641e) bumps it |
| 7 | High Quality H2O | `CStats::FiresExtinguished` 0x10054f040 int32 | Yes | `≥ 10` | E | Only `AnotherFireExtinguished` (opcode REGISTER_FIRE_EXTINGUISHED, FIRETRU M077, 4 sites) bumps it, so it counts Firefighter-mission fires only, as DE does |
| 8 | Pie Guy | `CStats::PizzasDelivered` 0x10054efd0 **float** | Yes | `≥ 10` | E | PIZZA (M092 @0x12dce6) `ADD_PIZZAS_DELIVERED $8022` runs in cleanup, so the stat moves at the **end** of each pizza run |
| 9 | Bull in a China Shop | `CStats::PropertyDestroyed` 0x10054f0e4 int32 ($) | Yes | `≥ 1,000,000` | E | Bumped in `CAutomobile/CBike::BlowUpCar`, `CVehicle::InflictDamage`, `CPhysical::ApplyCollision` |
| 10 | Vice City Mogul | `CStats::NumPropertyOwned` 0x10054f0e0 int32 (`PropertyOwned` bool[16] at 0x10054f0d0) | Yes | `≥ 10` | E | `AddPropertyAsOwned` 0x100119688, from SET_PROPERTY_AS_OWNED in the 15 buy scripts (ids 0–14). PowerPyx says "10 of 17"; natively there are 15 |
| 11 | Salutations My Little Friend | Mission `baron5` ("Rub Out", ASS_1). Diaz = **local 0@** (CREATE_CHAR 10 112 = special04 'IGDIAZ', health 1000). Pass flag `$239` | No | While `baron5` runs: on a fatal InflictDamage of `PoolsGetPed(*(int*)(script+0x38))`, or of the model-112 ped, with `by == player` and `weapon == 26` (M4), unlock | M | M016 @0x724c5 `CREATE_CHAR 10 112 … 0@`. Lance gives the M4: @0x71c87 `GIVE_WEAPON_TO_PLAYER $2 26 150` |
| 12 | Catch Me if You Can | Wanted level at `CPlayerPed+0x778` → `+0x20` | No | Poll for ≥ 6. Only possible after Rub Out raises the cap (`SET_MAX_WANTED_LEVEL 6` @0x74846) or by cheat | H | No saved "max stars ever" exists. The mod's own wanted model (wanted.cpp) drives the game's stars: count the game's level, not the HUD's |
| 13 | Somebody Call the Wambulance? | `CStats::HighestLevelAmbulanceMission` 0x10054f034 int32 | Yes | `≥ 12` | E | AMBULAN (M076 @0x108482) registers the level after its patients are delivered, then `$6758++`; 13 means all done |
| 14 | Greasy Palms | Bribe pickup (`MI_PICKUP_BRIBE`). `CPickups::GivePlayerGoodiesWithPickUpMI` 0x100075c24 calls `CPlayerPed::SetWantedLevel(max(0, lvl−1))` | No | Chain the already-patched `CPickup::Update`: note model (+0x2a) == bribe and the wanted level before; if it was > 0 and the pickup is collected, unlock. Or add `GivePlayerGoodiesWithPickUpMI` to DEFAULT_HOOKS | **X with the mod's default config.** H otherwise | **`wanted.cpp` `RemoveBribes()` deletes every bribe pickup while `realisticWanted` is on, which is the default.** It must spare bribes or the achievement can't happen |
| 15 | Just Like the Real Thing | RCRACE1 "won once" flag `$8243` (only used in M094); also `CStats::FastestTimes[4]` 0x10054f05c+0x10, seconds | Yes | `$8243 == 1` (or `FastestTimes[4] > 0`) | E | M094 @0x13371b `REGISTER_FASTEST_TIME 4`, @0x1337a3 `SET_VAR_INT $8243 1` |
| 16 | Keepie-Uppy Okie Dokie | `CStats::HighestScores[3]` (0x10054f0b8 + 0xc) int32 | Yes | `≥ 5` | E | `CObject::ProcessControl` @0x10017f2ec: beach-ball counter `CObject+0x1c3` (uint8) ++, then `RegisterHighestScore(3, bounces)` |
| 17 | Gun for Hire | Flags `$298 $299 $300 $301 $302` (JOB_1..5: Road Kill, Waste the Wife, Autocide, Check Out at the Check In, Loose Ends). Alt `CStats::Assassinations` 0x10054efcc float | Yes | All five == 1 (or Assassinations ≥ 5) | E | ASSIN1-5 (M067-071) pass blocks; `ADD_NUMBER_OF_ASSASSINATIONS 1` in each |
| 18 | I'm Famous! | `CStats::HighestChaseValue` 0x10054f0e8 float | Yes | `≥ 1800` ("Stuff of legends", CHASE21). PowerPyx says 1600+; natively 1600–1800 is "World event" | E | `CStats::FindChaseString` 0x100119218: last cut 0x44e10000 = 1800 |
| 19 | Don't Need Roads | Player in a vehicle with model 211 (deluxo). Speed `|CPhysical+0x84|` (units per 1/50 s; ×180 = km/h). Handling `CVehicle+0x160` → `tHandlingData+0x84` = Transmission.fMaxVelocity (already ×1/180: DELUXO 300 km/h → 1.667) | No | Poll while driving: `speed ≥ k·fMaxVelocity`, with k found by testing (drag may stop the car short of 1.0). Native has no "max speed" stat | H | handling.cfg `DELUXO … 300.0 27.0 …`. `ConvertDataToGameUnits` @0x100272c3c scales +0x84 by 0x3bb60b61 (1/180) |
| 20 | Daredevil | `CStats::NumberOfUniqueJumpsFound` 0x10054f008 int32 (total 36 at 0x10054f00c) | Yes | `≥ 36` | E | MAIN @0x138b9 `SET_UNIQUE_JUMPS_TOTAL 36`, @0x1557c `REGISTER_UNIQUE_JUMP_FOUND` |
| 21 | Legal Counsel | `$227` (LAWYER4, LAW_4 "Riot") | Yes | `== 1` | E | M006 @0x49e75 `SET_VAR_INT $227 1` in the pass block |
| 22 | Life of the Party | `$233` (COL_5, "All Hands On Deck!") | Yes | `== 1` | E | M011 @0x658a9 |
| 23 | South American Connection | `$238` (BARON4, COK_4 "Supply & Demand") | Yes | `== 1` | E | M015 @0x70cb0 |
| 24 | Big Heat from Little Havana | `$282` (CUBAN4, CUB_4 "Trojan Voodoo") | Yes | `== 1` | E | M059 pass block (`$5700=1`, `$282=1`) |
| 25 | Chauffeur | `$290` (ROCKB3, ROCK_3 "Publicity Tour") | Yes | `== 1` | E | M065 pass block |
| 26 | Tommy Two-Wheels | `$277` (BIKE3, BIKE_3 "Hog Tied") | Yes | `== 1` | E | M055 pass block |
| 27 | Grand Theft Auto | `CGarages::CarTypesCollected` 0x1003bbb40 int32[4], one per import list (garage types 8, 9, 10, 22) | Yes (`CGarages::Save`) | All four `& 0x3f == 0x3f` (24 cars) | E | `MarkThisCarAsCollectedForCraig` sets `1<<index` (0–5). The HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED opcode passes `slot−1`. Car table at 0x100321c30: {130,131,149,169,219,226} {205,221,135,139,151,175} {145,141,159,207,210,132} {142,164,187,215,153,178}. Garage `$687` |
| 28 | Bloodstained Hands | Criminal rating: call `CStats::FindCriminalRatingNumber()` 0x1001190b8 (built from saved stats and money) | Yes (derived) | `≥ 10000` (Butcher, RATNG39, is [10000, 20000)) | E | `FindCriminalRatingString` 0x1001188cc: `rating>>4 < 625` → RATNG38, `rating>>5 < 625` → RATNG39. Matches PowerPyx's 10,000 |
| 29 | Take the Cannoli | Same, plus `CPlayerInfo+0xcc` visible money | Yes (derived) | Native Godfather (RATNG52) = rating ≥ 1,000,000 **and** visible money > $10,000,000. **DE patch 1.02 lowered Godfather to 100,000** (PowerPyx). Pick one | E (definition ambiguity) | @0x100119070–0x1001190a8: Don if < 0xf4240; Godfather if money > 0x989680. (Kingpin also needs `FlightTime` > 39,599,999 ms) |
| 30 | Mischief Managed | `$270` (FIN_1 "Keep your Friends Close...") | Yes | `== 1` | E | M052 @0xcc5b8 |
| 31 | Chopper'd Up | Mission `serg3` ("Demolition Man", TEX_3). Hostiles = `$3311…$3326` (16 peds: 12 × model 61 WMYCW with a hammer, 4 × model 91 GDa with a Colt45). RC heli = `$3327` (GET_REMOTE_CONTROLLED_CAR; model 231 rcgoblin). Pass flag `$245` | No | While `serg3` runs, record each fatal InflictDamage on those handles with `weapon == 40` and `by == PoolsGetVehicle($3327)` (or model 231). Unlock when all 16 are dead and each was a blade kill (any other cause voids it). Resolve the handles when the script creates them | M | M019 @0x7c3de..0x7c72e. Blade damage path `CVehicle::BladeColSectorList` @0x10027c578. **DE guides count 17 NPCs; this main.scm creates 16** |
| 32 | City Sleuth | `CPlayerInfo+0xd0` int32 collected packages (total at +0xd4 = 100) | Yes | `≥ 100` | E | `CPickup::Update` @0x100075818 `ldp w9,w10,[x8,#0xd0]` ++; MAIN `SET_COLLECTABLE1_TOTAL 100` |
| 33 | Done it All | `CStats::GetPercentageProgress()` 0x1001194ec (float, clamped at 100). Inputs: `ProgressMade` 0x10054efe4 and `TotalProgressInGame` 0x10054efe8, both floats | Yes | `≥ 100.0` | E | MAIN `SET_PROGRESS_TOTAL 154`. Divides by total−1 when `CGame::nastyGame` is false |
| 34 | Kingpin | All other 42 | n/a | Meta | E | — |
| 35 | Put 'Em Up | `CStats::StoresKnockedOff` 0x10054efc4 **float** | Yes | `≥ 15` | E | 15 `ADD_STORES_KNOCKED_OFF 1` sites in MAIN |
| 36 | Offender | `CStats::WantedStarsAttained` 0x10054ef08 int32 | Yes | `≥ 50` | E | `CWanted::UpdateWantedLevel` adds (new − old). The mod's wanted.cpp re-raises stars via `CWanted::SetWantedLevel` after respray or clothes drops, which inflates this a little |
| 37 | Retro Lewk | Current outfit = model 0's name (`modelInfoPtrs[0]+8`). CPed::Undress loads the outfit into model 0; wanted.cpp already reads it | No (only clothes-unlock flags such as `$1265…$1278` are saved) | Poll and store a set of names seen. main.scm's 12 outfits: PLAYER, PLAYER2…PLAYER9, PLAY10, PLAY11, PLAY12 (compare case-insensitively) | H | UNDRESS_CHAR args across MAIN and missions. Some outfits are mission-worn (PLAYER3/4/5/6/7) |
| 38 | Scouring the City! | `CPlayerInfo+0xd0` | Yes | `≥ 50` | E | as #32 |
| 39 | Break it Up | `CStats::PropertyDestroyed` | Yes | `≥ 500,000` | E | as #9 |
| 40 | Target Practice | `CStats::HeadsPopped` 0x10054ef14 int32 | Yes | `≥ 50` | E | Bumped only in `CDarkel::RegisterKillByPlayer` (0x10005f89c, `headShot` arg), i.e. player headshot **kills**. `CPed::ApplyHeadShot` has no callers. The mod's custom shooting must still reach native InflictDamage with piece 6 (head) |
| 41 | Prime Real Estate | `CStats::NumPropertyOwned` (or all of `PropertyOwned[0..14]`) | Yes | `== 15` | E | 15 SET_PROPERTY_AS_OWNED ids 0–14 (M036–M050) |
| 42 | Vice Vice Baby | `$612` (Cherry Popper asset complete, set by ICECRE1 "Distribution", GXT ICE_AT1 "ICECREAM FACTORY ASSET COMPLETED"; story.cpp already uses 612) | Yes | `== 1` | E | M083 @0x124744 `SET_VAR_INT $612 1` |
| 43 | Baked Distractions | Mission `col_4` ("Sir, Yes Sir!", COL_4). **`$2525` = army alerted** (set by ~50 guarded triggers: soldier dead, convoy shot, approach, …). **`$2519` = player got into the tank.** Pass flag `$232` | No | While `col_4` runs, poll each frame. Latch `alerted` when `$2525 != 0` while `$2519 == 0`. When `$2519` goes 0→1 with no latch, unlock. The theft block itself sets `$2525=1` right after `$2519=1` in the same tick | M | M010 @0x5a034 `SET_VAR_INT $2519 1`, @0x5a02d `$2525 1`. Triggers such as @0x574d8, 0x5757f (HAS_CAR_BEEN_DAMAGED_BY_WEAPON). "Lure out the occupants" (COL4_B4) / doughnut-stop dialogue (COL4_19) |

### Counts

| Difficulty | Count | Entries |
|---|---|---|
| E | 34 | #1, 2, 5, 6, 7, 8, 9, 10, 13, 15–18, 20–30, 32–36, 38–42 |
| H | 4 | #12, 14, 19, 37 |
| M | 5 | #3, 4, 11, 31, 43 |
| X | 0 strictly | #14 is X with the default `realisticWanted` config |

All mission-specific checks reuse hooks that are already patched (CPed::InflictDamage, CPickup::Update). No new DEFAULT_HOOKS entry is required unless you prefer hooking `GivePlayerGoodiesWithPickUpMI` for #14.

## Loading an existing save: retroactive unlocks

Every "Saved? = Yes" source comes back from the save during the load: the CStats block, ScriptSpace globals, CPlayerInfo, CGarages and the radio listen times (via `ResetMusicAfterReload`).

So a mod that simply polls conditions will unlock **retroactively** on the first evaluation after loading a save that already meets them. That includes someone else's 100% save. A new game zeroes them again (`CStats::Init` via `CGame::ReInitGameObjectVariables`, main.scm defaults), but the mod's own unlock record (stored outside the save) stays.

Detect a load the way `story.cpp` and `wanted.cpp` do: `CTimer` ms going backwards, or jumping more than 10 s. Evaluate after the load settles, a few frames later. Choose one policy:
- **(a) Retroactive.** Award whatever the save already satisfies. Simple and forgiving.
- **(b) Baseline.** Snapshot the values at load, and award only when a value crosses its threshold upward while the mod watches. Closer to "earned live", but players with existing saves would be stuck below thresholds they had already passed.

Either way, consider gating on `CStats::CheatedCount == 0`. It is saved with the game, and DE disables trophies once cheats are used.

**Only earnable while the mod is running** (nothing persisted): #3, #4, #11, #31, #43 (in-mission conditions), #12 (six stars), #14 (bribe), #19 (Deluxo top speed), #37 (outfits worn). For #37 only the currently worn outfit is visible after a load; earlier wear is lost.

## Riskiest entries

1. **#14 Greasy Palms**: `wanted.cpp` removes every bribe pickup while `realisticWanted` (default on) is enabled. It can't be earned unless bribes are kept, or the achievement is redefined.
2. **#4 Not my First Time**: no native "fell" signal, so this is a z-threshold or surface heuristic over `$4525` stages 7–25. Mis-fires are likely around the low roofs at z ≈ 16.6, and when the player falls onto a lower roof without reaching the street.
3. **#31 Chopper'd Up**: which NPCs count (16 here vs. 17 per DE guides). Deaths by bomb or fall void a perfect run. The mod's `heliDamage` rotor-failure chain on `CVehicle::DoBladeCollision` (`flightmodel.cpp`) could break the RC rotor if it applies to the RC Goblin.
4. **#43 Baked Distractions**: relies on reading `$2525` as "alerted". It looks right (every trigger is guarded `IF $2525 == 0`), but proximity and damage triggers make it strict. The mission-local globals are only valid while `col_4` runs.
5. **#19 Don't Need Roads**: DE defines no threshold, and the car may never quite reach `fMaxVelocity`. Calibrate k (the fraction of `fMaxVelocity` that counts) with the runtime probe.
6. **Definition drift vs. DE**:
   - #29: DE patched Godfather down to a rating of 100,000; natively it's 1,000,000 plus $10M.
   - #18: guide says 1600+; native Stuff of Legends is 1800.
   - #1: does "music stations" include the 2 talk stations?
   - #10/#41: 15 properties natively vs. "17" in a guide.
   - #37: 12 outfit models.
7. **#40 and #36 with the mod's systems**: custom first-person shooting has to keep native headshot accounting (piece 6 through `CPed::InflictDamage` → `CDarkel::RegisterKillByPlayer`). The wanted overhaul's star restores inflate `WantedStarsAttained`.
8. **main.scm dependence**: every `$N` and local index is specific to this 1,277,288-byte main.scm. Reuse or extend story.cpp's byte-signature check before trusting any of them.
