#include "driveby.h"
#include "camera.h"
#include "canvas.h"
#include "config.h"
#include "fpcross.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace game;

namespace driveby {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kPistolSlot = 3, kSmgSlot = 5; // the weapons you can use from a vehicle
constexpr float kAimRange = 120.0f;          // how far the crosshair looks for what it's on
// The aiming camera orbits a point this far above the driver's seat (over the
// roof), closer in than the driving camera, so the crosshair isn't on the car.
constexpr float kPivotHeight = 1.25f;
constexpr float kCloser = 0.75f, kMinDistance = 3.0f, kMaxDistance = 10.0f;
constexpr float kMinPitch = -0.6f, kMaxPitch = 0.45f; // radians
constexpr float kAimFov = 60.0f;
constexpr float kEaseIn = 0.2f, kEaseOut = 0.25f, kArmEaseIn = 0.3f; // seconds
constexpr float kMaxArmTurn = 1.4f;                 // radians from the drive-by pose
constexpr int kBoneUpperArmR = 22, kBoneHandR = 24; // ConvertPedNode2BoneTag
constexpr auto kPistolInterval = std::chrono::milliseconds(250);

void (*orig_CarDriveBy)(uint8_t *);
void (*orig_BikeDriveBy)(uint8_t *);
void (*orig_BoatDriveBy)(uint8_t *);
void (*orig_AutoAim)(uint8_t *shooter, uint8_t *vehicle, CVector *source, CVector *target);
void (*orig_FollowCar)(uint8_t *cam, const CVector &target, float, float, float);

// Skip patches over each drive-by's "is it an SMG" check, set while it runs with a pistol.
volatile uint64_t *gAnyWeapon[3];

bool gInstalled;
bool gAiming;                 // aiming with a drive-by weapon in hand
bool gFireHeld, gShotPending; // RB; a pistol shot waiting to go
Clock::time_point gNextPistolShot;
int gRestoreSlot = -1; // the weapon held before a drive-by took out another, back on getting out
float gYaw, gPitch; // where the aim points (world, radians)
float gDistance;    // the aiming camera's distance from its pivot
float gBlend;       // 0 = the vehicle camera, 1 = fully aiming
Clock::time_point gLastFrame, gAimSince;
Im2DCanvas gCanvas;

struct Pose {
	CVector source, front, up;
	float fov;
};
uint8_t *gMovedCam;
Pose gRealPose;

// The arm's keyframe rotation as the animation left it, and as it was turned,
// so a frame the animation didn't redo isn't turned twice.
struct Quat {
	float x, y, z, w;
};
Quat gArmFromAnim, gArmTurned;
bool gArmWritten;
float gArmSign = 1.0f; // RtQuatRotate's handedness, checked on first use
bool gArmChecked, gArmOff;

CVector Sub(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CVector Add(const CVector &a, const CVector &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CVector Scale(const CVector &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Cross(const CVector &a, const CVector &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
CVector Normalised(const CVector &v)
{
	const float l = std::sqrt(Dot(v, v));
	return l > 1e-6f ? Scale(v, 1.0f / l) : v;
}
CVector Lerp(const CVector &a, const CVector &b, float t) { return Add(a, Scale(Sub(b, a), t)); }
float Smooth(float t) { return t * t * (3 - 2 * t); }

CVector &CamFront(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x168); }
CVector &CamSource(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x174); }
CVector &CamUp(uint8_t *cam) { return *reinterpret_cast<CVector *>(cam + 0x18c); }
float &CamFovRef(uint8_t *cam) { return *reinterpret_cast<float *>(cam + 0xb4); }
const CVector &VehicleRight(const uint8_t *vehicle) { return *reinterpret_cast<const CVector *>(vehicle + 0x8); }
const uint8_t *VehicleDriver(const uint8_t *vehicle) { return *reinterpret_cast<uint8_t *const *>(vehicle + 0x218); }

bool HasAmmo(uint8_t *ped, int slot)
{
	const uint8_t *w = PedWeapon(ped, slot);
	return WeaponType(w) != 0 && WeaponAmmoTotal(w) > 0;
}

// The weapon in hand if it's an SMG or a pistol, else the SMG, else the pistol.
int DriveByWeaponSlot(uint8_t *ped)
{
	const int current = PedCurrentWeaponSlot(ped);
	if ((current == kSmgSlot || current == kPistolSlot) && HasAmmo(ped, current))
		return current;
	if (HasAmmo(ped, kSmgSlot))
		return kSmgSlot;
	if (HasAmmo(ped, kPistolSlot))
		return kPistolSlot;
	return -1;
}

CVector Aim()
{
	return {std::cos(gPitch) * std::cos(gYaw), std::cos(gPitch) * std::sin(gYaw), std::sin(gPitch)};
}

// The point the aiming camera orbits: over the vehicle, at the driver's height.
CVector Pivot(const uint8_t *vehicle, const uint8_t *ped)
{
	const CVector &v = EntityPos(vehicle);
	return {v.x, v.y, EntityPos(ped).z + kPivotHeight};
}

// The player's own vehicle, driven by the player, while aiming: everyone else's
// drive-bys (gang cars) stay the game's.
bool AimingFrom(const uint8_t *vehicle)
{
	const uint8_t *ped = api.FindPlayerPed();
	return gAiming && vehicle && ped && PedInVehicle(ped) && PedVehicle(ped) == vehicle && VehicleDriver(vehicle) == ped;
}

// What the middle of the screen is on, looking past the player's own vehicle.
CVector CrosshairPoint(uint8_t *vehicle)
{
	uint8_t *cam = ActiveCam();
	const CVector from = CamSource(cam), dir = CamFront(cam);
	const CVector to = Add(from, Scale(dir, kAimRange));
	alignas(16) uint8_t colPoint[64] = {}; // CColPoint: the point first
	uint8_t *entity = nullptr;
	uint8_t *const ignored = *api.ignoreEntity;
	*api.ignoreEntity = vehicle;
	const bool hit = api.ProcessLineOfSight(from, to, colPoint, entity, true, true, true, true, false, true, false, true);
	*api.ignoreEntity = ignored;
	return hit ? *reinterpret_cast<const CVector *>(colPoint) : to;
}

// The game's drive-by, with the side taken from the aim and the fire button
// from RB. Cars, bikes and boats read the side from the camera's LookingLeft /
// LookingRight (+8 / +9; the pad's L2 / R2 in one camera mode) and fire on
// Circle (CPad::GetCarGunFired); those are set only for this call. They only
// shoot SMGs, unless this one's skip patch is set.
void DriveBy(uint8_t *vehicle, void (*orig)(uint8_t *), int which)
{
	if (!AimingFrom(vehicle)) {
		orig(vehicle);
		return;
	}
	uint8_t *ped = api.FindPlayerPed();
	uint8_t *cam = ActiveCam();
	uint8_t *pad = api.GetPad(0);
	const CVector &r = VehicleRight(vehicle), aim = Aim();
	const bool right = aim.x * r.x + aim.y * r.y > 0.0f;
	const bool pistol = PedCurrentWeaponSlot(ped) == kPistolSlot;
	const bool fire = pistol ? gShotPending && Clock::now() >= gNextPistolShot : gFireHeld;
	const uint8_t *weapon = PedWeapon(ped, PedCurrentWeaponSlot(ped));
	const int32_t ammo = WeaponAmmoTotal(weapon);

	const uint8_t lookLeft = cam[8], lookRight = cam[9];
	const int16_t lt = PadNew(pad, kPadLT), rt = PadNew(pad, kPadRT), circle = PadNew(pad, kPadCircle);
	cam[8] = !right;
	cam[9] = right;
	PadNew(pad, kPadLT) = right ? 0 : 255;
	PadNew(pad, kPadRT) = right ? 255 : 0;
	PadNew(pad, kPadCircle) = fire ? 255 : 0;
	if (gAnyWeapon[which])
		*gAnyWeapon[which] = pistol;
	orig(vehicle);
	if (gAnyWeapon[which])
		*gAnyWeapon[which] = 0;
	cam[8] = lookLeft;
	cam[9] = lookRight;
	PadNew(pad, kPadLT) = lt;
	PadNew(pad, kPadRT) = rt;
	PadNew(pad, kPadCircle) = circle;

	if (pistol && WeaponAmmoTotal(weapon) < ammo) { // one shot per press, at a pistol's pace
		gShotPending = false;
		gNextPistolShot = Clock::now() + kPistolInterval;
	}
}

void Hook_CarDriveBy(uint8_t *vehicle) { DriveBy(vehicle, orig_CarDriveBy, 0); }
void Hook_BikeDriveBy(uint8_t *vehicle) { DriveBy(vehicle, orig_BikeDriveBy, 1); }
void Hook_BoatDriveBy(uint8_t *vehicle) { DriveBy(vehicle, orig_BoatDriveBy, 2); }

// The shot keeps its source (the gun) and range but heads for the crosshair,
// instead of the game's spread and snapping to the nearest target.
void Hook_AutoAim(uint8_t *shooter, uint8_t *vehicle, CVector *source, CVector *target)
{
	if (!AimingFrom(vehicle)) {
		orig_AutoAim(shooter, vehicle, source, target);
		return;
	}
	const CVector shot = Sub(*target, *source);
	const CVector aim = Sub(CrosshairPoint(vehicle), *source);
	const float len = std::sqrt(Dot(aim, aim));
	if (len > 0.01f)
		*target = Add(*source, Scale(aim, std::sqrt(Dot(shot, shot)) / len));
}

// While aiming, the vehicle camera (cars, bikes, boats) is replaced by one
// orbiting a point over the vehicle along the aim, eased in and out. The game's
// own camera carries on underneath, from where it left itself.
void Hook_FollowCar(uint8_t *cam, const CVector &target, float a, float b, float c)
{
	if (cam == gMovedCam) {
		CamSource(cam) = gRealPose.source;
		CamFront(cam) = gRealPose.front;
		CamUp(cam) = gRealPose.up;
		CamFovRef(cam) = gRealPose.fov;
	}
	gMovedCam = nullptr;
	orig_FollowCar(cam, target, a, b, c);

	const Clock::time_point now = Clock::now();
	const float dt = std::min(std::chrono::duration<float>(now - gLastFrame).count(), 0.1f);
	gLastFrame = now;
	gBlend = gAiming ? std::min(1.0f, gBlend + dt / kEaseIn) : std::max(0.0f, gBlend - dt / kEaseOut);
	uint8_t *ped = api.FindPlayerPed();
	if (gBlend <= 0.0f || cam != ActiveCam() || !ped || !PedInVehicle(ped) || !PedVehicle(ped))
		return;
	const float e = Smooth(gBlend);
	const CVector dir = Aim(), pivot = Pivot(PedVehicle(ped), ped);
	CVector source = Sub(pivot, Scale(dir, gDistance));
	for (float k : {0.7f, 0.4f}) { // pulled in if a wall is in the way
		if (api.GetIsLineOfSightClear(pivot, source, true, false, false, true, false, true, false))
			break;
		source = Sub(pivot, Scale(dir, gDistance * k));
	}
	gRealPose = {CamSource(cam), CamFront(cam), CamUp(cam), CamFovRef(cam)};
	gMovedCam = cam;
	const CVector front = Normalised(Lerp(gRealPose.front, dir, e));
	CamFront(cam) = front;
	CamUp(cam) = Normalised({-front.x * front.z, -front.y * front.z, 1.0f - front.z * front.z}); // the world's
	CamSource(cam) = Lerp(gRealPose.source, source, e);
	CamFovRef(cam) += (kAimFov - CamFovRef(cam)) * e;
}

// An RwMatrix from the bone matrices: right, up, at, pos, each padded to 4 floats.
const float *BoneMatrix(const float *matrices, uint8_t *hier, int bone)
{
	const int i = api.HAnimIDGetIndex(hier, bone);
	return i >= 0 ? matrices + i * 16 : nullptr;
}
CVector Row(const float *m, int row) { return {m[row * 4], m[row * 4 + 1], m[row * 4 + 2]}; }
CVector ArmDirection(const float *mats, uint8_t *hier)
{
	return Normalised(Sub(Row(BoneMatrix(mats, hier, kBoneHandR), 3), Row(BoneMatrix(mats, hier, kBoneUpperArmR), 3)));
}

// Turns the right upper arm (it holds the gun in both drive-by poses) so the
// hand points at the crosshair, before CPed::PreRender rebuilds the skeleton
// from the keyframes: the pose is built from the animation alone, the turn
// worked out in the bone's own frame and applied to its keyframe.
void AimArm(uint8_t *ped, uint8_t *vehicle, float weight)
{
	uint8_t *hier = api.GetAnimHierarchyFromSkinClump(*reinterpret_cast<uint8_t **>(ped + 0x58));
	uint8_t *frame = *reinterpret_cast<uint8_t **>(ped + 0x200 + 4 * 8); // m_pFrames[PED_UPPERARMR]
	if (!hier || !frame)
		return;
	Quat &q = *reinterpret_cast<Quat *>(*reinterpret_cast<uint8_t **>(frame + 0x10) + 0x10); // its keyframe's
	if (gArmWritten && !memcmp(&q, &gArmTurned, sizeof q)) // the animation didn't redo it
		q = gArmFromAnim;
	gArmWritten = false;
	api.UpdateRpHAnim(ped);
	const float *mats = api.HAnimMatrixArray(hier);
	const float *upper = BoneMatrix(mats, hier, kBoneUpperArmR);
	if (!upper || !BoneMatrix(mats, hier, kBoneHandR))
		return;
	const CVector shoulder = Row(upper, 3), from = ArmDirection(mats, hier);
	const CVector to = Normalised(Sub(CrosshairPoint(vehicle), shoulder));
	const float cosine = std::clamp(Dot(from, to), -1.0f, 1.0f);
	const float angle = std::min(std::acos(cosine), kMaxArmTurn) * weight;
	const CVector axis = Normalised(Cross(from, to));
	if (angle < 0.01f || Dot(axis, axis) < 0.5f)
		return;
	CVector local = Normalised({Dot(Row(upper, 0), axis), Dot(Row(upper, 1), axis), Dot(Row(upper, 2), axis)});
	gArmFromAnim = q;
	constexpr int kPreConcat = 1; // rwCOMBINEPRECONCAT: about an axis in the bone's own frame
	api.RtQuatRotate(&q, &local, angle * gArmSign * 57.29578f, kPreConcat);
	if (!gArmChecked && angle > 0.2f) { // which way RtQuatRotate turns, checked once
		gArmChecked = true;
		api.UpdateRpHAnim(ped);
		if (Dot(ArmDirection(mats, hier), to) < cosine) {
			gArmSign = -gArmSign;
			q = gArmFromAnim;
			api.RtQuatRotate(&q, &local, angle * gArmSign * 57.29578f, kPreConcat);
			api.UpdateRpHAnim(ped);
			if (Dot(ArmDirection(mats, hier), to) < cosine) {
				q = gArmFromAnim;
				gArmOff = true;
				Log("driveby: couldn't turn the arm towards the aim; left as animated");
				return;
			}
		}
		Log("driveby: arm aiming on%s", gArmSign > 0 ? "" : " (rotation the other way round)");
	}
	gArmTurned = q;
	gArmWritten = true;
}

void Ring(float cx, float cy, float r0, float r1, CRGBA col)
{
	constexpr int kSteps = 40;
	for (int i = 0; i < kSteps; i++) {
		const float a0 = 6.2831853f * i / kSteps, a1 = 6.2831853f * (i + 1) / kSteps;
		const Vtx p0{cx + r0 * cosf(a0), cy + r0 * sinf(a0), 0, 0}, p1{cx + r1 * cosf(a0), cy + r1 * sinf(a0), 0, 0};
		const Vtx p2{cx + r1 * cosf(a1), cy + r1 * sinf(a1), 0, 0}, p3{cx + r0 * cosf(a1), cy + r0 * sinf(a1), 0, 0};
		gCanvas.Tri(p0, p1, p2, col);
		gCanvas.Tri(p0, p2, p3, col);
	}
}

} // namespace

void SetInput(bool aim, bool fire, float yaw, float pitch)
{
	const bool was = gAiming;
	gAiming = false;
	uint8_t *ped = api.FindPlayerPed();
	if (ped && !PedInVehicle(ped) && gRestoreSlot >= 0) {
		// The game itself only puts back what was held before an SMG it took out.
		if (gRestoreSlot == 0 || WeaponType(PedWeapon(ped, gRestoreSlot)) != 0)
			api.SetCurrentWeapon(ped, gRestoreSlot);
		gRestoreSlot = -1;
	}
	if (gInstalled && aim && ped && PedInVehicle(ped) && PedVehicle(ped)) {
		const int slot = DriveByWeaponSlot(ped);
		const int current = PedCurrentWeaponSlot(ped);
		if (slot >= 0 && (!was || current != slot)) {
			if (current != slot && current != kSmgSlot && gRestoreSlot < 0)
				gRestoreSlot = current;
			api.SetCurrentWeapon(ped, slot); // also back in his hand: getting in put it away
		}
		gAiming = slot >= 0 && PedCurrentWeaponSlot(ped) == slot;
	}
	if (!gAiming) {
		gShotPending = gFireHeld = false;
		return;
	}
	if (!was) { // start from where the vehicle camera looks
		uint8_t *cam = ActiveCam();
		const CVector f = CamFront(cam);
		gYaw = std::atan2(f.y, f.x);
		gPitch = std::clamp(std::asin(std::clamp(f.z, -1.0f, 1.0f)), kMinPitch, kMaxPitch);
		const CVector to = Sub(Pivot(PedVehicle(ped), ped), CamSource(cam));
		gDistance = std::clamp(std::sqrt(Dot(to, to)) * kCloser, kMinDistance, kMaxDistance);
		gAimSince = Clock::now();
		const uint8_t *w = PedWeapon(ped, PedCurrentWeaponSlot(ped));
		Log("driveby: aiming with weapon type %d (slot %d, %d rounds)", WeaponType(w), PedCurrentWeaponSlot(ped),
		    WeaponAmmoTotal(w));
	}
	gYaw -= yaw;
	gPitch = std::clamp(gPitch + pitch, kMinPitch, kMaxPitch);
	if (fire && !gFireHeld)
		gShotPending = true;
	gFireHeld = fire;
}

bool gPilotHolstered; // his gun was put away while he flew

void PilotHolster(uint8_t *ped)
{
	if (!api.RemoveWeaponModel || !api.AddWeaponModel)
		return;
	uint8_t *vehicle = PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	void *const model = *reinterpret_cast<void *const *>(ped + 0x290); // CPed::m_pWeaponModel
	if (vehicle && IsAircraft(vehicle) && *reinterpret_cast<uint8_t *const *>(vehicle + 0x218) == ped) { // the pilot
		if (model) {
			api.RemoveWeaponModel(ped, -1);
			gPilotHolstered = true;
		}
		return;
	}
	if (!gPilotHolstered || PedInVehicle(ped))
		return;
	// Out (CPed::ReplaceWeaponWhenExitingVehicle doesn't add the SMG he went in with back).
	gPilotHolstered = false;
	const int32_t type = WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)));
	const uint8_t *info = type > 0 ? api.GetWeaponInfo(type) : nullptr;
	const int32_t id = info ? *reinterpret_cast<const int32_t *>(info + 0x54) : -1; // CWeaponInfo::m_nModelId
	if (!model && id > 0)
		api.AddWeaponModel(ped, id);
}

