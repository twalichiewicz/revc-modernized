#include "controls.h"
#include "config.h"
#include "driveby.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "minimap.h"
#include "phone.h"
#include "wheel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace game;

namespace controls {
namespace {

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
constexpr float kStickDeadZone = 0.12f;  // round, on the right stick
constexpr float kLookYaw = 3.5f, kLookPitch = 2.2f; // radians per second at full deflection

struct Gamepad {
	bool buttons[kButtons] = {};
	float rx = 0, ry = 0, lt = -1, rt = -1;
};

void (*orig_Update)(uint8_t *pad, int16_t index);
Gamepad gPrev;
bool gLBPending;            // LB went down and hasn't been a hold yet
Clock::time_point gLBDownAt;
bool gLoggedGamepad;
bool gTestWheel; // VCHUD_TEST_WHEEL: open the wheel once, without LB, to look at it
Vec2 gMouse;     // the mouse's pointer on an open wheel, within kMouseRange
Clock::time_point gLastFrame;
Clock::time_point gSubMissionUntil; // R3 held for the scripts until then
GamepadButton gDpadHeld = kButtons;  // D-pad left / right went down in a vehicle, not yet a hold
Clock::time_point gDpadDownAt;
bool gDpadWheel;                     // the radio wheel is open on a D-pad hold

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
	for (int b = 0; b < kButtons; b++)
		g.buttons[b] = s.buttons[b];
	g.rx = s.axes[kRX];
	g.ry = s.axes[kRY];
	g.lt = s.axes[kLT];
	g.rt = s.axes[kRT];
	return g;
}

bool Pressed(const Gamepad &now, GamepadButton b) { return now.buttons[b] && !gPrev.buttons[b]; }

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
	if (now.lt > kTriggerPressed)
		PadJoy(pad, aim) = 255;
	if (now.rt > kTriggerPressed)
		PadJoy(pad, fire) = 255;
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
// after it (Hook_Update). Aircraft keep their controls (their triggers turn
// the tail).
Pedals TriggersDrive(uint8_t *pad, const Gamepad &now)
{
	if (*api.padAffectedByController && *reinterpret_cast<const uint16_t *>(pad + 0xec) == 3)
		return {};
	PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = 0;
	// Looking back was both triggers (CPad::GetLookBehindForCar); it moves to R3,
	// as in GTA V, whose own job (side missions) is on D-pad up.
	if (gConfig.dpadActions) {
		PadJoy(pad, kPadR3) = 0;
		if (now.buttons[kRThumb])
			PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = 255;
	}
	return {Pedal(now.rt), Pedal(now.lt)};
}

// In a vehicle, D-pad left / right: a tap retunes to the previous / next
// station, a hold opens the radio wheel, which letting go applies.
void RadioDpad(const Gamepad &now, uint8_t *vehicle) // vehicle: null when not in one, or no control
{
	if (gDpadWheel && (!wheel::IsOpen() || (!now.buttons[kLeft] && !now.buttons[kRight]))) {
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

// The right stick as turn rates (radians per second, to the right and up): a
// small round dead zone, a response curve, slower while aiming. The game's own
// stick handling only starts at 30% deflection, with a jump (CapturePad).
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
		gLBPending = gDpadWheel = false;
		gDpadHeld = kButtons;
		driveby::SetInput(false, false, 0, 0);
		gPrev = now;
		orig_Update(pad, index);
		return;
	}
	uint8_t *ped = api.FindPlayerPed();
	const bool control = ped && !PadControlsDisabled(pad);
	const Clock::time_point frame = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(frame - gLastFrame).count(), 0.1f);
	gLastFrame = frame;

	if (gConfig.dpadActions)
		for (PadButton b : {kPadDPadUp, kPadDPadDown, kPadDPadLeft, kPadDPadRight})
			PadJoy(pad, b) = 0;

	uint8_t *vehicle = ped && PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	const bool lbAims = driveby::Enabled() && vehicle && !IsAircraft(vehicle); // else LB is the wheel's

