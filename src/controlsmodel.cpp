#include "controlsmodel.h"

#include <algorithm>
#include <cctype>

namespace controlsmodel {
namespace {

using bindings::Action;
using bindings::Pad;

// A legend row: an action's button where it is (with a second action, the
// D-pad's left / right as one row), or a fixed button (the sticks).
struct RowSpec {
	Action action;
	Action pair;
	Pad fixed;
	const char *label;
	const char *more;
	Pad fixed2 = Pad::kNone;
};
constexpr Action kNo = Action::kModEnd;

const std::vector<RowSpec> &Rows(PageSituation s)
{
	static const std::vector<RowSpec> onFoot = {
	    {Action::kPedLockTarget, kNo, Pad::kNone, "AIM", ""},
	    {Action::kPedFireweapon, kNo, Pad::kNone, "FIRE", ""},
	    {Action::kWeaponWheel, kNo, Pad::kNone, "WEAPON WHEEL (HOLD)", ""},
	    {kNo, kNo, Pad::kStart, "PAUSE", ""},
	    {Action::kAnswerCall, kNo, Pad::kNone, "ANSWER PHONE", ""},
	    {Action::kPedCycleWeaponLeft, Action::kPedCycleWeaponRight, Pad::kNone, "PREVIOUS / NEXT WEAPON", ""},
	    {Action::kExpandedMap, kNo, Pad::kNone, "MINIMAP ZOOM", ""},
	    {kNo, kNo, Pad::kLeftStick, "MOVE", ""},
	    {Action::kVehicleEnterExit, kNo, Pad::kNone, "ENTER VEHICLE", ""},
	    {Action::kReload, kNo, Pad::kNone, "MELEE / RELOAD", ""},
	    {Action::kPedSprint, kNo, Pad::kNone, "SPRINT", ""},
	    {Action::kPedJumping, kNo, Pad::kNone, "JUMP", ""},
	    {kNo, kNo, Pad::kRightStick, "LOOK", "CLICK WHILE AIMING: BULLET TIME"},
	};
	static const std::vector<RowSpec> driving = {
	    {Action::kVehicleBrake, kNo, Pad::kNone, "BRAKE / REVERSE", ""},
	    {Action::kVehicleAccelerate, kNo, Pad::kNone, "ACCELERATE", ""},
	    {Action::kDriveByAim, kNo, Pad::kNone, "AIM A DRIVE-BY (HOLD)", ""},
	    {Action::kVehicleHandbrake, kNo, Pad::kNone, "HANDBRAKE / DRIVE-BY FIRE", ""},
	    {Action::kToggleSubmissions, kNo, Pad::kNone, "SIDE MISSION / ANSWER PHONE", ""},
	    {Action::kVehicleChangeRadioStation, kNo, Pad::kNone, "RADIO (HOLD: WHEEL)", ""},
	    {Action::kExpandedMap, kNo, Pad::kNone, "MINIMAP ZOOM", ""},
	    {kNo, kNo, Pad::kLeftStick, "STEER", ""},
	    {Action::kVehicleEnterExit, kNo, Pad::kNone, "EXIT", ""},
	    {Action::kCinematicCamera, kNo, Pad::kNone, "CINEMATIC CAMERA (HOLD)", ""},
	    {Action::kVehicleFireweapon, kNo, Pad::kNone, "CAR GUN", ""},
	    {Action::kVehicleHorn, kNo, Pad::kNone, "HORN", ""},
	    {kNo, kNo, Pad::kRightStick, "LOOK", "CLICK: LOOK BEHIND"},
	};
	static const std::vector<RowSpec> flying = {
	    {Action::kVehicleBrake, kNo, Pad::kNone, "DESCEND", ""},
	    {Action::kVehicleAccelerate, kNo, Pad::kNone, "CLIMB", ""},
	    {Action::kVehicleLookleft, kNo, Pad::kNone, "TURN LEFT", ""},
	    {Action::kVehicleLookright, kNo, Pad::kNone, "TURN RIGHT", ""},
	    {Action::kToggleSubmissions, kNo, Pad::kNone, "SIDE MISSION / ANSWER PHONE", ""},
	    {Action::kVehicleChangeRadioStation, kNo, Pad::kNone, "RADIO (HOLD: WHEEL)", ""},
	    {Action::kExpandedMap, kNo, Pad::kNone, "MINIMAP ZOOM", ""},
	    {kNo, kNo, Pad::kLeftStick, "PITCH AND ROLL", ""},
	    {Action::kVehicleEnterExit, kNo, Pad::kNone, "EXIT", ""},
	    {Action::kCinematicCamera, kNo, Pad::kNone, "CINEMATIC CAMERA (HOLD)", ""},
	    {Action::kPedFireweapon, kNo, Pad::kNone, "GUN / BOMBS", ""},
	    {kNo, kNo, Pad::kX, "ROCKETS (HUNTER)", ""},
	};
	// menu.cpp's pause map
	static const std::vector<RowSpec> pauseMap = {
	    {kNo, kNo, Pad::kLB, "TABS", "", Pad::kRB},
	    {kNo, kNo, Pad::kLT, "ZOOM IN / OUT", "", Pad::kRT},
	    {kNo, kNo, Pad::kLeftStick, "MOVE THE CURSOR", ""},
	    {kNo, kNo, Pad::kRightStick, "MOVE THE MAP", ""},
	    {kNo, kNo, Pad::kLeftRight, "NUDGE THE CURSOR", ""},
	    {kNo, kNo, Pad::kA, "SET DESTINATION", ""},
	    {kNo, kNo, Pad::kB, "BACK", ""},
	};
	switch (s) {
	case PageSituation::kOnFoot: return onFoot;
	case PageSituation::kDriving: return driving;
	case PageSituation::kFlying: return flying;
	default: return pauseMap;
	}
}

bindings::Situation SituationOf(PageSituation s)
{
	switch (s) {
	case PageSituation::kDriving: return bindings::Situation::kDriving;
	case PageSituation::kFlying: return bindings::Situation::kHelicopter;
	default: return bindings::Situation::kOnFoot;
	}
}

struct Named {
	Action action;
	const char *label;
};

struct NamedSection {
	const char *title;
	std::vector<Named> rows;
};

const std::vector<NamedSection> &Labelled()
{
	static const std::vector<NamedSection> sections = {
	    {"on foot", {
	    {Action::kGoForward, "FORWARD"},
	    {Action::kGoBack, "BACKWARDS"},
	    {Action::kGoLeft, "LEFT"},
	    {Action::kGoRight, "RIGHT"},
	    {Action::kPedFireweapon, "FIRE"},
	    {Action::kPedLockTarget, "AIM"},
	    {Action::kPedJumping, "JUMP"},
	    {Action::kPedSprint, "SPRINT"},
	    {Action::kVehicleEnterExit, "ENTER VEHICLE"},
	    {Action::kPedDuck, "CROUCH"},
	    {Action::kPedLookbehind, "LOOK BEHIND"},
	    {Action::kPedAnswerPhone, "ACTION"},
	    {Action::kPedCycleWeaponLeft, "PREVIOUS WEAPON"},
	    {Action::kPedCycleWeaponRight, "NEXT WEAPON"},
	    {Action::kPedCycleTargetLeft, "PREVIOUS TARGET"},
	    {Action::kPedCycleTargetRight, "NEXT TARGET"},
	    {Action::kPedSniperZoomIn, "ZOOM IN"},
	    {Action::kPedSniperZoomOut, "ZOOM OUT"},
	    {Action::kPedCenterCameraBehindPlayer, "CAMERA BEHIND"},
	    {Action::kPed1rstPersonLookLeft, "LOOK LEFT"},
	    {Action::kPed1rstPersonLookRight, "LOOK RIGHT"},
	    {Action::kPed1rstPersonLookUp, "LOOK UP"},
	    {Action::kPed1rstPersonLookDown, "LOOK DOWN"},
	    {Action::kCameraChangeViewAllSituations, "CHANGE CAMERA"},
	    {Action::kWeaponWheel, "WEAPON WHEEL"},
	    {Action::kReload, "RELOAD"},
	    {Action::kBulletTime, "BULLET TIME"},
	    {Action::kGreet, "GREET / YES"},
	    {Action::kInsult, "INSULT / NO"},
	    {Action::kExpandedMap, "BIG MAP"},
	    }},
	    {"driving", {
	    {Action::kVehicleAccelerate, "ACCELERATE"},
	    {Action::kVehicleBrake, "BRAKE / REVERSE"},
	    {Action::kVehicleHandbrake, "HANDBRAKE"},
	    {Action::kVehicleHorn, "HORN"},
	    {Action::kVehicleFireweapon, "VEHICLE FIRE"},
	    {Action::kVehicleChangeRadioStation, "NEXT STATION"},
	    {Action::kVehicleLookleft, "LOOK LEFT"},
	    {Action::kVehicleLookright, "LOOK RIGHT"},
	    {Action::kVehicleLookbehind, "LOOK BEHIND"},
	    {Action::kVehicleTurretleft, "TURRET LEFT"},
	    {Action::kVehicleTurretright, "TURRET RIGHT"},
	    {Action::kVehicleTurretup, "TURRET UP"},
	    {Action::kVehicleTurretdown, "TURRET DOWN"},
	    {Action::kToggleSubmissions, "SIDE MISSION"},
	    {Action::kRadioWheel, "RADIO WHEEL"},
	    {Action::kDriveByAim, "AIM A DRIVE-BY"},
	    {Action::kCinematicCamera, "CINEMATIC CAMERA"},
	    }},
	};
	return sections;
}

std::string Upper(std::string s)
{
	for (char &c : s)
		c = char(std::toupper(uint8_t(c)));
	return s;
}

bool Overlap(bindings::Scope a, bindings::Scope b)
{
	return a == b || a == bindings::Scope::kEverywhere || b == bindings::Scope::kEverywhere;
}

} // namespace

std::vector<LegendRow> Legend(uint16_t switches, PageSituation s)
{
	std::vector<LegendRow> rows;
	const bindings::Situation at = SituationOf(s);
	for (const RowSpec &spec : Rows(s)) {
		Pad pad = spec.fixed;
		if (spec.action != kNo) {
			pad = bindings::PadFor(switches, at, spec.action).pad;
			if (spec.pair != kNo && pad == Pad::kLeft && bindings::PadFor(switches, at, spec.pair).pad == Pad::kRight)
				pad = Pad::kLeftRight;
		}
		if (pad != Pad::kNone)
			rows.push_back({pad, spec.label, spec.more, spec.fixed2});
	}
	return rows;
}

const std::vector<KeySection> &KeySections()
{
	static const std::vector<KeySection> sections = [] {
		std::vector<KeySection> s;
		for (const NamedSection &named : Labelled()) {
			s.push_back({named.title, {}});
			for (const Named &n : named.rows)
				s.back().actions.push_back(n.action);
		}
		return s;
	}();
	return sections;
}

const char *Label(Action a)
{
	for (const NamedSection &named : Labelled())
		for (const Named &n : named.rows)
			if (n.action == a)
				return n.label;
	return "";
}

Status PadStatus(const std::vector<Joystick> &connected, int gameJoystick)
{
	if (connected.empty())
		return {Dot::kMissing, "NO CONTROLLER CONNECTED", ""};
	const Joystick *game = nullptr;
	for (const Joystick &j : connected)
		if (j.id == gameJoystick)
			game = &j;
	// A PlayStation or Nintendo pad the game doesn't read: Steam's virtual pad stands in for it.
	const Joystick *physical = nullptr;
	for (const Joystick &j : connected)
		if (&j != game && j.gamepad && bindings::FamilyForName(j.name) != bindings::PadFamily::kXbox &&
		    (!game || bindings::FamilyForName(game->name) != bindings::FamilyForName(j.name)))
			physical = physical ? physical : &j;
	if (!game) {
		const Joystick *first = physical;
		for (const Joystick &j : connected)
			first = first ? first : (j.gamepad ? &j : nullptr);
		if (!first)
			first = &connected.front();
		return {Dot::kAttention, Upper(first->name), "THE GAME ISN'T READING IT", first->id};
	}
	if (!game->gamepad)
		return {Dot::kAttention, Upper(game->name), "NO BUTTON LAYOUT FOR THIS PAD"};
	if (game->name.find("Steam") != std::string::npos || physical)
		return {Dot::kAttention, Upper((physical ? physical : game)->name), "THE GAME IS READING STEAM'S VIRTUAL PAD",
		        physical ? physical->id : -1};
	return {Dot::kReady, Upper(game->name), ""};
}

Status KeyStatus(int unbound)
{
	if (unbound <= 0)
		return {Dot::kReady, "KEYBOARD AND MOUSE", ""};
	return {Dot::kAttention, "KEYBOARD AND MOUSE",
	        std::to_string(unbound) + (unbound == 1 ? " ACTION HAS NO KEY" : " ACTIONS HAVE NO KEY")};
}

int Unbound(const KeyTable &table)
{
	int n = 0;
	for (const auto &[action, keys] : table)
		n += keys[0] == 0 && keys[1] == 0 && keys[2] == 0;
	return n;
}

std::vector<Action> Rebind(KeyTable &table, Action a, int slot, uint16_t key)
{
	std::vector<Action> taken;
	const bindings::Scope scope = bindings::ScopeOf(a);
	for (auto &[other, keys] : table) {
		if (other == a || !Overlap(scope, bindings::ScopeOf(other)))
			continue;
		bool had = false;
		for (uint16_t &k : keys)
			if (k == key) {
				k = 0;
				had = true;
			}
		if (had)
			taken.push_back(other);
	}
	std::array<uint16_t, 3> &own = table[a];
	for (uint16_t &k : own) // not twice on the same action
		if (k == key)
			k = 0;
	own[slot] = key;
	return taken;
}

bool Apply(Focus &f, Input in, const Sizes &sizes)
{
	if (f.capturing) // a key is wanted: Escape or Back cancels, the rest is the key
		return in == Input::kEscape || in == Input::kBack ? (f.capturing = false, true) : true;
	if (in == Input::kBack || in == Input::kEscape)
		return false;
	const std::vector<Area> areas = f.tab == Tab::kController
	                                    ? std::vector<Area>{Area::kTabs, Area::kSituations, Area::kSettings}
	                                    : std::vector<Area>{Area::kTabs, Area::kKeys};
	size_t at = 0;
	while (at + 1 < areas.size() && areas[at] != f.area)
		at++;
	const int step = in == Input::kLeft || in == Input::kUp ? -1 : 1;
	switch (in) {
	case Input::kUp:
	case Input::kDown:
		if (f.area == Area::kKeys && f.item + step >= 0 && f.item + step < sizes.keyRows) {
			f.item += step;
			return true;
		}
		if ((step < 0 && at > 0) || (step > 0 && at + 1 < areas.size())) {
			f.area = areas[at + step];
			f.item = 0;
			f.slot = 0;
		}
		return true;
	case Input::kLeft:
	case Input::kRight:
		switch (f.area) {
		case Area::kTabs: f.tab = step < 0 ? Tab::kController : Tab::kKeyboard; break;
		case Area::kSituations: {
			const int n = int(PageSituation::kCount);
			f.situation = PageSituation((int(f.situation) + step + n) % n);
			break;
		}
		case Area::kSettings:
			if (f.item != sizes.slider) // the page steps the slider (SliderStep)
				f.item = std::clamp(f.item + step, 0, sizes.settings - 1);
			break;
		case Area::kKeys: f.slot = std::clamp(f.slot + step, 0, 2); break;
		}
		return true;
	case Input::kSelect:
		if (f.area == Area::kKeys)
			f.capturing = true;
		return true;
	default:
		return false;
	}
}

int SliderStep(const Focus &f, Input in, const Sizes &sizes)
{
	if (f.capturing || f.tab != Tab::kController || f.area != Area::kSettings || f.item != sizes.slider)
		return 0;
	return in == Input::kLeft ? -1 : in == Input::kRight ? 1 : 0;
}

CaptureClick OnClickWhileCapturing(bool onCap, bool onTarget)
{
	return onCap ? CaptureClick::kBind : onTarget ? CaptureClick::kRedirect : CaptureClick::kCancel;
}

} // namespace controlsmodel