void PreRender(uint8_t *ped)
{
	if (!gInstalled || gArmOff || ped != api.FindPlayerPed())
		return;
	if (PedInVehicle(ped) && AimingFrom(PedVehicle(ped))) {
		const float since = std::chrono::duration<float>(Clock::now() - gAimSince).count();
		AimArm(ped, PedVehicle(ped), Smooth(std::min(1.0f, since / kArmEaseIn)));
	} else {
		gArmWritten = false;
	}
}

bool Enabled() { return gInstalled; }
CVector AimDirection() { return Aim(); }

bool Aiming() { return gAiming; }

void Draw()
{
	if (!gAiming || (gBlend < 0.5f && !camera::InSeat())) // its camera, or the first-person seat's
		return;
	const float cx = ScreenWidth() / 2.0f, cy = ScreenHeight() / 2.0f, h = float(ScreenHeight());
	// As thick as first person's crosshair (fpcross.h), with its dark edge.
	const float t = std::max(1.0f, std::round(fpcross::Thickness(h))), o = fpcross::Edge(h);
	const float r = h * 0.016f, w = t / 2, dot = t;
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	Ring(cx, cy, r - w - o, r + w + o, {0, 0, 0, 150});
	Ring(cx, cy, r - w, r + w, {255, 255, 255, 230});
	gCanvas.Pill(cx - dot - o, cy - dot - o, 2 * (dot + o), 2 * (dot + o), {0, 0, 0, 150});
	gCanvas.Pill(cx - dot, cy - dot, 2 * dot, 2 * dot, {255, 255, 255, 230});
	gCanvas.Flush();
}

