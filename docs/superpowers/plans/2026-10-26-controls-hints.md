# Controls 1/2: the Bindings Table, Hints and Keyboard Keys Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every hint and mod prompt names the button or key that really does the action now and follows changes live. The mod's pad-only features get keyboard keys. Actions the mod's layout stranded get pad buttons.

**Architecture:**
- **Pure modules** (unit-tested, no game calls):
  - `keys`: the port's key codes and their names.
  - `bindings` + `layout`: the table of what each action is on, for each situation and ini switch set.
  - `hinttext`: `~k~` token expansion and the rewritten English hints.
- **Game side:**
  - `hints` hooks `CMessages::InsertPlayerControlKeysInString` (every help box, brief and briefs page goes through it) and the two `CFont::ParseToken` overloads (D-pad, Start and Back icons). It also loads the icon set of the pad you have.
  - `actions` reads the keyboard keys of the mod's own actions.
  - `controls.cpp` reads its pad buttons from `layout.h`.

  Plan 2 (the Controls page) builds on these interfaces.

**Tech Stack:** C++17, clang on macOS arm64. Hooks are patched into the game binary by `tools/patch_macvc.py` and installed by `./install.sh`. Unit tests are plain executables run by `make test`.

**Spec:** `docs/superpowers/specs/2026-10-26-controls-and-hints-design.md`

## Global Constraints

- **Target binary:** macVC 1.0.5217 (`kBuildUuid` in `src/game.cpp`). Offsets below come from it, unmodified copy at `~/Library/Application Support/macVC/vcmodernhud/macVC.vanilla`.
- **Style:** match the surrounding code: tabs, comment density, `namespace x { namespace { ... } }`, `Log(...)` for one-line diagnostics.
- **New hooks:** add each hooked function to `DEFAULT_HOOKS` in `tools/patch_macvc.py` and install it with `hooks::Install`. A missing hook logs a line and leaves the game's behaviour as it was.
- **Game text** is `uint16_t` arrays (UTF-16, null-terminated). Inside pure code use `std::u16string` and convert at the boundary.
- **The game's buffers** that hints expand into hold 256 units including the terminator: `hinttext::kCapacity = 256`.
- **English only** for the mod's own text.
- **Commits:** don't commit. The user decides when to commit (this branch already carries uncommitted map work).
- **Game state:** `./install.sh` refuses to run while the game is open. Ask the user to quit it if needed, and never kill it.
- **Unit-tested switches:** the default ini (all switches on) and all switches off.

## Review Focus

1. **Keyboard only, no pad connected:**
   - Hints name keys.
   - `LoadButtons` isn't called with a missing family.
   - The mod's keyboard actions work with `Gamepad::live == false`.

   Task 8 adds a step for this.
2. **Steam Input, with Steam's virtual pad and a physical DualSense both connected:** the icons must be PlayStation's (any Sony pad connected counts, as `controls::PlayStationPad` already does). Pinned by `TestFamilyForName` (Task 2) and the family rule in Task 5.
3. **A long hint near 256 units,** whose expansion grows (a key name is longer than its token): truncate, never overflow. Pinned by `TestExpandTruncatesAtCapacity` (Task 3).
4. **An ini switch turned off** (e.g. `ModernTriggers=0`): that action's hint falls back to the game's own button. Pinned by `TestSwitchOffFallsBackToGame` (Task 2).
5. **A player who already has Q (or another mod default) bound to a game action** on foot: the mod action starts with no key instead of sharing it. Pinned by `TestDefaultKeySkipsGameKey` (Task 2).

---

## Facts read from the binary (use these, don't re-derive)

**The game's actions** (`e_ControllerAction`, names at `ControlsManager + 0x8c + 0x50·i`). `Action` values 0..43 are these, in this order:

```
0 PED_FIREWEAPON  1 PED_CYCLE_WEAPON_RIGHT  2 PED_CYCLE_WEAPON_LEFT  3 GO_FORWARD  4 GO_BACK  5 GO_LEFT  6 GO_RIGHT
7 PED_SNIPER_ZOOM_IN  8 PED_SNIPER_ZOOM_OUT  9 VEHICLE_ENTER_EXIT  10 CAMERA_CHANGE_VIEW_ALL_SITUATIONS  11 PED_JUMPING
12 PED_SPRINT  13 PED_LOOKBEHIND  14 PED_DUCK  15 PED_ANSWER_PHONE  16 VEHICLE_FIREWEAPON  17 VEHICLE_ACCELERATE
18 VEHICLE_BRAKE  19 VEHICLE_CHANGE_RADIO_STATION  20 VEHICLE_HORN  21 TOGGLE_SUBMISSIONS  22 VEHICLE_HANDBRAKE
23 PED_1RST_PERSON_LOOK_LEFT  24 PED_1RST_PERSON_LOOK_RIGHT  25 VEHICLE_LOOKLEFT  26 VEHICLE_LOOKRIGHT  27 VEHICLE_LOOKBEHIND
28 VEHICLE_TURRETLEFT  29 VEHICLE_TURRETRIGHT  30 VEHICLE_TURRETUP  31 VEHICLE_TURRETDOWN  32 PED_CYCLE_TARGET_LEFT
33 PED_CYCLE_TARGET_RIGHT  34 PED_CENTER_CAMERA_BEHIND_PLAYER  35 PED_LOCK_TARGET  36 NETWORK_TALK
37 PED_1RST_PERSON_LOOK_UP  38 PED_1RST_PERSON_LOOK_DOWN  39 (unused, "_CONTROLLERACTION_36")  40 TOGGLE_DPAD
41 SWITCH_DEBUG_CAM_ON  42 TAKE_SCREEN_SHOT  43 SHOW_MOUSE_POINTER_TOGGLE
```

**The game's own pad button per action** (setup 1, `CPad::Mode` 0, from its icon table at `0x100369b58`), in Xbox names: A = Cross, B = Circle, X = Square, Y = Triangle.

| Button | Actions |
|---|---|
| B | 0, 16 |
| RT | 1, 26, 33 |
| LT | 2, 25, 27 (both triggers), 32 |
| left stick | 3–6, 30, 31 |
| X | 7, 11, 18 |
| A | 8, 12, 17 |
| Y | 9 |
| Back | 10 |
| R3 | 13, 21 |
| L3 | 14, 20 |
| LB | 15, 19, 34 |
| RB | 22, 35 |
| right stick | 23, 24, 28, 29, 37, 38 |
| none | 36, 39–43 |

**Icon tokens** (re3 `BUTTON_ICONS`): A `~X~`, B `~O~`, X `~Q~`, Y `~T~`, LB `~K~`, LT `~M~`, L3 `~A~`, RB `~J~`, RT `~V~`, R3 `~C~`.
- These draw `CFont::ButtonSprite[0..17]` via `CFont::DrawButton`, which draws `ButtonSprite[CFont::PS2Symbol]` through `CSprite2d::Draw`. `PS2Symbol == -1` means no button.
- The D-pad, Start and Back icons exist in every button TXD (`up down left right dlr dud start select`) but the game never loads them as sprites. The mod adds tokens `~@u~ ~@d~ ~@l~ ~@r~ ~@h~ ~@v~ ~@s~ ~@b~` (up, down, left, right, left/right, up/down, start, back) as `PS2Symbol` 18..25.

**Key codes** (`RsKeyCodes` in this port):
- **Printable keys:** their uppercase ASCII code, space = 32.
- **Specials:** 1000 + index in

  ```
  ESC F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12 INS DEL HOME END PGUP PGDN UP DOWN LEFT RIGHT DIVIDE TIMES PLUS MINUS
  PADDEL PADEND PADDOWN PADPGDN PADLEFT PAD5 NUMLOCK PADRIGHT PADHOME PADUP PADPGUP PADINS PADENTER SCROLL PAUSE
  BACKSP TAB CAPSLK ENTER LSHIFT RSHIFT SHIFT LCTRL RCTRL LALT RALT LWIN RWIN APPS NULL
  ```

  so ESC = 1000, LSHIFT = 1046, NULL = 1056 = no key.
