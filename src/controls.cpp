#include "controls.h"
#include "actions.h"
#include "alert.h"
#include "aimcam.h"
#include "climb.h"
#include "config.h"
#include "crouch.h"
#include "shootdodge.h"
#include "driveby.h"
#include "interact.h"
#include "layout.h"
#include "game.h"
#include "hints.h"
#include "hooks.h"
#include "log.h"
#include "minimap.h"
#include "watch.h"
#include "phone.h"
#include "rumble.h"
#include "wheel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace game;

namespace controls {
namespace {

using bindings::Action;

using Clock = std::chrono::steady_clock;

// GLFWgamepadstate
enum GamepadButton {
	kA, kB, kX, kY, kLB, kRB, kBack, kStart, kGuide, kLThumb, kRThumb, kUp, kRight, kDown, kLeft, kButtons
};
enum GamepadAxis { kLX, kLY, kRX, kRY, kLT, kRT, kAxes };
struct GlfwGamepadState {
	unsigned char buttons[kButtons];
	float axes[kAxes];
};
constexpr int kMaxJoysticks = 16;

constexpr auto kHoldToOpen = std::chrono::milliseconds(200);
constexpr int kRetuneFrames = 20; // as for a press of the station button
constexpr auto kSubMissionHold = std::chrono::milliseconds(300); // R3, as the scripts poll it
constexpr float kMouseRange = 60.0f;    // mouse travel (pixels) that counts as a full stick push
constexpr float kTriggerPressed = -0.7f; // GLFW triggers rest at -1 and are fully in at 1
constexpr float kPedalSlack = 0.04f, kPedalFloor = 0.96f; // trigger travel (0..1) read as untouched / fully in
constexpr int32_t kCinematicZoom = 5; // CCamera::m_nCarZoom that turns on the Obbe cinema camera (CCamera::CamControl)
constexpr float kStickDeadZone = 0.12f;  // round, on the right stick
// The left stick: a round dead zone too, and full from this far out (a stick
// rarely reaches 1 on the diagonals).
constexpr float kMoveDeadZone = 0.12f, kMoveFull = 0.95f;
constexpr float kLookYaw = 3.5f, kLookPitch = 2.2f; // radians per second at full deflection
constexpr float kMouseAimSensitivity = 0.0025f;     // radians a pixel, aiming a drive-by by key (the game's default)

struct Gamepad {
	bool buttons[kButtons] = {};
	float lx = 0, ly = 0, rx = 0, ry = 0, lt = -1, rt = -1;
	bool live = false; // read from a gamepad this frame
};

Gamepad FromState(const GlfwGamepadState &s)
{
	Gamepad g;
	for (int b = 0; b < kButtons; b++)
		g.buttons[b] = s.buttons[b];
	g.lx = s.axes[kLX];
	g.ly = s.axes[kLY];
	g.rx = s.axes[kRX];
	g.ry = s.axes[kRY];
	g.lt = s.axes[kLT];
	g.rt = s.axes[kRT];
	g.live = true;
	return g;
}

void (*orig_Update)(uint8_t *pad, int16_t index);
Gamepad gPrev;
bool gLBPending;            // LB went down and hasn't been a hold yet
Clock::time_point gLBDownAt;
bool gKeyWheel;             // a wheel's key opened the wheel open now
bool gKeyWheelPending;      // a wheel's key went down and hasn't been a hold yet
Clock::time_point gKeyWheelAt;
bool gLoggedGamepad;
bool gTestWheel; // VCHUD_TEST_WHEEL: open the wheel once, without LB, to look at it
Vec2 gMouse;     // the mouse's pointer on an open wheel, within kMouseRange
Clock::time_point gLastFrame;
Clock::time_point gSubMissionUntil; // R3 held for the scripts until then
GamepadButton gDpadHeld = kButtons;  // D-pad left / right went down in a vehicle, not yet a hold
Clock::time_point gDpadDownAt;
bool gDpadWheel;                     // the radio wheel is open on a D-pad hold
int32_t gCarZoomBeforeB = -1;        // B is held for the cinematic camera: the car camera to go back to
GamepadButton gAnswering = kButtons; // a face button that answered a prompt, until it's let go
bindings::PadFamily gFamily;         // the pads connected: PlayStation if any is, then Nintendo, else Xbox
int gPadScanIn;                      // frames until the pads' names are looked at again
bool gJumpHeld;                      // the jump that started a climb, until it's let go

// The game's gamecontrollerdb.txt maps Sony's own pads (PS4 Controller, PS5
// Controller, Sony DualSense) on macOS with the right stick's Y on axis 3,
// which is the left trigger: the camera tilts with L2 and the stick's Y does
// nothing. On macOS these pads report LX, LY, RX, L2, R2, RY (axes 0-5), as
// the database's third-party PS4 pads already have it. The game reads the file
// once at start-up; GLFW lets a later mapping for the same pad replace it.
void FixControllerMappings()
{
	FILE *f = api.glfwUpdateGamepadMappings ? fopen("gamecontrollerdb.txt", "r") : nullptr; // cwd: GameData
	if (!f)
		return;
	std::string fixed;
	int count = 0;
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		std::string s = line;
		if (s.find("platform:Mac OS X") == std::string::npos || s.find(",righty:a3,") == std::string::npos ||
		    (s.find(",lefttrigger:a3,") == std::string::npos && s.find(",lefttrigger:+a3,") == std::string::npos))
			continue;
		auto replace = [&s](const std::string &from, const std::string &to) {
			const size_t at = s.find(from);
			if (at != std::string::npos)
				s.replace(at, from.size(), to);
		};
		replace(",righty:a3,", ",righty:a5,");
		replace(",lefttrigger:+a3,", ",lefttrigger:a3,");
		replace(",righttrigger:+a4,", ",righttrigger:a4,");
		fixed += s;
		count++;
	}
	fclose(f);
	if (count && api.glfwUpdateGamepadMappings(fixed.c_str()))
		Log("controls: corrected %d PlayStation controller mapping(s) (right stick Y was on the left trigger)", count);
}

// The joystick the game reads for pad 0 (CapturePad: RsGlobal.ps->joy1id), -1 for none.
int GameJoystick() { return *(*reinterpret_cast<int8_t *const *>(api.rsGlobal + 0x20) + 0x21); }


// Only the game's own joystick: with Steam Input there are two gamepads (the
// physical one and Steam's virtual one), which must not be mixed.
Gamepad Poll()
{
	Gamepad g;
	if (--gPadScanIn <= 0) { // the game's may be Steam's virtual pad: any Sony pad connected counts
		gPadScanIn = 120;
		bool any = false;
		bindings::PadFamily family = bindings::PadFamily::kXbox;
		for (int j = 0; j < kMaxJoysticks; j++) {
			if (!api.glfwJoystickIsGamepad(j))
				continue;
			const char *name = api.glfwGetJoystickName(j);
			const bindings::PadFamily f = bindings::FamilyForName(name ? name : "");
			if (!any || f == bindings::PadFamily::kPlayStation ||
			    (f == bindings::PadFamily::kNintendo && family == bindings::PadFamily::kXbox))
				family = f;
			any = true;
		}
		if (any) // none: the last pad's
			gFamily = family;
	}
	const int jid = GameJoystick();
	GlfwGamepadState s;
	if (jid < 0 || !api.glfwJoystickIsGamepad(jid) || !api.glfwGetGamepadState(jid, &s))
		return g;
	if (!gLoggedGamepad) {
		gLoggedGamepad = true;
		for (int j = 0; j < kMaxJoysticks; j++)
			if (api.glfwJoystickIsGamepad(j)) {
				const char *name = api.glfwGetJoystickName(j);
				Log("controls: gamepad %d (%s)%s", j, name ? name : "?", j == jid ? ", the game's" : "");
			}
	}
	return FromState(s);
}

bool Pressed(const Gamepad &now, GamepadButton b) { return now.buttons[b] && !gPrev.buttons[b]; }

// layout.h's buttons on the GLFW pad: the remaps below read them through
// these, so moving one there moves it here and in the hints.
GamepadButton Button(bindings::Pad b)
{
	switch (b) {
	case bindings::Pad::kA: return kA;
	case bindings::Pad::kB: return kB;
	case bindings::Pad::kX: return kX;
	case bindings::Pad::kY: return kY;
	case bindings::Pad::kLB: return kLB;
	case bindings::Pad::kRB: return kRB;
	case bindings::Pad::kBack: return kBack;
	case bindings::Pad::kStart: return kStart;
	case bindings::Pad::kL3: return kLThumb;
	case bindings::Pad::kR3: return kRThumb;
	case bindings::Pad::kUp: return kUp;
	case bindings::Pad::kDown: return kDown;
	case bindings::Pad::kLeft: return kLeft;
	case bindings::Pad::kRight: return kRight;
	default: return kButtons; // the triggers, the sticks, the D-pad's left / right pair
	}
}

// How far a trigger is pulled (GLFW: -1 at rest, 1 fully in).
float Axis(const Gamepad &pad, bindings::Pad b) { return b == bindings::Pad::kLT ? pad.lt : b == bindings::Pad::kRT ? pad.rt : -1.0f; }

bool Down(const Gamepad &pad, bindings::Pad b)
{
	if (b == bindings::Pad::kLT || b == bindings::Pad::kRT)
		return Axis(pad, b) > kTriggerPressed;
	if (b == bindings::Pad::kLeftRight)
		return pad.buttons[kLeft] || pad.buttons[kRight];
	const GamepadButton g = Button(b);
	return g != kButtons && pad.buttons[g];
}

bool Pressed(const Gamepad &now, bindings::Pad b) { return Down(now, b) && !Down(gPrev, b); }

// The game's own field for a face button pressed on the GLFW pad.
PadButton GameFace(GamepadButton b) { return b == kA ? kPadCross : b == kB ? kPadCircle : b == kX ? kPadSquare : kPadTriangle; }

float Length(float x, float y) { return std::sqrt(x * x + y * y); }

// The right stick picks, or the mouse (which Steam Input can turn the stick
// into); the camera gets neither while the wheel is open.
void SteerWheel(uint8_t *pad, const Gamepad &now)
{
	auto &mx = *reinterpret_cast<float *>(api.newMouseState + 8);
	auto &my = *reinterpret_cast<float *>(api.newMouseState + 12);
	gMouse = {gMouse.x + mx, gMouse.y + my};
	const float m = Length(gMouse.x, gMouse.y);
	if (m > kMouseRange)
		gMouse = {gMouse.x * kMouseRange / m, gMouse.y * kMouseRange / m};
	if (Length(now.rx, now.ry) >= Length(gMouse.x, gMouse.y) / kMouseRange)
		wheel::Steer(now.rx, now.ry);
	else
		wheel::Steer(gMouse.x / kMouseRange, gMouse.y / kMouseRange);
	PadJoy(pad, kPadRightStickX) = 0;
	PadJoy(pad, kPadRightStickY) = 0;
	mx = my = 0.0f;
}

void OpenWheel()
{
	if (wheel::Open())
		gMouse = {};
}

// On foot, LT aims and RT fires (modern layout) instead of cycling weapons as on
// the PS2, and R1 / Circle no longer aim / fire, so they are free. Done on the
// joystick's own state, so keyboard bindings are untouched. The triggers are
// read directly: the game only reports LT while its binding (previous weapon)
// applies, which stops being the case once the player aims, so aiming through
// it would flicker on and off every frame.
void TriggersAimAndFire(uint8_t *pad, const Gamepad &now)
{
	// The buttons CPad::GetTarget / CPad::GetWeapon read in each controller mode.
	const int mode = *api.padAffectedByController ? *reinterpret_cast<const uint16_t *>(pad + 0xec) : 0;
	if (mode == 3) // aims with LB, which opens the wheels here
		return;
	const PadButton aim = kPadRB, fire = mode == 2 ? kPadCross : kPadCircle;
	PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = PadJoy(pad, aim) = PadJoy(pad, fire) = 0;
	if (Down(now, layout::kAim))
		PadJoy(pad, aim) = 255;
	if (Down(now, layout::kFire))
		PadJoy(pad, fire) = 255;
}

// Vehicles whose gun the game fires on Circle (CPad::GetCarGunFired): the
// Rhino's cannon, the fire truck's hose, the RC Bandit's charge, the Predator's cannons.
bool ArmedVehicle(const uint8_t *vehicle)
{
	return bindings::ForVehicle(EntityModel(vehicle), false) == bindings::Situation::kArmedVehicle;
}

// GTA V's B on foot: a punch or swing with fists or a melee weapon, a reload
// with a gun (A sprint, X jump and Y enter are the game's own already).
void FaceButtonsOnFoot(uint8_t *pad, uint8_t *ped, const Gamepad &now)
{
	uint8_t *weapon = PedWeapon(ped, PedCurrentWeaponSlot(ped));
	const int32_t type = WeaponType(weapon);
	if (type < kFirstThrownWeapon) {
		const int mode = *api.padAffectedByController ? *reinterpret_cast<const uint16_t *>(pad + 0xec) : 0;
		if (Down(now, layout::kMeleeReload))
			PadJoy(pad, mode == 2 ? kPadCross : kPadCircle) = 255; // as RT fires
		return;
	}
	// A reload as an empty clip starts one (CWeapon::Fire): reloading until the weapon's reload time is up.
	const uint8_t *info = api.GetWeaponInfo(type);
	int32_t &state = *reinterpret_cast<int32_t *>(weapon + 4), &clip = *reinterpret_cast<int32_t *>(weapon + 8);
	const int32_t clipSize = *reinterpret_cast<const int32_t *>(info + 0x10);
	if (!(Pressed(now, layout::kMeleeReload) || actions::KeyPressed(Action::kReload)) || type < kFirstGun || state != kWeaponReady || clip >= clipSize ||
	    WeaponAmmoTotal(weapon) <= clip)
		return;
	const uint32_t reload = *reinterpret_cast<const uint32_t *>(info + 0xc);
	state = kWeaponReloading;
	*reinterpret_cast<uint32_t *>(weapon + 0x10) = *api.timeMs + (FastReload(PlayerInfo()) ? reload / 4 : reload);
}

// The cinematic camera is held: B, or its key.
bool Cinematic(const Gamepad &now) { return Down(now, layout::kCinematic) || actions::KeyHeld(Action::kCinematicCamera); }

// GTA V's face buttons in a vehicle: A and X no longer drive (the triggers
// do), B held is the cinematic camera, and on an armed vehicle RB fires its
// gun instead of the handbrake, which goes to A. On any other A is the
// game's car gun (B): arming a car bomb, a bike's guns.
void FaceButtonsInVehicle(uint8_t *pad, const uint8_t *vehicle, const Gamepad &now)
{
	PadJoy(pad, kPadCross) = PadJoy(pad, kPadSquare) = 0;
	if (ArmedVehicle(vehicle)) {
		PadJoy(pad, kPadCircle) = Down(now, layout::kVehicleGun) ? 255 : 0;
		PadJoy(pad, kPadRB) = Down(now, layout::kArmedHandbrake) ? 255 : 0;
	} else {
		PadJoy(pad, kPadCircle) = Down(now, layout::kCarGun) ? 255 : 0;
	}
	int32_t &zoom = *reinterpret_cast<int32_t *>(api.theCamera + 0xe4); // CCamera::m_nCarZoom
	if (Cinematic(now) && gCarZoomBeforeB < 0) {
		gCarZoomBeforeB = zoom;
		zoom = kCinematicZoom;
	} else if (!Cinematic(now) && gCarZoomBeforeB >= 0) {
		zoom = gCarZoomBeforeB;
		gCarZoomBeforeB = -1;
	}
}

// How far a trigger is pulled, as a pedal: 0 (off) to 1 (floored).
float Pedal(float axis)
{
	return std::clamp(((axis + 1.0f) / 2.0f - kPedalSlack) / (kPedalFloor - kPedalSlack), 0.0f, 1.0f);
}

struct Pedals {
	float gas = 0, brake = 0;
};

// In a car, bike or boat, RT accelerates and LT brakes or reverses, as far as
// they're pulled, instead of looking left / right. CPad::GetAccelerate and
// GetBrake read Cross and Square in controller modes 0-2 as pressure (0-255,
// from the PS2's pressure-sensitive buttons), but CPad::Update's merge of the
// joystick and keyboard turns every button into 0 or 255, so the pedals go in
// after it (Hook_Update). Aircraft: TriggersFly.
Pedals TriggersDrive(uint8_t *pad, const Gamepad &now)
{
	if (*api.padAffectedByController && *reinterpret_cast<const uint16_t *>(pad + 0xec) == 3)
		return {};
	PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = 0;
	// Looking back was both triggers (CPad::GetLookBehindForCar); it moves to R3,
	// as in GTA V, whose own job (side missions) is on D-pad up.
	if (gConfig.dpadActions) {
		PadJoy(pad, kPadR3) = 0;
		if (Down(now, layout::kLookBehind))
			PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = 255;
	}
	return {Pedal(Axis(now, layout::kGas)), Pedal(Axis(now, layout::kBrake))};
}

// Flying, as GTA V: RT climbs (a plane's throttle) and LT descends, as far as
// they're pulled (CVehicle::FlyingControl's thrust is accelerate less brake);
// LB and RB turn the tail (the look left / right buttons, the triggers on a
// pad: a helicopter's yaw, a plane's rudder, the RC Baron's roll); the left
// stick pitches and rolls, as it did. The guns go to the face buttons: A the
// gun (the Hunter's cannon and the Sea Sparrow's on the handbrake, the RC
// Baron's on Circle, the RC helicopters' bombs on both), X the Hunter's
// rockets (Circle).
constexpr int16_t kHunter = 155, kSeaSparrow = 177, kRcBaron = 194;

Pedals TriggersFly(uint8_t *pad, const Gamepad &now)
{
	if (*api.padAffectedByController && *reinterpret_cast<const uint16_t *>(pad + 0xec) == 3)
		return {};
	PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = PadJoy(pad, kPadLB) = PadJoy(pad, kPadRB) = 0;
	if (gConfig.dpadActions) // (a car's look behind, both look buttons: here both ways at once)
		PadJoy(pad, kPadR3) = 0;
	PadJoy(pad, kPadLT) = Down(now, layout::kYawLeft) ? 255 : 0;
	PadJoy(pad, kPadRT) = Down(now, layout::kYawRight) ? 255 : 0;
	return {Pedal(Axis(now, layout::kClimb)), Pedal(Axis(now, layout::kDescend))};
}

void FaceButtonsInAircraft(uint8_t *pad, const uint8_t *vehicle, const Gamepad &now)
{
	PadJoy(pad, kPadCross) = PadJoy(pad, kPadSquare) = PadJoy(pad, kPadCircle) = 0;
	const int16_t model = EntityModel(vehicle);
	const int16_t gun = Down(now, layout::kAircraftGun) ? 255 : 0;
	if (model == kHunter || model == kSeaSparrow)
		PadJoy(pad, kPadRB) = gun;
	else if (model == kRcBaron)
		PadJoy(pad, kPadCircle) = gun;
	else // the RC helicopters' bombs and detonation (fire or handbrake), whatever a plane fires
		PadJoy(pad, kPadCircle) = PadJoy(pad, kPadRB) = gun;
	if (model == kHunter)
		PadJoy(pad, kPadCircle) = Down(now, layout::kHunterRockets) ? 255 : 0;
	int32_t &zoom = *reinterpret_cast<int32_t *>(api.theCamera + 0xe4); // B held: the cinematic camera, as in a car
	if (Cinematic(now) && gCarZoomBeforeB < 0) {
		gCarZoomBeforeB = zoom;
		zoom = kCinematicZoom;
	} else if (!Cinematic(now) && gCarZoomBeforeB >= 0) {
		zoom = gCarZoomBeforeB;
		gCarZoomBeforeB = -1;
	}
}

// In a vehicle, D-pad left / right: a tap retunes to the previous / next
// station, a hold opens the radio wheel, which letting go applies.
void RadioDpad(const Gamepad &now, uint8_t *vehicle) // vehicle: null when not in one, or no control
{
	static_assert(layout::kRadio == bindings::Pad::kLeftRight, "a tap of the pair's left / right steps back / on");
	if (gDpadWheel && (!wheel::IsOpen() || !Down(now, layout::kRadio))) {
		wheel::Close(true);
		gDpadWheel = false;
	}
	if (!vehicle || wheel::IsOpen()) {
		gDpadHeld = kButtons;
		return;
	}
	for (GamepadButton b : {kLeft, kRight})
		if (Pressed(now, b)) {
			gDpadHeld = b;
			gDpadDownAt = Clock::now();
		}
	if (gDpadHeld == kButtons)
		return;
	if (!now.buttons[gDpadHeld]) {
		if (RadioUsable(vehicle))
			RetuneBy(vehicle, gDpadHeld == kRight ? 1 : -1, kRetuneFrames);
		gDpadHeld = kButtons;
	} else if (Clock::now() - gDpadDownAt >= kHoldToOpen) {
		gDpadHeld = kButtons;
		if (gConfig.wheels) {
			OpenWheel();
			gDpadWheel = wheel::IsOpen();
		}
	}
}

// The left stick as the game should have it: one round dead zone and the rest
// of the way out, in any direction. The port's CapturePad drops each axis under
// 0.3 on its own and then jumps to 38 of 128, so walking snapped to straight
// lines and had only a sliver of push to walk in (it runs past 60 of 128).
void LeftStick(uint8_t *pad, const Gamepad &now)
{
	if (!now.live)
		return; // (no gamepad: the keyboard's, as it was)
	const float m = Length(now.lx, now.ly);
	int16_t x = 0, y = 0;
	if (m > kMoveDeadZone) {
		const float n = std::min(1.0f, (m - kMoveDeadZone) / (kMoveFull - kMoveDeadZone)) * 128.0f / m;
		x = int16_t(std::clamp(std::lround(now.lx * n), -128L, 128L));
		y = int16_t(std::clamp(std::lround(now.ly * n), -128L, 128L));
	}
	PadJoy(pad, kPadLeftStickX) = x;
	PadJoy(pad, kPadLeftStickY) = y;
}

// The right stick as turn rates (radians per second, to the right and up): a
// small round dead zone, a response curve, slower while aiming. The game's own
// stick handling only starts at 30% deflection, with a jump (CapturePad).
bool gLookTurn;
float gLookYaw, gLookPitch; // this frame's, for the first-person seat view

bool StickRates(const Gamepad &now, bool precise, float &yaw, float &pitch)
{
	const float m = Length(now.rx, now.ry);
	if (m <= kStickDeadZone)
		return false;
	const float n = std::min(1.0f, (m - kStickDeadZone) / (1.0f - kStickDeadZone));
	float speed = std::pow(n, precise ? 2.0f : 1.7f) / m * gConfig.lookSensitivity;
	if (precise)
		speed *= gConfig.aimSensitivity;
	yaw = now.rx * speed * kLookYaw;
	pitch = (*api.invertLookForPad ? now.ry : -now.ry) * speed * kLookPitch; // the way the game's own stick goes
	return true;
}

// Camera look from the right stick, on foot. The scoped views look with the
// left stick (CPad::SniperModeLookLeftRight); instead the stick drives the
// cameras' mouse input, at StickRates and as much slower in a scope as it
// magnifies. Mouse movement turns the follow camera by x * -2.5 and y * 4 times
// the mouse sensitivity and FOV / 80 (CCam::Process_FollowPedWithMouse), the
// scoped views by -3 and 4 (CCam::Process_Sniper).
void StickLook(uint8_t *pad, const Gamepad &now, bool aiming, float dt)
{
	const uint8_t *cam = ActiveCam();
	const uint16_t mode = CamMode(cam);
	const bool follow = mode == kCamFollowPed && *api.useMouse3rdPerson;
	const bool runabout = mode >= kCamSniperRunabout && mode <= kCamM16RunaboutLast; // can walk: left stick moves
	const bool scoped = mode == kCamSniper || mode == kCamRocketLauncher || mode == kCamM16FirstPerson ||
	                    mode == kCamHeliCannon || mode == kCamCameraWeapon;
	if (!follow && !runabout && !scoped)
		return;
	PadJoy(pad, kPadRightStickX) = PadJoy(pad, kPadRightStickY) = 0;
	if (scoped)
		PadJoy(pad, kPadLeftStickX) = PadJoy(pad, kPadLeftStickY) = 0;
	float yaw, pitch;
	if (!StickRates(now, !follow || aiming, yaw, pitch) || *api.mouseAccelH <= 0.0f || *api.mouseAccelV <= 0.0f)
		return;
	const float fov = CamFov(cam);
	const float zoom = follow ? 1.0f : fov / 70.0f;
	const float perMouse = fov / 80.0f; // radians per unit of mouse movement, before the axis factor
	const float yawPerMouse = (follow ? 2.5f : 3.0f) * *api.mouseAccelH * perMouse;
	const float pitchPerMouse = 4.0f * *api.mouseAccelV * perMouse;
	*reinterpret_cast<float *>(api.newMouseState + 8) += yaw * zoom * dt / yawPerMouse;
	*reinterpret_cast<float *>(api.newMouseState + 12) += pitch * zoom * dt / pitchPerMouse;
}

void Hook_Update(uint8_t *pad, int16_t index)
{
	watch::Beat(); // the game's loop came round (in its menus too)
	if (pad == api.GetPad(0))
		hints::Frame();
	if (pad == api.GetPad(0))
		alert::Frame(); // (paused too: it goes quiet)
	if (pad != api.GetPad(0)) {
		orig_Update(pad, index);
		return;
	}
	static bool mappingsFixed;
	if (!mappingsFixed) { // the game has loaded its database by its first frame
		mappingsFixed = true;
		FixControllerMappings();
	}
	const Gamepad now = Poll();
	if (MenuActive(api.frontEndMenuManager)) { // the frontend keeps the D-pad and LB
		wheel::Close(false);
		gLBPending = gDpadWheel = gKeyWheel = gKeyWheelPending = false;
		gDpadHeld = kButtons;
		driveby::SetInput(false, false, 0, 0);
		rumble::Drive(nullptr, 0, 0);
		gPrev = now;
		orig_Update(pad, index);
		return;
	}
	actions::Update(); // the keys of the mod's own actions
	LeftStick(pad, now);
	uint8_t *ped = api.FindPlayerPed();
	const bool control = ped && !PadControlsDisabled(pad);
	const Clock::time_point frame = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(frame - gLastFrame).count(), 0.1f);
	gLastFrame = frame;

