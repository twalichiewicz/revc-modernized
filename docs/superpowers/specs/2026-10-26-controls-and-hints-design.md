# Controls: hints that name the right buttons, and one Controls page

Design for the macVC mod in this repo (`vc-minimap`, the "VC Modern HUD" dylib for the
native Apple Silicon port of Vice City, macVC 1.0.5217, built from reVC).

## Why

The mod has rebuilt the controller layout GTA V style: RT fires, LT aims, holding LB opens
the weapon wheel, the D-pad switches weapons and answers the phone, and so on
(`src/controls.h`). It does this by reading the pad through GLFW and rewriting the game's
pad state, so the game's own button mapping never learns about it. Two things are now
wrong:

- **The game's hints name the old buttons.** 169 of the game's English text entries name a
  button through a `~k~~ACTION~` token (`Press the ~h~~k~~PED_FIREWEAPON~~w~ ...`), across
  28 actions. The game fills the token in when the text is drawn
  (`CControllerConfigManager::GetWideStringOfCommandKeys`). With a pad in use
  (`CPad::IsAffectedByController`), it doesn't read any binding: it takes a fixed per-action
  table for the game's controller configuration (Setup 1-4), and `CFont` draws the
  resulting token as a button icon. With the keyboard, it names the bound keys, which are
  right, because the mod leaves the keyboard alone.
- **The mod's own prompts have their button written into the code.** Climb, Greet / Insult
  and Answer (`prompt::Draw` with a fixed `prompt::Button`) always show a pad button, even
  to someone playing on the keyboard.

Also, the mod's own features work only on a pad: the weapon and radio wheels, greet /
insult, bullet time, drive-by aiming, the cinematic camera and the D-pad actions.

The game's settings for controls are spread over Redefine Controls (On Foot / In Vehicle,
keyboard and mouse only), Mouse Settings, Configuration Setup 1-4 (which the mod's layout
overrides anyway) and macVC's Controller Display. The player wants one modern page, in
Vice City's look, that shows whether the pad and keyboard are set up, maps the keyboard,
and shows the pad's layout as a picture with a legend.

## Goals

1. Every button or key named in a hint is the one that actually does that action now, and
   it's looked up when the text is drawn, so it follows any change straight away: a key
   rebound on the Controls page, a different pad plugged in, a switch between pad and
   keyboard.
2. One table of what each action is bound to feeds the game's hints, the mod's prompts,
   the Controls page and, where practical, the mod's input code. Then they can't disagree.
3. The mod's own features get keyboard (and mouse) keys, so that every action has a key.
4. One Controls page replaces the game's Controller Setup pages. It has two tabs:
   Controller (status, a picture of the pad with a legend for each situation, and a live
   button test) and Keyboard & Mouse (status, rebindable keys by situation, mouse
   settings).
5. It looks like Vice City: the game's own fonts, background, pad pictures and button
   icons (see "The page").

## Non-goals

- Rebinding the pad. The Controller tab shows the layout and tests it. It doesn't change
  it. (The layout's ini switches, `ModernTriggers` and so on, stay in the ini; they're read
  at start-up, and the table follows whatever is active.)
- Changing the pad layout itself. This makes the existing layout visible and named
  correctly. The one exception is filling gaps (see "Decided with the player").
- Languages other than English for the mod's own text. The game's hints keep their
  language and only their button names change. The few rewritten hints are rewritten in
  English only.
- New art. Everything visual comes from the game's files or is drawn with the game's
  fonts.

## Anti-goals (it fails if...)

- A hint names a button that doesn't do the thing, in any situation the hint can appear.
- The pad picture's legend disagrees with what the buttons do.
- The page looks like a modern web settings page rather than Vice City's frontend.
- A keyboard action that worked before stops working, or a game key binding is lost.
- Input or frame time gets noticeably worse.

## Decided with the player

