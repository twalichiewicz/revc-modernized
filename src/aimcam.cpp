#include "aimcam.h"
#include "body.h"
#include "camera.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "player.h"
#include "fpfeedback.h"
#include "fpfire.h"
#include "controls.h"
#include "reactions.h"

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
// CPed::bIsDucking or bCrouchWhenShooting (bits 36 and 35 of the flags at +0x188).
bool Crouched(const uint8_t *ped) { return (*reinterpret_cast<const uint64_t *>(ped + 0x188) >> 35 & 3) != 0; }
// Both: CPed::SetPointGunAt raises the gun in the crouch's firing pose.
bool CrouchFiring(const uint8_t *ped) { return (*reinterpret_cast<const uint64_t *>(ped + 0x188) >> 35 & 3) == 3; }
bool gAimCrouched; // the aiming pose he's in is the crouch's

// Two-handed guns (no CWeaponInfo flag 0x80, "aims with the arm": the shotguns,
// the MP5, rifles, the M60) are aimed by the torso alone (CPedIK::PointGunInDirection),
// the arms holding the gun as the animation has it: up at the shoulder in the
// standing aim (PED_AIM_GUN), down at the hip in the walk. So his arms in the
// standing aim are learnt, for each such gun, relative to his chest, and laid
// on the walk while he walks aiming. They're learnt the first time he aims
// that gun standing; walking aimed before then, he's never held still for it
// (#20), the walk holding the gun its own way meanwhile.
uint8_t *(*gWeaponInfo)(int32_t type);
void (*gSetAnimTime)(uint8_t *assoc, float time); // CAnimBlendAssociation::SetCurrentTime
constexpr uint32_t kFireAnim = 0xcd;               // ANIM_WEAPON_FIRE (CPed::PointGunAt)
constexpr size_t kAssocTime = 0x30, kInfoLoopStart = 0x38; // its clock; CWeaponInfo's loop start (s)
constexpr int kWeaponTypes = 40;
// The upper chest, clavicles and arms share the aim frame. Native walking
// twists their joint origins: restoring arm rotations alone separates the
// offhand from the gun. The lower torso and legs retain locomotion.
constexpr body::Bone kArms[] = {body::kSpine1,	 body::kClavicleL, body::kUpperArmL, body::kForearmL,
				body::kHandL,	 body::kFingerL,   body::kClavicleR, body::kUpperArmR,
				body::kForearmR, body::kHandR,	   body::kFingerR};
body::Held gArms[kWeaponTypes];
fpfeedback::AimPose gArmsPose;

bool TwoHanded(int32_t type)
{
	const uint8_t *info = type > 0 && type < kWeaponTypes && gWeaponInfo ? gWeaponInfo(type) : nullptr;
	return info && !(*reinterpret_cast<const uint32_t *>(info + 0x5c) & 0x80);
}

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

// Where the crosshair rests: the game's position, or in first person the
// middle (the hip-fire dot and the aimed crosshair in one place, where the
// shots go: camera.cpp).
float BaseX() { return camera::FirstPersonOnFoot() ? 0.5f : gBaseX; }
float BaseY() { return camera::FirstPersonOnFoot() ? 0.5f : gBaseY; }

// Eases the crosshair (and so the aim) from where it rests to the screen centre.
void PlaceCrosshair(float e)
{
	gWrittenX = *api.crosshairMultX = BaseX() + (0.5f - BaseX()) * e;
	gWrittenY = *api.crosshairMultY = BaseY() + (0.5f - BaseY()) * e;
}

