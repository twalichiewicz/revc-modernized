#include "aimcam.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>

using namespace game;

namespace aimcam {
namespace {

using Clock = std::chrono::steady_clock;

// The shoulder view, relative to the player (metres) and as a field of view.
constexpr float kHeadHeight = 0.55f; // above the ped's position
constexpr float kDistance = 2.0f;    // behind the head
constexpr float kShoulder = 0.6f;    // to the right
constexpr float kRise = 0.18f;
constexpr float kAimFov = 52.0f;     // the follow camera's is 70
constexpr float kEaseIn = 0.15f, kEaseOut = 0.2f; // seconds
constexpr float kWalkFrom = 0.2f;                  // of a full (walking) push: he's walking, not standing

void (*orig_Process)(uint8_t *cam, const CVector &target, float, float, float);
bool gInstalled;
bool gGunRaised;   // we put him in PED_AIM_GUN for aiming
uint8_t *gFreeCam; // CCamera::bFreeCam: 1 for the classic movement, 0 for the PC's mouse-aim movement
volatile uint64_t *gRifleThirdPerson; // skip patch: the M4/Ruger/M60 don't switch to first person
float gBlend;              // 0 = the game's camera, 1 = fully over the shoulder
Clock::time_point gLastFrame;
bool gTestAim; // VCHUD_TEST_AIM: aim whenever a gun is out, to look at the camera
float gBaseX, gBaseY;      // the game's crosshair position (set in CCamera::Init)
float gWrittenX = -1, gWrittenY = -1;

// Where the camera really was before we moved it over the shoulder.
struct Pose {
	CVector source, front, up;
};
uint8_t *gMovedCam;
Pose gRealPose;

CVector &Front(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x168); }
CVector &Source(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x174); }
CVector &Up(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x18c); }
float &Fov(uint8_t *cam) { return *reinterpret_cast<float *>(cam + 0xb4); }

// The weapons CHud::Draw gives the third-person crosshair: pistols to the
// Ruger, the flamethrower, M60 and minigun.
bool ThirdPersonGun(int32_t type) { return (type >= 17 && type <= 27) || (type >= 31 && type <= 33); }
// Of those, the ones the game aims in first person (MODE_M16_1STPERSON) unless patched.
bool FirstPersonRifle(int32_t type) { return type == 26 || type == 27 || type == 32; }