bool Install()
{
	if (!gConfig.driveByAim)
		return true;
	const bool ok =
	    hooks::Install("_ZN11CAutomobile18DoDriveByShootingsEv", Hook_CarDriveBy, orig_CarDriveBy) &&
	    hooks::Install("_ZN5CBike18DoDriveByShootingsEv", Hook_BikeDriveBy, orig_BikeDriveBy) &&
	    hooks::Install("_ZN5CBoat18DoDriveByShootingsEv", Hook_BoatDriveBy, orig_BoatDriveBy) &&
	    hooks::Install("_ZN7CWeapon19DoDriveByAutoAimingEP7CEntityP8CVehicleP7CVectorS5_", Hook_AutoAim,
	                   orig_AutoAim) &&
	    hooks::Install("_ZN4CCam20Process_FollowCar_SAERK7CVectorfff", Hook_FollowCar, orig_FollowCar);
	if (!ok)
		return false;
	gInstalled = true;
	gArmOff = !player::PoseHooked();
	gAnyWeapon[0] = hooks::Flag("skip:driveby_pistol_car");
	gAnyWeapon[1] = hooks::Flag("skip:driveby_pistol_bike");
	gAnyWeapon[2] = hooks::Flag("skip:driveby_pistol_boat");
	gLastFrame = Clock::now();
	Log("driveby: free aim on%s%s", gAnyWeapon[0] ? "" : " (SMGs only: re-run install.sh)",
	    gArmOff ? " (arm aiming off: re-run install.sh)" : "");
	return true;
}

} // namespace driveby