- **Mouse buttons** 1..7 = `LEFT MIDDLE RIGHT WHLUP WHLDOWN X1 X2`.
- **`macVC.ini [Bindings]` format:** `PED_FIREWEAPON=kbd:PADINS,2ndKbd:LCTRL,mouse:LEFT,joy:1`, with space written `SPC`.

**Symbols to bind** (`Bind` in `src/game.cpp`, fields in `game::Api`):

| Field | Symbol | Notes |
|---|---|---|
| `controlsManager` | `ControlsManager` | `m_aSettings` at `+0xec0`, 32 bytes an action: KEYBOARD `+0`, OPTIONAL_EXTRA `+8`, MOUSE `+0x10`, JOYSTICK `+0x18`, each `int32 key, int32 order` |
| `GetWideStringOfCommandKeys` | `_ZN24CControllerConfigManager26GetWideStringOfCommandKeysEtPtt` | `(uint8_t *ctrl, uint16_t action, uint16_t *out, uint16_t len)` |
| `GetIsKeyboardKeyDown` | `_ZN24CControllerConfigManager20GetIsKeyboardKeyDownE10RsKeyCodes` | `bool (*)(uint8_t *ctrl, int key)` |
| `GetIsMouseButtonDown` | `_ZN24CControllerConfigManager20GetIsMouseButtonDownE10RsKeyCodes` | `bool (*)(uint8_t *ctrl, int button)` |
| `FontLoadButtons` | `_ZN5CFont11LoadButtonsEPKc` | `void (*)(const char *txdPath)`: reloads cleanly into the same slot |
| `fontPs2Symbol` | `_ZN5CFont9PS2SymbolE` | `int32_t *` |
| `fontButtonSprites` | `_ZN5CFont12ButtonSpriteE` | `uint8_t *`, 8 bytes a sprite |
| `fontButtonsSlot` | `_ZN5CFont11ButtonsSlotE` | `int32_t *`, a TXD slot |
| `helpMessage` | `_ZN4CHud13m_HelpMessageE` | `uint16_t *`, 256 units |
| `lastHelpMessage` | `_ZN4CHud17m_LastHelpMessageE` | `uint16_t *`, 256 units |
| `helpMessageToPrint` | `_ZN4CHud20m_HelpMessageToPrintE` | `uint16_t *`, 256 units |

**New hooks** (add to `DEFAULT_HOOKS`):
- `_ZN9CMessages31InsertPlayerControlKeysInStringEPt`: `void (uint16_t *text)`, in place, 256 units. Its callers are `CHud::SetHelpMessage` (the call returns to image offset `0x1fe8a8`), `CMessages::Display` and `CMenuManager::PrintBriefs`.
- `_ZN5CFont10ParseTokenEPtR5CRGBARbS3_`: `uint16_t *(uint16_t *s, CRGBA &col, bool &flash, bool &bold)`.
- `_ZN5CFont10ParseTokenEPt`: `uint16_t *(uint16_t *s)`.