	if (gConfig.dpadActions)
		for (PadButton b : {kPadDPadUp, kPadDPadDown, kPadDPadLeft, kPadDPadRight})
			PadJoy(pad, b) = 0;

	uint8_t *vehicle = ped && PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	const bool lbAims = driveby::Enabled() && vehicle && !IsAircraft(vehicle) && !ArmedVehicle(vehicle); // else LB is the wheel's
	const bool lbFlies = gConfig.modernTriggers && control && vehicle && IsAircraft(vehicle); // LB and RB: the tail

	// While a prompt is up, X (Square) answers yes: greet, pick up; B (Circle)
	// no: insult, wave off, as RDR2's greet and antagonize. The button is the
	// prompt's until it's let go, so it doesn't also jump, punch or hold the
	// cinematic camera.
	interact::Update();
	if (control && interact::Prompting() && (Pressed(now, layout::kGreet) || Pressed(now, layout::kInsult))) {
		interact::Respond(Pressed(now, layout::kGreet));
		gAnswering = Button(Pressed(now, layout::kGreet) ? layout::kGreet : layout::kInsult);
	}
	if (control && interact::Prompting() &&
	    (actions::KeyPressed(Action::kGreet) || actions::KeyPressed(Action::kInsult)))
		interact::Respond(actions::KeyPressed(Action::kGreet));
	if (gAnswering != kButtons && !now.buttons[gAnswering])
		gAnswering = kButtons;
	Gamepad face = now; // the buttons the layout below sees
	if (gAnswering != kButtons) {
		face.buttons[gAnswering] = false;
		PadJoy(pad, GameFace(gAnswering)) = 0;
	}

