#include "aimcam.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

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

void (*orig_Process)(uint8_t *cam, const CVector &target, float, float, float);
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
	gRifleThirdPerson = hooks::Flag("skip:rifle_first_person");
	if (!gRifleThirdPerson)
		Log("aimcam: the M4, Ruger and M60 keep first-person aiming (re-run install.sh)");
	Log("aimcam: over-the-shoulder aiming on");
	return true;
}

} // namespace aimcam
