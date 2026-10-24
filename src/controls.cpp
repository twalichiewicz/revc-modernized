#include "controls.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
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
constexpr float kMouseRange = 60.0f;    // mouse travel (pixels) that counts as a full stick push
constexpr float kTriggerPressed = -0.7f; // GLFW triggers rest at -1 and are fully in at 1

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
// the PS2. Done on the joystick's own state, so keyboard bindings are untouched.
// The triggers are read directly: the game only reports LT while its binding
// (previous weapon) applies, which stops being the case once the player aims,
// so aiming through it would flicker on and off every frame.
void TriggersAimAndFire(uint8_t *pad, const Gamepad &now)
{
	// The buttons CPad::GetTarget / CPad::GetWeapon read in each controller mode.
	const int mode = *api.padAffectedByController ? *reinterpret_cast<const uint16_t *>(pad + 0xec) : 0;
	if (mode == 3) // aims with LB, which opens the wheels here
		return;
	PadJoy(pad, kPadLT) = PadJoy(pad, kPadRT) = 0; // no weapon cycling on the triggers
	if (now.lt > kTriggerPressed)
		PadJoy(pad, kPadRB) = 255;
	if (now.rt > kTriggerPressed)
		PadJoy(pad, mode == 2 ? kPadCross : kPadCircle) = 255;
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
		gLBPending = false;
		gPrev = now;
		orig_Update(pad, index);
		return;
	}
	uint8_t *ped = api.FindPlayerPed();
	const bool control = ped && !PadControlsDisabled(pad);

	if (gConfig.dpadActions)
		for (PadButton b : {kPadDPadUp, kPadDPadDown, kPadDPadLeft, kPadDPadRight})
			PadJoy(pad, b) = 0;

	bool tapLB = false;
	if (gConfig.modernTriggers && control && !PedInVehicle(ped))
		TriggersAimAndFire(pad, now);

	if (gTestWheel && control && !wheel::IsOpen()) {
		gTestWheel = false;
		OpenWheel();
	}
	if (gConfig.wheels) {
		PadJoy(pad, kPadLB) = 0; // LB reaches the game only as a tap, below
		if (Pressed(now, kLB)) {
			gLBPending = true;
			gLBDownAt = Clock::now();
		}
		if (now.buttons[kLB] && gLBPending && Clock::now() - gLBDownAt >= kHoldToOpen) {
			gLBPending = false;
			if (control)
				OpenWheel();
		}
		if (!now.buttons[kLB] && gPrev.buttons[kLB]) {
			if (wheel::IsOpen())
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

	bool cycle = false; // weapon cycling: the game's own, on LT/RT
	PadButton cycleButton = kPadRT;
	if (gConfig.dpadActions && control && !wheel::IsOpen() && (Pressed(now, kLeft) || Pressed(now, kRight))) {
		const int dir = Pressed(now, kRight) ? 1 : -1;
		if (!PedInVehicle(ped)) {
			cycle = true;
			cycleButton = dir > 0 ? kPadRT : kPadLT;
		} else if (uint8_t *vehicle = PedVehicle(ped); vehicle && RadioUsable(vehicle))
			RetuneBy(vehicle, dir, kRetuneFrames);
	}
	gPrev = now;

	orig_Update(pad, index);

	// One-frame presses, seen as "just down" by the game this frame.
	if (tapLB)
		PadNew(pad, kPadLB) = 255;
	if (cycle)
		PadNew(pad, cycleButton) = 255;
}

} // namespace

bool Install()
{
	if (!gConfig.wheels && !gConfig.dpadActions && !gConfig.modernTriggers)
		return true;
	if (!api.glfwGetGamepadState || !api.glfwJoystickIsGamepad || !api.glfwGetJoystickName) {
		Log("controls: GLFW gamepad functions not found; controller changes off");
		return false;
	}
	if (!hooks::Install("_ZN4CPad6UpdateEs", Hook_Update, orig_Update))
		return false;
	gTestWheel = gConfig.wheels && getenv("VCHUD_TEST_WHEEL");
	Log("controls: weapon/radio wheels %s, D-pad actions %s, LT aim / RT fire %s", gConfig.wheels ? "on" : "off",
	    gConfig.dpadActions ? "on" : "off", gConfig.modernTriggers ? "on" : "off");
	return true;
}

} // namespace controls