	bool tapLB = false;
	Pedals pedals;
	if (gConfig.modernTriggers && control) {
		if (!vehicle) {
			TriggersAimAndFire(pad, now);
			if (gConfig.modernButtons)
				FaceButtonsOnFoot(pad, ped, face);
		} else if (!IsAircraft(vehicle)) {
			pedals = TriggersDrive(pad, now);
			if (gConfig.modernButtons)
				FaceButtonsInVehicle(pad, vehicle, face);
		} else {
			pedals = TriggersFly(pad, now);
			if (gConfig.modernButtons)
				FaceButtonsInAircraft(pad, vehicle, face);
		}
	}
	if (gCarZoomBeforeB >= 0 && (!vehicle || !control)) { // got out with B held
		*reinterpret_cast<int32_t *>(api.theCamera + 0xe4) = gCarZoomBeforeB;
		gCarZoomBeforeB = -1;
	}

	gLookTurn = StickRates(now, false, gLookYaw, gLookPitch);
	gLookYaw *= dt;
	gLookPitch *= dt;

	// In a car, on a bike or a boat: hold LB to aim, the right stick turns the
	// aim, RB shoots (otherwise the handbrake).
	float aimYaw = 0, aimPitch = 0;
	if (StickRates(now, true, aimYaw, aimPitch)) {
		aimYaw *= dt;
		aimPitch *= dt;
	}
	// Or its key held: the mouse turns the aim, and the game's vehicle fire shoots.
	const bool keyAim = lbAims && control && actions::KeyHeld(Action::kDriveByAim) && !wheel::IsOpen();
	if (keyAim) {
		auto &mx = *reinterpret_cast<float *>(api.newMouseState + 8);
		auto &my = *reinterpret_cast<float *>(api.newMouseState + 12);
		const float sensitivity = api.mouseAccel ? *api.mouseAccel : kMouseAimSensitivity;
		aimYaw += mx * sensitivity;
		aimPitch -= my * sensitivity;
		mx = my = 0.0f; // the camera doesn't follow the mouse meanwhile
	}
	driveby::SetInput((lbAims && control && Down(now, layout::kDriveByAim) && !wheel::IsOpen()) || keyAim,
	                  Down(now, layout::kDriveByFire) || (keyAim && actions::KeyHeld(Action::kDriveByFire)), aimYaw,
	                  aimPitch);
	if (lbAims)
		PadJoy(pad, kPadLB) = 0;
	if (driveby::Aiming())
		PadJoy(pad, kPadRB) = PadJoy(pad, kPadRightStickX) = PadJoy(pad, kPadRightStickY) = 0;