**Vehicle models** (`data/default.ide`):
- Hunter 155, Sea Sparrow 177, RC Baron 194, Predator 160.
- `controls.cpp`'s `Helicopter()` list: 155 177 195 199 217 218 227 231 (RC Raider 195, RC Goblin 231).
- `ArmedVehicle()` today: 162 137 171.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/keys.h/.cpp` (new, pure) | Key codes ↔ ini names ↔ display names; `Binding` (key + alternative) parse and format |
| `src/bindings.h/.cpp` (new, pure) | Actions, situations, pad buttons; the mod's overrides per situation and ini switch; reachability; token names; default mod keys; pad family by name |
| `src/layout.h` (new, pure) | Where the mod puts each thing on the pad, as `bindings::Pad` constants: the one place `bindings.cpp` and `controls.cpp` both read |
| `src/hinttext.h/.cpp` (new, pure) | Expand `~k~~NAME~` in a 256-unit buffer through a resolver; the rewritten English hints |
| `src/hints.h/.cpp` (new) | The game-side hooks: expansion, ParseToken, icon set, D-pad sprites, live help-box refresh, glyphs for prompts |
| `src/actions.h/.cpp` (new) | The keys of the mod's own actions (ini or defaults) and their state each frame; the game's keys for its actions |
| `src/controls.cpp` | Reads `layout.h`; fills the gaps; keyboard paths for the mod's actions |
| `src/prompt.h/.cpp`, `src/climb.cpp`, `src/interact.cpp`, `src/phone.cpp` | Prompts take an `Action` |
| `src/game.h/.cpp`, `tools/patch_macvc.py`, `src/minimap.cpp`, `src/menu.cpp`, `src/config.h/.cpp`, `Makefile`, `README.md` | Wiring |
| `tests/test_keys.cpp`, `tests/test_bindings.cpp`, `tests/test_hinttext.cpp` (new) | Unit tests in the style of `tests/test_magnet.cpp` (the `CHECK` macro, `gFailures`, `main` prints "all … tests passed") |

---

### Task 1: Key codes and names (`keys`)

**Files:**
- Create: `src/keys.h`, `src/keys.cpp`, `tests/test_keys.cpp`
- Modify: `Makefile` (add `$(BUILD)/test_keys` like `test_magnet`, add it to `test`, add `src/keys.cpp` to `PLUGIN`)

**Interfaces:**
- Produces:

  ```cpp
  namespace keys {
  constexpr uint16_t kNone = 0;
  constexpr uint16_t kSpecialBase = 1000; // RsKeyCodes ESC; NULL = 1056
  constexpr uint16_t kMouseBase = 2000;   // + 1..7 = LEFT MIDDLE RIGHT WHLUP WHLDOWN X1 X2
  struct Binding {
  	uint16_t key[2] = {kNone, kNone}; // the key and its alternative; either may be a mouse button
  	bool operator==(const Binding &o) const { return key[0] == o.key[0] && key[1] == o.key[1]; }
  	bool Empty() const { return key[0] == kNone && key[1] == kNone; }
  };
  std::string IniName(uint16_t code);                      // "Q", "SPC", "LSHIFT", "MIDDLE"; "" for kNone
  uint16_t FromIniName(std::string_view name, bool mouse); // kNone if unknown
  std::string Format(const Binding &b);                    // "kbd:Q,2ndKbd:F1", mouse slots as "mouse:MIDDLE"
  Binding Parse(std::string_view value);                   // the macVC.ini [Bindings] shape; "joy:" parts ignored
  std::string DisplayName(uint16_t code);                  // hints and the page
  }
  ```

  `DisplayName`:
  - Mouse: `LMB MMB RMB "MS WHEEL UP" "MS WHEEL DN" MXB1 MXB2`.
  - Space: `SPC`.
  - Other keys: the ini name.

- [ ] **Step 1: Write the failing tests** in `tests/test_keys.cpp`

```cpp
static void TestIniNames()
{
	CHECK(keys::IniName(1000) == "ESC");
	CHECK(keys::IniName(1032) == "PAD5");
	CHECK(keys::IniName(1046) == "LSHIFT");
	CHECK(keys::IniName(' ') == "SPC");
	CHECK(keys::IniName('Q') == "Q");
	CHECK(keys::IniName(keys::kMouseBase + 2) == "MIDDLE");
	CHECK(keys::IniName(keys::kNone).empty());
}
static void TestFromIniName()
{
	CHECK(keys::FromIniName("LSHIFT", false) == 1046);
	CHECK(keys::FromIniName("LEFT", false) == 1021);              // the arrow key
	CHECK(keys::FromIniName("LEFT", true) == keys::kMouseBase + 1); // the mouse button
	CHECK(keys::FromIniName("SPC", false) == ' ');
	CHECK(keys::FromIniName("#", false) == '#');
	CHECK(keys::FromIniName("NOPE", false) == keys::kNone);
}
static void TestParseAndFormat()
{
	const keys::Binding b = keys::Parse("kbd:Q,2ndKbd:F1");
	CHECK(b.key[0] == 'Q' && b.key[1] == 1001);
	CHECK(keys::Format(b) == "kbd:Q,2ndKbd:F1");
	const keys::Binding m = keys::Parse("mouse:MIDDLE");
	CHECK(m.key[0] == keys::kMouseBase + 2 && m.key[1] == keys::kNone);
	CHECK(keys::Format(m) == "mouse:MIDDLE");
	CHECK(keys::Parse("kbd:PADINS,2ndKbd:LCTRL,mouse:LEFT,joy:1").key[1] == 1049); // a third key is dropped
	CHECK(keys::Parse("").Empty());
	CHECK(keys::Format(keys::Binding{}).empty());
}
static void TestDisplayNames()
{
	CHECK(keys::DisplayName(keys::kMouseBase + 1) == "LMB");
	CHECK(keys::DisplayName(keys::kMouseBase + 4) == "MS WHEEL UP");
	CHECK(keys::DisplayName(' ') == "SPC");
	CHECK(keys::DisplayName(1046) == "LSHIFT");
}
```

- [ ] **Step 2: Run them and see them fail.** Run `make test`. Expected: compile errors (no `keys.h`).
- [ ] **Step 3: Implement** `src/keys.h` / `src/keys.cpp` with the interface above. The special-key names are the 57-entry list in "Facts", the mouse names the 7-entry list.
- [ ] **Step 4: Run** `make test`. Expected: "all key tests passed" and the existing suites still pass.

---

### Task 2: The bindings table (`bindings`, `layout`)

**Files:**
- Create: `src/bindings.h`, `src/bindings.cpp`, `src/layout.h`, `tests/test_bindings.cpp`
- Modify: `Makefile` (`test_bindings` builds `tests/test_bindings.cpp src/bindings.cpp src/keys.cpp`; add `src/bindings.cpp` to `PLUGIN`)

**Interfaces:**
- Consumes: `keys::Binding`, `keys::kNone` (Task 1)
- Produces:

  ```cpp
  namespace bindings {
  // 0..43: the game's e_ControllerAction, in its order ("Facts"); member names are k + the game name in
  // PascalCase (PED_FIREWEAPON -> kPedFireweapon, PED_1RST_PERSON_LOOK_LEFT -> kPed1rstPersonLookLeft, 39 -> kUnused39).
  enum class Action : uint8_t {
  	/* 0..43 */
  	kGameCount = 44,
  	kWeaponWheel = 64, kRadioWheel, kReload, kGreet, kInsult, kBulletTime, kDriveByAim, kDriveByFire,
  	kCinematicCamera, kExpandedMap, kAnswerCall, kModEnd
  };
  enum class Situation : uint8_t { kOnFoot, kDriving, kArmedVehicle, kPlane, kHelicopter, kHunter, kSeaSparrow, kRcBaron, kCount };
  enum class Pad : uint8_t { kNone, kA, kB, kX, kY, kLB, kRB, kLT, kRT, kL3, kR3, kBack, kStart,
  	kUp, kDown, kLeft, kRight, kLeftRight, kLeftStick, kRightStick, kCount };
  enum class Press : uint8_t { kPress, kHold, kTap, kWhileAiming, kWhileDriveByAim };
  enum Switch : uint16_t { kTriggers = 1, kButtons = 2, kDpad = 4, kWheels = 8, kDriveBy = 16, kBulletTime = 32,
  	kInteractions = 64, kPhone = 128, kAll = 255 };
  enum class Scope : uint8_t { kOnFoot, kVehicle, kEverywhere };
  enum class PadFamily : uint8_t { kXbox, kPlayStation, kNintendo };
  struct PadBinding { Pad pad = Pad::kNone; Press press = Press::kPress; bool ours = false; }; // !ours: the game's own button
  PadBinding PadFor(uint16_t switches, Situation s, Action a);
  bool Reachable(uint16_t switches, Situation s, Action a); // a button does it: ours, or the game's own and not taken
  Situation ForVehicle(int16_t model, bool aircraft);
  Situation ForHint(Situation player, Action a); // a vehicle action named on foot: kDriving
  Scope ScopeOf(Action a);
  const char *Name(Action a);                    // the ~k~ name: "PED_FIREWEAPON", "MOD_WEAPON_WHEEL"; null for 39
  bool FromName(std::string_view name, Action &out);
  const char *Token(Pad p);                      // "~X~", "~@u~"; "" for kNone and the sticks
  bool KeyAlias(Action a, Action &game);         // kAnswerCall -> kPedAnswerPhone, kDriveByFire -> kVehicleFireweapon
  bool HasOwnKeys(Action a);                     // a mod action with keys of its own (all but the two aliases)
  keys::Binding DefaultKeys(Action a, const std::function<bool(uint16_t key, Scope scope)> &gameUses);
  PadFamily FamilyForName(std::string_view joystickName);
  }
  ```

  Mod action names:
  - `MOD_WEAPON_WHEEL`, `MOD_RADIO_WHEEL`, `MOD_RELOAD`, `MOD_GREET`, `MOD_INSULT`, `MOD_BULLET_TIME`
  - `MOD_DRIVEBY_AIM`, `MOD_DRIVEBY_FIRE`, `MOD_CINEMATIC_CAMERA`, `MOD_EXPANDED_MAP`, `MOD_ANSWER_CALL`

**`src/layout.h`** declares each placement as a `constexpr bindings::Pad` in `namespace layout`, one comment line each:

| Group | Constants |
|---|---|
| On foot | `kAim = kLT`, `kFire = kRT`, `kWeaponWheel = kLB`, `kPrevWeapon = kLeft`, `kNextWeapon = kRight`, `kMeleeReload = kB`, `kGreet = kX`, `kInsult = kB`, `kBulletTime = kR3` |
| Any situation | `kAnswerCall = kUp`, `kExpandedMap = kDown` |
| Vehicles | `kSideMission = kUp`, `kRadio = kLeftRight`, `kGas = kRT`, `kBrake = kLT`, `kLookBehind = kR3`, `kCinematic = kB`, `kDriveByAim = kLB`, `kDriveByFire = kRB`, `kVehicleGun = kRB`, `kArmedHandbrake = kA` |
| Aircraft | `kClimb = kRT`, `kDescend = kLT`, `kYawLeft = kLB`, `kYawRight = kRB`, `kAircraftGun = kA`, `kHunterRockets = kX` |

**The override table** in `bindings.cpp`:
- Key: `{situations, action, layout constant, press, switches needed}`. A row applies when all its needed switches are on.
- Abbreviations: F on foot, Dr driving, Ar armed vehicle, Air = Pl He Hu Ss Rb.
- Switches: T kTriggers, B kButtons, D kDpad, W kWheels, V kDriveBy, S kBulletTime, I kInteractions, P kPhone.

On foot:

| Situations | Action | Button | Press | Switches |
|---|---|---|---|---|
| F | `kPedFireweapon` | kFire | press | T |
| F | `kPedLockTarget` | kAim | hold | T |
| F | `kPedCycleWeaponLeft` / `kPedCycleTargetLeft` | kPrevWeapon | press / while aiming | D / T+D |
| F | `kPedCycleWeaponRight` / `kPedCycleTargetRight` | kNextWeapon | press / while aiming | D / T+D |
| F | `kPedAnswerPhone` | kWeaponWheel | tap | W |
| F | `kWeaponWheel` | kWeaponWheel | hold | W |
| F | `kReload` | kMeleeReload | press | B |
| F | `kBulletTime` | kBulletTime | while aiming | S |

Any situation:

| Situations | Action | Button | Press | Switches |
|---|---|---|---|---|
| F Dr Ar | `kGreet` | kGreet | press | I |
| F Dr Ar | `kInsult` | kInsult | press | I |
| all | `kAnswerCall` | kAnswerCall | press | P |
| all | `kExpandedMap` | kExpandedMap | press | D |

Vehicles:

| Situations | Action | Button | Press | Switches |
|---|---|---|---|---|
| Dr Ar | `kVehicleAccelerate` | kGas | hold | T |
| Dr Ar | `kVehicleBrake` | kBrake | hold | T |
| Dr Ar | `kVehicleLookleft`, `kVehicleLookright` | kNone | n/a | T |
| Dr Ar | `kVehicleLookbehind` | kLookBehind | hold | T+D |
| Dr Ar Air | `kVehicleChangeRadioStation` | kRadio | tap | D |
| Dr Ar Air | `kRadioWheel` | kRadio | hold | D+W |
| Dr Ar Air | `kToggleSubmissions` | kSideMission | press | D |
| Dr Ar Air | `kCinematicCamera` | kCinematic | hold | B |
| Dr | `kDriveByAim` | kDriveByAim | hold | V |
| Dr | `kDriveByFire`, `kPedFireweapon`, `kVehicleFireweapon` | kDriveByFire | while drive-by aim | V |
| Ar | `kPedFireweapon`, `kVehicleFireweapon` | kVehicleGun | press | B |

Aircraft:

| Situations | Action | Button | Press | Switches |
|---|---|---|---|---|
| Air | `kVehicleAccelerate` | kClimb | hold | T |
| Air | `kVehicleBrake` | kDescend | hold | T |
| He Hu Ss Rb | `kVehicleLookleft` | kYawLeft | hold | T |
| He Hu Ss Rb | `kVehicleLookright` | kYawRight | hold | T |
| Air | `kVehicleLookbehind` | kNone | n/a | T+D |
| Hu | `kVehicleHandbrake` | kAircraftGun | hold | B |
| Hu | `kPedFireweapon`, `kVehicleFireweapon` | kHunterRockets | press | B |
| Ss | `kVehicleHandbrake`, `kPedFireweapon`, `kVehicleFireweapon` | kAircraftGun | press | B |
| Rb | `kPedFireweapon`, `kVehicleFireweapon` | kAircraftGun | press | B |

**Buttons the mod takes** (for `Reachable`): a game action whose own button ("Facts") is taken in that situation, and has no row of ours, can't be pressed.

| Situation | Taken buttons |
|---|---|
| F | LT RT RB (T); B (B); D-pad (D) |
| Dr | LT RT (T); R3 (T+D); A X B (B); LB (V); D-pad (D) |
| Ar | as Dr, but LB isn't taken and RB is (B) |
| Air | LT RT LB RB (T); R3 (T+D); A X B (B); D-pad (D) |

**Other rules:**
- **`ForVehicle`:** model 155 → kHunter, 177 → kSeaSparrow, 194 → kRcBaron. Another aircraft in the `Helicopter()` list → kHelicopter, and any other aircraft → kPlane. 162/137/171 → kArmedVehicle. Anything else → kDriving.
- **`ScopeOf`:**
  - kVehicle for `VEHICLE_*`, `TOGGLE_SUBMISSIONS`, `kRadioWheel`, `kDriveByAim`, `kDriveByFire`, `kCinematicCamera`.
  - kEverywhere for `GO_LEFT/RIGHT`, `CAMERA_CHANGE_VIEW_ALL_SITUATIONS`, `kGreet`, `kInsult`, `kExpandedMap`, `kAnswerCall`, 36..43.
  - kOnFoot for the rest.
- **`DefaultKeys`:** the mod defaults are WeaponWheel `Q`, RadioWheel `G`, Reload `R`, Greet `Y`, Insult `N`, BulletTime `B`, DriveByAim `X`, CinematicCamera `C`, ExpandedMap `M`.
  - Each goes in slot 0, unless `gameUses(key, s)` is true for a scope `s` that overlaps the action's own; then the binding is empty.
  - kEverywhere overlaps all three scopes; kOnFoot and kVehicle overlap only themselves and kEverywhere.
  - `gameUses(key, s)` means "a game action whose scope is exactly `s` has this key".
- **`FamilyForName`:**
  - kPlayStation if the name contains any of `DualSense DualShock DUALSHOCK PlayStation PS3 PS4 PS5` (the list `controls.cpp`'s `SonyPad` uses).
  - kNintendo if it contains `Nintendo`, `Pro Controller` or `Joy-Con`.
  - Else kXbox.

- [ ] **Step 1: Write the failing tests** in `tests/test_bindings.cpp`. Each assertion pins a row or rule above. `kAll` is every switch on.

```cpp
using namespace bindings;
static void TestOnFootModern()
{
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedFireweapon).pad == Pad::kRT);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedLockTarget).pad == Pad::kLT);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedCycleWeaponLeft).pad == Pad::kLeft);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kWeaponWheel).press == Press::kHold);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kPedAnswerPhone).press == Press::kTap);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kAnswerCall).pad == Pad::kUp);
	CHECK(PadFor(kAll, Situation::kOnFoot, Action::kBulletTime).press == Press::kWhileAiming);
	CHECK(!PadFor(kAll, Situation::kOnFoot, Action::kPedSprint).ours); // the game's own A
}
static void TestSwitchOffFallsBackToGame()
{
	CHECK(!PadFor(0, Situation::kOnFoot, Action::kPedFireweapon).ours);
	CHECK(!PadFor(kAll & ~kTriggers, Situation::kDriving, Action::kVehicleAccelerate).ours);
	CHECK(Reachable(0, Situation::kDriving, Action::kVehicleLookleft)); // nothing taken with everything off
}
static void TestDrivingAndArmed()
{
	CHECK(PadFor(kAll, Situation::kDriving, Action::kVehicleChangeRadioStation).pad == Pad::kLeftRight);
	CHECK(PadFor(kAll, Situation::kDriving, Action::kPedFireweapon).press == Press::kWhileDriveByAim);
	CHECK(PadFor(kAll, Situation::kArmedVehicle, Action::kPedFireweapon).pad == Pad::kRB);
	CHECK(PadFor(kAll, Situation::kArmedVehicle, Action::kDriveByAim).pad == Pad::kNone);
	CHECK(PadFor(kAll, Situation::kDriving, Action::kVehicleLookbehind).pad == Pad::kR3);
}
static void TestAircraft()
{
	CHECK(PadFor(kAll, Situation::kHunter, Action::kPedFireweapon).pad == Pad::kX);
	CHECK(PadFor(kAll, Situation::kHunter, Action::kVehicleHandbrake).pad == Pad::kA);
	CHECK(PadFor(kAll, Situation::kHelicopter, Action::kVehicleLookleft).pad == Pad::kLB);
}
static void TestForVehicleAndHint()
{
	CHECK(ForVehicle(155, true) == Situation::kHunter);
	CHECK(ForVehicle(231, true) == Situation::kHelicopter);
	CHECK(ForVehicle(190, true) == Situation::kPlane);     // Skimmer
	CHECK(ForVehicle(162, false) == Situation::kArmedVehicle);
	CHECK(ForVehicle(130, false) == Situation::kDriving);
	CHECK(ForHint(Situation::kOnFoot, Action::kVehicleHandbrake) == Situation::kDriving);
	CHECK(ForHint(Situation::kHunter, Action::kPedFireweapon) == Situation::kHunter);
}
// The audit: every action a hint names is pressable where it can be named. The rows marked GAP
// are filled by Task 7 and are skipped until then.
static void TestHintActionsReachable()
{
	const Action onFoot[] = {Action::kPedFireweapon, Action::kPedLockTarget, Action::kPedCycleWeaponLeft,
	    Action::kPedCycleWeaponRight, Action::kPedCycleTargetLeft, Action::kPedCycleTargetRight, Action::kPedSprint,
	    Action::kPedJumping, Action::kPedDuck, Action::kPedLookbehind, Action::kPedAnswerPhone,
	    Action::kPedSniperZoomIn, Action::kPedSniperZoomOut, Action::kVehicleEnterExit, Action::kGoLeft,
	    Action::kGoRight, Action::kCameraChangeViewAllSituations};
	for (Action a : onFoot)
		CHECK(Reachable(kAll, Situation::kOnFoot, a));
	const Action driving[] = {Action::kVehicleAccelerate, Action::kVehicleBrake, Action::kVehicleHandbrake,
	    Action::kVehicleHorn, Action::kVehicleEnterExit, Action::kToggleSubmissions,
	    Action::kVehicleChangeRadioStation, Action::kVehicleLookbehind, Action::kPedFireweapon};
	for (Action a : driving)
		CHECK(Reachable(kAll, Situation::kDriving, a));
	CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kPedFireweapon));
	CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kVehicleTurretleft));
	// GAP (Task 7): CHECK(Reachable(kAll, Situation::kArmedVehicle, Action::kVehicleHandbrake));
	const Situation air[] = {Situation::kPlane, Situation::kHelicopter, Situation::kHunter, Situation::kSeaSparrow,
	                         Situation::kRcBaron};
	for (Situation s : air) {
		CHECK(Reachable(kAll, s, Action::kVehicleAccelerate));
		CHECK(Reachable(kAll, s, Action::kVehicleBrake));
		CHECK(Reachable(kAll, s, Action::kVehicleEnterExit));
	}
	// GAP (Task 7): kPlane and kHelicopter: kPedFireweapon, kVehicleHandbrake; kPlane: kVehicleLookleft/right.
}
static void TestNamesRoundTrip()
{
	for (int i = 0; i < int(Action::kGameCount); i++) {
		if (i == 39) continue;
		Action a;
		CHECK(Name(Action(i)) && FromName(Name(Action(i)), a) && a == Action(i));
	}
	Action a;
	CHECK(FromName("MOD_ANSWER_CALL", a) && a == Action::kAnswerCall);
	CHECK(!FromName("NOT_AN_ACTION", a));
	CHECK(std::string(Token(Pad::kRT)) == "~V~" && std::string(Token(Pad::kUp)) == "~@u~");
	Action game;
	CHECK(KeyAlias(Action::kAnswerCall, game) && game == Action::kPedAnswerPhone && !HasOwnKeys(Action::kAnswerCall));
}
static void TestDefaultKeySkipsGameKey()
{
	auto none = [](uint16_t, Scope) { return false; };
	CHECK(DefaultKeys(Action::kWeaponWheel, none).key[0] == 'Q');
	auto qOnFoot = [](uint16_t k, Scope s) { return k == 'Q' && s == Scope::kOnFoot; };
	CHECK(DefaultKeys(Action::kWeaponWheel, qOnFoot).Empty());
	auto qInCar = [](uint16_t k, Scope s) { return k == 'Q' && s == Scope::kVehicle; };
	CHECK(DefaultKeys(Action::kWeaponWheel, qInCar).key[0] == 'Q'); // look left in a car doesn't clash on foot
}
static void TestFamilyForName()
{
	CHECK(FamilyForName("DualSense Wireless Controller") == PadFamily::kPlayStation);
	CHECK(FamilyForName("Nintendo Switch Pro Controller") == PadFamily::kNintendo);
	CHECK(FamilyForName("Xbox Wireless Controller") == PadFamily::kXbox);
	CHECK(FamilyForName("") == PadFamily::kXbox);
}
```

- [ ] **Step 2: Run** `make test`. Expected: compile failure (no `bindings.h`).
- [ ] **Step 3: Implement** `layout.h` and `bindings.h/.cpp` from the tables above.
  - The overrides are a static array of rows. `PadFor` returns the first row matching `(situation, action)` whose switches are all on. Otherwise it returns the game's own button with `ours = false`.
  - `Reachable` is: ours and not kNone; or not ours, the game button isn't kNone, and it isn't in the taken set.
- [ ] **Step 4: Run** `make test`. Expected: "all bindings tests passed".

---

### Task 3: Expanding hint text and the rewritten hints (`hinttext`)

**Files:**
- Create: `src/hinttext.h`, `src/hinttext.cpp`, `tests/test_hinttext.cpp`
- Modify: `Makefile` (test target; add `src/hinttext.cpp` to `PLUGIN`)

**Interfaces:**
- Consumes: `bindings::Switch` bits (Task 2)
- Produces:

  ```cpp
  namespace hinttext {
  constexpr size_t kCapacity = 256;
  using Resolver = std::function<std::u16string(std::string_view name)>;
  // Each ~k~~NAME~ in `text` becomes resolve(NAME) (empty: the token goes); other ~x~ tokens stay. In place,
  // never past kCapacity units with the terminator.
  void Expand(uint16_t *text, const Resolver &resolve);
  // The mod's English text for a hint whose controls it changed, or null (switches as in bindings).
  const char *Rewrite(std::string_view key, uint16_t switches);
  }
  ```

  **The rewrites** (more are added by Task 7's audit):

  | Key | Needs | Text |
  |---|---|---|
  | `ANSWER` | `kPhone` | `Press ~h~~k~~MOD_ANSWER_CALL~~w~ to answer your cell phone.` |
  | `HELP31` | `kDriveBy` | `To do a drive-by, hold ~h~~k~~MOD_DRIVEBY_AIM~~w~ and aim.` |
  | `HELP32` | `kDriveBy` | `Then fire using ~h~~k~~MOD_DRIVEBY_FIRE~~w~.` |
  | `GEN3_40` | `kDriveBy` | `To ~h~shoot ~w~from a ~h~motorbike ~w~hold ~h~~k~~MOD_DRIVEBY_AIM~~w~ and press ~h~~k~~MOD_DRIVEBY_FIRE~~w~.` |
  | `GEN3_64` | `kDriveBy` | `To shoot while on a bike hold ~h~~k~~MOD_DRIVEBY_AIM~~w~ and press ~h~~k~~MOD_DRIVEBY_FIRE~~w~.` |

- [ ] **Step 1: Write the failing tests** in `tests/test_hinttext.cpp`. A helper `Wide(const char *)` fills a `uint16_t[hinttext::kCapacity]`, and `Ascii(const uint16_t *)` converts back.

```cpp
static std::u16string Fire(std::string_view n) { return n == "PED_FIREWEAPON" ? u"~V~" : u""; }
static void TestExpandReplacesActionToken()
{
	uint16_t t[hinttext::kCapacity];
	Wide(t, "Press the ~h~~k~~PED_FIREWEAPON~~w~ to fire.");
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == "Press the ~h~~V~~w~ to fire.");
}
static void TestExpandDropsUnknownAndKeepsTwo()
{
	uint16_t t[hinttext::kCapacity];
	Wide(t, "~k~~PED_FIREWEAPON~ or ~k~~NOPE~ then ~k~~PED_FIREWEAPON~");
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == "~V~ or  then ~V~");
}
static void TestExpandTruncatesAtCapacity()
{
	uint16_t t[hinttext::kCapacity + 8];
	std::fill(std::begin(t), std::end(t), uint16_t('#'));
	std::string longText(250, 'a');
	longText += "~k~~PED_FIREWEAPON~";
	Wide(t, longText.c_str());
	hinttext::Expand(t, [](std::string_view) { return std::u16string(40, u'K'); });
	CHECK(t[hinttext::kCapacity - 1] == 0);   // terminated within the buffer
	CHECK(t[hinttext::kCapacity] == '#');     // nothing written past it
}
static void TestRewrites()
{
	CHECK(hinttext::Rewrite("HELP31", bindings::kAll) != nullptr);
	CHECK(std::string(hinttext::Rewrite("HELP31", bindings::kAll)).find("~k~~MOD_DRIVEBY_AIM~") != std::string::npos);
	CHECK(hinttext::Rewrite("HELP31", 0) == nullptr);
	CHECK(hinttext::Rewrite("ANSWER", bindings::kPhone) != nullptr);
	CHECK(hinttext::Rewrite("BANK_C", bindings::kAll) == nullptr);
}
```

- [ ] **Step 2: Run** `make test`. Expected: compile failure.
- [ ] **Step 3: Implement** `hinttext.cpp`: scan for `~k~~`, read the name up to the next `~`, append the resolver's output, copy everything else verbatim, and stop at `kCapacity - 1`.
- [ ] **Step 4: Run** `make test`. Expected: "all hinttext tests passed".

---

### Task 4: Hints name the mod's buttons (`hints`, game side)

**Files:**
- Create: `src/hints.h`, `src/hints.cpp`
- Modify:
  - `src/game.h`, `src/game.cpp`: `controlsManager` and `GetWideStringOfCommandKeys` from "Facts". Each later task binds what it uses.
  - `tools/patch_macvc.py`: add the `InsertPlayerControlKeysInString` hook under a comment `# controls: hints name the mod's buttons`.
  - `src/minimap.cpp`: call `hints::Install()` after `controls::Install()`, logging on failure like its neighbours.
  - `src/menu.cpp`, `Hook_TextGet`: before the lookup, `if (const uint16_t *r = hints::RewriteText(key)) return const_cast<uint16_t *>(r);`.
  - `Makefile`: add `src/hints.cpp`.

