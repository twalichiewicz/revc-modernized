# Controls 2/2: the Controls Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One Vice City-styled Controls page replaces the game's Controller Setup pages. Its Controller tab has a status dot, a picture of your pad with a legend for each situation and a live button test, and the pad settings. Its Keyboard & Mouse tab rebinds every action's keys and holds the mouse settings.

**Architecture:**
- A pure, unit-tested model (`controlsmodel`):
  - what the legend and the key lists show;
  - the status classification;
  - the rebinding conflict rule;
  - the page's focus and navigation.
- A game-side view (`controlspage`) takes over the game's page 26 ("CONTROLLER SETUP"). The mod's menu hooks hand it:
  - drawing, after the game's frontend has drawn its background, title and Back;
  - input: the high-level up / down / left / right / select the game computes in `ProcessUserInput`, mouse clicks, and raw keys while capturing a new key.
- It reads and writes the bindings through plan 1's `bindings` / `actions` / `keys` and the game's own `CControllerConfigManager`.

**Tech Stack:** as plan 1 (C++17, statically patched hooks, `make test`).

**Spec:** `docs/superpowers/specs/2026-10-26-controls-and-hints-design.md`. The look is pinned by the approved mockup `.superpowers/brainstorm/22900-1793053865/content/controls-vc-8.html`, whose 1600×900 stage coordinates this plan uses.

**Builds on:** plan 1 (`docs/superpowers/plans/2026-10-26-controls-hints.md`): `bindings.h`, `layout.h`, `keys.h`, `actions.h`, `hints.h`, `controls::Family()`.

## Global Constraints

- Every Global Constraint of plan 1 applies: the target binary, the style, hooks in `DEFAULT_HOOKS`, no commits, and never killing the game.
- **Colours:**

  | Use | Colour |
  |---|---|
  | Heading pink | (255,150,225) |
  | Ready green | (124,204,116) |
  | Attention amber | (232,176,74) |
  | Dim white | 60% alpha |
  | Boxes | black at 62% |

- **Fonts:** headings in `text::Style` font 2 (the heading font), everything else in font 1, as `text.h` draws them.
- **Coordinates:** the mockup's 1600×900 stage maps to the screen with `s = ScreenHeight() / 900`, centred horizontally: `x = (ScreenWidth() - 1600 * s) / 2 + X * s`.
- **Only control settings on this page.** Head bob isn't here (spec).
- **Hidden and fixed:** the Standard/Classic method stays Standard (`FrontEndMenuManager + 0x5b` set to 0 on entering the page), and the hint icons follow the pad (plan 1).

## Review Focus

1. **Capturing a key, then pressing Escape:** the capture is cancelled, the page stays, and the menu doesn't close. Pinned by `TestCaptureEscapeCancels` (Task 2).
2. **A key already used by another action in an overlapping situation:** it moves to the new action, the other loses it, the keyboard status turns amber, and an action in a non-overlapping situation keeps it. Pinned by `TestRebindTakesKeyFromOverlap` and `TestRebindKeepsNonOverlap` (Task 1).
3. **Unplugging the pad while the Controller tab is open:** the dot goes hollow grey and the picture and legend stay drawn. Pinned by `TestStatusMissing` (Task 1); the view is in Task 5.
4. **Mouse clicks on the page's own rows don't reach the game's frontend** (where a click on the hidden list would select "Back"). Task 6 adds a step for it.
5. **Leaving and re-entering the page:** the trimmed entry list stays trimmed after the game rebuilds its custom options. Task 4 re-applies the trim each time the page is entered.

---

## Facts read from the binary

- **Menu pages:**
  - Options is page 27. Its entry "Controller Setup" (`FEO_CON`) opens page 26.
  - Page 26 has the title key `FET_CTL` ("CONTROLLER SETUP") and these entries:

    | Entry | Text | Action | Opens |
    |---|---|---|---|
    | `FET_STI` | Standard Controls | 49 | |
    | `FEC_RED` | Redefine Controls | 36 | page 30 |
    | `FEC_JOD` | Detect Joystick | 4 | page 42 |
    | `FEC_MOU` | Mouse Settings | 4 | page 31 |
    | `FET_DEF` | Restore Defaults | 48 | |

  - Back is action 35 (`kActionGoBack` in `menu.cpp`).
  - `aScreens` is filled at start-up, and macVC's custom options may add entries.