	if (gTestWheel && control && !wheel::IsOpen()) {
		gTestWheel = false;
		OpenWheel();
	}
	if (gConfig.wheels) {
		if (!lbFlies)
			PadJoy(pad, kPadLB) = 0; // LB reaches the game only as a tap, below
		if (lbAims || lbFlies)
			gLBPending = false;
		else if (Pressed(now, layout::kWeaponWheel)) {
			gLBPending = true;
			gLBDownAt = Clock::now();
		}
		if (Down(now, layout::kWeaponWheel) && gLBPending && Clock::now() - gLBDownAt >= kHoldToOpen) {
			gLBPending = false;
			if (control)
				OpenWheel();
		}
		if (!Down(now, layout::kWeaponWheel) && Down(gPrev, layout::kWeaponWheel) && !lbAims && !lbFlies) {
			if (wheel::IsOpen() && !gDpadWheel)
				wheel::Close(true);
			else if (gLBPending)
				tapLB = true;
			gLBPending = false;
		}
		// Its key held opens the wheel too (the weapons' on foot, the radio's in a
		// vehicle) and letting go applies it; a tap does nothing.
		const Action wheelKey = vehicle ? Action::kRadioWheel : Action::kWeaponWheel;
		if (actions::KeyPressed(wheelKey)) {
			gKeyWheelPending = true;
			gKeyWheelAt = Clock::now();
		}
		if (actions::KeyHeld(wheelKey) && gKeyWheelPending && Clock::now() - gKeyWheelAt >= kHoldToOpen) {
			gKeyWheelPending = false;
			if (control && !wheel::IsOpen()) {
				OpenWheel();
				gKeyWheel = wheel::IsOpen();
			}
		}
		if (!actions::KeyHeld(wheelKey)) {
			gKeyWheelPending = false;
			if (gKeyWheel && wheel::IsOpen())
				wheel::Close(true);
			gKeyWheel = false;
		}
		if (wheel::IsOpen()) {
			if (!control)
				wheel::Close(false);
			else
				SteerWheel(pad, now);
		}
	}

