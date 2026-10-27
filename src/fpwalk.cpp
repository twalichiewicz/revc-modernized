#include "fpwalk.h"
#include "fpshoot.h"
#include "aimcam.h"
#include "camera.h"
#include "sniper.h"
#include "climb.h"
#include "config.h"
#include "fpgait.h"
#include "fpmove.h"
#include "game.h"
#include "log.h"
#include "shootdodge.h"
#include "swim.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>

using namespace game;

namespace fpwalk {
namespace {

constexpr int32_t kMoveSprint = 5;               // CPed::m_nMoveState
constexpr int32_t kFirstGun = 17, kLastGun = 33; // eWeaponType: the Colt to the minigun
constexpr uint32_t kOneHanded = 0x80;            // CWeaponInfo flags (+0x5c): CANAIM_WITHARM
constexpr float kScopedCreep = 0.6f;             // m/s, scoped (a slow walk)
constexpr uint32_t kPadQuiet = 500;              // ms: the pad hook hasn't run this long, it isn't hooked

uint8_t *(*gWeaponInfo)(int32_t type); // CWeaponInfo::GetWeaponInfo
uint8_t *gFreeCam;                     // CCamera::bFreeCam: 0 is the strafing control
Vec2 gStick;                           // the push: x right, y ahead (the stick or the keys)
bool gDisabled;                        // the pad's controls are off (a script's, a cutscene's)
uint32_t gPadAt;                       // when TakeStick last ran (game time)
bool gPadSeen;
Vec2 gVelocity;                        // m/s, before ProcessControl: last frame's, as collision left it
bool gCaptured;                        // ... this frame
bool gStrafing;                        // we turned bFreeCam off for this ProcessControl
bool gActive, gLogged;
bool gLogSpeed;
uint32_t gLastLog;

bool TwoHanded(int32_t type)
{
	if (type < kFirstGun || type > kLastGun || !gWeaponInfo)
		return false;
	const uint8_t *info = gWeaponInfo(type);
	return info && !(*reinterpret_cast<const uint32_t *>(info + 0x5c) & kOneHanded);
}

float TopSpeed(uint8_t *ped)
{
	if (sniper::Wanted())
		return kScopedCreep; // creeping, the rifle at his eye (scoped, or raising it, or its bolt)
	fpgait::Gait g;
	g.ducking = PedDucking(ped);
	g.aiming = aimcam::Aiming();
	// The game's (its stamina, not with a heavy gun); firing drops him to a run, as the spec has it.
	g.sprinting = PedMoveState(ped) == kMoveSprint && !fpshoot::Shooting();
	g.twoHanded = TwoHanded(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
	return fpgait::TopSpeed(g);
}

// How the stick may move him now: first person on foot, the pad hook running,
// not swimming, climbing or diving; then fpgait's rule (his own states, or the air).
fpgait::Mode Mode(uint8_t *ped)
{
	if (!gConfig.firstPersonMovement || !(camera::FirstPersonOnFoot() || sniper::Active()) || !ped || ped != api.FindPlayerPed() ||
	    PedInVehicle(ped) || swim::Swimming() || climb::Active() || shootdodge::Posing())
		return fpgait::Mode::kNone;
	if (!gPadSeen || *api.timeMs - gPadAt > kPadQuiet)
		return fpgait::Mode::kNone; // no pad hook: the game's movement, not a frozen one
	fpgait::Situation s;
	s.state = PedState(ped);
	s.standing = (PedFlags(ped) & kPedStanding) != 0;
	s.inTheAir = (PedFlags(ped) & kPedInTheAir) != 0;
	s.inControl = api.IsPedInControl(ped);
	s.controlsDisabled = gDisabled;
	s.objective = PedObjective(ped);
	s.scoped = sniper::Active(); // the game's sniper state: his to creep in
	return fpgait::MoveMode(s);
}

void LogSpeed(uint8_t *ped)
{
	const uint32_t now = *api.timeMs;
	if (now - gLastLog < 1000)
		return;
	gLastLog = now;
	const CVector &v = EntitySpeed(ped);
	Log("fpwalk: speed %.2f m/s (move state %d, %s)", std::hypot(v.x, v.y) * 50.0f, PedMoveState(ped),
	    camera::FirstPersonOnFoot() ? "first person" : "third person");
}

} // namespace

bool Install()
{
	gWeaponInfo = reinterpret_cast<decltype(gWeaponInfo)>(dlsym(RTLD_MAIN_ONLY, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType"));
	gFreeCam = static_cast<uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN7CCamera8bFreeCamE"));
	gLogSpeed = getenv("VCHUD_LOG_SPEED") != nullptr;
	if (!gWeaponInfo)
		Log("fpwalk: CWeaponInfo::GetWeaponInfo is missing; every gun runs at a one-handed pace");
	if (!gFreeCam)
		Log("fpwalk: CCamera::bFreeCam is missing; his legs keep the game's turning walk in first person");
	return true;
}

void TakeStick(const uint8_t *pad)
{
	uint8_t *p = const_cast<uint8_t *>(pad);
	gDisabled = PadControlsDisabled(pad);
	gStick = fpgait::Push(PadNew(p, kPadLeftStickX), PadNew(p, kPadLeftStickY), PadNew(p, kPadDPadUp),
	                      PadNew(p, kPadDPadDown), PadNew(p, kPadDPadLeft), PadNew(p, kPadDPadRight));
	gPadAt = *api.timeMs;
	gPadSeen = true;
}

bool BeforeControl()
{
	gCaptured = gStrafing = false;
	uint8_t *ped = api.FindPlayerPed();
	if (!ped || Mode(ped) == fpgait::Mode::kNone)
		return false;
	const CVector &v = EntitySpeed(ped);
	gVelocity = {v.x * 50.0f, v.y * 50.0f};
	gCaptured = true;
	if (!gFreeCam || !*gFreeCam) // off already (aimcam's, while aiming)
		return false;
	*gFreeCam = 0;
	gStrafing = true;
	return true;
}

bool BeforeWeapon()
{
	if (!gStrafing || !gFreeCam)
		return false;
	*gFreeCam = 1; // the weapon is handled as it was (fists, melee, throws, the hip)
	return true;
}

void AfterWeapon(bool restored)
{
	if (restored && gFreeCam)
		*gFreeCam = 0;
}

void AfterControl(uint8_t *ped, bool strafed)
{
	if (strafed && gFreeCam)
		*gFreeCam = 1;
	gStrafing = false;
	gActive = false;
	if (gLogSpeed && ped == api.FindPlayerPed())
		LogSpeed(ped);
	const float dt = *api.timeStep / 50.0f;
	const bool captured = gCaptured;
	gCaptured = false;
	const fpgait::Mode mode = captured ? Mode(ped) : fpgait::Mode::kNone;
	if (mode == fpgait::Mode::kNone || dt <= 0.0f)
		return;
	const CVector front = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const float flat = std::hypot(front.x, front.y);
	if (flat < 1e-3f)
		return;
	const Vec2 ahead{front.x / flat, front.y / flat}, right{ahead.y, -ahead.x};
	const Vec2 wish{right.x * gStick.x + ahead.x * gStick.y, right.y * gStick.x + ahead.y * gStick.y};
	const float push = std::min(1.0f, std::hypot(wish.x, wish.y));
	const Vec2 dir = push > 1e-4f ? Vec2{wish.x / std::hypot(wish.x, wish.y), wish.y / std::hypot(wish.x, wish.y)} : Vec2{0, 0};
	const float top = TopSpeed(ped);
	fpmove::Tuning tuning;
	tuning.accel = gConfig.fpAccel;
	tuning.friction = gConfig.fpFriction;
	const bool ground = mode == fpgait::Mode::kGround;
	const Vec2 v = ground ? fpmove::Ground(gVelocity, dir, push * top, top, dt, tuning)
	                      : fpmove::Air(gVelocity, dir, push * top, dt, tuning);
	CVector &speed = EntitySpeed(ped);
	speed.x = v.x / 50.0f;
	speed.y = v.y / 50.0f;
	if (ground)
		FaceHeading(ped, CameraHeading());
	gActive = true;
	if (!gLogged) {
		gLogged = true;
		Log("fpwalk: first-person movement on");
	}
}

bool Active() { return gActive; }

} // namespace fpwalk