- **Game settings:**

  | Setting | Where it lives | ini |
  |---|---|---|
  | Vibration | `_FrontEndMenuManager + 9` (bool) | `Vibration` |
  | Method | `_FrontEndMenuManager + 0x5b` (int8, 0 Standard) | `Method` |
  | Invert pad look | `CPad::bInvertLook4Pad` (already `api.invertLookForPad`) | `InvertPad` |
  | Mouse sensitivity | `CCamera::m_fMouseAccelHorzntl` (already `api.mouseAccel`), clamped by `CMenuManager::CheckSliderMovement` to [1/3200, 1/200] | `HorizantalMouseSens` |
  | Invert mouse | `_MousePointerStateHelper + 1` (bool) | `InvertMouseVertically` |
  | Mouse steering | `CVehicle::m_bDisableMouseSteering` (bool, inverted) | `DisableMouseSteering` |
  | The game's joystick name | the 128-byte buffer at image offset `0x7a14f4` | `JoystickName` |

  The joystick the game reads is `joy1id`: `int8` at `*(RsGlobal + 0x20) + 0x21`, as `controls.cpp`'s `GameJoystick()` reads it.
- **Saving:** `_Z15SaveINISettingsv` writes `macVC.ini` (vibration, mouse). `_Z25SaveINIControllerSettingsv` writes `[Controller]` and `[Bindings]`.
- **Rebinding the game's actions** (`CControllerConfigManager`, `this = ControlsManager`; types: 0 keyboard, 1 the extra key, 2 mouse):
  - `_ZN24CControllerConfigManager33ClearSettingsAssociatedWithActionE18e_ControllerAction15eControllerType` `(ctrl, action, type)`
  - `_ZN24CControllerConfigManager36SetControllerKeyAssociatedWithActionE18e_ControllerActioni15eControllerType` `(ctrl, action, key, type)`
  - `_ZN24CControllerConfigManager34SetMouseButtonAssociatedWithActionE18e_ControllerActioni` `(ctrl, action, button)`
  - `_ZN24CControllerConfigManager17ResetSettingOrderE18e_ControllerAction` `(ctrl, action)`
  - `_ZN24CControllerConfigManager24GetIsKeyboardKeyJustDownE10RsKeyCodes` `(ctrl, key)`