	if (gConfig.aimCamera && control && !wheel::IsOpen() && !vehicle)
		StickLook(pad, now, Down(now, layout::kAim), dt);

	// D-pad up answers the phone while it rings.
	const bool answer = control && phone::Ringing() &&
	                    (Pressed(now, layout::kAnswerCall) || actions::KeyPressed(Action::kAnswerCall));
	if (answer)
		phone::Answer();

	// D-pad down: the expanded minimap; up, in a vehicle: its side mission (taxi,
	// vigilante, paramedic, firefighter), which the scripts start on R3.
	if (gConfig.dpadActions && control && !wheel::IsOpen()) {
		if (Pressed(now, layout::kExpandedMap))
			minimap::ToggleExpanded();
		if (Pressed(now, layout::kSideMission) && PedInVehicle(ped) && !answer)
			gSubMissionUntil = Clock::now() + kSubMissionHold;
	}

	if (control && actions::KeyPressed(Action::kExpandedMap)) // its key, whatever DpadActions says
		minimap::ToggleExpanded();

	// D-pad left / right on foot: the next / previous weapon (the game's own
	// cycling, on LT/RT).
	bool cycle = false;
	PadButton cycleButton = kPadRT;
	if (gConfig.dpadActions && control && !vehicle && !wheel::IsOpen() && (Pressed(now, layout::kPrevWeapon) || Pressed(now, layout::kNextWeapon))) {
		cycle = true;
		cycleButton = Pressed(now, layout::kNextWeapon) ? kPadRT : kPadLT;
	}
	if (gConfig.dpadActions)
		RadioDpad(now, control ? vehicle : nullptr);
	gPrev = now;