**Interfaces:**
- Consumes: `bindings::*` (Task 2), `hinttext::Expand`, `hinttext::Rewrite` (Task 3)
- Produces:

  ```cpp
  namespace hints {
  bool Install();
  // The player's situation now (bindings::ForVehicle on his vehicle, else kOnFoot).
  bindings::Situation Now();
  // The ini switches as bindings::Switch bits: ModernTriggers kTriggers, ModernButtons kButtons, DpadActions kDpad,
  // Wheels kWheels, DriveByAim kDriveBy, BulletTime kBulletTime, Interactions kInteractions, Phone kPhone.
  uint16_t Switches();
  // The hint the mod rewrote for this text key (CText::Get), wide, or null.
  const uint16_t *RewriteText(const char *key);
  // What a hint shows for `action` now: an icon token on a pad, else the key names ("Q", "LMB or TAB").
  std::u16string Glyph(bindings::Action action);
  bool KeyboardNow(); // !CPad::IsAffectedByController
  }
  ```

- **Resolver for `Expand`:** the name goes through `bindings::FromName`.
  - **On a pad:**
    - If `PadFor(Switches(), ForHint(Now(), a), a)` is ours and not kNone, return `Token(pad)`.
    - For a game action, otherwise call the game's `GetWideStringOfCommandKeys(controlsManager, a, buf, 64)`. That's its own setup table.
  - **On keyboard:**
    - A game action: the game's function.
    - A mod action: `actions::Keys(a)` (Task 8; until then, `bindings::DefaultKeys` with no clashes) as `keys::DisplayName` of slot 0, plus ` or ` and slot 1 if set.
    - An alias: the game's function for its game action.