- **Pad pictures** (texture `fe_controller`, 256×256): `MODELS/FRONTEND_DS4.TXD` (PlayStation), `MODELS/FRONTEND_XONE.TXD` (Xbox), `MODELS/FRONTEND_NSW.TXD` (Nintendo). Load them with the mod's existing `api.TxdAddSlot` / `TxdLoad` / `TxdAddRef` and `TexDictionaryFindNamedTexture(TxdDictionary(slot), "fe_controller")`; the raster is the texture's first field.
- **Button centres on the DS4 picture,** in its 256 px (measured for the mockup):

  | Button | Centre | Button | Centre |
  |---|---|---|---|
  | L2 | (57,40) | R2 | (199,40) |
  | L1 | (60,52) | R1 | (196,52) |
  | D-pad up | (52,92) | Options (Start) | (172,68) |
  | D-pad left | (41,103) | Share (Back) | (86,68) |
  | D-pad right | (63,103) | Triangle | (201,90) |
  | D-pad down | (52,114) | Circle | (218,104) |
  | left stick | (92,138) | Cross | (201,119) |
  | right stick | (163,138) | Square | (184,104) |

  The Xbox One and Switch pictures are measured in Task 5.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/controlsmodel.h/.cpp` (new, pure) | Legend rows per page situation; the key lists; status; unbound count; the rebind rule; focus and navigation |
| `src/inifile.h/.cpp` (new, pure) | Replacing or adding one `key=value` line in an ini text, in its section |
| `src/controlspage.h/.cpp` (new) | The page: takeover of page 26, drawing, input, key capture, saving |
| `src/actions.h/.cpp` | `SetKeys` for the mod's actions (saved to the mod's ini) |
| `src/controls.h/.cpp` | `Pads()` (the gamepads connected, for the status) and `Down(bindings::Pad)` (the live test) |
| `src/menu.cpp` | Hands page 26 to `controlspage`; the text keys `FEO_CON` / `FET_CTL` |
| `src/config.h/.cpp` | `SaveValue(key, value)` through `inifile` |
| `src/game.h/.cpp`, `Makefile`, `README.md` | Wiring |
| `tests/test_controlsmodel.cpp`, `tests/test_inifile.cpp` (new) | Unit tests |

---

### Task 1: The model: legend, key lists, status, rebinding

**Files:**
- Create: `src/controlsmodel.h`, `src/controlsmodel.cpp`, `tests/test_controlsmodel.cpp`
- Modify: `Makefile` (test target with `src/controlsmodel.cpp src/bindings.cpp src/keys.cpp`; add `src/controlsmodel.cpp` to `PLUGIN`)

**Interfaces:**
- Consumes: `bindings::*`, `layout::*`, `keys::*` (plan 1)
- Produces:

  ```cpp
  namespace controlsmodel {
  enum class PageSituation : uint8_t { kOnFoot, kDriving, kFlying, kPauseMap, kCount };
  struct LegendRow { bindings::Pad pad; std::string label; std::string more; }; // more: a second line ("CLICK WHILE AIMING: BULLET TIME")
  // The pad's buttons and what they do there, from bindings::PadFor (the pause map: a fixed list).
  std::vector<LegendRow> Legend(uint16_t switches, PageSituation s);
  struct KeySection { const char *title; std::vector<bindings::Action> actions; }; // "on foot", "driving"
  const std::vector<KeySection> &KeySections();
  const char *Label(bindings::Action a);               // "FIRE", "WEAPON WHEEL"
  enum class Dot : uint8_t { kReady, kAttention, kMissing };
  struct Joystick { int id; std::string name; bool gamepad; };
  struct Status { Dot dot; std::string name; std::string why; int useThis = -1; }; // useThis: the pad "USE THIS ONE" switches to
  Status PadStatus(const std::vector<Joystick> &connected, int gameJoystick);
  Status KeyStatus(int unbound);
  // The keys of every listed action, a slot each: [0] keyboard, [1] the second key, [2] the mouse button (game
  // actions); the mod's use [0] and [1] (either may be a mouse button).
  using KeyTable = std::map<bindings::Action, std::array<uint16_t, 3>>;
  int Unbound(const KeyTable &table);                  // listed actions with no key at all
  // `key` goes to `a`'s `slot`; taken from every other action whose scope overlaps (bindings::ScopeOf).
  // Returns the actions it was taken from.
  std::vector<bindings::Action> Rebind(KeyTable &table, bindings::Action a, int slot, uint16_t key);
  }
  ```

**What it shows:**
- **Legend rows,** in this order and with these labels:
  - **On foot:** AIM (LT), FIRE (RT), WEAPON WHEEL (HOLD) (LB), PAUSE (Start), ANSWER PHONE (up), PREVIOUS / NEXT WEAPON (left/right as one row, `Pad::kLeftRight`), MINIMAP ZOOM (down), MOVE (left stick), ENTER VEHICLE (Y), MELEE / RELOAD (B), SPRINT (A), JUMP (X), LOOK (right stick) with the second line CLICK WHILE AIMING: BULLET TIME.
  - **Driving:** BRAKE / REVERSE (LT), ACCELERATE (RT), AIM A DRIVE-BY (HOLD) (LB), HANDBRAKE / DRIVE-BY FIRE (RB), SIDE MISSION / ANSWER PHONE (up), RADIO (HOLD: WHEEL) (left/right), MINIMAP ZOOM (down), STEER (left stick), EXIT (Y), CINEMATIC CAMERA (HOLD) (B), CAR GUN (A), HORN (L3), LOOK (right stick) with the second line CLICK: LOOK BEHIND.
  - **Flying:** DESCEND (LT), CLIMB (RT), TURN LEFT (LB), TURN RIGHT (RB), the same three D-pad rows as Driving, PITCH AND ROLL (left stick), EXIT (Y), CINEMATIC CAMERA (HOLD) (B), GUN / BOMBS (A), ROCKETS (HUNTER) (X).
  - **Pause map:** TABS (LB / RB), ZOOM IN / OUT (LT / RT), MOVE THE CURSOR (left stick), MOVE THE MAP (right stick), NUDGE THE CURSOR (D-pad), SET DESTINATION (A), BACK (B).
  - With a switch off, a row whose button is the game's own shows the game's job for it. The test pins only the all-on lists and the `ModernTriggers=0` fire row.
- **Key sections:**
  - **on foot:** GO_FORWARD FORWARD, GO_BACK BACKWARDS, GO_LEFT LEFT, GO_RIGHT RIGHT, PED_FIREWEAPON FIRE, PED_LOCK_TARGET AIM, PED_JUMPING JUMP, PED_SPRINT SPRINT, VEHICLE_ENTER_EXIT ENTER VEHICLE, PED_DUCK CROUCH, PED_LOOKBEHIND LOOK BEHIND, PED_ANSWER_PHONE ACTION, PED_CYCLE_WEAPON_LEFT PREVIOUS WEAPON, PED_CYCLE_WEAPON_RIGHT NEXT WEAPON, PED_CYCLE_TARGET_LEFT PREVIOUS TARGET, PED_CYCLE_TARGET_RIGHT NEXT TARGET, PED_SNIPER_ZOOM_IN ZOOM IN, PED_SNIPER_ZOOM_OUT ZOOM OUT, PED_CENTER_CAMERA_BEHIND_PLAYER CAMERA BEHIND, the four PED_1RST_PERSON_LOOK_* (LOOK LEFT / RIGHT / UP / DOWN), CAMERA_CHANGE_VIEW_ALL_SITUATIONS CHANGE CAMERA, kWeaponWheel WEAPON WHEEL, kReload RELOAD, kBulletTime BULLET TIME, kGreet GREET / YES, kInsult INSULT / NO, kExpandedMap BIG MAP.
  - **driving:** VEHICLE_ACCELERATE ACCELERATE, VEHICLE_BRAKE BRAKE / REVERSE, VEHICLE_HANDBRAKE HANDBRAKE, VEHICLE_HORN HORN, VEHICLE_FIREWEAPON VEHICLE FIRE, VEHICLE_CHANGE_RADIO_STATION NEXT STATION, VEHICLE_LOOKLEFT LOOK LEFT, VEHICLE_LOOKRIGHT LOOK RIGHT, VEHICLE_LOOKBEHIND LOOK BEHIND, the four VEHICLE_TURRET* (TURRET LEFT / RIGHT / UP / DOWN), TOGGLE_SUBMISSIONS SIDE MISSION, kRadioWheel RADIO WHEEL, kDriveByAim AIM A DRIVE-BY, kCinematicCamera CINEMATIC CAMERA.
  - The debug and unused actions (36, 39–43) aren't listed. The two aliases (kAnswerCall, kDriveByFire) aren't rows: they're the game's ACTION and VEHICLE FIRE.
- **Status:**
  - No joystick → kMissing, name "NO CONTROLLER CONNECTED".
  - The game's joystick isn't a gamepad → kAttention, why "NO BUTTON LAYOUT FOR THIS PAD".
  - The game's joystick's name contains "Steam", or a PlayStation or Nintendo gamepad is connected while the game reads one of another family → kAttention, why "THE GAME IS READING STEAM'S VIRTUAL PAD", `useThis` = the first such gamepad.
  - Else kReady with the game's joystick's name.
  - Names are shown in capitals.
  - `KeyStatus(0)` is kReady "KEYBOARD AND MOUSE". Otherwise it's kAttention with why "N ACTIONS HAVE NO KEY" ("1 ACTION HAS NO KEY").
- **`Rebind`:** clears `key` from every slot of every other listed action whose scope overlaps. The overlap rule is `bindings::DefaultKeys`': kEverywhere overlaps all three scopes; kOnFoot and kVehicle overlap only themselves and kEverywhere. Then it sets the slot.

- [ ] **Step 1: Write the failing tests** in `tests/test_controlsmodel.cpp` (the `test_magnet` style):

```cpp
using namespace controlsmodel;
using bindings::Action;
using bindings::Pad;
static const LegendRow *Row(const std::vector<LegendRow> &rows, Pad pad)
{
	for (const LegendRow &r : rows)
		if (r.pad == pad)
			return &r;
	return nullptr;
}
static void TestLegendOnFoot()
{
	const auto rows = Legend(bindings::kAll, PageSituation::kOnFoot);
	CHECK(Row(rows, Pad::kRT) && Row(rows, Pad::kRT)->label == "FIRE");
	CHECK(Row(rows, Pad::kLeftRight) && Row(rows, Pad::kLeftRight)->label == "PREVIOUS / NEXT WEAPON");
	CHECK(Row(rows, Pad::kRightStick) && Row(rows, Pad::kRightStick)->more == "CLICK WHILE AIMING: BULLET TIME");
	CHECK(rows.size() == 13);
}
static void TestLegendFollowsSwitches()
{
	const auto rows = Legend(bindings::kAll & ~bindings::kTriggers, PageSituation::kOnFoot);
	CHECK(Row(rows, Pad::kB) && Row(rows, Pad::kB)->label == "FIRE"); // the game's own B
}
static void TestLegendPauseMap()
{
	const auto rows = Legend(bindings::kAll, PageSituation::kPauseMap);
	CHECK(Row(rows, Pad::kA) && Row(rows, Pad::kA)->label == "SET DESTINATION");
	CHECK(rows.size() == 7);
}
static void TestKeySections()
{
	CHECK(KeySections().size() == 2);
	CHECK(std::string(KeySections()[0].title) == "on foot" && std::string(KeySections()[1].title) == "driving");
	CHECK(std::string(Label(Action::kWeaponWheel)) == "WEAPON WHEEL");
}
static void TestStatusMissing()
{
	const Status s = PadStatus({}, -1);
	CHECK(s.dot == Dot::kMissing && s.name == "NO CONTROLLER CONNECTED");
}
static void TestStatusReady()
{
	const Status s = PadStatus({{0, "DualSense Wireless Controller", true}}, 0);
	CHECK(s.dot == Dot::kReady && s.name == "DUALSENSE WIRELESS CONTROLLER" && s.why.empty());
}
static void TestStatusNoLayout()
{
	const Status s = PadStatus({{0, "8BitDo Pro 2", false}}, 0);
	CHECK(s.dot == Dot::kAttention && s.why == "NO BUTTON LAYOUT FOR THIS PAD");
}
static void TestStatusSteamVirtual()
{
	const Status s = PadStatus({{0, "Steam Virtual Gamepad", true}, {1, "DualSense Wireless Controller", true}}, 0);
	CHECK(s.dot == Dot::kAttention && s.why == "THE GAME IS READING STEAM'S VIRTUAL PAD" && s.useThis == 1);
}
static void TestKeyStatus()
{
	CHECK(KeyStatus(0).dot == Dot::kReady && KeyStatus(0).name == "KEYBOARD AND MOUSE");
	CHECK(KeyStatus(2).why == "2 ACTIONS HAVE NO KEY" && KeyStatus(1).why == "1 ACTION HAS NO KEY");
}
static void TestRebindTakesKeyFromOverlap()
{
	KeyTable t;
	t[Action::kPedJumping] = {' ', 0, 0};
	t[Action::kWeaponWheel] = {'Q', 0, 0};
	const auto taken = Rebind(t, Action::kWeaponWheel, 0, ' ');
	CHECK(taken.size() == 1 && taken[0] == Action::kPedJumping);
	CHECK(t[Action::kPedJumping][0] == 0 && t[Action::kWeaponWheel][0] == ' ');
	CHECK(Unbound(t) == 1);
}
static void TestRebindKeepsNonOverlap()
{
	KeyTable t;
	t[Action::kVehicleHandbrake] = {' ', 0, 0}; // in a vehicle
	t[Action::kPedJumping] = {'J', 0, 0};       // on foot
	CHECK(Rebind(t, Action::kPedJumping, 0, ' ').empty());
	CHECK(t[Action::kVehicleHandbrake][0] == ' ');
}
```

- [ ] **Step 2: Run** `make test`. Expected: compile failure (no `controlsmodel.h`).
- [ ] **Step 3: Implement** `controlsmodel` from the lists above.
  - `Legend` builds each row from `bindings::PadFor` for that row's action in the page situation's `bindings::Situation`: kOnFoot, kDriving, and kHelicopter for flying.
  - The sticks' rows and the pause map's are fixed.
- [ ] **Step 4: Run** `make test`. Expected: "all controlsmodel tests passed".

---

### Task 2: Focus and navigation (model)

**Files:**
- Modify: `src/controlsmodel.h/.cpp`, `tests/test_controlsmodel.cpp`

**Interfaces:**
- Produces:

  ```cpp
  namespace controlsmodel {
  enum class Tab : uint8_t { kController, kKeyboard };
  enum class Area : uint8_t { kTabs, kSituations, kSettings, kKeys };
  struct Focus {
  	Tab tab = Tab::kController;
  	Area area = Area::kTabs;
  	PageSituation situation = PageSituation::kOnFoot;
  	int item = 0;        // the setting, or the key row (across the sections, in order)
  	int slot = 0;        // the key cap in a row: 0, 1, 2
  	bool capturing = false;
  };
  enum class Input : uint8_t { kUp, kDown, kLeft, kRight, kSelect, kBack, kEscape };
  struct Sizes { int settings; int keyRows; }; // the Controller tab's settings (3), the key rows (KeySections' total)
  // What the input does: moves the focus, starts or cancels a capture, or (false) is the frontend's (Back leaving the page).
  bool Apply(Focus &f, Input in, const Sizes &sizes);
  }
  ```

**The rules:**
- The areas are, top to bottom, kTabs → kSituations → kSettings on the Controller tab, and kTabs → kKeys on the Keyboard tab.
- **Up/down** move between areas and through `item` within kKeys.
- **Left/right:**
  - on kTabs, switch the tab and reset the area to kTabs;
  - on kSituations, step through the four situations, wrapping;
  - on kSettings, move between the 3 settings (changing a value is the view's job, on select);
  - on kKeys, move `slot` 0..2.
- **Select on kKeys** sets `capturing`.
- **Escape while capturing** clears `capturing` and returns true: the page keeps it.
- **Back, or Escape not capturing,** returns false.

- [ ] **Step 1: Write the failing tests:**

```cpp
static void TestFocusMovesThroughAreas()
{
	Focus f;
	const Sizes sizes{3, 40};
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kSituations);
	CHECK(Apply(f, Input::kRight, sizes) && f.situation == PageSituation::kDriving);
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kSettings);
	CHECK(Apply(f, Input::kUp, sizes) && Apply(f, Input::kUp, sizes) && f.area == Area::kTabs);
	CHECK(Apply(f, Input::kRight, sizes) && f.tab == Tab::kKeyboard);
	CHECK(Apply(f, Input::kDown, sizes) && f.area == Area::kKeys && f.item == 0);
}
static void TestCaptureEscapeCancels()
{
	Focus f;
	f.tab = Tab::kKeyboard;
	f.area = Area::kKeys;
	const Sizes sizes{3, 40};
	CHECK(Apply(f, Input::kSelect, sizes) && f.capturing);
	CHECK(Apply(f, Input::kEscape, sizes) && !f.capturing); // the page keeps the Escape
	CHECK(!Apply(f, Input::kEscape, sizes));                 // now it's the frontend's: back to Options
}
static void TestSituationsWrap()
{
	Focus f;
	f.area = Area::kSituations;
	CHECK(Apply(f, Input::kLeft, {3, 40}) && f.situation == PageSituation::kPauseMap);
}
```

- [ ] **Step 2: Run** `make test`. Expected: compile failure.
- [ ] **Step 3: Implement** `Apply`.
- [ ] **Step 4: Run** `make test`. Expected: "all controlsmodel tests passed".

---

### Task 3: Writing one setting back to an ini (`inifile`, `config::SaveValue`, `actions::SetKeys`)

**Files:**
- Create: `src/inifile.h`, `src/inifile.cpp`, `tests/test_inifile.cpp`
- Modify: `src/config.h/.cpp` (`void SaveValue(const char *section, const char *key, const std::string &value)`: reads `vcmodernhud.ini`, `inifile::WithValue`, writes it back), `src/actions.h/.cpp`, `Makefile`

**Interfaces:**
- Produces:
  - `std::string inifile::WithValue(const std::string &ini, const std::string &section, const std::string &key, const std::string &value)`:
    - replaces the first `key=` line (case-insensitive), keeping a trailing `; comment`;
    - else adds `key=value` at the end of `[section]`;
    - else adds `[section]` and the line at the end.
  - `void actions::SetKeys(bindings::Action a, const keys::Binding &b)`: a mod action's keys from now on; `Version()` goes up, and they're saved as `[Keys] MOD_…=` with `keys::Format`.
- [ ] **Step 1: Write the failing tests:**

```cpp
static void TestReplacesAndKeepsComment()
{
	CHECK(inifile::WithValue("[Minimap]\nLookSensitivity=1    ; right stick\n", "Minimap", "LookSensitivity", "1.25") ==
	      "[Minimap]\nLookSensitivity=1.25    ; right stick\n");
}
static void TestAddsToSection()
{
	CHECK(inifile::WithValue("[Minimap]\nGPS=1\n[Keys]\n; MOD_RELOAD=kbd:R\n", "Keys", "MOD_RELOAD", "kbd:T") ==
	      "[Minimap]\nGPS=1\n[Keys]\n; MOD_RELOAD=kbd:R\nMOD_RELOAD=kbd:T\n");
}
static void TestAddsSection()
{
	CHECK(inifile::WithValue("[Minimap]\nGPS=1\n", "Keys", "MOD_RELOAD", "kbd:T") ==
	      "[Minimap]\nGPS=1\n[Keys]\nMOD_RELOAD=kbd:T\n");
}
```

- [ ] **Step 2: Run** `make test`. Expected: compile failure.
- [ ] **Step 3: Implement** `inifile`, `config::SaveValue` and `actions::SetKeys`.
  - `SetKeys` sets `gKeys`, bumps `gVersion` and calls `config::SaveValue("Keys", Name(a), Format(b))`.
- [ ] **Step 4: Run** `make test`. Expected: "all inifile tests passed".

---

### Task 4: Taking over page 26

**Files:**
- Create: `src/controlspage.h`, `src/controlspage.cpp`
- Modify:
  - `src/menu.cpp`
  - `src/game.h/.cpp`: `frontEndMenuManager` is already there; add `mousePointerStateHelper` (`_MousePointerStateHelper`), `disableMouseSteering` (`_ZN8CVehicle23m_bDisableMouseSteeringE`), `SaveINISettings`, `SaveINIControllerSettings`.
  - `Makefile`.

**Interfaces:**
- Produces:

  ```cpp
  namespace controlspage {
  constexpr int32_t kPage = 26; // the game's CONTROLLER SETUP
  // menu.cpp: entering the page (each time): its own list trimmed to Back, Method set to Standard.
  void Enter(uint8_t *menu);
  // From DrawBackground, after the frontend and the tabs.
  void Draw(uint8_t *menu);
  // From UserInput, before the frontend's: true if the page took this frame's input (the frontend's is then skipped).
  bool UserInput(uint8_t *menu);
  // From ProcessUserInput: the frontend's up / down / left / right / select / back, for the page; what it keeps is zeroed.
  void ProcessInput(uint8_t *menu, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change);
  // CText::Get: the page's names ("FEO_CON" Controls, "FET_CTL" CONTROLS), or null.
  const uint16_t *Text(const char *key);
  }
  ```

- **Behaviour:**
  - `menu.cpp` calls these when `MenuScreen(menu) == controlspage::kPage`.
    - `Enter` runs the first frame the page is shown after another page.
    - `Hook_TextGet` asks `controlspage::Text` before `hints::RewriteText`.
  - **`Enter` trims page 26's entries** (as `TrimPauseList` does for the pause list) to the one with action 35 (Back), and sets `MenuOptionCount` to 1. It also sets `FrontEndMenuManager + 0x5b` (Method) to 0.
- [ ] **Step 1: Implement** the skeleton: Enter / Text / an empty Draw / pass-through input, and the menu.cpp hooks.
- [ ] **Step 2: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean, `Installed`.
- [ ] **Step 3: Collect the in-game check:** Options shows "Controls"; it opens a page titled CONTROLS with the OPTIONS tab lit, only Back, and B/Escape returns to Options.

---

### Task 5: Drawing the page

**Files:**
- Modify: `src/controlspage.cpp`; `src/controls.h/.cpp`:
  - `std::vector<controlsmodel::Joystick> Pads()`: each `glfwJoystickPresent` joystick 0..15, with its name and `glfwJoystickIsGamepad`.
  - `int GameJoystickId()`: the existing `GameJoystick()`.
  - `bool Down(bindings::Pad)`: the last `Poll()`.

  Bind `glfwJoystickPresent` beside the other GLFW functions in `game.cpp`.

**Behaviour** (positions in the mockup's stage):
- **Tab switch:** CONTROLLER | KEYBOARD & MOUSE at y 128, as the pause tabs' pills (`menu.cpp DrawTabs` style). The selected tab is a white pill with black text.
- **Status band** at y 184: a dot (green, amber, or a hollow grey ring), then the name, then for amber " - " and the reason in amber. On the Controller tab, `useThis >= 0` adds a white pill "△/Y USE THIS ONE" (the font's `~T~` token, then text).
- **Controller tab:**
  - **Situations** in the heading font at y 236: on foot, driving, flying, pause map (the current one pink, the others dim).
  - **The pad picture** for `controls::Family()`, 560 px wide at (520, 282).
  - **Legend rows:** left column right-aligned to x 1098 from the right edge, right column from x 1102. Each row has the font's icon token for its pad (`bindings::Token`, through `hinttext::Printable`) and the label; a `more` line in pink, smaller. Rows are assigned left/right by the button's side on the pad: LT LB Up Left/Right Down Back L3 and the left stick go left; the rest go right.
  - **Black 3-px leader lines** from each row to the button's centre (the DS4 table; Xbox One and Switch measured from `fe_controller` in this step and added as two more tables).
  - **A held button** (`controls::Down`) glows pink on the picture, and its row turns pink.
  - **The settings strip** at y 768: VIBRATION ON/OFF, INVERT LOOK ON/OFF, and LOOK SENSITIVITY as 16 stepped bars, pink up to the value.
- **Keyboard & Mouse tab:**
  - **Left box** (130, 250, 650×488): "on foot" rows. **Right box** (820, 250): "driving" rows, then "mouse" with SENSITIVITY (16 bars), INVERT LOOK and MOUSE STEERING.
  - **Each row:** its label at +28, and key caps from x +340: `keys::DisplayName` in a 2-px outline box.
  - The focused row is pink; while capturing, its focused cap reads "PRESS A KEY...".
  - The boxes scroll to keep the focused row in view (no scroll bar).
- **Back and help text:** the green "back" button is the frontend's (its Back entry). The help text bottom right is "PRESS ANY BUTTON TO TEST IT" or "SELECT AN ACTION, THEN PRESS ITS NEW KEY".
- [ ] **Step 1: Measure** the Xbox One and Switch pictures: dump `fe_controller` from each TXD as in this session's scratchpad (`extract_fe.py`), and write the button centres into tables like the DS4's.
- [ ] **Step 2: Implement** the drawing.
- [ ] **Step 3: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 4: Collect the in-game check:** compare the page with the mockup, on both tabs and in all four situations; held buttons glow; unplug the pad and the dot goes grey.

---

### Task 6: Input, rebinding and saving

**Files:**
- Modify: `src/controlspage.cpp`, `src/game.h/.cpp` (the rebinding functions, `GetIsKeyboardKeyJustDown`, the joystick name buffer by image offset)

**Behaviour:**
- **Navigation:** `ProcessInput` maps the frontend's inputs to `controlsmodel::Input`:
  - `up` / `down` → kUp / kDown;
  - `change` −1 / +1 → kLeft / kRight;
  - `select` → kSelect;
  - `back` → kBack.

  It applies them and zeroes what the page kept. Select also does the following:
  - **On a setting:** toggles it, or steps a slider by one bar (left/right on a focused slider step it too).
  - **On the status pill:** "USE THIS ONE" does three things:
    - sets `joy1id` to `useThis`;
    - copies that pad's name into the JoystickName buffer (127 characters at most);
    - calls `SaveINIControllerSettings`.
- **Mouse:** `UserInput` handles a left click on a tab, a situation, a setting or a key cap (focus it, then select), and zeroes `api.newMouseState[0]` so the frontend doesn't also take it.
- **Capturing a key:** `UserInput` returns true, so the frontend's input is skipped.
  - **Escape (1000)** cancels.
  - **Any other key:** the first of 32..126 and 1001..1055 that `GetIsKeyboardKeyJustDown` reports, or a mouse button 1..7 newly down.
  - **The new key is applied:**
    - `controlsmodel::Rebind` on the current `KeyTable`;
    - for each game action whose keys changed: `ClearSettingsAssociatedWithAction` for that slot's type, then `SetControllerKeyAssociatedWithAction` (types 0 and 1) or `SetMouseButtonAssociatedWithAction` (type 2), then `ResetSettingOrder`;
    - for the mod's actions: `actions::SetKeys`.
  - **Then it saves:** `SaveINIControllerSettings()`, and the hints refresh (`actions::Version()` going up).
- **Saving settings:**
  - Vibration, mouse sensitivity, mouse invert and mouse steering → `SaveINISettings()`.
  - Invert pad look → `SaveINIControllerSettings()`.
  - Look sensitivity → `config::SaveValue("Minimap", "LookSensitivity", …)` and `gConfig.lookSensitivity`.
  - Look sensitivity's 16 bars run from 0.25 to 2.0; mouse sensitivity's 16 bars run from 1/3200 to 1/200, evenly.
- [ ] **Step 1: Read** `GetIsKeyboardKeyJustDown`'s first instructions and confirm it takes `(this, RsKeyCodes)` like `GetIsKeyboardKeyDown`.
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Build and install.** Run `make all && make test && ./install.sh`. Expected: clean.
- [ ] **Step 4: Collect the in-game checks:**
  - Rebind Jump to J on the keyboard: the hint and the prompt say J at once, and it survives a restart (`macVC.ini [Bindings]`).
  - Rebind the weapon wheel to the middle mouse button: `vcmodernhud.ini [Keys]` has `MOD_WEAPON_WHEEL=mouse:MIDDLE`.
  - Escape while capturing cancels, and the menu stays.
  - Clicks on rows don't select Back.
  - Each setting changes and survives a restart.

---

### Task 7: README

**Files:**
- Modify: `README.md`: replace the line about the game's Controller Setup, if any, and add a short "Controls page" paragraph under Controls: the two tabs, the status dot's states, rebinding, and that head bob stays in `macVC.ini` for now.

- [ ] **Step 1: Update the README.**
- [ ] **Step 2: Final verification.** Run `make clean && make all && make test && ./install.sh`. Expected: all suites pass, `Installed`.
- [ ] **Step 3: Report** the in-game checklist from Tasks 4–6.