	orig_Update(pad, index);

	// A climb has the controls (keyboard too: this is after the merge), and the
	// jump that started it isn't a new one once it's done.
	if (climb::Active()) {
		gJumpHeld = true;
		for (PadButton b : {kPadLeftStickX, kPadLeftStickY, kPadCross, kPadCircle, kPadTriangle, kPadLB, kPadRB,
		                    kPadLT, kPadRT})
			PadNew(pad, b) = 0;
	}
	if (gJumpHeld && !PadNew(pad, kPadSquare))
		gJumpHeld = false;
	if (gJumpHeld)
		PadNew(pad, kPadSquare) = 0;

	// One-frame presses, seen as "just down" by the game this frame.
	if (tapLB)
		PadNew(pad, kPadLB) = 255;
	if (cycle)
		PadNew(pad, cycleButton) = 255;
	if (Clock::now() < gSubMissionUntil)
		PadNew(pad, kPadR3) = 255;
	phone::HoldAnswer(pad);
	if (gConfig.bulletTime && control && aimcam::Aiming() && actions::KeyPressed(Action::kBulletTime))
		PadNew(pad, kPadR3) = 255; // as the right stick's click (shootdodge.cpp)
	if (keyAim) // the drive-by's fire key is the game's vehicle fire: the aim shoots, not the game
		PadNew(pad, kPadCircle) = 0;
	// A held A button or key still floors it.
	PadNew(pad, kPadCross) = std::max(PadNew(pad, kPadCross), int16_t(pedals.gas * 255.0f));
	PadNew(pad, kPadSquare) = std::max(PadNew(pad, kPadSquare), int16_t(pedals.brake * 255.0f));
	const bool driving = vehicle && control && !IsAircraft(vehicle);
	rumble::Drive(driving ? vehicle : nullptr, PadNew(pad, kPadCross) / 255.0f, PadNew(pad, kPadSquare) / 255.0f);
	shootdodge::FromPad(pad); // aiming: bullet time and the dive
	crouch::TakeStick(pad); // crouched, he walks by crouch.cpp, not the game
	if (aimcam::Aiming()) // a walk (the game runs past about half a push)
		for (PadButton axis : {kPadLeftStickX, kPadLeftStickY})
			PadNew(pad, axis) = int16_t(PadNew(pad, axis) * aimcam::kWalkStick);
}

} // namespace

