#include "shootdodge.h"
#include "aimcam.h"
#include "climb.h"
#include "config.h"
#include "game.h"
#include "grade.h"
#include "log.h"
#include "swim.h"
#include "wheel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <dlfcn.h>

using namespace game;
using namespace body;

namespace shootdodge {
namespace {

using Clock = std::chrono::steady_clock;

// Bullet time: the clock's speed; a full bar of stamina lasts this long (real
// seconds), and it takes this much of one to start.
constexpr float kSlow = 0.3f, kLasts = 5.0f, kToStart = 0.2f;
constexpr float kLookIn = 4.0f; // the picture's change, per real second
// The dive: the stick it takes; its speed (m/s); in the air, lying (game
// seconds); the root's drop lying down (from standing).
constexpr float kPush = 0.4f, kSpeed = 5.5f;
constexpr float kAir = 0.65f, kLie = 0.55f;
constexpr float kLying = -0.85f, kRiseUp = 0.3f; // and how high the dive carries him at most
constexpr size_t kFlags = 0x188;                  // CPed flags: bIsDucking is bit 36
constexpr uint64_t kDucking = 1ull << 36;
// Getting up: the game's own, from his back (getup) or front (getup_front,
// which it plays when a "frontal" animation such as floor_hit_f is on), a
// little quicker than it plays them. Each starts lying with his head this way
// (ped frame) and ends standing facing forward. Ours hands over to it in
// kHandOver, and he turns to the aim once he's off the floor.
constexpr int32_t kGetUp = 0x89, kGetUpFront = 0x8c, kFloorFront = 0x2b; // AnimationId
constexpr float kGetUpSpeed = 1.5f, kGetUpFrontSpeed = 1.2f;
constexpr CVector kHeadBack{0.27f, -0.96f, 0.0f}, kHeadFront{-0.14f, 0.99f, 0.0f};
constexpr float kHandOver = 0.3f, kOffTheFloor = 0.5f, kUpLongest = 3.0f;
constexpr size_t kAssocSpeed = 0x34, kAssocBlendDelta = 0x2c; // CAnimBlendAssociation

enum class Dive : uint8_t { None, Air, Lie, Up };

void (*gClearDuck)(uint8_t *ped, bool now);
void (*gSetGetUp)(uint8_t *ped);
uint8_t *(*gBlendAnimation)(uint8_t *clump, int32_t group, int32_t anim, float delta);
uint8_t *(*gAssociation)(uint8_t *clump, uint32_t anim);
bool gSlow;          // bullet time
bool gSlowForDive;   // ... started by the dive, and ending with it
bool gRestoreClock;  // it ended with the weapon wheel open (which keeps its own slow clock)
float gLook;         // bullet time's picture, 0..1
Clock::time_point gLastFrame;
bool gClickWas, gJumpWas, gJumpKept; // the right stick's click and jump last frame; jump kept from the game
bool gReady;
Dive gDive;
float gT;            // game seconds into the dive's stage
CVector gWay;        // where it goes (world, level)
float gWeight;

float &StaminaOf(uint8_t *ped) { return *reinterpret_cast<float *>(ped + 0x78c); }
float Share(uint8_t *ped) { return Stamina(ped) / std::max(1.0f, MaxStamina(ped)); }

void StartSlow(bool forDive)
{
	if (wheel::IsOpen())
		return;
	gSlow = true;
	gSlowForDive = forDive;
	*api.timeScale = kSlow;
}

void EndSlow()
{
	if (!gSlow)
		return;
	gSlow = gSlowForDive = false;
	if (wheel::IsOpen())
		gRestoreClock = true;
	else
		*api.timeScale = 1.0f;
}

CVector Row(const uint8_t *entity, size_t at) { return *reinterpret_cast<const CVector *>(entity + at); }
// A world direction in the ped's frame (x right, y forward, z up).
CVector InPed(const uint8_t *ped, const CVector &v)
{
	return {Dot(v, Row(ped, 0x8)), Dot(v, Row(ped, 0x18)), Dot(v, Row(ped, 0x28))};
}

// The body laid along the dive (`way`, ped frame) and turned to the aim: on
// his back diving away from it, chest down diving at it, on his side across it.
Quat Laid(const CVector &way, const CVector &aim)
{
	CVector level = Normalised({aim.x, aim.y, 0.0f});
	if (Dot(level, level) < 0.5f)
		level = {0.0f, 1.0f, 0.0f};
	const float at = Dot(level, way);
	const CVector chest = Normalised(Add(Sub(level, Scale(way, at)), {0.0f, 0.0f, -at}));
	const Quat head = Arc({0.0f, 0.0f, 1.0f}, way); // his head along the dive
	const CVector front = Rotate(head, {0.0f, 1.0f, 0.0f});
	const float roll = std::atan2(Dot(Cross(front, chest), way), Dot(front, chest));
	return AxisAngle(way, roll) * head;
}

// Down after the dive: up off the floor as the game gets peds up, from his
// back or his front as he lies, turned so its first frame lies as he does.
void GetUp(uint8_t *ped)
{
	const CVector &look = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const CVector chest = Rotate(Laid(Normalised(InPed(ped, gWay)), Normalised(InPed(ped, look))), {0.0f, 1.0f, 0.0f});
	const bool front = chest.z < 0.0f;
	const CVector head = front ? kHeadFront : kHeadBack;
	FaceHeading(ped, std::remainder(HeadingOf(gWay.x, gWay.y) - HeadingOf(head.x, head.y), 6.2831853f));
	api.ClearPointGunAt(ped); // the gun down: the aiming pose is held on top of anything else
	uint8_t *clump = EntityClump(ped);
	if (front)
		gBlendAnimation(clump, 0, kFloorFront, 1000.0f);
	gSetGetUp(ped);
	if (uint8_t *floor = front ? gAssociation(clump, kFloorFront) : nullptr)
		*reinterpret_cast<float *>(floor + kAssocBlendDelta) = -8.0f; // it was only for the choice
	uint8_t *up = gAssociation(clump, front ? kGetUpFront : kGetUp);
	if (PedState(ped) != kPedGetUp || !up) { // a car on top of him: the game sees to it
		gDive = Dive::None;
		return;
	}
	*reinterpret_cast<float *>(up + kAssocSpeed) = front ? kGetUpFrontSpeed : kGetUpSpeed;
	gDive = Dive::Up;
	gT = 0.0f;
}

} // namespace

bool Install()
{
	gClearDuck = reinterpret_cast<decltype(gClearDuck)>(dlsym(RTLD_MAIN_ONLY, "_ZN4CPed9ClearDuckEb"));
	gSetGetUp = reinterpret_cast<decltype(gSetGetUp)>(dlsym(RTLD_MAIN_ONLY, "_ZN4CPed8SetGetUpEv"));
	gBlendAnimation = reinterpret_cast<decltype(gBlendAnimation)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf"));
	gAssociation = reinterpret_cast<decltype(gAssociation)>(
	    dlsym(RTLD_MAIN_ONLY, "_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj"));
	gLastFrame = Clock::now();
	gReady = gClearDuck && gSetGetUp && gBlendAnimation && gAssociation;
	return gReady;
}

void FromPad(uint8_t *pad)
{
	const Clock::time_point now = Clock::now();
	const float real = std::min(std::chrono::duration<float>(now - gLastFrame).count(), 0.1f);
	gLastFrame = now;
	gLook += ((gSlow ? 1.0f : 0.0f) - gLook) * std::min(1.0f, real * kLookIn);
	grade::SlowMotion(gLook);

	const bool click = PadNew(pad, kPadR3) != 0, jump = PadNew(pad, kPadSquare) != 0;
	const bool clicked = click && !gClickWas, jumped = jump && !gJumpWas;
	gClickWas = click;
	gJumpWas = jump;
	uint8_t *ped = api.FindPlayerPed();
	if (!gConfig.bulletTime || !gReady || !ped || PedInVehicle(ped) || PadControlsDisabled(pad) || Health(ped) <= 0.0f ||
	    swim::Swimming() || climb::Active()) {
		EndSlow();
		gDive = Dive::None;
		return;
	}
	const bool aiming = aimcam::Aiming();
	if (aiming && click)
		PadNew(pad, kPadR3) = 0; // ours, not the game's
	if (clicked && (aiming || gSlow)) {
		if (gSlow)
			EndSlow();
		else if (Share(ped) >= kToStart)
			StartSlow(false);
	}
	if (gSlow && !gSlowForDive && !aiming)
		EndSlow();

	// Jump with the stick pushed, aiming: the dive that way (from the camera).
	const float x = PadNew(pad, kPadLeftStickX) / 128.0f, y = -PadNew(pad, kPadLeftStickY) / 128.0f;
	if (jumped && aiming && gDive == Dive::None && std::hypot(x, y) >= kPush && api.IsPedInControl(ped)) {
		const CVector &front = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
		const CVector f = Normalised({front.x, front.y, 0.0f}), right{f.y, -f.x, 0.0f};
		gWay = Normalised(Add(Scale(right, x), Scale(f, y)));
		gDive = Dive::Air;
		gT = 0.0f;
		gJumpKept = true;
		if (*reinterpret_cast<uint64_t *>(ped + kFlags) & kDucking)
			gClearDuck(ped, true);
		if (!gSlow && Share(ped) >= kToStart)
			StartSlow(true);
	}
	if (gJumpKept && !jump)
		gJumpKept = false;
	if (gJumpKept)
		PadNew(pad, kPadSquare) = 0; // no jump (or climb) from it, now or once he's up
	if (gDive != Dive::None)
		PadNew(pad, kPadLeftStickX) = PadNew(pad, kPadLeftStickY) = 0; // he's in the air, not walking
}

void AfterControl(uint8_t *ped)
{
	if (gRestoreClock && !wheel::IsOpen()) {
		gRestoreClock = false;
		if (!gSlow)
			*api.timeScale = 1.0f;
	}
	if (gSlow) {
		// Stamina goes in real time; the clock is kept slow (the wheel may have reset it).
		float &stamina = StaminaOf(ped);
		stamina -= MaxStamina(ped) / kLasts * *api.timeStep / 50.0f / kSlow;
		if (stamina <= 0.0f) {
			stamina = 0.0f;
			EndSlow();
		} else if (!wheel::IsOpen()) {
			*api.timeScale = kSlow;
		}
	}
	if (gDive == Dive::None)
		return;
	const float dt = *api.timeStep / 50.0f;
	gT += dt;
	if (gDive == Dive::Up) {
		// Turned to the aim once he's off the floor; done when the game's done.
		if (gT > kOffTheFloor && aimcam::Aiming()) {
			const float now = *reinterpret_cast<const float *>(ped + 0x46c);
			FaceHeading(ped, now + std::remainder(CameraHeading() - now, 6.2831853f) * std::min(1.0f, dt * 5.0f));
		}
		if (gT > kUpLongest || (gT > kHandOver && PedState(ped) != kPedGetUp))
			gDive = Dive::None;
		return;
	}
	if (!api.IsPedInControl(ped) || PedInVehicle(ped)) { // knocked down, or got in something
		gDive = Dive::None;
		if (gSlowForDive)
			EndSlow();
		return;
	}
	float speed = 0.0f;
	if (gDive == Dive::Air) {
		speed = kSpeed;
		if (gT >= kAir) {
			gDive = Dive::Lie;
			gT = 0.0f;
		}
	} else if (gDive == Dive::Lie) {
		speed = kSpeed * std::max(0.0f, 1.0f - gT / 0.2f); // a short slide
		if (gT >= kLie) {
			if (gSlowForDive)
				EndSlow();
			GetUp(ped);
			speed = 0.0f;
		}
	}
	CVector &v = EntitySpeed(ped);
	v.x = gWay.x * speed / 50.0f;
	v.y = gWay.y * speed / 50.0f;
}

bool Posing() { return gDive != Dive::None || gWeight > 0.0f; }

float Pose(uint8_t *ped, body::Pose &p)
{
	float lay = 1.0f, height = kLying;
	switch (gDive) {
	case Dive::Air: {
		const float u = std::min(gT / kAir, 1.0f);
		lay = Smooth(gT / 0.18f);
		height = kRiseUp * std::sin(3.14159265f * u) + kLying * u * u;
		gWeight = std::min(1.0f, gT / 0.08f);
		break;
	}
	case Dive::Lie:
		gWeight = 1.0f;
		break;
	case Dive::Up: // handing over to the game's getting up, which starts lying as he does
		gWeight = 1.0f - Smooth(gT / kHandOver);
		break;
	case Dive::None:
		gWeight = std::max(0.0f, gWeight - *api.timeStep / 50.0f * 4.0f);
		break;
	}
	if (gWeight <= 0.0f)
		return 0.0f;
	const CVector &front = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const CVector aim = Normalised(InPed(ped, front)), way = Normalised(InPed(ped, gWay));
	const Quat laid = Slerp(Quat{}, Laid(way, aim), lay);
	// Laid out, legs together and a little bent; the rest as the game aims.
	p.Turn(kRoot, laid);
	p.Turn(kPelvis, laid);
	p.rootMove = {0.0f, 0.0f, height};
	p.Turn(kThighL, laid * X(0.2f));
	p.Turn(kCalfL, laid * X(-0.1f));
	p.Turn(kThighR, laid * X(0.4f));
	p.Turn(kCalfR, laid * X(-0.2f));
	p.Turn(kFootL, laid);
	p.Turn(kFootR, laid);
	// The gun arm (as the game's aiming or firing pose holds it, not while
	// reloading) turned onto the aim, at the waist and chest.
	const int32_t state = PedState(ped);
	if (state == kPedAimGun || state == kPedAttack) {
		const CVector arm = Normalised(Sub(JointOver(ped, p, kHandR), JointOver(ped, p, kUpperArmR)));
		const Quat onto = Arc(arm, aim);
		TurnOver(ped, p, kSpine, Slerp(Quat{}, onto, 0.4f));
		TurnOver(ped, p, kSpine1, Slerp(Quat{}, onto, 0.6f));
	}
	return gWeight;
}

} // namespace shootdodge
