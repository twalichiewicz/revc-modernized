#include "bindings.h"
#include "layout.h"

namespace bindings {
namespace {

using S = Situation;

constexpr uint16_t Bit(S s) { return uint16_t(1u << unsigned(s)); }
constexpr uint16_t kFoot = Bit(S::kOnFoot), kCar = Bit(S::kDriving), kArmed = Bit(S::kArmedVehicle);
constexpr uint16_t kPlaneBit = Bit(S::kPlane), kHeli = Bit(S::kHelicopter), kHunterBit = Bit(S::kHunter);
constexpr uint16_t kSparrow = Bit(S::kSeaSparrow), kBaron = Bit(S::kRcBaron);
constexpr uint16_t kAir = kPlaneBit | kHeli | kHunterBit | kSparrow | kBaron, kCars = kCar | kArmed;
constexpr uint16_t kVehicles = kCars | kAir;
constexpr uint16_t kAnywhere = kFoot | kVehicles;

// The mod's placements: a row applies in its situations while all the switches it needs are on
// (ModernButtons' rows need ModernTriggers too: controls.cpp remaps the face buttons inside it).
struct Row {
	uint16_t situations;
	Action action;
	Pad pad;
	Press press;
	uint16_t needs;
};

constexpr Row kRows[] = {
    // On foot
    {kFoot, Action::kPedFireweapon, layout::kFire, Press::kPress, kTriggers},
    {kFoot, Action::kPedLockTarget, layout::kAim, Press::kHold, kTriggers},
    {kFoot, Action::kPedCycleWeaponLeft, layout::kPrevWeapon, Press::kPress, kDpad},
    {kFoot, Action::kPedCycleWeaponRight, layout::kNextWeapon, Press::kPress, kDpad},
    {kFoot, Action::kPedCycleTargetLeft, layout::kPrevWeapon, Press::kWhileAiming, kTriggers | kDpad},
    {kFoot, Action::kPedCycleTargetRight, layout::kNextWeapon, Press::kWhileAiming, kTriggers | kDpad},
    {kFoot, Action::kPedAnswerPhone, layout::kWeaponWheel, Press::kTap, kWheels},
    {kFoot, Action::kWeaponWheel, layout::kWeaponWheel, Press::kHold, kWheels},
    {kFoot, Action::kReload, layout::kMeleeReload, Press::kPress, kTriggers | kButtons},
    {kFoot, Action::kBulletTime, layout::kBulletTime, Press::kWhileAiming, kBulletTime},
    // Any situation
    {kFoot | kCars, Action::kGreet, layout::kGreet, Press::kPress, kInteractions},
    {kFoot | kCars, Action::kInsult, layout::kInsult, Press::kPress, kInteractions},
    {kAnywhere, Action::kAnswerCall, layout::kAnswerCall, Press::kPress, kPhone},
    {kAnywhere, Action::kExpandedMap, layout::kExpandedMap, Press::kPress, kDpad},
    // Cars, bikes and boats
    {kCars, Action::kVehicleAccelerate, layout::kGas, Press::kHold, kTriggers},
    {kCars, Action::kVehicleBrake, layout::kBrake, Press::kHold, kTriggers},
    {kCars, Action::kVehicleLookleft, Pad::kNone, Press::kPress, kTriggers},
    {kCars, Action::kVehicleLookright, Pad::kNone, Press::kPress, kTriggers},
    {kCars, Action::kVehicleLookbehind, layout::kLookBehind, Press::kHold, kTriggers | kDpad},
    {kVehicles, Action::kVehicleChangeRadioStation, layout::kRadio, Press::kTap, kDpad},
    {kVehicles, Action::kRadioWheel, layout::kRadio, Press::kHold, kDpad | kWheels},
    {kVehicles, Action::kToggleSubmissions, layout::kSideMission, Press::kPress, kDpad},
    {kVehicles, Action::kCinematicCamera, layout::kCinematic, Press::kHold, kTriggers | kButtons},
    {kCar, Action::kDriveByAim, layout::kDriveByAim, Press::kHold, kDriveBy},
    {kCar, Action::kDriveByFire, layout::kDriveByFire, Press::kWhileDriveByAim, kDriveBy},
    {kCar, Action::kPedFireweapon, layout::kCarGun, Press::kPress, kTriggers | kButtons},
    {kCar, Action::kVehicleFireweapon, layout::kCarGun, Press::kPress, kTriggers | kButtons},
    {kArmed, Action::kPedFireweapon, layout::kVehicleGun, Press::kPress, kTriggers | kButtons},
    {kArmed, Action::kVehicleFireweapon, layout::kVehicleGun, Press::kPress, kTriggers | kButtons},
    {kArmed, Action::kVehicleHandbrake, layout::kArmedHandbrake, Press::kHold, kTriggers | kButtons},
    // Aircraft
    {kAir, Action::kVehicleAccelerate, layout::kClimb, Press::kHold, kTriggers},
    {kAir, Action::kVehicleBrake, layout::kDescend, Press::kHold, kTriggers},
    {kAir, Action::kVehicleLookleft, layout::kYawLeft, Press::kHold, kTriggers}, // a plane's rudder, a helicopter's tail
    {kAir, Action::kVehicleLookright, layout::kYawRight, Press::kHold, kTriggers},
    {kAir, Action::kVehicleLookbehind, Pad::kNone, Press::kPress, kTriggers | kDpad},
    {kHunterBit, Action::kVehicleHandbrake, layout::kAircraftGun, Press::kHold, kTriggers | kButtons},
    {kHunterBit, Action::kPedFireweapon, layout::kHunterRockets, Press::kPress, kTriggers | kButtons},
    {kHunterBit, Action::kVehicleFireweapon, layout::kHunterRockets, Press::kPress, kTriggers | kButtons},
    {kSparrow, Action::kVehicleHandbrake, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kSparrow, Action::kPedFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kSparrow, Action::kVehicleFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kBaron, Action::kPedFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kBaron, Action::kVehicleFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    // the rest: the RC helicopters' bombs and detonation, whatever a plane fires
    {kPlaneBit | kHeli, Action::kPedFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kPlaneBit | kHeli, Action::kVehicleFireweapon, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
    {kPlaneBit | kHeli, Action::kVehicleHandbrake, layout::kAircraftGun, Press::kPress, kTriggers | kButtons},
};

// The game's own button for each of its actions (controller setup 1, its icon table).
constexpr Pad kGamePad[] = {
    Pad::kB,          Pad::kRT,         Pad::kLT,         Pad::kLeftStick,  Pad::kLeftStick,  Pad::kLeftStick,
    Pad::kLeftStick,  Pad::kX,          Pad::kA,          Pad::kY,          Pad::kBack,       Pad::kX,
    Pad::kA,          Pad::kR3,         Pad::kL3,         Pad::kLB,         Pad::kB,          Pad::kA,
    Pad::kX,          Pad::kLB,         Pad::kL3,         Pad::kR3,         Pad::kRB,         Pad::kRightStick,
    Pad::kRightStick, Pad::kLT,         Pad::kRT,         Pad::kLT /* with RT */, Pad::kRightStick, Pad::kRightStick,
    Pad::kLeftStick,  Pad::kLeftStick,  Pad::kLT,         Pad::kRT,         Pad::kLB,         Pad::kRB,
    Pad::kNone,       Pad::kRightStick, Pad::kRightStick, Pad::kNone,       Pad::kNone,       Pad::kNone,
    Pad::kNone,       Pad::kNone,
};
static_assert(sizeof kGamePad / sizeof kGamePad[0] == size_t(Action::kGameCount), "one button per game action");

constexpr const char *kGameNames[] = {
    "PED_FIREWEAPON", "PED_CYCLE_WEAPON_RIGHT", "PED_CYCLE_WEAPON_LEFT", "GO_FORWARD", "GO_BACK", "GO_LEFT",
    "GO_RIGHT", "PED_SNIPER_ZOOM_IN", "PED_SNIPER_ZOOM_OUT", "VEHICLE_ENTER_EXIT",
    "CAMERA_CHANGE_VIEW_ALL_SITUATIONS", "PED_JUMPING", "PED_SPRINT", "PED_LOOKBEHIND", "PED_DUCK",
    "PED_ANSWER_PHONE", "VEHICLE_FIREWEAPON", "VEHICLE_ACCELERATE", "VEHICLE_BRAKE", "VEHICLE_CHANGE_RADIO_STATION",
    "VEHICLE_HORN", "TOGGLE_SUBMISSIONS", "VEHICLE_HANDBRAKE", "PED_1RST_PERSON_LOOK_LEFT",
    "PED_1RST_PERSON_LOOK_RIGHT", "VEHICLE_LOOKLEFT", "VEHICLE_LOOKRIGHT", "VEHICLE_LOOKBEHIND",
    "VEHICLE_TURRETLEFT", "VEHICLE_TURRETRIGHT", "VEHICLE_TURRETUP", "VEHICLE_TURRETDOWN", "PED_CYCLE_TARGET_LEFT",
    "PED_CYCLE_TARGET_RIGHT", "PED_CENTER_CAMERA_BEHIND_PLAYER", "PED_LOCK_TARGET", "NETWORK_TALK",
    "PED_1RST_PERSON_LOOK_UP", "PED_1RST_PERSON_LOOK_DOWN", nullptr, "TOGGLE_DPAD", "SWITCH_DEBUG_CAM_ON",
    "TAKE_SCREEN_SHOT", "SHOW_MOUSE_POINTER_TOGGLE",
};
static_assert(sizeof kGameNames / sizeof kGameNames[0] == size_t(Action::kGameCount), "one name per game action");

constexpr const char *kModNames[] = {
    "MOD_WEAPON_WHEEL", "MOD_RADIO_WHEEL", "MOD_RELOAD", "MOD_GREET", "MOD_INSULT", "MOD_BULLET_TIME",
    "MOD_DRIVEBY_AIM", "MOD_DRIVEBY_FIRE", "MOD_CINEMATIC_CAMERA", "MOD_EXPANDED_MAP", "MOD_ANSWER_CALL",
};
constexpr int kFirstMod = int(Action::kWeaponWheel);
static_assert(sizeof kModNames / sizeof kModNames[0] == size_t(int(Action::kModEnd) - kFirstMod), "one name per mod action");

bool IsGame(Action a) { return a < Action::kGameCount; }
bool IsMod(Action a) { return a >= Action::kWeaponWheel && a < Action::kModEnd; }

bool Dpad(Pad p) { return p == Pad::kUp || p == Pad::kDown || p == Pad::kLeft || p == Pad::kRight || p == Pad::kLeftRight; }

// The buttons the mod has given other jobs there, so the game's own actions on them are gone.
bool Taken(uint16_t sw, S s, Pad p)
{
	// controls.cpp remaps the face buttons only inside ModernTriggers
	const bool triggers = sw & kTriggers, buttons = triggers && (sw & kButtons), dpad = sw & kDpad, driveBy = sw & kDriveBy;
	if (dpad && Dpad(p))
		return true;
	switch (s) {
	case S::kOnFoot:
		return (triggers && (p == Pad::kLT || p == Pad::kRT || p == Pad::kRB)) || (buttons && p == Pad::kB);
	case S::kDriving:
	case S::kArmedVehicle:
		if (triggers && (p == Pad::kLT || p == Pad::kRT || (dpad && p == Pad::kR3)))
			return true;
		if (buttons && (p == Pad::kA || p == Pad::kX || p == Pad::kB))
			return true;
		return s == S::kDriving ? driveBy && p == Pad::kLB : buttons && p == Pad::kRB;
	default: // aircraft
		if (triggers && (p == Pad::kLT || p == Pad::kRT || p == Pad::kLB || p == Pad::kRB || (dpad && p == Pad::kR3)))
			return true;
		return buttons && (p == Pad::kA || p == Pad::kX || p == Pad::kB);
	}
}

bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

} // namespace

PadBinding PadFor(uint16_t switches, Situation s, Action a)
{
	for (const Row &row : kRows)
		if (row.action == a && (row.situations & Bit(s)) && (row.needs & switches) == row.needs)
			return {row.pad, row.press, true};
	return {IsGame(a) ? kGamePad[int(a)] : Pad::kNone, Press::kPress, false};
}

bool Reachable(uint16_t switches, Situation s, Action a)
{
	const PadBinding b = PadFor(switches, s, a);
	if (b.ours)
		return b.pad != Pad::kNone;
	return b.pad != Pad::kNone && !Taken(switches, s, b.pad);
}

Situation ForVehicle(int16_t model, bool aircraft)
{
	if (aircraft)
		switch (model) {
		case 155: return S::kHunter;
		case 177: return S::kSeaSparrow;
		case 194: return S::kRcBaron;
		case 195: case 199: case 217: case 218: case 227: case 231: return S::kHelicopter;
		default: return S::kPlane;
		}
	switch (model) {
	case 162: case 137: case 171: case 160: return S::kArmedVehicle; // the Rhino, the fire truck, the RC Bandit, the Predator
	default: return S::kDriving;
	}
}

Situation ForHint(Situation player, Action a)
{
	return player == S::kOnFoot && ScopeOf(a) == Scope::kVehicle ? S::kDriving : player;
}

Scope ScopeOf(Action a)
{
	switch (a) {
	case Action::kGoLeft:
	case Action::kGoRight:
	case Action::kCameraChangeViewAllSituations:
	case Action::kVehicleEnterExit: // the game checks it against the on-foot actions too
	case Action::kGreet:
	case Action::kInsult:
	case Action::kExpandedMap:
	case Action::kAnswerCall:
		return Scope::kEverywhere;
	case Action::kToggleSubmissions:
	case Action::kRadioWheel:
	case Action::kDriveByAim:
	case Action::kDriveByFire:
	case Action::kCinematicCamera:
		return Scope::kVehicle;
	case Action::kPed1rstPersonLookUp: // the game's first-person (sniper) look, on foot only
	case Action::kPed1rstPersonLookDown:
		return Scope::kOnFoot;
	default:
		break;
	}
	if (a >= Action::kNetworkTalk && a < Action::kGameCount)
		return Scope::kEverywhere;
	if (IsGame(a) && kGameNames[int(a)] && std::string_view(kGameNames[int(a)]).substr(0, 8) == "VEHICLE_")
		return Scope::kVehicle;
	return Scope::kOnFoot;
}

const char *Name(Action a)
{
	if (IsGame(a))
		return kGameNames[int(a)];
	if (IsMod(a))
		return kModNames[int(a) - kFirstMod];
	return nullptr;
}

bool FromName(std::string_view name, Action &out)
{
	for (int i = 0; i < int(Action::kGameCount); i++)
		if (kGameNames[i] && name == kGameNames[i]) {
			out = Action(i);
			return true;
		}
	for (int i = kFirstMod; i < int(Action::kModEnd); i++)
		if (name == kModNames[i - kFirstMod]) {
			out = Action(i);
			return true;
		}
	return false;
}

const char *Token(Pad p)
{
	switch (p) {
	case Pad::kA: return "~X~";
	case Pad::kB: return "~O~";
	case Pad::kX: return "~Q~";
	case Pad::kY: return "~T~";
	case Pad::kLB: return "~K~";
	case Pad::kRB: return "~J~";
	case Pad::kLT: return "~M~";
	case Pad::kRT: return "~V~";
	case Pad::kL3: return "~A~";
	case Pad::kR3: return "~C~";
	case Pad::kBack: return "~@b~";
	case Pad::kStart: return "~@s~";
	case Pad::kUp: return "~@u~";
	case Pad::kDown: return "~@d~";
	case Pad::kLeft: return "~@l~";
	case Pad::kRight: return "~@r~";
	case Pad::kLeftRight: return "~@h~";
	default: return "";
	}
}

const char *HintToken(uint16_t switches, Situation s, Action a, bool &game)
{
	const PadBinding b = PadFor(switches, s, a);
	game = !b.ours;
	return b.ours ? Token(b.pad) : "";
}

bool KeyAlias(Action a, Action &game)
{
	switch (a) {
	case Action::kAnswerCall: game = Action::kPedAnswerPhone; return true;
	case Action::kDriveByFire: game = Action::kVehicleFireweapon; return true;
	default: return false;
	}
}

bool HasOwnKeys(Action a)
{
	Action game;
	return IsMod(a) && !KeyAlias(a, game);
}

keys::Binding DefaultKeys(Action a, const std::function<bool(uint16_t key, Scope scope)> &gameUses)
{
	uint16_t key = keys::kNone;
	switch (a) {
	case Action::kWeaponWheel: key = 'Q'; break;
	case Action::kRadioWheel: key = 'G'; break;
	case Action::kReload: key = 'R'; break;
	case Action::kGreet: key = 'Y'; break;
	case Action::kInsult: key = 'N'; break;
	case Action::kBulletTime: key = 'B'; break;
	case Action::kDriveByAim: key = 'X'; break;
	case Action::kCinematicCamera: key = 'C'; break;
	case Action::kExpandedMap: key = 'M'; break;
	default: return {};
	}
	const Scope own = ScopeOf(a);
	for (Scope s : {Scope::kOnFoot, Scope::kVehicle, Scope::kEverywhere}) {
		const bool overlaps = own == Scope::kEverywhere || s == Scope::kEverywhere || s == own;
		if (overlaps && gameUses(key, s))
			return {};
	}
	keys::Binding b;
	b.key[0] = key;
	return b;
}

PadFamily FamilyForName(std::string_view name)
{
	for (std::string_view sony : {"DualSense", "DualShock", "DUALSHOCK", "PlayStation", "PS3", "PS4", "PS5"})
		if (Contains(name, sony))
			return PadFamily::kPlayStation;
	for (std::string_view nintendo : {"Nintendo", "Pro Controller", "Joy-Con"})
		if (Contains(name, nintendo))
			return PadFamily::kNintendo;
	return PadFamily::kXbox;
}

} // namespace bindings