CVector Lerp(const CVector &a, const CVector &b, float t)
{
	return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Normalised(const CVector &v)
{
	const float l = std::sqrt(Dot(v, v));
	return l > 1e-6f ? CVector{v.x / l, v.y / l, v.z / l} : v;
}

// Behind the right shoulder, pulled in if a wall is in the way.
bool ShoulderSource(uint8_t *ped, const CVector &front, CVector &out)
{
	const CVector &pos = EntityPos(ped);
	const CVector head{pos.x, pos.y, pos.z + kHeadHeight};
	const float flat = std::hypot(front.x, front.y);
	if (flat < 0.01f)
		return false;
	const float rx = front.y / flat, ry = -front.x / flat; // the camera's right, level
	for (float k : {1.0f, 0.6f, 0.3f}) {
		out = {head.x - front.x * kDistance * k + rx * kShoulder * k, head.y - front.y * kDistance * k + ry * kShoulder * k,
		       head.z - front.z * kDistance * k + kRise * k};
		if (api.GetIsLineOfSightClear(head, out, true, true, false, true, false, true, false))
			return true;
	}
	return false;
}

// The crosshair position the game set (adopted again whenever it changes it).
void SyncCrosshairBase()
{
	if (*api.crosshairMultX != gWrittenX || *api.crosshairMultY != gWrittenY) {
		gBaseX = *api.crosshairMultX;
		gBaseY = *api.crosshairMultY;
	}
}

// Eases the crosshair (and so the aim) from the game's position to the screen centre.
void PlaceCrosshair(float e)
{
	gWrittenX = *api.crosshairMultX = gBaseX + (0.5f - gBaseX) * e;
	gWrittenY = *api.crosshairMultY = gBaseY + (0.5f - gBaseY) * e;
}

// Where the hip-fire dot points: the game's crosshair on its own camera, as the
// game works out a shot (CCamera::Find3rdPersonCamTargetVector).
CVector HipFireDirection(uint8_t *ped, uint8_t *cam)
{
	*api.crosshairMultX = gBaseX;
	*api.crosshairMultY = gBaseY;
	CVector source, target;
	api.Find3rdPersonCamTargetVector(api.theCamera, 50.0f, EntityPos(ped), source, target);
	const CVector dir = Normalised({target.x - source.x, target.y - source.y, target.z - source.z});
	return Dot(dir, dir) > 0.5f ? dir : Front(cam);
}

// CPlayerPed::ProcessPlayerWeapon raises the gun on the target button only for
// one-handed weapons (weapon flag 0x80), the rest only while firing. Holding
// LT raises any gun, the way a mouse-camera shot does, and lowers it again.
// He's walking while aiming: the left stick past a nudge (the pad hook has
// already scaled it down to a walk).
bool Walking(const uint8_t *pad)
{
	const float x = *reinterpret_cast<const int16_t *>(pad + kPadLeftStickX);
	const float y = *reinterpret_cast<const int16_t *>(pad + kPadLeftStickY);
	return std::hypot(x, y) > 128.0f * kWalkStick * kWalkFrom;
}

// The run-around control runs after ProcessPlayerWeapon and turns him to
// TheCamera.Orientation, which with the pad's camera is the aim mirrored (he
// fired backwards): so he's turned back to the aim once it's done. It also
// walks him along his facing, its own strafing bending that by 50 degrees at
// most (CPed::CalculateNewVelocity): so the step the walk animation took this
// frame is pointed along the stick instead, camera-relative.
void FaceAndStrafe(uint8_t *ped)
{
	const float h = CameraHeading();
	FaceHeading(ped, h);
	const uint8_t *pad = api.GetPad(0);
	if (!Walking(pad))
		return;
	CVector &v = *reinterpret_cast<CVector *>(ped + 0x84);
	const float speed = std::hypot(v.x, v.y);
	const float x = *reinterpret_cast<const int16_t *>(pad + kPadLeftStickX);
	const float y = -*reinterpret_cast<const int16_t *>(pad + kPadLeftStickY); // up is forward
	const float fx = -sinf(h), fy = cosf(h), rx = cosf(h), ry = sinf(h);  // forward, right
	const float mx = rx * x + fx * y, my = ry * x + fy * y, len = std::hypot(mx, my);
	if (speed < 1e-4f || len < 1e-3f)
		return;
	v.x = mx / len * speed;
	v.y = my / len * speed;
}

void Hook_Process(uint8_t *cam, const CVector &target, float a, float b, float c)
{
	if (cam == gMovedCam) { // the game carries on from where it left its camera, not from ours
		Source(cam) = gRealPose.source;
		Front(cam) = gRealPose.front;
		Up(cam) = gRealPose.up;
	}
	gMovedCam = nullptr;
	orig_Process(cam, target, a, b, c);

	uint8_t *ped = api.FindPlayerPed();
	if (gRifleThirdPerson && ped) // read when the target button goes down, in CPlayerPed::ProcessPlayerWeapon
		*gRifleThirdPerson = FirstPersonRifle(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));

	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastFrame).count(), 0.1f);
	gLastFrame = now;
	gBlend = Aiming() ? std::min(1.0f, gBlend + dt / kEaseIn) : std::max(0.0f, gBlend - dt / kEaseOut);
	const float e = gBlend * gBlend * (3 - 2 * gBlend); // smoothstep
	SyncCrosshairBase();
	if (e <= 0.0f || !ped) {
		PlaceCrosshair(0.0f);
		return;
	}
	// Turn so that the screen centre, where the aiming crosshair sits, points
	// where the hip-fire dot did: aiming in doesn't move the aim.
	const CVector aim = HipFireDirection(ped, cam);
	PlaceCrosshair(e);
	gMovedCam = cam;
	gRealPose = {Source(cam), Front(cam), Up(cam)};
	const CVector front = Normalised(Lerp(gRealPose.front, aim, e));
	const CVector &up = gRealPose.up;
	const float lift = Dot(up, front); // keep up perpendicular to the new front
	Front(cam) = front;
	Up(cam) = Normalised({up.x - front.x * lift, up.y - front.y * lift, up.z - front.z * lift});
	CVector shoulder;
	if (ShoulderSource(ped, front, shoulder))
		Source(cam) = Lerp(gRealPose.source, shoulder, e);
	Fov(cam) += (kAimFov - Fov(cam)) * e;
}

} // namespace