- **Hook:** `Hook_InsertKeys(uint16_t *text)` calls `hinttext::Expand(text, resolver)`.
  - It doesn't call the original, so mod names like `MOD_ANSWER_CALL` work.
  - If `hints::Install` failed, the game's original runs (the hook isn't installed).
- **`RewriteText`:** keeps one static wide copy per rewritten key, converted from `hinttext::Rewrite(key, Switches())` (ASCII to UTF-16).

- [ ] **Step 1: Implement** the above.
- [ ] **Step 2: Build and test.** Run `make all && make test`. Expected: no warnings, all suites pass.
- [ ] **Step 3: Install.** Run `./install.sh` (with the game closed). Expected: `patched 65 hooks and 6 skips`, `Installed`.
- [ ] **Step 4: Ask the user to check in the game, on a pad:**
  - A for-sale sign's hint shows LB.
  - The first weapon tutorial's fire hint shows RT.
  - On keyboard, the same hints show the game's keys.

  Their report is the pass/fail.

---

### Task 5: The icon set of the pad you have

**Files:**
- Modify:
  - `src/game.h/.cpp`: `FontLoadButtons`, `fontButtonsSlot`.
  - `src/controls.h/.cpp`: replace `SonyPad` with `bindings::FamilyForName`; add `bindings::PadFamily Family()`. The rule is as today's `gPlayStation`: any connected gamepad that is PlayStation wins, then Nintendo, else Xbox. Scanned every 120 polls as now; `PlayStationPad()` returns `Family() == kPlayStation`.
  - `src/hints.cpp`.

