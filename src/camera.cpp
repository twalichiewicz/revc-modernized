#include "camera.h"
#include "aimcam.h"
#include "climb.h"
#include "config.h"
#include "controls.h"
#include "driveby.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "phone.h"
#include "swim.h"
#include "traffic.h"
#include "viewmodel.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace camera {
namespace {

// CCamera (TheCamera): its matrix (right, forward, up, position) is at +0.
constexpr size_t kFirstPersonUsed = 0x57; // m_bFirstPersonBeingUsed: the game's own (scopes, rockets)
constexpr size_t kLookingAtPlayer = 0x63; // m_bLookingAtPlayer: not a scripted camera
constexpr size_t kWideScreen = 0x75;      // m_WideScreenOn: a cutscene
constexpr size_t kPedZoom = 0x16c;        // PedZoomIndicator: 1 near .. 3 far, on foot
constexpr size_t kRwCamera = 0x858;       // m_pRwCamera
constexpr size_t kCarZoom = 0xe4;         // CarZoomIndicator: 0 the bumper view (first person), 1..3, ...
constexpr int32_t kZoomNear = 1, kZoomFar = 3;
// CCam::Mode (ActiveCam() + 0xc): on foot, and the vehicle's bumper view
// (MODE_1STPERSON), which first person replaces with his eyes.
constexpr uint16_t kModeFollowPed = 4, kModeFightCam = 36, kModeBumper = 16;
constexpr size_t kPedIK = 0x2c0;          // CPed::m_pedIK
constexpr uint32_t kPedHead = 2;          // PED_HEAD (CPedIK::GetComponentPosition)
constexpr int kBoneHead = 5;              // the head's bone tag (its skinning matrix)
constexpr int kBoneNeck = 4, kBoneClavicles[2] = {21, 31};
constexpr size_t kRwObject = 0x58;        // CEntity::m_rwObject (the clump)
// CPlayerPed's copy of the camera he aimed with (source, front, up), and
// CPedIK's flags (the gun pointed: 1).
constexpr size_t kAimedSource = 0x8ac, kAimedFront = 0x8b8, kAimedUp = 0x8c4, kPedIKFlags = 0x2e8;
constexpr uint8_t kGunPointed = 1;
// CCam: front, source, up (as driveby.cpp has them).
constexpr size_t kCamFront = 0x168, kCamSource = 0x174, kCamUp = 0x18c;

// First person.
constexpr float kNearClipFoot = 0.15f, kNearClipVehicle = 0.08f; // m (the game's: 0.9)
constexpr float kEyeUp = 0.08f, kEyeAhead = 0.06f; // m from the head bone
constexpr float kLevel = 0.17f;                 // rad: the follow camera looks down at him; level it
// In the seat: the right stick looks about, as it does on foot; let go a
// moment, the view eases back to the road.
constexpr float kLookYaw = 2.6f, kLookUp = 0.6f, kLookDown = 0.7f; // rad, at most
constexpr float kLookBackAfter = 0.3f, kLookBackRate = 4.0f;   // s, per second
// Looking behind (R3) from the seat: the eye moves to the middle of the car,
// ahead of the headrests, to look out of the back window.
constexpr float kBehindAhead = 0.35f, kBehindUp = 0.05f; // m
// On a bike: the eye back a little, following his head sooner (his lean
// forward and back, from the left stick, shows), the view tipping with it.
constexpr int32_t kVehicleBike = 5;          // CVehicle::m_vehType (+0x348)
constexpr size_t kRiderLean = 0x55c;         // CBike::m_fPedLeanAmountUD: back < 0 < forward
constexpr float kBikeBack = 0.12f, kSettleOnBike = 0.08f, kLeanTip = 0.15f; // m, s, rad
// Getting in and out (CPed states: opening the door, pulling someone out,
// getting in, getting out): the eye stays in his head, the view turning
// towards where he'll look next.
constexpr int32_t kStateOpenDoor = 0x35, kStateCarJack = 0x38, kStateEnterCar = 0x3a, kStateExitCar = 0x3c;
constexpr float kTurnRate = 3.0f;               // per second
constexpr float kAimRate = 6.0f;                // per second, easing in and out of aiming
// Looking far enough round while he stands, he turns to face that way
// (walking, he already goes where you look; aiming, the game turns him);
// sooner with something in his hand, his arms holding it to the view.
constexpr int32_t kStateIdle = 1, kMoveStill = 1; // CPed::m_nPedState, m_nMoveState
constexpr size_t kWeaponModel = 0x290;            // CPed::m_pWeaponModel
constexpr float kBodyTurnAt = 1.1f, kBodyTurnAtArmed = 0.5f, kBodyFaced = 0.05f; // rad, the view from his body
constexpr float kBodyTurnSettle = 0.12f, kBodyTurnMost = 7.0f;                    // s; rad per second
// Steadied as first-person games are: the eye follows his body, and his
// head about it only slowly (the animations' bob and lean go, crouching or
// climbing stays); his height eases over steps; a gentle bob of its own.
constexpr float kSettle = 0.28f, kSettleInVehicle = 0.2f, kStepSettle = 0.06f; // s
constexpr float kBobHeight = 0.018f, kBobSide = 0.012f; // m, at a run
constexpr float kStepsPerSecond = 1.4f, kStepsPerSpeed = 0.45f; // per m/s

// Shake, in degrees and hertz.
constexpr float kFootDrift = 0.12f, kFootDriftPerSpeed = 0.05f; // per m/s
constexpr float kFootRate = 0.3f, kFootRatePerSpeed = 0.07f;
constexpr float kSway = 0.0025f, kSwayPerSpeed = 0.000125f, kSwayRate = 0.5f;
constexpr float kHum = 0.0005f, kHumPerSpeed = 0.000035f, kHumRate = 11.0f;
constexpr float kJolt = 0.03f, kJoltRate = 13.0f, kJoltFade = 0.3f; // s
constexpr float kAimingShake = 0.25f, kFirstPersonShake = 0.3f;

void (*orig_Process)(uint8_t *camera);
void (*gUpdateRW)(void *matrix);           // CMatrix::UpdateRW
void (*gUpdateRwFrame)(uint8_t *entity);   // CEntity::UpdateRwFrame
void (*gUpdateRpHAnim)(uint8_t *entity);   // CEntity::UpdateRpHAnim
void (*gComponentPosition)(void *ik, CVector *pos, uint32_t node); // CPedIK::GetComponentPosition
void *(*gCameraFrame)(const void *camera);
float *(*gFrameMatrix)(void *frame);
void (*gMatrixUpdate)(float *matrix);
void (*gFrameUpdateObjects)(void *frame);
void (*gFrameOrthoNormalize)(void *frame);
void (*gSetNearClip)(void *camera, float);
void (*gDerivedValues)(uint8_t *camera);   // CCamera::CalculateDerivedValues: the frustum
const uint8_t *gScene;
float *gNearClipZ;                         // CDraw::ms_fNearClipZ
void *(*gHierarchy)(void *clump);          // GetAnimHierarchyFromSkinClump
int (*gBoneIndex)(void *hierarchy, int tag); // RpHAnimIDGetIndex
float *(*gBoneMatrices)(void *hierarchy);  // RpHAnimHierarchyGetMatrixArray: 16 floats each

bool gFirstPerson;   // chosen with the Select button
int32_t gLastZoom = -1;
float gAim;          // 0..1, eased
float gPhaseFoot, gPhaseSway, gPhaseHum, gPhaseJolt;
float gJolt;
CVector gLastVelocity;
bool gHaveVelocity;
// First person, steadied.
CVector gHeadLocal;         // his head about his body or vehicle
const uint8_t *gHeadAbout;  // which
bool gHeadReady, gHeightReady;
float gHeight, gBobPhase, gBobAmount;
bool gHideHead;             // this frame (after CPed::PreRender)
bool gOnFoot;               // first person on foot, this frame
float gLookYaw, gLookPitch; // the right stick's look in the seat, eased
CVector gViewForward{0.0f, 1.0f, 0.0f}; // last frame's, in first person
uint8_t *gCockpit;          // the vehicle he's driving in first person, drawn about him
bool gWasInVehicle;
bool gBodyTurning;
int32_t gCarZoomSeen;
int32_t (*orig_SetupEntityVisibility)(uint8_t *entity);
bool (*orig_Fire)(uint8_t *weapon, uint8_t *shooter, CVector *source); // CWeapon::Fire
uint8_t *gFreeCam;                         // CCamera::bFreeCam
float gLookIdle;                           // s since the right stick last looked about the seat

CVector &Row(size_t offset) { return *reinterpret_cast<CVector *>(api.theCamera + offset); }
CVector Add(const CVector &a, const CVector &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
CVector Sub(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
CVector Mul(const CVector &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Cross(const CVector &a, const CVector &b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
CVector Normal(const CVector &a)
{
	const float l = std::sqrt(Dot(a, a));
	return l > 1e-6f ? Mul(a, 1.0f / l) : a;
}
// v turned by `angle` (rad) about the unit `axis`.
CVector Turn(const CVector &v, const CVector &axis, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	return Add(Add(Mul(v, c), Mul(Cross(axis, v), s)), Mul(axis, Dot(axis, v) * (1.0f - c)));
}

// The camera turned by small angles (rad): yaw about its up, pitch about its
// side, roll about its forward. Its "right" is up x forward, as the game has it.
void TurnCamera(float yaw, float pitch, float roll)
{
	CVector front = Row(0x10), up = Row(0x20);
	const CVector side = Normal(Cross(front, up));
	front = Turn(front, up, yaw);
	front = Turn(front, side, pitch);
	up = Turn(up, side, pitch);
	up = Turn(up, front, roll);
	front = Normal(front);
	up = Normal(Sub(up, Mul(front, Dot(up, front))));
	Row(0x10) = front;
	Row(0x20) = up;
	Row(0x00) = Cross(up, front);
}

// Hands the matrix to RenderWare as CCamera::Process does, and the frustum.
void ToRenderWare()
{
	void *rw = *reinterpret_cast<void *const *>(api.theCamera + kRwCamera);
	void *frame = rw ? gCameraFrame(rw) : nullptr;
	if (!frame)
		return;
	float *m = gFrameMatrix(frame); // right, up, at, position (16 bytes each)
	const CVector rows[4] = {Row(0x00), Row(0x20), Row(0x10), Row(0x30)};
	for (int r = 0; r < 4; r++) {
		m[r * 4 + 0] = rows[r].x;
		m[r * 4 + 1] = rows[r].y;
		m[r * 4 + 2] = rows[r].z;
	}
	gMatrixUpdate(m);
	gFrameUpdateObjects(frame);
	gFrameOrthoNormalize(frame);
	gDerivedValues(api.theCamera);
}

// Smooth noise, -1..1, in octaves.
float Noise(float t, uint32_t seed)
{
	auto at = [seed](int32_t i) {
		uint32_t h = uint32_t(i) * 0x9E3779B1u ^ seed * 0x85EBCA77u;
		h ^= h >> 15;
		h *= 0x2C1B3C6Du;
		h ^= h >> 12;
		return float(h & 0xffff) / 32767.5f - 1.0f;
	};
	const float i = std::floor(t), f = t - i;
	const float s = f * f * (3.0f - 2.0f * f);
	return at(int32_t(i)) + (at(int32_t(i) + 1) - at(int32_t(i))) * s;
}
float Wobble(float t, uint32_t seed)
{
	return Noise(t, seed) * 0.6f + Noise(t * 2.31f, seed + 7) * 0.3f + Noise(t * 5.17f, seed + 13) * 0.1f;
}

bool OurCamera()
{
	const uint8_t *cam = api.theCamera;
	if (cam[kWideScreen] || !cam[kLookingAtPlayer] || cam[kFirstPersonUsed])
		return false;
	const uint16_t mode = *reinterpret_cast<const uint16_t *>(ActiveCam() + 0xc);
	return mode == kModeFollowPed || mode == kModeFightCam;
}

// The Select button's cycle on foot: near, mid, far, first person, near...
// The game moves the zoom; going from far to near is first person's turn.
void Cycle(uint8_t *ped)
{
	int32_t &zoom = *reinterpret_cast<int32_t *>(api.theCamera + kPedZoom);
	if (ped && !PedInVehicle(ped) && OurCamera() && gLastZoom >= 0 && zoom != gLastZoom) {
		if (gFirstPerson) {
			gFirstPerson = false; // to wherever the game went
		} else if (gLastZoom == kZoomFar && zoom == kZoomNear) {
			gFirstPerson = true;
			zoom = kZoomFar; // so the next press goes on to near
		}
	}
	gLastZoom = zoom;
}

// An entity's axes (CPlaceable's matrix, at +8): right, forward, up.
const CVector &Axis(const uint8_t *entity, size_t row) { return *reinterpret_cast<const CVector *>(entity + 8 + row); }
CVector ToLocal(const uint8_t *e, const CVector &v) { return {Dot(v, Axis(e, 0)), Dot(v, Axis(e, 0x10)), Dot(v, Axis(e, 0x20))}; }
CVector ToWorld(const uint8_t *e, const CVector &v)
{
	return Add(Add(Mul(Axis(e, 0), v.x), Mul(Axis(e, 0x10), v.y)), Mul(Axis(e, 0x20), v.z));
}
float Ease(float dt, float settle) { return 1.0f - std::exp(-dt / settle); }

// His head now (the game's own first-person cameras update him first too).
CVector Head(uint8_t *ped)
{
	gUpdateRW(ped + 0x8);
	gUpdateRwFrame(ped);
	gUpdateRpHAnim(ped);
	CVector head{0.0f, 0.0f, 0.0f};
	gComponentPosition(ped + kPedIK, &head, kPedHead);
	return head;
}

// His head about `about` (his body, or the vehicle he's in), eased.
CVector SteadyHead(uint8_t *ped, const uint8_t *about, float dt, float settle)
{
	const CVector local = ToLocal(about, Sub(Head(ped), EntityPos(about)));
	if (!gHeadReady || gHeadAbout != about)
		gHeadLocal = local;
	else
		gHeadLocal = Add(gHeadLocal, Mul(Sub(local, gHeadLocal), Ease(dt, settle)));
	gHeadReady = true;
	gHeadAbout = about;
	return gHeadLocal;
}

void TurnBody(uint8_t *ped, const CVector &flat, float dt)
{
	const bool standing = PedState(ped) == kStateIdle && PedMoveState(ped) <= kMoveStill && gAim < 0.05f &&
	                      !swim::Swimming() && !climb::Active();
	const CVector &facing = Axis(ped, 0x10);
	const float pi = 3.14159265f, heading = HeadingOf(facing.x, facing.y);
	const float off = std::remainder(HeadingOf(flat.x, flat.y) - heading, 2.0f * pi);
	const bool armed = *reinterpret_cast<void *const *>(ped + kWeaponModel) != nullptr;
	if (!standing || std::fabs(off) < kBodyFaced) {
		gBodyTurning = false;
		return;
	}
	gBodyTurning |= std::fabs(off) > (armed ? kBodyTurnAtArmed : kBodyTurnAt);
	if (gBodyTurning) {
		const float step = std::clamp(off * Ease(dt, kBodyTurnSettle), -kBodyTurnMost * dt, kBodyTurnMost * dt);
		FaceHeading(ped, std::remainder(heading + step, 2.0f * pi));
	}
}

void NearClip(float near)
{
	if (void *rw = *reinterpret_cast<void *const *>(gScene + 8)) // Scene.camera
		gSetNearClip(rw, near);
	if (gNearClipZ)
		*gNearClipZ = near;
}

void OnFoot(uint8_t *ped, float dt)
{
	// Level the view, less as he aims (then it's the line exactly).
	TurnCamera(0.0f, kLevel * (1.0f - gAim), 0.0f);
	const CVector front = Row(0x10);
	const CVector flat = Normal(CVector{front.x, front.y, 0.0f}), side = Normal(Cross(flat, CVector{0.0f, 0.0f, 1.0f}));
	TurnBody(ped, flat, dt); // before his head is found: it turns with him
	CVector body = EntityPos(ped);
	gHeight = gHeightReady ? gHeight + (body.z - gHeight) * Ease(dt, kStepSettle) : body.z;
	gHeightReady = true;
	body.z = gHeight;
	// A step's bob, for how fast he's going.
	const CVector v = EntitySpeed(ped);
	const float speed = std::sqrt(v.x * v.x + v.y * v.y) * 50.0f; // m/s
	gBobAmount += (std::min(speed / 6.0f, 1.0f) - gBobAmount) * Ease(dt, 0.25f);
	gBobPhase = std::fmod(gBobPhase + (kStepsPerSecond + kStepsPerSpeed * speed) * dt, 2.0f);
	const float pi = 3.14159265f;
	const float bob = kBobHeight * gBobAmount * std::sin(gBobPhase * 2.0f * pi);
	const float sway = kBobSide * gBobAmount * std::sin(gBobPhase * pi);
	CVector eye = Add(body, ToWorld(ped, SteadyHead(ped, ped, dt, kSettle)));
	eye = Add(eye, Add(CVector{0.0f, 0.0f, kEyeUp + bob}, Add(Mul(flat, kEyeAhead), Mul(side, sway))));
	Row(0x30) = eye;
	NearClip(kNearClipFoot);
}

// The bumper view's direction (it looks left, right and behind with the
// buttons), from his eyes in the seat.
void InVehicle(uint8_t *ped, uint8_t *vehicle, float dt)
{
	const bool bike = *reinterpret_cast<const int32_t *>(vehicle + 0x348) == kVehicleBike;
	const CVector head = SteadyHead(ped, vehicle, dt, bike ? kSettleOnBike : kSettleInVehicle);
	CVector eye = Add(Add(EntityPos(vehicle), ToWorld(vehicle, head)), Mul(Axis(vehicle, 0x20), kEyeUp * 0.6f));
	if (bike)
		eye = Sub(eye, Mul(Axis(vehicle, 0x10), kBikeBack));
	if (ActiveCam()[7]) // CCam::LookingBehind
		eye = Add(Add(EntityPos(vehicle), ToWorld(vehicle, CVector{0.0f, head.y + kBehindAhead, head.z})),
		          Mul(Axis(vehicle, 0x20), kEyeUp + kBehindUp));
	Row(0x30) = eye;
	if (bike) {
		const float lean = std::clamp(*reinterpret_cast<const float *>(vehicle + kRiderLean), -1.0f, 1.0f);
		TurnCamera(0.0f, -lean * kLeanTip, 0.0f);
	}
	if (driveby::Aiming()) {
		// A drive-by: the view goes where it aims (the right stick turns it there).
		const CVector front = driveby::AimDirection();
		const CVector up = Normal(Sub(CVector{0.0f, 0.0f, 1.0f}, Mul(front, front.z)));
		Row(0x10) = front;
		Row(0x20) = up;
		Row(0x00) = Cross(up, front);
		gLookYaw = gLookPitch = 0.0f;
	} else {
		// The right stick looks about the car from his seat.
		float yaw, pitch;
		if (controls::LookTurn(yaw, pitch)) {
			gLookYaw = std::clamp(gLookYaw - yaw, -kLookYaw, kLookYaw);
			gLookPitch = std::clamp(gLookPitch + pitch, -kLookDown, kLookUp);
			gLookIdle = 0.0f;
		} else if ((gLookIdle += dt) > kLookBackAfter) {
			gLookYaw -= gLookYaw * Ease(dt, 1.0f / kLookBackRate);
			gLookPitch -= gLookPitch * Ease(dt, 1.0f / kLookBackRate);
		}
	}
	if (std::fabs(gLookYaw) > 1e-3f || std::fabs(gLookPitch) > 1e-3f) {
		const CVector up = Axis(vehicle, 0x20);
		CVector front = Turn(Row(0x10), up, gLookYaw), top = Turn(Row(0x20), up, gLookYaw);
		const CVector side = Normal(Cross(front, top));
		front = Normal(Turn(front, side, gLookPitch));
		top = Normal(Turn(top, side, gLookPitch));
		Row(0x10) = front;
		Row(0x20) = top;
		Row(0x00) = Cross(top, front);
	}
	NearClip(kNearClipVehicle);
	gCockpit = vehicle;
	// The vehicle camera looks this way too, for what reads it (a drive-by's
	// crosshair and shots, where it starts aiming).
	uint8_t *cam = ActiveCam();
	*reinterpret_cast<CVector *>(cam + kCamSource) = Row(0x30);
	*reinterpret_cast<CVector *>(cam + kCamFront) = Row(0x10);
	*reinterpret_cast<CVector *>(cam + kCamUp) = Row(0x20);
}

// Getting in or out: in his head, turning towards the car's way in, his own
// way out.
void InTheDoor(uint8_t *ped, uint8_t *vehicle, bool gettingIn, float dt)
{
	const CVector eye = Add(EntityPos(ped), ToWorld(ped, SteadyHead(ped, ped, dt, kSettle)));
	const CVector &facing = gettingIn && vehicle ? Axis(vehicle, 0x10) : Axis(ped, 0x10);
	const CVector want = Normal(CVector{facing.x, facing.y, -0.1f});
	const CVector front = Normal(Add(gViewForward, Mul(Sub(want, gViewForward), Ease(dt, 1.0f / kTurnRate))));
	const CVector up = Normal(Sub(CVector{0.0f, 0.0f, 1.0f}, Mul(front, front.z)));
	Row(0x10) = front;
	Row(0x20) = up;
	Row(0x00) = Cross(up, front);
	Row(0x30) = Add(eye, CVector{0.0f, 0.0f, kEyeUp});
	NearClip(kNearClipVehicle);
	gCockpit = vehicle;
}

// In first person a shot goes from his eye through the middle of the view,
// aimed or from the hip. A player's shot heads along the camera he aimed with
// (CPlayerPed's copy of it, with CCamera::bFreeCam) once his arm has pointed
// the gun (CPedIK flag 1), else straight out of his hand as the animation
// holds it. For first person's shots the copy is the view, the gun counts as
// pointed and the free camera is on.
bool Hook_Fire(uint8_t *weapon, uint8_t *shooter, CVector *source)
{
	if (!gOnFoot || !shooter || shooter != api.FindPlayerPed())
		return orig_Fire(weapon, shooter, source);
	*reinterpret_cast<CVector *>(shooter + kAimedSource) = Row(0x30);
	*reinterpret_cast<CVector *>(shooter + kAimedFront) = Row(0x10);
	*reinterpret_cast<CVector *>(shooter + kAimedUp) = Row(0x20);
	uint8_t &ik = shooter[kPedIKFlags];
	const uint8_t ikWas = ik, freeWas = gFreeCam ? *gFreeCam : 0;
	ik |= kGunPointed;
	if (gFreeCam)
		*gFreeCam = 1;
	const bool fired = orig_Fire(weapon, shooter, source);
	ik = (ik & ~kGunPointed) | (ikWas & kGunPointed);
	if (gFreeCam)
		*gFreeCam = freeWas;
	return fired;
}

// First person goes with him into vehicles and out again: getting in with it
// on, the vehicle's camera starts in the seat view; getting out of one in the
// seat view, it's first person on foot.
void Vehicles(uint8_t *vehicle)
{
	int32_t &carZoom = *reinterpret_cast<int32_t *>(api.theCamera + kCarZoom);
	if (vehicle && !gWasInVehicle && gFirstPerson)
		carZoom = 0;
	if (!vehicle && gWasInVehicle)
		gFirstPerson = gCarZoomSeen == 0;
	if (vehicle)
		gCarZoomSeen = carZoom;
	gWasInVehicle = vehicle != nullptr;
}

// In the bumper view the game leaves his own vehicle undrawn (the camera was
// in front of it, CRenderer::SetupEntityVisibility); from the seat it's
// drawn about him: the wheel, the dash, his hands on it.
int32_t Hook_SetupEntityVisibility(uint8_t *entity)
{
	if (!gCockpit || entity != gCockpit)
		return orig_SetupEntityVisibility(entity);
	uint16_t &mode = *reinterpret_cast<uint16_t *>(ActiveCam() + 0xc);
	const uint16_t was = mode;
	mode = kModeFollowPed;
	const int32_t visibility = orig_SetupEntityVisibility(entity);
	mode = was;
	return visibility;
}

// True if it turned the camera.
bool Shake(uint8_t *ped, float dt, bool firstPerson)
{
	const float strength = std::clamp(gConfig.cameraShake, 0.0f, 3.0f);
	if (strength <= 0.0f || !ped || dt <= 0.0f)
		return false;
	float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
	auto add = [&](float amplitude, float phase, uint32_t seed) {
		yaw += amplitude * Wobble(phase, seed);
		pitch += amplitude * Wobble(phase, seed + 101);
		roll += amplitude * 0.6f * Wobble(phase, seed + 211);
	};
	if (PedInVehicle(ped) && PedVehicle(ped)) {
		uint8_t *vehicle = PedVehicle(ped);
		const CVector velocity = Mul(EntitySpeed(vehicle), 50.0f); // m/s
		const float speed = std::sqrt(Dot(velocity, velocity));
		// Bumps and knocks: the vehicle's speed changing faster than driving does.
		if (gHaveVelocity) {
			const CVector change = Mul(Sub(velocity, gLastVelocity), 1.0f / dt);
			const float vertical = std::fabs(change.z), across = std::sqrt(change.x * change.x + change.y * change.y);
			const float knock = std::clamp((vertical - 3.0f) / 25.0f, 0.0f, 1.0f) +
			                    std::clamp((across - 12.0f) / 60.0f, 0.0f, 1.0f);
			gJolt = std::max(gJolt * std::exp(-dt / kJoltFade), std::min(knock, 1.5f));
		}
		gLastVelocity = velocity;
		gHaveVelocity = true;
		gPhaseSway += kSwayRate * dt;
		gPhaseHum += kHumRate * dt;
		gPhaseJolt += kJoltRate * dt;
		add(kSway + kSwayPerSpeed * speed, gPhaseSway, 1);
		add(kHum + kHumPerSpeed * speed, gPhaseHum, 2);
		add(kJolt * gJolt, gPhaseJolt, 3);
	} else {
		gHaveVelocity = false;
		gJolt = 0.0f;
		const CVector v = EntitySpeed(ped);
		const float speed = std::sqrt(v.x * v.x + v.y * v.y) * 50.0f;
		gPhaseFoot += (kFootRate + kFootRatePerSpeed * speed) * dt;
		add(kFootDrift + kFootDriftPerSpeed * speed, gPhaseFoot, 4);
	}
	float scale = strength * (3.14159265f / 180.0f);
	if (aimcam::Aiming())
		scale *= kAimingShake;
	if (firstPerson)
		scale *= kFirstPersonShake;
	TurnCamera(yaw * scale, pitch * scale, roll * scale);
	return true;
}

void Hook_Process(uint8_t *cam)
{
	orig_Process(cam);
	traffic::AfterCamera();
	phone::AfterWorld();
	uint8_t *ped = api.FindPlayerPed();
	const float dt = *api.timeStep / 50.0f; // s
	Cycle(ped);
	const float aimTo = aimcam::Aiming() ? 1.0f : 0.0f;
	gAim += std::clamp(aimTo - gAim, -kAimRate * dt, kAimRate * dt);
	const uint8_t *cam8 = api.theCamera;
	const bool scripted = cam8[kWideScreen] || !cam8[kLookingAtPlayer];
	const uint16_t mode = *reinterpret_cast<const uint16_t *>(ActiveCam() + 0xc);
	uint8_t *vehicle = ped && PedInVehicle(ped) ? PedVehicle(ped) : nullptr;
	if (gConfig.firstPerson && ped)
		Vehicles(vehicle);
	gCockpit = nullptr;
	gOnFoot = false;
	bool firstPerson = false;
	const int32_t state = ped ? PedState(ped) : 0;
	const bool gettingIn = state == kStateOpenDoor || state == kStateCarJack || state == kStateEnterCar;
	const bool gettingOut = state == kStateExitCar;
	if (gConfig.firstPerson && ped && !scripted) {
		if (vehicle && mode == kModeBumper && !gettingOut) {
			InVehicle(ped, vehicle, dt);
			firstPerson = true;
		} else if ((gettingIn && gFirstPerson) || (gettingOut && gCarZoomSeen == 0)) {
			InTheDoor(ped, PedVehicle(ped), gettingIn, dt);
			firstPerson = true;
		} else if (!vehicle && gFirstPerson && OurCamera()) {
			OnFoot(ped, dt);
			firstPerson = gOnFoot = true;
		}
	}
	if (firstPerson)
		gViewForward = Row(0x10);
	else
		gHeadReady = gHeightReady = false;
	if (!gCockpit)
		gLookYaw = gLookPitch = 0.0f;
	gHideHead = firstPerson;
	bool moved = firstPerson;
	if (OurCamera() || (ped && PedInVehicle(ped) && !api.theCamera[kWideScreen]))
		moved |= Shake(ped, dt, firstPerson);
	if (moved)
		ToRenderWare();
}

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("camera: missing %s", name);
	return out != nullptr;
}

} // namespace

bool Install()
{
	gNearClipZ = static_cast<float *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CDraw13ms_fNearClipZE"));
	const bool bound = Bind(gUpdateRW, "_ZN7CMatrix8UpdateRWEv") && Bind(gUpdateRwFrame, "_ZN7CEntity13UpdateRwFrameEv") &&
	                   Bind(gUpdateRpHAnim, "_ZN7CEntity13UpdateRpHAnimEv") &&
	                   Bind(gComponentPosition, "_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj") &&
	                   Bind(gCameraFrame, "_Z16RwCameraGetFramePKN2rw6CameraE") &&
	                   Bind(gFrameMatrix, "_Z16RwFrameGetMatrixPN2rw5FrameE") &&
	                   Bind(gMatrixUpdate, "_Z14RwMatrixUpdatePN2rw6MatrixE") &&
	                   Bind(gFrameUpdateObjects, "_Z20RwFrameUpdateObjectsPN2rw5FrameE") &&
	                   Bind(gFrameOrthoNormalize, "_Z21RwFrameOrthoNormalizePN2rw5FrameE") &&
	                   Bind(gSetNearClip, "_Z24RwCameraSetNearClipPlanePN2rw6CameraEf") &&
	                   Bind(gDerivedValues, "_ZN7CCamera22CalculateDerivedValuesEv") && Bind(gScene, "Scene");
	if (!Bind(gHierarchy, "_Z29GetAnimHierarchyFromSkinClumpPN2rw5ClumpE") ||
	    !Bind(gBoneIndex, "_Z17RpHAnimIDGetIndexPN2rw14HAnimHierarchyEi") ||
	    !Bind(gBoneMatrices, "_Z30RpHAnimHierarchyGetMatrixArrayPN2rw14HAnimHierarchyE"))
		gHierarchy = nullptr; // his head stays (first person still works)
	gFreeCam = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN7CCamera8bFreeCamE"));
	if (!hooks::Install("_ZN7CWeapon4FireEP7CEntityP7CVector", Hook_Fire, orig_Fire))
		Log("camera: no CWeapon::Fire hook (re-run install.sh); shots in first person go where the game aims them");
	if (!hooks::Install("_ZN9CRenderer21SetupEntityVisibilityEP7CEntity", Hook_SetupEntityVisibility,
	                    orig_SetupEntityVisibility))
		Log("camera: no SetupEntityVisibility hook (re-run install.sh); his vehicle isn't drawn in first person");
	return bound && hooks::Install("_ZN7CCamera7ProcessEv", Hook_Process, orig_Process);
}

bool FirstPersonOnFoot() { return gOnFoot; }
bool InSeat() { return gCockpit != nullptr; }
float AimBlend() { return gAim; }
float StepPhase() { return gBobPhase; }
float StepAmount() { return gBobAmount; }

void AfterPreRender(uint8_t *ped)
{
	if (!gHideHead || !gHierarchy || ped != api.FindPlayerPed())
		return;
	if (gOnFoot)
		viewmodel::Apply(ped);
	else if (gCockpit && driveby::Aiming())
		viewmodel::Apply(ped, true); // the gun held as on foot, pointing where it shoots
	void *clump = *reinterpret_cast<void *const *>(ped + kRwObject);
	void *hierarchy = clump ? gHierarchy(clump) : nullptr;
	if (!hierarchy)
		return;
	float *matrices = gBoneMatrices(hierarchy);
	// A bone's skin shrunk to a point where it is (the skin draws it there),
	// or its axes put back to their length.
	auto bone = [&](int tag) {
		const int i = gBoneIndex(hierarchy, tag);
		return i >= 0 ? matrices + i * 16 : nullptr;
	};
	auto shrink = [](float *m) {
		for (int row = 0; m && row < 3; row++)
			for (int col = 0; col < 3; col++)
				m[row * 4 + col] *= 0.001f;
	};
	auto unscale = [](float *m) {
		for (int row = 0; m && row < 3; row++) {
			const float l = std::sqrt(m[row * 4] * m[row * 4] + m[row * 4 + 1] * m[row * 4 + 1] + m[row * 4 + 2] * m[row * 4 + 2]);
			for (int col = 0; l > 1e-6f && col < 3; col++)
				m[row * 4 + col] /= l;
		}
	};
	shrink(bone(kBoneHead));
	// In the seat his neck too, and his shoulders steadied: on a bike (or
	// with the top down) CPed::PreRender flaps his collar in the wind,
	// scaling them at random every frame, right under the eye.
	if (gCockpit) {
		shrink(bone(kBoneNeck));
		for (int tag : kBoneClavicles)
			unscale(bone(tag));
	}
}

} // namespace camera