// Where the hip-fire dot points: the game's crosshair on its own camera, as the
// game works out a shot (CCamera::Find3rdPersonCamTargetVector).
CVector HipFireDirection(uint8_t *ped, uint8_t *cam)
{
	*api.crosshairMultX = BaseX();
	*api.crosshairMultY = BaseY();
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

// RT only starts his fire animation; native CPed::Attack fires on its fire
// frame. Standing aimed, CPed::PointGunAt parks that animation at the gun's
// loop start, so the shot comes a frame or few after the pull; walking aimed
// he's out of that pose (it would hold him still), and a fresh animation would
// wait the whole draw (~0.5 s). Start it where the standing aim would have
// parked it, still running: native fires it, its cadence and the crouch's own
// pose (CPed::PointGunAt's other animation) untouched (#20).
void StartAimedShot(uint8_t *ped)
{
	if (!gSetAnimTime || !gWeaponInfo || !api.GetAssociation || PedState(ped) != kPedAttack || CrouchFiring(ped))
		return;
	const uint8_t *info = gWeaponInfo(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
	uint8_t *clump = EntityClump(ped);
	uint8_t *fire = info && clump ? api.GetAssociation(clump, kFireAnim) : nullptr;
	if (!fire)
		return;
	const float loop = *reinterpret_cast<const float *>(info + kInfoLoopStart);
	if (std::isfinite(loop) && loop > 0 && *reinterpret_cast<const float *>(fire + kAssocTime) < loop)
		gSetAnimTime(fire, loop);
}

fpfire::Moving MovingOf(uint8_t *ped)
{
	if (PedFlags(ped) & kPedInTheAir)
		return fpfire::Moving::kAir;
	const int32_t move = PedMoveState(ped);
	return move >= 4 ? fpfire::Moving::kRunning : move > 1 ? fpfire::Moving::kWalking : fpfire::Moving::kStill;
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
	// Only the active player-follow view owns first-person pitch. Other
	// CCams may still be evaluated while a script/cutscene controls the view.
	const bool eyes = camera::FirstPersonChosen() && cam == ActiveCam() && api.theCamera[0x63] && !api.theCamera[0x75];
	const float alphaBefore = *reinterpret_cast<float *>(cam + 0xac);
	const float mousePitch = api.newMouseState ? *reinterpret_cast<float *>(api.newMouseState + 12) : 0.0f;
	const float inputPitch = controls::LookFrozen() || !api.mouseAccelV ? 0.0f
	    : mousePitch * 4.0f * *api.mouseAccelV * Fov(cam) / 80.0f;
	orig_Process(cam, target, a, b, c);
	if (eyes) {
		// macVC 1.0.5217 clamps Alpha to 45 degrees at 0x1000b6f88.
		// Input is already mouse+controller merged by controls.cpp. Keep an
		// independent bounded pitch instead of accumulating against that clamp.
		float &alpha = *reinterpret_cast<float *>(cam + 0xac);
		alpha = fpfeedback::Pitch(alphaBefore + inputPitch + 0.17f) - 0.17f;
	}

	uint8_t *ped = api.FindPlayerPed();
	if (gRifleThirdPerson && ped) // read when the target button goes down, in CPlayerPed::ProcessPlayerWeapon
		*gRifleThirdPerson = gConfig.aimCamera && FirstPersonRifle(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));

	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastFrame).count(), 0.1f);
	gLastFrame = now;
	gBlend = Aiming() ? std::min(1.0f, gBlend + dt / kEaseIn) : std::max(0.0f, gBlend - dt / kEaseOut);
	const float e = gBlend * gBlend * (3 - 2 * gBlend); // smoothstep
	SyncCrosshairBase();
	if (eyes) {
		PlaceCrosshair(1.0f);
		Fov(cam) += (kAimFov - Fov(cam)) * e;
		return; // first-person orientation/eye are placed by camera::OnFoot
	}
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
// (CPed::AimGun). Crouched, the game's crouched firing pose holds the gun and
// he turns to aim, as the game has it: the arm-pointing (made for the standing
// pose) would twist the gun off to the left of the crouch's turned torso.
void AfterPlayerWeapon(uint8_t *ped, uint8_t *pad)
{
	if (!gInstalled || ped != api.FindPlayerPed())
		return;
	if (!Aiming()) {
		// Let go: the aim we raised, and an aiming pose the game took up again
		// (a frame late, after a shot) once the button was up. Not mid-shot.
		const int32_t state = PedState(ped);
		gAimCrouched = CrouchFiring(ped);
		if (api.PadGetWeapon(pad) || state == kPedAttack)
			return;
		if (gGunRaised || (state == kPedAimGun && !PadNew(pad, kPadRB)))
			api.ClearPointGunAt(ped);
		gGunRaised = false;
		return;
	}
	const float heading = CameraHeading();
	FaceHeading(ped, heading);
	gGunRaised = true;
	if (api.PadGetWeapon(pad)) { // firing aims the gun already
		StartAimedShot(ped);
		return;
	}
	int32_t state = PedState(ped);
	// Crouched or stood up with the gun raised: it's raised again, in the pose
	// for how he is now (SetPointGunAt picks the crouch's or the standing one;
	// left as it was, the other pose held the gun pointing off to the side).
	if (state == kPedAimGun && CrouchFiring(ped) != gAimCrouched) {
		api.ClearPointGunAt(ped);
		state = PedState(ped);
	}
	gAimCrouched = CrouchFiring(ped);
	if (Crouched(ped)) {
		if (state != kPedAttack && state != kPedFight && state != kPedAimGun)
			api.SetPointGunAt(ped, nullptr);
		return;
	}
	api.SetLookFlag(ped, heading, true, true);
	api.SetAimFlag(ped, heading);
	api.SetLookTimer(ped, 0x7fffffff);
	*reinterpret_cast<float *>(ped + 0x888) = api.Find3rdPersonQuickAimPitch(api.theCamera); // the arm's pitch
	ped[0x8a8] = 1;
	// Walking he's never held still, even to learn a gun's arms (#20): they're
	// learnt the next time he aims standing.
	if (Walking(pad)) {
		if (state == kPedAimGun)
			api.ClearPointGunAt(ped);
		api.SetAimFlag(ped, heading); // ClearPointGunAt lowers it
	} else if (state != kPedAttack && state != kPedFight && state != kPedAimGun) {
		api.SetPointGunAt(ped, nullptr);
	}
}

