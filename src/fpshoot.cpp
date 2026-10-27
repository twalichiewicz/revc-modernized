#include "fpshoot.h"
#include "camera.h"
#include "config.h"
#include "fpfire.h"
#include "fpgun.h"
#include "game.h"
#include "shootdodge.h"
#include "log.h"

#include <cstdlib>
#include <dlfcn.h>

using namespace game;

namespace fpshoot {
namespace {

constexpr int32_t kFirstFired = 17, kLastFired = 33;          // the Colt to the minigun
constexpr int32_t kSniper = 28, kLaser = 29, kRocket = 30;    // they fire from the game's scope
constexpr size_t kLoopStart = 0x38, kLoopEnd = 0x3c;          // CWeaponInfo: the attack animation's loop
constexpr uint32_t kInTheAir = 1u << 11; // CPed flags at +0x188
constexpr int32_t kMoveRun = 4; // CPed::m_nMoveState: run (sprint 5)

bool (*gFire)(uint8_t *weapon, uint8_t *shooter, CVector *source); // CWeapon::Fire (through its hook)
const uint32_t *gFrameCounter;                                     // CTimer::m_FrameCounter
bool gReady;
bool gRt, gTaken;           // RT as the pad last read it; the mod fires with it
uint32_t gRtFrame;          // the frame it was read (the pad isn't read in menus, cutscenes, a car...)
float gSinceShot = 1e9f;    // s, game time
constexpr float kShootingHold = 0.8f; // s after a shot he stays out of a sprint (tapping, the gun stays up)
fpfire::Trigger gTrigger;
fpfire::Punch gPunch;
fpfire::Jolt gJolt; // the shot's shake through him (the gun kicks little)
fpfire::Spread gSpread;
int32_t gType = -1;
float gSpreadNow;           // for the shot leaving now
float gSpreadShown;         // the next shot's, for the crosshair

// The gun he has fires the mod's way.
bool Ours(int32_t type)
{
	return type >= kFirstFired && type <= kLastFired && type != kSniper && type != kLaser && type != kRocket;
}

// Seconds between its shots: its attack animation's loop (weapon.dat's columns R-S, kept in seconds).
float Interval(int32_t type)
{
	const uint8_t *info = api.GetWeaponInfo ? api.GetWeaponInfo(type) : nullptr;
	if (!info)
		return 0.25f;
	float loop = *reinterpret_cast<const float *>(info + kLoopEnd) - *reinterpret_cast<const float *>(info + kLoopStart);
	if (loop > 2.0f)
		loop /= 30.0f; // in frames after all
	return loop > 0.02f ? loop : 0.1f;
}

fpfire::Moving MovingOf(const uint8_t *ped)
{
	// The shootdodge's dive and landing are standing still: dive shots are as accurate as Max Payne's.
	if (shootdodge::Posing())
		return fpfire::Moving::kStill;
	// In the air by the game's own flag alone (its standing flag can drop for a frame on stairs).
	if (*reinterpret_cast<const uint32_t *>(ped + 0x188) & kInTheAir)
		return fpfire::Moving::kAir;
	const int32_t move = PedMoveState(ped);
	return move >= kMoveRun ? fpfire::Moving::kRunning : move > 1 ? fpfire::Moving::kWalking : fpfire::Moving::kStill;
}

} // namespace

bool Install()
{
	gFire = reinterpret_cast<bool (*)(uint8_t *, uint8_t *, CVector *)>(dlsym(RTLD_MAIN_ONLY, "_ZN7CWeapon4FireEP7CEntityP7CVector"));
	gFrameCounter = static_cast<const uint32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN6CTimer14m_FrameCounterE"));
	gReady = gFire && gFrameCounter;
	if (!gReady)
		Log("fpshoot: no CWeapon::Fire; first person fires as the game does");
	return gReady;
}

bool FromPad(bool rtDown)
{
	gRt = rtDown;
	gRtFrame = gFrameCounter ? *gFrameCounter : 0;
	uint8_t *ped = api.FindPlayerPed();
	// Only with the new viewmodel: a gun still held the old way fires the old way.
	gTaken = gReady && gConfig.firstPersonGuns && camera::FirstPersonOnFoot() && fpgun::Active() && ped &&
	         !PedInVehicle(ped) && Ours(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
	return gTaken;
}

void Tick(float dt)
{
	gPunch.Decay(dt);
	gJolt.Decay(dt);
	gSpread.Update(dt);
	gSinceShot += dt;
}

void AfterWeapon(uint8_t *ped, uint8_t *pad)
{
	gSpreadNow = 0.0f;
	if (!gReady || !ped || ped != api.FindPlayerPed())
		return;
	const float dt = *api.timeStep / 50.0f; // game time: bullet time slows it
	// RT only as read this frame; and the mod's to fire with only as things are now (a script can take
	// his controls, or the gun change, after the pad was read).
	const bool fresh = *gFrameCounter - gRtFrame <= 1, held = gRt && fresh;
	uint8_t *weapon = PedWeapon(ped, PedCurrentWeaponSlot(ped));
	const int32_t type = WeaponType(weapon), state = *reinterpret_cast<const int32_t *>(weapon + 4);
	if (type != gType) {
		gTrigger.Reset(held);
		gType = type;
	}
	const bool taken = gTaken && fresh && Ours(type) && camera::FirstPersonOnFoot() && fpgun::Active() &&
	                   !(pad && PadControlsDisabled(pad));
	const int32_t &clip = *reinterpret_cast<const int32_t *>(weapon + 0x8);
	const fpfire::Kind kind = fpfire::KindOf(type);
	const bool ready = taken && (state == kWeaponReady || state == kWeaponFiring) && clip > 0 && fpgun::Ready();
	// Always told, so a release while it can't fire (the dive, third person) still counts.
	const int shots = gTrigger.Update(held, ready, Interval(type), fpfire::Automatic(kind), dt);
	if (!taken)
		return;
	const bool crouched = PedDucking(ped), aiming = camera::AimBlend() > 0.5f;
	gSpreadShown = gSpread.Angle(kind, MovingOf(ped), crouched, aiming);
	// From the drawn muzzle, unless a wall's between it and his eye (it's never drawn into one).
	const CVector eye = *reinterpret_cast<const CVector *>(api.theCamera + 0x30);
	CVector source = fpgun::Muzzle();
	if (api.GetIsLineOfSightClear && !api.GetIsLineOfSightClear(eye, source, true, true, false, true, false, true, false))
		source = eye;
	for (int i = 0; i < shots && clip > 0; i++) {
		gSpreadNow = gSpread.Angle(kind, MovingOf(ped), crouched, aiming);
		if (!gFire(weapon, ped, &source))
			break; // not ready after all
		gSinceShot = 0.0f;
		gSpread.Shot();
		gPunch.Kick(kind, gSpread.Burst(), float(rand() % 1000) / 1000.0f);
		gJolt.Kick(kind, float(rand() % 1000) / 1000.0f);
	}
	gSpreadNow = 0.0f;
}

bool TriggerHeld()
{
	return gRt && gTaken && gReady && *gFrameCounter - gRtFrame <= 1;
}

bool Shooting() { return TriggerHeld() || gSinceShot < kShootingHold; }

void Punch(float &up, float &right)
{
	up = gPunch.pitch;
	right = gPunch.yaw;
}

void Jolt(float &left, float &up, float &roll)
{
	left = gJolt.yaw;
	up = gJolt.pitch;
	roll = gJolt.roll;
}

float SpreadNow() { return gSpreadNow; }
bool Firing() { return gTaken; }
float SpreadShown() { return gSpreadShown; }

} // namespace fpshoot