	bool tapLB = false;
	Pedals pedals;
	if (gConfig.modernTriggers && control) {
		if (!vehicle)
			TriggersAimAndFire(pad, now);
		else if (!IsAircraft(vehicle))
			pedals = TriggersDrive(pad, now);
	}

	// In a car, on a bike or a boat: hold LB to aim, the right stick turns the
	// aim, RB shoots (otherwise the handbrake).
	float aimYaw = 0, aimPitch = 0;
	if (StickRates(now, true, aimYaw, aimPitch)) {
		aimYaw *= dt;
		aimPitch *= dt;
	}
	driveby::SetInput(lbAims && control && now.buttons[kLB] && !wheel::IsOpen(), now.buttons[kRB], aimYaw, aimPitch);
	if (lbAims)
		PadJoy(pad, kPadLB) = 0;
	if (driveby::Aiming())
		PadJoy(pad, kPadRB) = PadJoy(pad, kPadRightStickX) = PadJoy(pad, kPadRightStickY) = 0;

	if (gTestWheel && control && !wheel::IsOpen()) {
		gTestWheel = false;
		OpenWheel();
	}
	if (gConfig.wheels) {
		PadJoy(pad, kPadLB) = 0; // LB reaches the game only as a tap, below
		if (lbAims)
			gLBPending = false;
		else if (Pressed(now, kLB)) {
			gLBPending = true;
			gLBDownAt = Clock::now();
		}
		if (now.buttons[kLB] && gLBPending && Clock::now() - gLBDownAt >= kHoldToOpen) {
			gLBPending = false;
			if (control)
				OpenWheel();
		}
		if (!now.buttons[kLB] && gPrev.buttons[kLB] && !lbAims) {
			if (wheel::IsOpen() && !gDpadWheel)
				wheel::Close(true);
			else if (gLBPending)
				tapLB = true;
			gLBPending = false;
		}
		if (wheel::IsOpen()) {
			if (!control)
				wheel::Close(false);
			else
				SteerWheel(pad, now);
		}
	}

	if (gConfig.aimCamera && control && !wheel::IsOpen() && !vehicle)
		StickLook(pad, now, now.lt > kTriggerPressed, dt);

	// D-pad up answers the phone while it rings.
	const bool answer = control && Pressed(now, kUp) && phone::Ringing();
	if (answer)
		phone::Answer();

	// D-pad down: the expanded minimap; up, in a vehicle: its side mission (taxi,
	// vigilante, paramedic, firefighter), which the scripts start on R3.
	if (gConfig.dpadActions && control && !wheel::IsOpen()) {
		if (Pressed(now, kDown))
			minimap::ToggleExpanded();
		if (Pressed(now, kUp) && PedInVehicle(ped) && !answer)
			gSubMissionUntil = Clock::now() + kSubMissionHold;
	}

	// D-pad left / right on foot: the next / previous weapon (the game's own
	// cycling, on LT/RT).
	bool cycle = false;
	PadButton cycleButton = kPadRT;
	if (gConfig.dpadActions && control && !vehicle && !wheel::IsOpen() &&
	    (Pressed(now, kLeft) || Pressed(now, kRight))) {
		cycle = true;
		cycleButton = Pressed(now, kRight) ? kPadRT : kPadLT;
	}
	if (gConfig.dpadActions)
		RadioDpad(now, control ? vehicle : nullptr);
	gPrev = now;

	orig_Update(pad, index);

	// One-frame presses, seen as "just down" by the game this frame.
	if (tapLB)
		PadNew(pad, kPadLB) = 255;
	if (cycle)
		PadNew(pad, cycleButton) = 255;
	if (Clock::now() < gSubMissionUntil)
		PadNew(pad, kPadR3) = 255;
	phone::HoldAnswer(pad);
	// A held A button or key still floors it.
	PadNew(pad, kPadCross) = std::max(PadNew(pad, kPadCross), int16_t(pedals.gas * 255.0f));
	PadNew(pad, kPadSquare) = std::max(PadNew(pad, kPadSquare), int16_t(pedals.brake * 255.0f));
}

} // namespace

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