// CPlayerPed::ProcessPlayerWeapon raises the gun on the target button only for
// one-handed weapons (weapon flag 0x80), the rest only while firing. Holding
// LT raises any gun the way a mouse-camera shot does: standing, in PED_AIM_GUN
// (the full aiming pose); walking, out of it, since that state's full-body
// pose holds the walk on the spot, with the aim flag alone pointing his arms
// (CPed::AimGun).
void AfterPlayerWeapon(uint8_t *ped, uint8_t *pad)
{
	if (!gInstalled || ped != api.FindPlayerPed())
		return;
	if (!Aiming()) {
		if (gGunRaised && PedState(ped) == kPedAimGun && !api.PadGetWeapon(pad))
			api.ClearPointGunAt(ped);
		gGunRaised = false;
		return;
	}
	const float heading = CameraHeading();
	FaceHeading(ped, heading);
	gGunRaised = true;
	if (api.PadGetWeapon(pad)) // firing aims the gun already
		return;
	api.SetLookFlag(ped, heading, true, true);
	api.SetAimFlag(ped, heading);
	api.SetLookTimer(ped, 0x7fffffff);
	*reinterpret_cast<float *>(ped + 0x888) = api.Find3rdPersonQuickAimPitch(api.theCamera); // the arm's pitch
	ped[0x8a8] = 1;
	const int32_t state = PedState(ped);
	if (Walking(pad)) {
		if (state == kPedAimGun)
			api.ClearPointGunAt(ped);
		api.SetAimFlag(ped, heading); // ClearPointGunAt lowers it
	} else if (state != kPedAttack && state != kPedFight && state != kPedAimGun) {
		api.SetPointGunAt(ped, nullptr);
	}
}

void Lower(uint8_t *ped)
{
	if (gGunRaised && PedState(ped) == kPedAimGun)
		api.ClearPointGunAt(ped);
	gGunRaised = false;
}

// With the gun up, the classic movement (CPlayerPed::PlayerControlZelda)
// turns him to where he walks; the PC's mouse-aim movement
// (PlayerControl1stPersonRunAround) walks facing the camera, with the walk
// animation. bFreeCam picks between them, and only the camera reads it
// otherwise, later on.
bool BeforeControl()
{
	const bool strafe = gInstalled && gGunRaised && gFreeCam && *gFreeCam;
	if (strafe)
		*gFreeCam = 0;
	return strafe;
}

void AfterControl(uint8_t *ped, bool strafed)
{
	if (strafed)
		*gFreeCam = 1;
	if (gInstalled && gGunRaised && ped == api.FindPlayerPed() && !PedInVehicle(ped) && api.IsPedInControl(ped))
		FaceAndStrafe(ped);
}

bool Aiming()
{
	uint8_t *ped = api.FindPlayerPed();
	if (!ped || PedInVehicle(ped) || !*api.useMouse3rdPerson)
		return false;
	return (gTestAim || PadNew(api.GetPad(0), kPadRB)) &&
	       ThirdPersonGun(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
}

bool Install()
{
	if (!gConfig.aimCamera)
		return true;
	if (!hooks::Install("_ZN4CCam26Process_FollowPedWithMouseERK7CVectorfff", Hook_Process, orig_Process))
		return false;
	gLastFrame = Clock::now();
	gTestAim = getenv("VCHUD_TEST_AIM") != nullptr;
	gInstalled = true;
	if (!player::ControlHooked())
		Log("aimcam: Tommy only raises two-handed guns once he fires, and stands still aiming (re-run install.sh)");
	gFreeCam = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN7CCamera8bFreeCamE"));
	gRifleThirdPerson = hooks::Flag("skip:rifle_first_person");
	if (!gRifleThirdPerson)
		Log("aimcam: the M4, Ruger and M60 keep first-person aiming (re-run install.sh)");
	Log("aimcam: over-the-shoulder aiming on");
	return true;
}

} // namespace aimcam
