#pragma once
// What each action is on, for every situation the player can be in: the
// game's own actions (the ~k~~NAME~ tokens its hints name) and the mod's.
// The mod moves things on the pad (layout.h, controls.cpp); where it doesn't,
// an action stays on the game's own button for its controller setup. This is
// what hints, prompts and the Controls page name, so they can't disagree with
// each other. No game calls here: unit-tested (tests/test_bindings.cpp).
#include "keys.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace bindings {

// 0..43: the game's e_ControllerAction, in its order (the names its
// CControllerConfigManager::InitialiseControllerActionNameArray gives them);
// then the mod's own.
enum class Action : uint8_t {
	kPedFireweapon,
	kPedCycleWeaponRight,
	kPedCycleWeaponLeft,
	kGoForward,
	kGoBack,
	kGoLeft,
	kGoRight,
	kPedSniperZoomIn,
	kPedSniperZoomOut,
	kVehicleEnterExit,
	kCameraChangeViewAllSituations,
	kPedJumping,
	kPedSprint,
	kPedLookbehind,
	kPedDuck,
	kPedAnswerPhone, // the game's action button: answering, buying, picking up
	kVehicleFireweapon,
	kVehicleAccelerate,
	kVehicleBrake,
	kVehicleChangeRadioStation,
	kVehicleHorn,
	kToggleSubmissions,
	kVehicleHandbrake,
	kPed1rstPersonLookLeft,
	kPed1rstPersonLookRight,
	kVehicleLookleft,
	kVehicleLookright,
	kVehicleLookbehind,
	kVehicleTurretleft,
	kVehicleTurretright,
	kVehicleTurretup,
	kVehicleTurretdown,
	kPedCycleTargetLeft,
	kPedCycleTargetRight,
	kPedCenterCameraBehindPlayer,
	kPedLockTarget,
	kNetworkTalk,
	kPed1rstPersonLookUp,
	kPed1rstPersonLookDown,
	kUnused39,
	kToggleDpad,
	kSwitchDebugCamOn,
	kTakeScreenShot,
	kShowMousePointerToggle,
	kGameCount,
	kWeaponWheel = 64,
	kRadioWheel,
	kReload,
	kGreet,
	kInsult,
	kBulletTime,
	kDriveByAim,
	kDriveByFire,
	kCinematicCamera,
	kExpandedMap,
	kAnswerCall, // the phone's call (D-pad up), not the game's action button
	kModEnd
};

enum class Situation : uint8_t { kOnFoot, kDriving, kArmedVehicle, kPlane, kHelicopter, kHunter, kSeaSparrow, kRcBaron, kCount };

// Pad buttons by where they are (Xbox names: A is the bottom face button, Cross).
enum class Pad : uint8_t {
	kNone, kA, kB, kX, kY, kLB, kRB, kLT, kRT, kL3, kR3, kBack, kStart,
	kUp, kDown, kLeft, kRight, kLeftRight, kLeftStick, kRightStick, kCount
};

enum class Press : uint8_t { kPress, kHold, kTap, kWhileAiming, kWhileDriveByAim };

// The ini switches that move things on the pad.
enum Switch : uint16_t {
	kTriggers = 1,       // ModernTriggers
	kButtons = 2,        // ModernButtons
	kDpad = 4,           // DpadActions
	kWheels = 8,         // Wheels
	kDriveBy = 16,       // DriveByAim
	kBulletTime = 32,    // BulletTime
	kInteractions = 64,  // Interactions
	kPhone = 128,        // Phone
	kAll = 255,
};

// Where an action applies, for keys that mustn't clash.
enum class Scope : uint8_t { kOnFoot, kVehicle, kEverywhere };

enum class PadFamily : uint8_t { kXbox, kPlayStation, kNintendo };

struct PadBinding {
	Pad pad = Pad::kNone;
	Press press = Press::kPress;
	bool ours = false; // the mod put it there; otherwise it's the game's own button (its token stands)
};

PadBinding PadFor(uint16_t switches, Situation s, Action a);
// A button does it there: ours, or the game's own and the mod hasn't taken that button.
bool Reachable(uint16_t switches, Situation s, Action a);
Situation ForVehicle(int16_t model, bool aircraft);
// The situation a hint naming `a` means: the player's, except a vehicle action named on foot (driving).
Situation ForHint(Situation player, Action a);
Scope ScopeOf(Action a);
// The ~k~ token's name ("PED_FIREWEAPON", "MOD_WEAPON_WHEEL"); null for kUnused39 and non-actions.
const char *Name(Action a);
bool FromName(std::string_view name, Action &out);
// The font's token for a button ("~X~", "~@u~"); "" for kNone and the sticks.
const char *Token(Pad p);
// What a hint names for `a` on a pad: our button's token; "" with `game` false
// where the mod left it no button (the game's old one would be wrong); "" with
// `game` true where it's the game's own button (its token stands).
const char *HintToken(uint16_t switches, Situation s, Action a, bool &game);
// A mod action whose keys are a game action's: kAnswerCall (the game's action button), kDriveByFire (vehicle fire).
bool KeyAlias(Action a, Action &game);
bool HasOwnKeys(Action a);
// A mod action's default key, unless a game action whose scope overlaps already has it
// (`gameUses(key, scope)`: a game action of exactly that scope has the key).
keys::Binding DefaultKeys(Action a, const std::function<bool(uint16_t key, Scope scope)> &gameUses);
PadFamily FamilyForName(std::string_view joystickName);

} // namespace bindings