- **The page's layout and look** are as in the approved mockup
  (`.superpowers/brainstorm/22900-1793053865/content/controls-vc-6.html`, served by the
  brainstorm companion; it renders the game's own assets):
  - The pause menu's tab row (MAP BRIEF STATS GAME OPTIONS) with OPTIONS selected, the
    camo panel, the "vice city" logo and a pink heading-font title "controls" top right.
  - Under it, a two-item switch, **CONTROLLER | KEYBOARD & MOUSE**, in the same white-pill
    style as the tab row. There's no "in use" marker on it.
  - Under the switch, one **status line** on a dark band: a dot, then the device's name.
    There's no other text when all is well.
  - **Controller tab:**
    - A row of situations in the heading font: **on foot**, **driving**, **flying**,
      **pause map**. The current one is pink, the others dim.
    - The pad's picture in the middle. Legend rows down both sides, each with the game's
      button icon and what it does, and black leader lines from each row to its button.
    - A button lights up (pink glow on the picture, pink row) while it's held: "PRESS ANY
      BUTTON TO TEST IT".
  - **Keyboard & Mouse tab:**
    - Dark boxes per situation, with pink heading-font titles. Each row has the action and
      its key and alternative key as key caps.
    - Selecting a row and pressing a key rebinds it: the row turns pink and shows "PRESS A
      KEY...".
    - A **mouse** section with the game's stepped-bar slider for sensitivity, and invert
      look.
  - The green **back** button bottom left, and white help text bottom right.
- **The status dot** has three states:
  - **Green:** connected, recognised and read by the game. Only the name is shown.
  - **Amber:** something needs fixing, and the reason follows in amber. Examples: "NO
    BUTTON LAYOUT FOR THIS PAD", "THE GAME IS READING STEAM'S VIRTUAL PAD", and on the
    keyboard tab "2 ACTIONS HAVE NO KEY".
  - **Hollow grey:** "NO CONTROLLER CONNECTED".
- The player explicitly removed "CONNECTED", "PLAYSTATION BUTTONS" and "STICKS AND TRIGGERS
  MAPPED" from the status: recognising the pad implies them.
- **The mod's pad-only features get keyboard keys** as part of this.
- **The page holds only control settings.** The pages it replaces had these, and they go
  here (mockup `controls-vc-8.html`):
  - **Controller tab:** a strip along the bottom, beside "back": VIBRATION, INVERT LOOK
    (the port's `InvertPad`, which the mod's stick look reads) and LOOK SENSITIVITY (the
    mod's `LookSensitivity`).
  - **Keyboard & Mouse tab, mouse box:** SENSITIVITY, INVERT LOOK and MOUSE STEERING (the
    port's `DisableMouseSteering`, the other way round).
  - **Detect Joystick** becomes a fix on the amber status line: "THE GAME IS READING
    STEAM'S VIRTUAL PAD [△ USE THIS ONE]". That one button sets the game's joystick (the
    port's `JoystickName`) to the pad being pressed.
  - **Hidden, fixed:**
    - Standard/Classic (`Method`) is fixed at Standard, which the mod's stick look needs.
    - The hint icon set (Controller Display) follows the connected pad.
- **Actions with no pad button get one.**
  - Audit every game action in every situation and vehicle class against the mod's
    layout.
  - An action left with no pad button gets one, the way the layout already does it: the
    RC Baron fires on Cross/A.
  - Known case: under the flying layout (`TriggersFly` / `FaceButtonsInAircraft`), the RC
    Raider and RC Goblin lose RB and Circle, so their bomb drop and detonation can't be
    pressed. From the code, their missions can't be finished on a pad; this isn't
    confirmed in the game.
  - The reviewer suspected an unarmed boat's cannon and cycling targets too. The audit
    settles those.
- **Hints whose controls changed are rewritten.**
  - Some English hints describe a control the mod replaced. For example, the drive-by
    tutorial (`HELP31`, `HELP32`) says "look left or right, then fire". With `DriveByAim`
    it's hold L1 / LB to aim and R1 / RB to shoot, and the look buttons are the pedals.
  - These get new English text, by text key, through the mod's `CText::Get` hook. The
    text still uses `~k~` tokens, so it names the buttons by the same lookup.
  - The audit lists them. No hint may name a button that doesn't do what it says.
- **Not on this page:** head bob in first person (`HeadBob1stPerson`) is a game feature,
  not a control. It belongs in a future Options section for game features, which is
  deferred. Until then it has no menu entry and stays settable in `macVC.ini`.

## How it works

### The bindings table

A single description of the controls, for each **situation** (on foot, driving,
flying, pause map; the builder may split these further where the layout differs, e.g. an
armed vehicle, a helicopter, a prompt being up):

- For each **action**, the pad button it's on (with "hold" / "tap" / "click while
  aiming" where that matters) and the keyboard keys (up to two, as in the game).
- Actions are both the game's (`e_ControllerAction`, as the `~k~` tokens name them) and the
  mod's own (weapon wheel, radio wheel, greet, insult, bullet time, drive-by aim,
  cinematic camera, expanded minimap, ...).
- The pad side is derived from the active ini switches (`ModernTriggers`, `ModernButtons`,
  `DpadActions`, `Wheels`, `DriveByAim`, ...): with a switch off, the action is where the
  game has it.
- The keyboard side of the game's actions *is* the game's own binding
  (`CControllerConfigManager`), read live. The mod's actions have their own, stored in the
  mod's ini.

The pure parts are plain C++ with unit tests in `tests/`, as `magnet` and `turns` are: the
table, resolving an action to a button or key for a situation and device, and the status
classification.

Goal 2 asks the mod's input code (`controls.cpp`, `wheel.cpp`, `interact.cpp`, ...) to read
its buttons from the same table where practical, so the two can't drift. The builder
decides how far to take that. At the least, a test or check must make a drift impossible
to miss.

### Hints

- **The game's hints:** hook `GetWideStringOfCommandKeys`.
  - With a pad in use (the game's own `CPad::IsAffectedByController`, which the mod
    binds as `api.padAffectedByController`), return the icon token for the button the
    table gives. `CFont::ParseToken` / `DrawButton` draw it from `ps3btns.txd`,
    `x360btns.txd` or `nswbtns.txd`.
  - With the keyboard, keep the game's own behaviour.
- **Which situation a hint means:** where the player is now, first. In a vehicle, that
  vehicle's layout (car, bike, boat, plane, helicopter, armed or not). On foot, the on-foot
  layout. *(My call.)*
  - A hint can name an action from another situation, such as a vehicle action shown on
    foot. Then the action's own situation decides.
  - Some actions mean different buttons depending on the hint.
    - `PED_ANSWER_PHONE` is D-pad up in the `ANSWER` hint (answering the phone,
      `phone.cpp`). In the 38 property-purchase hints, the weapon pickups and `HELP50`, it's
      a *tap* of LB (a hold opens the wheel).
    - `PED_FIREWEAPON` named while driving the fire truck, a boat or an RC vehicle is that
      vehicle's gun button, not RT (the throttle).
  - So the hint's text key can override the table, which needs the key known when the
    token is expanded. The game's `CText::Get` (hooked already) or the help box's setter
    could pass it on. The builder chooses how.
- **Text already on screen updates too.** *(My call: "live" was the player's word.)*
  - Brief messages are expanded every frame (`CMessages::Display`), so they follow by
    themselves.
  - Help boxes are expanded once, when set (`CHud::SetHelpMessage` →
    `InsertPlayerControlKeysInString`, called at 0x1001fe8a4). Keep a box's raw text and
    expand it again when the device switches or a binding changes, without making the
    help box treat it as a new message.
- **The mod's prompts** (`prompt::Draw`) take an action instead of a fixed button. They
  show the pad button's icon on a pad, and a key cap with the key's name on the keyboard.
- **Pad vs keyboard** follows the game's own switch (whichever was used last), so the hints
  and the mod's prompts always agree.

### Keyboard keys for the mod's features

Each pad-only feature gets one or two bindable keys, with defaults that don't clash with
the game's defaults in the same situation. The wheels pick with the mouse while their key
is held (`SteerWheel` already takes mouse movement). The game's own actions that the mod
moved (answering the phone, weapon cycling, side missions) keep working on their game key.
The builder chooses the defaults and lists them in the README's controls section.

### The page

- **Where:** Options → **Controls** replaces the Controller Setup entry and its pages
  (Redefine Controls, Mouse Settings, Configuration, Controller Display). The game's
  Configuration Setup choice is hidden, since the mod's layout replaces it. The builder
  must keep whatever setup value the mod's code relies on (e.g. `mode == 2` branches in
  `controls.cpp`).
- **Drawing:**
  - Text: `text.h` / `CFont`. The heading font is style 2: lower-case-looking chunky
    letters, pink (255,150,225) with a dark drop shadow. The body is the wide capitals of
    style 1.
  - Status colours: green (124,204,116), amber (232,176,74).
  - The page title and tab row as the mod's pause menu already draws them
    (`menu.cpp DrawTabs`).
- **Pad pictures:** `fe_controller` from `frontend_ds4.txd` (DualSense and DualShock 4:
  there's no DualSense picture), `frontend_ds3.txd`, `frontend_xone.txd` (Xbox One and
  Series), `frontend_x360.txd`, `frontend_nsw.txd` (Switch Pro). Chosen by the pad's name,
  as `controls::PlayStationPad` already does. Each picture needs its buttons' positions
  for the leader lines. The `fe_arrows1-4` textures show where the game's own page points.
- **Button icons:** the same sets the hints use.
- **The live test:** while the page is open, the buttons held now (from the mod's GLFW
  poll) glow on the picture, and their legend rows turn pink. Nothing reaches the game.
- **Status checks:**
  - Controller: no joystick → grey. A joystick without a gamepad mapping (GLFW: not a
    gamepad) → amber "NO BUTTON LAYOUT FOR THIS PAD". The game reading a different
    joystick than the one being used, or Steam's virtual pad (`GameJoystick()` against the
    connected gamepads) → amber, saying so. Otherwise green.
  - Keyboard: amber "N ACTIONS HAVE NO KEY" when any bindable action has none, else green.
- **What the Keyboard & Mouse tab lists:** every rebindable action, the game's (about 45)
  and the mod's, grouped by situation. The boxes scroll; the mockup shows the top of the
  list. Nothing that can be rebound today stops being rebindable. *(My call.)*
- **"N ACTIONS HAVE NO KEY"** counts:
  - the game's actions that its own check requires a key for (the check behind its "One
    or more control actions are not bound" error, `FEC_ERI`);
  - all of the mod's actions.

  A fresh install is green. *(My call.)*
- **Rebinding:** select a row, press the new key or mouse button; Escape cancels.
  - Game actions go through the game's own binding code, so its settings file keeps
    them.
  - Mod actions are saved to the ini.
  - The hints and prompts show the new key at once.
  - A key already used by another action in the same situation is taken from it. That
    action then shows no key, and the status turns amber. *(My call.)*
- **Navigation:**
  - By pad, by keyboard and by mouse (clicking rows, tabs, situations and back), in the
    pause menu's conventions: Circle / B is back (`CircleBack`), and LB / RB (Q / E) are
    the top tabs.
  - The builder picks the buttons for the CONTROLLER | KEYBOARD switch and the situation
    row. They must not take the top tabs' buttons, and they show in the help text bottom
    right.

## Verification

- **Unit tests:** resolving actions for each situation and ini combination; the keyboard
  conflict rule; status classification from fake joystick lists.
- **The audit:** a table of every game action × situation / vehicle class, with its pad
  button under the mod's layout. Nothing may be left with no button. A second table lists
  each hint text key that names a token in a situation where it reads wrong, with the fix
  (an override or a rewrite).
- **In the game, on a pad:**
  - For each of the 28 hint actions, see a hint that uses it (or force one) and check that
    the named button does it, on foot, driving and flying.
  - Drop and detonate with the RC Raider and RC Goblin.
- **On the keyboard:** each mod feature works by its key, and the hints and prompts name
  it. Rebind one game action and one mod action, check the hint changes at once, quit,
  relaunch, and check both bindings survived.
- **The page:** compare it with the mockup. Check each status state: unplug the pad, try
  Steam Input on, and unbind a key.

## Left to the builder

- **Hook mechanics:** how to take over the Controller Setup page, and how the page's input
  is read.
- **The table's shape:** how far `controls.cpp` is made to read from it (goal 2).
- **Keys:** the mod actions' default keys and their ini section and format.
- **Navigation:** the buttons for the tab switch and the situation row.
- **Pictures:** button positions on each pad picture.
- **The pause-map situation's legend:** the mod's map controls, from `menu.cpp`.