**Interfaces:**
- Produces: `controls::Family()`; `hints::Frame()`, called from `controls.cpp`'s `Hook_Update` at its start (it runs in menus too).
- **Behaviour:** when `controls::Family()` differs from the set loaded, call `FontLoadButtons` with:

  | Family | File |
  |---|---|
  | kPlayStation | `"MODELS/PS3BTNS.TXD"` |
  | kNintendo | `"MODELS/NSWBTNS.TXD"` |
  | kXbox | `"MODELS/X360BTNS.TXD"` |

  - Log once: `hints: button icons <file>`.
  - Don't reload while no gamepad is connected (keep the last set).
  - The game loads X360 at start-up.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean, `Installed`.
- [ ] **Step 3: Ask the user to check in the game with the DualSense:**
  - Hints show PlayStation symbols.
  - The log has `hints: button icons MODELS/PS3BTNS.TXD`.

---

### Task 6: D-pad, Start and Back icons in text

**Files:**
- Modify:
  - `tools/patch_macvc.py`: both `ParseToken` hooks.
  - `src/game.h/.cpp`: `fontPs2Symbol`, `fontButtonSprites`.
  - `src/hints.h/.cpp`.
  - `src/minimap.cpp`, `Hook_SpriteDraw`: first line `if (hints::SpriteDraw(sprite, rect, col)) return;`.