bool LookTurn(float &yaw, float &pitch)
{
	yaw = gLookTurn ? gLookYaw : 0.0f;
	pitch = gLookTurn ? gLookPitch : 0.0f;
	return gLookTurn;
}

bindings::PadFamily Family() { return gFamily; }

std::vector<controlsmodel::Joystick> Pads()
{
	std::vector<controlsmodel::Joystick> pads;
	if (!api.glfwJoystickPresent)
		return pads;
	for (int j = 0; j < kMaxJoysticks; j++)
		if (api.glfwJoystickPresent(j)) {
			const char *name = api.glfwGetJoystickName(j);
			pads.push_back({j, name ? name : "", api.glfwJoystickIsGamepad(j) != 0});
		}
	return pads;
}

int GameJoystickId() { return GameJoystick(); }

void UseJoystick(int id) { *(*reinterpret_cast<int8_t *const *>(api.rsGlobal + 0x20) + 0x21) = int8_t(id); }

bool Down(bindings::Pad pad) { return Down(gPrev, pad); }

bool Down(int joystick, bindings::Pad pad)
{
	GlfwGamepadState s;
	if (joystick < 0 || !api.glfwJoystickIsGamepad || !api.glfwJoystickIsGamepad(joystick) ||
	    !api.glfwGetGamepadState(joystick, &s))
		return false;
	return Down(FromState(s), pad);
}

void Triggers(float &lt, float &rt)
{
	lt = Pedal(gPrev.lt);
	rt = Pedal(gPrev.rt);
}

bool Install()
{
	if (!gConfig.wheels && !gConfig.dpadActions && !gConfig.modernTriggers && !gConfig.aimCamera &&
	    !gConfig.driveByAim && !gConfig.phone)
		return true;
	if (!api.glfwGetGamepadState || !api.glfwJoystickIsGamepad || !api.glfwGetJoystickName) {
		Log("controls: GLFW gamepad functions not found; controller changes off");
		return false;
	}
	if (!hooks::Install("_ZN4CPad6UpdateEs", Hook_Update, orig_Update))
		return false;
	gTestWheel = gConfig.wheels && getenv("VCHUD_TEST_WHEEL");
	Log("controls: weapon/radio wheels %s, D-pad actions %s, modern triggers %s", gConfig.wheels ? "on" : "off",
	    gConfig.dpadActions ? "on" : "off", gConfig.modernTriggers ? "on" : "off");
	return true;
}

} // namespace controls