// The way he aims, as a turn in his own frame (x right, y forward, z up):
// where the camera, and so the crosshair, looks, as a yaw and a pitch.
body::Quat AimFrame(const uint8_t *ped)
{
	const CVector &look = *reinterpret_cast<const CVector *>(api.theCamera + 0x10); // its matrix (no vtable)
	const CVector a{body::Dot(look, *reinterpret_cast<const CVector *>(ped + 0x08)),
	                body::Dot(look, EntityForward(ped)), body::Dot(look, *reinterpret_cast<const CVector *>(ped + 0x28))};
	return body::Z(std::atan2(-a.x, a.y)) * body::X(std::atan2(a.z, std::hypot(a.x, a.y)));
}

void PoseArms(uint8_t *ped)
{
	if (!gInstalled || !gWeaponInfo || !ped || !fpfeedback::CanPose(Health(ped), PedState(ped), true)) {
		gArmsPose = {};
		return;
	}
	const uint32_t now = *api.timeMs;
	const int32_t state = PedState(ped), type = WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)));
	const bool ownsPose = !Crouched(ped) && !camera::FirstPersonOnFoot() && !PedInVehicle(ped) && state != kPedAttack;
	const bool upright = Aiming() && gGunRaised && ownsPose && TwoHanded(type);
	const float dt = std::clamp(*api.timeStep, 0.0f, 5.0f) / 50.0f;
	const auto transition = gArmsPose.Update(type, reinterpret_cast<uintptr_t>(EntityClump(ped)), now, upright,
	                                        state == kPedAimGun, ownsPose, dt);
	// Learn exactly once per gun/outfit. Recapturing our own blended pose
	// every standing frame gradually twisted the next walking aim target.
	if (transition.capture && body::Prepare(ped) &&
	    body::HoldIn(ped, gArms[type], AimFrame(ped), kArms, int(std::size(kArms)))) {
		gArmsPose.Captured(type);
		Log("aimcam: weapon type %d's shouldered arms learnt for walking aimed", type);
	}
	// A stable target through standing/walking changes; exponential easing
	// also lowers it without a frame-rate-dependent pose snap.
	if (transition.weapon >= 0 && transition.weight > 0.0f && body::Prepare(ped)) {
		body::RestoreIn(ped, gArms[transition.weapon], AimFrame(ped), body::Smooth(transition.weight));
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
	// Knocked down (#25): his ragdoll owns him till he's up; no aim, turn or raised gun.
	if (!gConfig.aimCamera || !ped || PedInVehicle(ped) || !*api.useMouse3rdPerson || reactions::PlayerDown())
		return false;
	return (gTestAim || PadNew(api.GetPad(0), kPadRB)) &&
	       ThirdPersonGun(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
}

float Spread()
{
	uint8_t *ped = api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	if (!ped || !Aiming() || camera::FirstPersonOnFoot())
		return 0.0f;
	return fpfire::AimedMoving(fpfire::KindOf(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)))), MovingOf(ped));
}

int SprintField(uint16_t mode, bool controller)
{
	if (!controller || mode < 2 || mode == 3)
		return kPadCross;
	return mode == 2 ? 0x22 : -1;
}

bool Install()
{
	if (!gConfig.aimCamera && !gConfig.firstPerson)
		return true;
	if (!hooks::Install("_ZN4CCam26Process_FollowPedWithMouseERK7CVectorfff", Hook_Process, orig_Process))
		return false;
	gLastFrame = Clock::now();
	gTestAim = getenv("VCHUD_TEST_AIM") != nullptr;
	gInstalled = true;
	if (!player::ControlHooked())
		Log("aimcam: Tommy only raises two-handed guns once he fires, and stands still aiming (re-run install.sh)");
	gFreeCam = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN7CCamera8bFreeCamE"));
	gWeaponInfo = reinterpret_cast<decltype(gWeaponInfo)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType"));
	gSetAnimTime = reinterpret_cast<decltype(gSetAnimTime)>(dlsym(RTLD_MAIN_ONLY, "_ZN21CAnimBlendAssociation14SetCurrentTimeEf"));
	if (!gSetAnimTime)
		Log("aimcam: walking aimed, the first shot waits for the gun's draw");
	if (!gWeaponInfo || !player::PoseHooked())
		Log("aimcam: Tommy holds two-handed guns at the hip while he walks aiming");
	gRifleThirdPerson = hooks::Flag("skip:rifle_first_person");
	if (!gRifleThirdPerson)
		Log("aimcam: the M4, Ruger and M60 keep first-person aiming (re-run install.sh)");
	Log("aimcam: over-the-shoulder aiming on");
	return true;
}

} // namespace aimcam
