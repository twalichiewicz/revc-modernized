#include "crouch.h"
#include "aimcam.h"
#include "camera.h"
#include "climb.h"
#include "config.h"
#include "game.h"
#include "log.h"
#include "swim.h"

#include <algorithm>
#include <cmath>

using namespace game;
using body::Bone;

namespace crouch {
namespace {

// CPed (macVC 1.0.5217).
constexpr size_t kDuckFlags = 0x18c;  // bIsDucking: bit 4
constexpr uint8_t kDucking = 0x10;
constexpr size_t kDuckTimer = 0x6a8;  // m_duckTimer: he stands up at it (game time)
constexpr uint32_t kDuckFor = 60000;
// The stick: its dead zone (of its travel); his pace (m/s) at a full push.
constexpr float kDead = 0.15f, kPace = 1.6f;
constexpr float kTurnRate = 9.0f; // per second, towards where he goes
// His crouch walking: the hips this much lower than standing (the game's
// crouch is near a kneel), a step this long (m), a foot lifted this high,
// the feet this much wider apart than the hips; into and out of it, per second.
constexpr float kHipDrop = 0.30f, kStep = 0.42f, kLift = 0.08f, kWide = 1.35f, kBlend = 7.0f;
constexpr float kPi = 3.14159265f;

Vec2 gStick;          // the stick's push, kept from the game (x right, y ahead)
bool gWalking;        // this frame
float gWeight;        // of his legs' pose
float gAimWeight;     // of the aiming crouch's body (below)
bool gLoggedAiming;
float gCycle;         // two steps, 0..1
CVector gWay{0, 1, 0}; // where he goes, in his own frame
float gGround;        // his ankles standing, in his own frame (where the ground is)

bool Ducking(const uint8_t *ped) { return (ped[kDuckFlags] & kDucking) != 0; }
// The gun raised (CPed::SetPointGunAt) or firing, crouched: the crouch's firing
// pose, which turns his whole body about the root to hold the gun where he faces.
bool AimingPose(const uint8_t *ped)
{
	const int32_t state = PedState(ped);
	return state == kPedAimGun || state == kPedAttack;
}

// A foot to `foot` from `hip` (ped frame): the knee bent forward (and out).
void Leg(body::Pose &p, Bone thigh, Bone calf, Bone foot, CVector hip, CVector target, float side, float toe)
{
	using namespace body;
	const float l1 = Length(thigh), l2 = Length(calf);
	const CVector d = Sub(target, hip);
	const float reach = std::clamp(std::sqrt(Dot(d, d)), std::fabs(l1 - l2) + 0.02f, l1 + l2 - 0.01f);
	const CVector along = Normalised(d);
	const CVector pole = Normalised(CVector{0.25f * side, 1.0f, 0.0f});
	const CVector out = Normalised(Sub(pole, Scale(along, Dot(pole, along))));
	const float cosA = std::clamp((l1 * l1 + reach * reach - l2 * l2) / (2.0f * l1 * reach), -1.0f, 1.0f);
	const CVector knee = Add(hip, Add(Scale(along, l1 * cosA), Scale(out, l1 * std::sqrt(1.0f - cosA * cosA))));
	Point(p, thigh, Sub(knee, hip));
	Point(p, calf, Sub(Add(hip, Scale(along, reach)), knee));
	p.Turn(foot, X(-toe)); // flat as he stands, the toes down a little as it swings
}

} // namespace

void TakeStick(uint8_t *pad)
{
	gStick = {0.0f, 0.0f};
	uint8_t *ped = api.FindPlayerPed();
	if (!gConfig.crouchWalk || !ped || PedInVehicle(ped) || !Ducking(ped) || swim::Swimming() || climb::Active() ||
	    PadControlsDisabled(pad) || !api.IsPedInControl(ped))
		return;
	const float x = PadNew(pad, kPadLeftStickX) / 128.0f, y = -PadNew(pad, kPadLeftStickY) / 128.0f;
	if (std::hypot(x, y) < kDead)
		return;
	gStick = {x, y};
	PadNew(pad, kPadLeftStickX) = PadNew(pad, kPadLeftStickY) = 0; // the game sees him crouched and still
}

void AfterControl(uint8_t *ped)
{
	gWalking = false;
	const float push = std::min(std::hypot(gStick.x, gStick.y), 1.0f);
	if (push < kDead || !Ducking(ped) || PedInVehicle(ped) || !api.IsPedInControl(ped))
		return;
	const float dt = *api.timeStep / 50.0f;
	// Where he goes: the stick, from the camera's way.
	const CVector front = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const float flat = std::hypot(front.x, front.y);
	if (flat < 1e-3f)
		return;
	const CVector f{front.x / flat, front.y / flat, 0.0f}, right{f.y, -f.x, 0.0f};
	const CVector way = body::Normalised(body::Add(body::Scale(right, gStick.x), body::Scale(f, gStick.y)));
	const float pace = kPace * (push - kDead) / (1.0f - kDead);
	CVector &v = EntitySpeed(ped);
	v.x = way.x * pace / 50.0f;
	v.y = way.y * pace / 50.0f;
	// He turns to where he goes; aiming or in first person he keeps his facing.
	if (!aimcam::Aiming() && !camera::FirstPersonOnFoot()) {
		const CVector &facing = *reinterpret_cast<const CVector *>(ped + 0x18);
		const float now = HeadingOf(facing.x, facing.y);
		const float off = std::remainder(HeadingOf(way.x, way.y) - now, 2.0f * kPi);
		FaceHeading(ped, std::remainder(now + off * std::min(1.0f, dt * kTurnRate), 2.0f * kPi));
	}
	const CVector &pedRight = *reinterpret_cast<const CVector *>(ped + 0x8), &pedFront = *reinterpret_cast<const CVector *>(ped + 0x18);
	gWay = body::Normalised(CVector{body::Dot(way, pedRight), body::Dot(way, pedFront), 0.0f});
	gCycle = std::fmod(gCycle + pace * dt / (2.0f * kStep), 1.0f);
	*reinterpret_cast<uint32_t *>(ped + kDuckTimer) = *api.timeMs + kDuckFor; // he stays down
	gWalking = true;
	if (AimingPose(ped) && !gLoggedAiming) {
		gLoggedAiming = true;
		Log("crouch: walking crouched with the gun raised (the crouch's firing pose, state %d)", PedState(ped));
	}
}

bool Posing() { return gWalking || gWeight > 0.0f; }

float Pose(uint8_t *ped, body::Pose &p)
{
	using namespace body;
	const float dt = *api.timeStep / 50.0f;
	gWeight += ((gWalking ? 1.0f : 0.0f) - gWeight) * std::min(1.0f, dt * kBlend);
	if (gWeight < 0.01f) {
		gWeight = 0.0f;
		return 0.0f;
	}
	if (gGround == 0.0f)
		gGround = Joint(body::Pose{}, kFootL).z;
	gAimWeight += ((AimingPose(ped) ? 1.0f : 0.0f) - gAimWeight) * std::min(1.0f, dt * kBlend);
	// His hips raised from the crouch's, the body over them as the game crouches
	// it: upright and facing ahead; aiming, turned about the root as the crouch's
	// firing pose has it (upright, that pose's gun pointed off up and to the left).
	const body::Pose none;
	TurnOver(ped, p, kRoot, Quat{});
	p.Turn(kRoot, Slerp(Quat{}, p.turn[kRoot], gAimWeight));
	const CVector animated = JointOver(ped, none, kRoot), rest = Joint(none, kRoot);
	p.rootMove = {(animated.x - rest.x) * gAimWeight, (animated.y - rest.y) * gAimWeight, -kHipDrop};
	for (int s = 0; s < 2; s++) {
		const bool left = s == 0;
		const float side = left ? -1.0f : 1.0f;
		const CVector hip = JointOver(ped, p, left ? kThighL : kThighR);
		// Two steps a cycle, the feet half a cycle apart: planted and moving
		// back under him, then lifted and swung forward.
		const float u = std::fmod(gCycle + (left ? 0.0f : 0.5f), 1.0f);
		float along, lift = 0.0f, toe = 0.0f;
		if (u < 0.55f) {
			along = kStep * (0.5f - u / 0.55f);
		} else {
			const float t = (u - 0.55f) / 0.45f;
			along = kStep * (t - 0.5f);
			lift = kLift * std::sin(kPi * t);
			toe = 0.35f * std::sin(kPi * t);
		}
		const CVector foot{hip.x * kWide + gWay.x * along, hip.y + gWay.y * along, gGround + lift};
		Leg(p, left ? kThighL : kThighR, left ? kCalfL : kCalfR, left ? kFootL : kFootR, hip, foot, side, toe);
	}
	return Smooth(gWeight);
}

} // namespace crouch