**Interfaces:**
- Produces: `bool hints::SpriteDraw(const CSprite2d *sprite, const CRect &rect, const CRGBA &col)`. True if `sprite` is `fontButtonSprites + 8 * (18..25)`; it then draws the matching texture in `rect`, tinted by `col`, with `Im2DCanvas` (`SetHudRenderStates`, `SetTexture(raster)`, two textured triangles, `Flush`).
- **Textures:** after each `FontLoadButtons` (and once on the first `Frame` for the game's own set), look up `up down left right dlr dud start select` with `api.TexDictionaryFindNamedTexture(TxdDictionary(*fontButtonsSlot), name)`. The raster is the `RwTexture`'s first field.
- **ParseToken hooks:** if the token is `~@x~` with `x` in `u d l r h v s b`:
  - set `*fontPs2Symbol` to `18 + "udlrhvsb".find(x)`;
  - return the pointer past the closing `~`.

  Otherwise call the original.

- [ ] **Step 1: Check the binary before relying on this.**
  - **Where `s` points:** in `dis.txt` (or `objdump -d` on `macVC.vanilla`), read the first instructions of both `ParseToken` overloads. Confirm whether `s` points at the opening `~` (the letter is read at `s + 1`) and that the return value points past the closing `~`. Match that.
  - **Who reads `CFont::PS2Symbol`** (`0x100370c54`): list every instruction that does. Confirm that only `DrawButton` turns it into a sprite address, that it doesn't dereference the sprite itself, and that width code only compares it with -1.

  If anything else indexes `ButtonSprite` by it, stop and report: the out-of-range scheme would then be unsafe.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build and install.** Run `make all && make test && ./install.sh`. Expected: `patched 67 hooks`, `Installed`.
- [ ] **Step 4: Ask the user to check in the game:** a hint with `~@u~` (the `ANSWER` rewrite, when a call comes) shows the D-pad-up icon at the size of the other icons.

---

### Task 7: Fill the layout's gaps

**Files:**
- Modify: `src/bindings.cpp` (new rows, `ForVehicle`), `tests/test_bindings.cpp` (un-skip the GAP checks), `src/controls.cpp`, `src/hinttext.cpp` (rewrites the audit finds)

**The fills** (the user approved filling gaps):

| Situations | Action | Button | Press | Switches |
|---|---|---|---|---|
| Ar | `kVehicleHandbrake` | layout::kArmedHandbrake (A) | hold | B |
| Pl He | `kPedFireweapon`, `kVehicleFireweapon`, `kVehicleHandbrake` | layout::kAircraftGun (A) | press | B |
| Pl | `kVehicleLookleft` | kYawLeft | hold | T |
| Pl | `kVehicleLookright` | kYawRight | hold | T |

Also `ForVehicle(160, false)` (the Predator) → kArmedVehicle.

- [ ] **Step 1: Turn the tests on.** Uncomment the GAP checks in `TestHintActionsReachable`; add `CHECK(ForVehicle(160, false) == Situation::kArmedVehicle)`.
- [ ] **Step 2: Run** `make test`. Expected: those checks FAIL.
- [ ] **Step 3: Add the rows** and the Predator to `bindings.cpp`.
- [ ] **Step 4: Run** `make test`. Expected: PASS.
- [ ] **Step 5: Make `controls.cpp` read `layout.h` and match the new rows:**
  - **Reading `layout.h`:** every physical button or trigger it reads for a remap becomes a `layout::` constant, through two local helpers:
    - `bool Down(const Gamepad &, bindings::Pad)`: buttons map to the `GamepadButton` index; LT/RT are `> kTriggerPressed`.
    - `bool Pressed(const Gamepad &, bindings::Pad)`.

    A grep for `kLB`, `kRB`, `kUp`, `kDown`, `kLeft`, `kRight`, `kRThumb`, `now.lt`, `now.rt` and `buttons[kB]`/`[kX]`/`[kA]` in the remap code must find only the helpers.
  - `ArmedVehicle` includes 160.
  - In `FaceButtonsInVehicle`, an armed vehicle's handbrake is A: `PadJoy(pad, kPadRB) = Down(now, layout::kArmedHandbrake) ? 255 : 0`, instead of 0.
  - `TriggersFly` turns every aircraft's tail with LB/RB (drop the `Helicopter || RcBaron` condition).
  - `FaceButtonsInAircraft`: an aircraft that isn't the Hunter, Sea Sparrow or RC Baron fires and drops on A. `PadJoy` Circle and RB are both set from `layout::kAircraftGun`.
- [ ] **Step 6: Audit the hints.**
  - For each of the 169 English hint entries (`TEXT/american.gxt`; `gxt.py` in the session scratchpad can list them), check each `~k~` action against the situations where the hint can appear.
  - Where the named button reads wrong, add a rewrite to `hinttext.cpp` (with its switches) and a `TestRewrites` assertion. Known: none beyond Task 3's.
  - Record what was checked in a short table at the end of this plan file under "Audit results".
- [ ] **Step 7: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 8: Ask the user to check in the game:**
  - The RC Raider / RC Goblin drop and detonate on A.
  - The Predator's cannon fires on RB.
  - The Rhino's handbrake works on A.
  - The Dodo / Skimmer rudder works on LB/RB.

---

### Task 8: Keyboard keys for the mod's own actions (`actions`)

**Files:**
- Create: `src/actions.h`, `src/actions.cpp`
- Modify:
  - `src/config.h/.cpp`: `std::string modKeys[16]`, indexed by `action - kWeaponWheel`, filled by `Apply` for keys starting `MOD_` that `bindings::FromName` knows. The default ini gets a commented `[Keys]` section listing each `MOD_` name with its default, e.g. `; MOD_WEAPON_WHEEL=kbd:Q`.
  - `src/game.h/.cpp`: `GetIsKeyboardKeyDown`, `GetIsMouseButtonDown`.
  - `src/controls.cpp`, `src/hints.cpp` (use `actions::Keys`).
  - `Makefile`.

**Interfaces:**
- Consumes: `keys::*` (Task 1), `bindings::*` (Task 2)
- Produces:

  ```cpp
  namespace actions {
  // The keys of the mod's own action (the ini's MOD_ line, else bindings::DefaultKeys against the game's
  // bindings); for an alias, its game action's.
  keys::Binding Keys(bindings::Action a);
  // The game's keys for one of its actions: KEYBOARD and OPTIONAL_EXTRA, or the MOUSE button (kMouseBase + n)
  // in the first empty slot.
  keys::Binding GameKeys(bindings::Action a);
  // Goes up whenever a binding changes; the hints refresh what's on screen when it does.
  uint32_t Version();
  // From CPad::Update each frame (controls.cpp): reads the keys' state.
  void Update();
  bool KeyHeld(bindings::Action a);
  bool KeyPressed(bindings::Action a);   // went down this frame
  bool KeyReleased(bindings::Action a);
  }
  ```

  - **Loading:** lazy, on the first `Update`, after the game has read `macVC.ini [Bindings]`. The `gameUses(key, scope)` passed to `DefaultKeys` is true if a game action with `bindings::ScopeOf(action) == scope` has `key` in `GameKeys`. `DefaultKeys` itself asks about each overlapping scope.
  - **State:** keys below `kMouseBase` use `GetIsKeyboardKeyDown(controlsManager, key)`; mouse buttons use `GetIsMouseButtonDown(controlsManager, key - kMouseBase)`. Edges are computed from the previous frame's state.
- **`controls.cpp`:** call `actions::Update()` in `Hook_Update` right after `Poll()`, outside menus. The pad paths stay; add:
  - **Weapon wheel (on foot):** hold `KeyHeld(kWeaponWheel)` for `kHoldToOpen` → `OpenWheel()`. `KeyReleased` → `wheel::Close(true)`. A short tap does nothing on keyboard; `SteerWheel` already steers by mouse.
  - **Radio wheel (in a vehicle):** the same with `kRadioWheel`.
  - **Reload:** `KeyPressed(kReload)` with a gun does what B does in `FaceButtonsOnFoot`'s reload branch.
  - **Greet / Insult:** `interact::Prompting() && KeyPressed(kGreet | kInsult)` → `interact::Respond(kGreet)`.
  - **Bullet time:** `KeyPressed(kBulletTime)` sets `PadNew(pad, kPadR3) = 255` after the merge, before `shootdodge::FromPad`.
  - **Drive-by:**
    - `driveby::SetInput` aims while `lbAims && (LB || KeyHeld(kDriveByAim))`, and fires on RB, or on the game's `VEHICLE_FIREWEAPON` keys (`KeyHeld(kDriveByFire)`) while aiming by key.
    - Aiming by key turns with the mouse: yaw = `mx * *mouseAccelHorzntl`, pitch = `-my * …`. Bind `_ZN7CCamera20m_fMouseAccelHorzntlE` as `float *mouseAccel`. Zero `newMouseState +8/+0xc` after use, as `SteerWheel` does.
    - While aiming by key, zero the game's vehicle fire: `PadNew(pad, kPadCircle) = 0` after the merge.
  - **Cinematic camera:** `FaceButtonsInVehicle` / `InAircraft` hold the cinematic camera while `Down(now, layout::kCinematic) || KeyHeld(kCinematicCamera)`.
  - **Expanded map:** `KeyPressed(kExpandedMap)` → `minimap::ToggleExpanded()`.
  - **Answer call:** `phone::Ringing() && KeyPressed(kAnswerCall)` → `phone::Answer()`.
  - **Gating:** each key works only when its feature's ini switch is on (`Wheels`, `ModernButtons`, `Interactions`, `BulletTime`, `DriveByAim`, `Phone`). The expanded map always works.

- [ ] **Step 1: Implement** `actions` and the config parsing.
- [ ] **Step 2: Wire `controls.cpp`** as listed.
- [ ] **Step 3: Check the keyboard-only path.** With no pad (`Poll()` returns `live = false`), make sure every keyboard path above still runs: none may sit behind an early `return` for a missing pad (`LeftStick` returns early; keep the new code out of that).
- [ ] **Step 4: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 5: Ask the user to check in the game with the pad unplugged:**
  - Q opens the weapon wheel and the mouse picks.
  - G opens the radio wheel in a car.
  - X + mouse aims a drive-by and LMB fires.
  - B in aim slows time.
  - Y/N answer a greet prompt.
  - C holds the cinematic camera, M expands the map, R reloads.
  - The hints name these keys.

---

### Task 9: Text already on screen changes when you switch

**Files:**
- Modify: `src/game.h/.cpp` (the three `CHud` buffers), `src/hints.cpp`

**Interfaces:**
- Consumes: `actions::Version()` (Task 8), `controls::Family()` (Task 5), `hints::Now()`
- **Behaviour:**
  - **Saving:** in `Hook_InsertKeys`, when the return address is the `SetHelpMessage` site (image offset `0x1fe8a8`), keep a copy of the raw text before expanding it, and of the result after.
  - **Detecting a change:** `hints::Frame()` computes a state from `(KeyboardNow(), Family(), Version(), Now())`. When it differs from the last frame's, and `helpMessage` still equals the saved result, it does the following:
    - Copy the raw text into a scratch buffer, `Expand` it, and write the result into `helpMessage`, `lastHelpMessage` and `helpMessageToPrint`, so `CHud::Draw` doesn't see a new message.
    - Save the new result.

- [ ] **Step 1: Implement.**
- [ ] **Step 2: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 3: Ask the user to check in the game:**
  - With a "Press … to purchase" box up, touch the keyboard and then the pad. The button name switches each time, and the box doesn't restart its fade-in.
  - Get into a car under a fire hint: it changes to the car's button.

---

### Task 10: The mod's prompts name their action

**Files:**
- Modify: `src/prompt.h`, `src/prompt.cpp`, `src/climb.cpp:519`, `src/interact.cpp:206-207`, `src/phone.cpp:584`

**Interfaces:**
- Consumes: `hints::Glyph`, `hints::KeyboardNow` (Task 4)
- Produces:

  ```cpp
  namespace prompt {
  float Height(float screenH);
  // A prompt ending at `right`, centred on `cy`, for `action`: its button's icon on a pad (the font's, as in
  // hints), or a key cap with the key's name on the keyboard. Text is queued for the next text::Flush.
  float Draw(Canvas &canvas, float right, float cy, float screenH, bindings::Action action, const char *label, float alpha = 1.0f);
  }
  ```

  - `prompt::Button` and the vector button drawings go.
  - **Pad:** the icon is drawn by queuing the token as text (e.g. `"~O~"`) with `text::Draw`, so it's the same icon as the hints.
  - **Keyboard:** the key cap is a `RoundRectOutline` (from `canvas.h`) around `keys::DisplayName` text.
  - **Callers:** climb → `kPedJumping` (climbing is the jump). Interact → `kGreet` / `kInsult`. Phone → `kAnswerCall`.

- [ ] **Step 1: Implement** and update the callers.
- [ ] **Step 2: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 3: Ask the user to check in the game:**
  - The greet prompt shows the PlayStation Square / Circle icons on a DualSense, and Y / N key caps on the keyboard.
  - The phone prompt shows the D-pad-up icon.

---

### Task 11: README and the user's checklist

**Files:**
- Modify: `README.md` (controls section: the mod's keyboard keys and the `[Keys]` lines; Settings: `MOD_…` keys; a line on hints following the pad and keyboard, and on the gaps filled)

- [ ] **Step 1: Update the README.** Keep the existing tone; list the defaults from Task 2.
- [ ] **Step 2: Final verification.**
  - Run `make clean && make all && make test`. Expected: all suites pass, no warnings.
  - Run `./install.sh`. Expected: `Installed`.
- [ ] **Step 3: Report** to the user what was verified by tests, and list the in-game checks from Tasks 4–10 that are still theirs to make.

## Audit results

All 169 English hints that name a button (28 actions) were checked against the situations they appear in, with every ini switch on.

| Action (hints) | Where | Names | Result |
|---|---|---|---|
| PED_ANSWER_PHONE (51) | on foot: buying property, pickups, the camera behind (HELP50-52) | LB, a tap | right; ANSWER rewritten to D-pad up |
| PED_FIREWEAPON (46) | on foot (sniper, melee, camera, rifle) | RT | right |
| | armed: Predator (PBOAT_1/2), RC Bandit (RCR1_3), fire truck (SPRAY_1/4) | RB | right after the Predator fill |
| | aircraft: RC Raider/Goblin (SEG3_4, RCH1_8, TEX3_4/29, HAT2_B3), RC Baron (RCPL1_6) | A | right after the RC helicopter fill |
| | car bombs (GA_6/6B/7/7B), bikes (GEN3_40/41/64/65/66) | A | **gap filled**: a car's own gun on A |
| | the drive-by tutorial (HELP32/33) | — | rewritten: LB to aim, RB to fire |
| PED_LOCK_TARGET (11) | on foot | LT | right |
| TOGGLE_SUBMISSIONS (9) | vehicles, also shown on foot | D-pad up | right |
| VEHICLE_ENTER_EXIT (9), PED_SPRINT (8), PED_JUMPING, PED_DUCK, PED_LOOKBEHIND | | the game's own (Y, A, X, L3, R3) | right: not taken |
| VEHICLE_HORN (7) | cars, the fire truck, the ice cream van | L3 | right |
| VEHICLE_HANDBRAKE (6) | cars (HELP6_*) | RB | right |
| | RC helicopters' bombs (BOLLOX, TEX3_39/40) | A | right after the fill |
| VEHICLE_ACCELERATE (5), VEHICLE_BRAKE (2) | cars; planes (PLANE_4/H); helicopters (TEX3_22/26/38) | RT, LT | right |
| PED_SNIPER_ZOOM_IN/OUT (5) | on foot, scoped | X, A (the game's) | right |
| VEHICLE_LOOKLEFT/RIGHT (3) | helicopter yaw (TEX3_24), look behind with both (TEX3_32) | LB, RB | right |
| | the drive-by tutorial (HELP31) | — | rewritten |
| VEHICLE_TURRET* (5), GO_LEFT/RIGHT (2) | the turret, leaning on a bike, tilting a helicopter | the sticks (the game's) | right |
| CAMERA_CHANGE_VIEW (2) | everywhere | Back (the game's text "BACK") | right |
| PED_CYCLE_TARGET_LEFT/RIGHT (2), PED_CYCLE_WEAPON_LEFT/RIGHT | on foot | D-pad left/right (while aiming: targets) | right; to confirm in the game |
