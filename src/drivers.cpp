#include "drivers.h"
#include "aimcam.h"
#include "camera.h"
#include "config.h"
#include "driveby.h"
#include "game.h"
#include "intimidation.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <unordered_map>

using namespace game;
namespace drivers {
namespace {
uint8_t **gVehicles, **gPedPool;
bool *gCutscene;
int32_t *gArea;
int (*gRef)(uint8_t *);
int (*gPedRef)(uint8_t *);
void (*gPointGun)(uint8_t *, uint8_t *);
void (*gClearWait)(uint8_t *);
void (*gSwitchPhysics)(uint8_t *);
void (*gRam)(uint8_t *, uint8_t *);
void (*gWantedThreat)(uint8_t *, int32_t);
struct Driver {
	intimidation::Driver rule;
	uint32_t last = 0, seen = 0;
	int ped = -1;
	intimidation::Response applied = intimidation::Response::None;
};
std::unordered_map<int, Driver> gDrivers, gPeople;
uint32_t gLast;

float Dot(CVector a, CVector b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector Sub(CVector a, CVector b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
bool Gun(int type) { return (type >= kFirstGun && type <= 33) || type == 35; }
bool Armed(uint8_t *ped)
{
	const int slot = PedCurrentWeaponSlot(ped);
	return slot < kWeaponSlots && Gun(WeaponType(PedWeapon(ped, slot))) && WeaponAmmoTotal(PedWeapon(ped, slot)) > 0;
}
void Reset()
{
	gDrivers.clear();
	gPeople.clear();
}
bool Available(uint8_t *ped, uint8_t *player)
{
	return ped && ped != player && !PedFromMission(ped) && PedType(ped) != 13 &&
	       *reinterpret_cast<uint8_t *const *>(ped + 0x1c8) != player && Health(ped) > 0 &&
	       !*reinterpret_cast<const void *const *>(ped + 0x720);
}
bool Perceived(uint8_t *ped, uint8_t *car, const CVector &eye, const CVector &aim, float &range)
{
	CVector head = EntityPos(ped);
	head.z += 0.5f;
	const CVector delta = Sub(head, eye);
	const float square = Dot(delta, delta), along = Dot(delta, aim);
	range = std::sqrt(square);
	if (!std::isfinite(range) || range > 35 || range < 0.1f || along <= 0 || square - along * along > 1.0f)
		return false;
	// The threat must enter the person's front/side field of view. The eye is
	// CCamera's actual matrix position, not CEntity's different +0x38 offset.
	if (Dot(Sub(eye, head), EntityForward(car ? car : ped)) / range < -0.5f)
		return false;
	uint8_t *ignored = *api.ignoreEntity;
	// Ignore the driver's own vehicle, or the target ped, whose body ends the ray.
	*api.ignoreEntity = car ? car : ped;
	const bool visible = api.GetIsLineOfSightClear(eye, head, true, true, false, true, false, true, false);
	*api.ignoreEntity = ignored;
	return visible;
}
intimidation::Threat Facts(uint8_t *ped, uint8_t *car, uint8_t *player, float range)
{
	intimidation::Threat t;
	t.aiming = t.visible = t.targeted = true;
	t.distance = range;
	t.law = PedType(ped) == 6;
	t.onFoot = !car;
	t.targetInCar = PedInVehicle(player) && PedVehicle(player);
	t.gang = PedType(ped) >= 7 && PedType(ped) <= 15;
	t.armed = Armed(ped);
	const uint8_t *stats = *reinterpret_cast<const uint8_t *const *>(ped + 0x500);
	// macVC CPedStats::LoadPedStats writes fear/temper here; native ReactToAttack
	// compares the same signed bytes. Flags at +0x30 include native gun panic.
	t.fear = stats ? std::clamp(int(int8_t(stats[0x24])), 0, 100) : 45;
	t.temper = stats ? std::clamp(int(int8_t(stats[0x25])), 0, 100) : 40;
	if (stats && (*reinterpret_cast<const uint16_t *>(stats + 0x30) & 128)) t.fear = std::max(t.fear, 65);
	if (car) {
		const CVector v = EntitySpeed(car);
		t.speed = std::hypot(v.x, v.y) * 50.0f;
	}
	return t;
}
void Respond(Driver &d, uint8_t *ped, uint8_t *car, uint8_t *player, float range, float dt, uint32_t now)
{
	const auto t = Facts(ped, car, player, range);
	const auto response = d.rule.Update(t, dt);
	if (response == intimidation::Response::None || response == d.applied || (d.last && now - d.last < 1200))
		return;
	d.last = now;
	d.applied = response;
	if (t.law && gWantedThreat) {
		// ReactToPointGun owns this on foot, but rejects PED_DRIVING. Register
		// the same perceived threat for seated officers so native police AI
		// does not immediately cancel their fight/ram because there are no stars.
		if (uint8_t *wanted = *reinterpret_cast<uint8_t **>(player + 0x778)) gWantedThreat(wanted, 2);
	}
	if (response == intimidation::Response::Brake) {
		if (gSwitchPhysics) gSwitchPhysics(car);
		car[0x19f] = 1; // TEMPACT_WAIT, native CCarAI temporary-action switch
		*reinterpret_cast<uint32_t *>(car + 0x1a0) = now + 1200;
	} else if (response == intimidation::Response::Ram) {
		if (gSwitchPhysics) gSwitchPhysics(car);
		if (gRam) gRam(car, PedVehicle(player)); // registers target's engine reference
	} else if (response == intimidation::Response::Flee && car) {
		if (gSwitchPhysics) gSwitchPhysics(car);
		car[0x19f] = 0;
		car[0x19d] = 2; // AVOID_CARS
		car[0x19e] = 1; // CRUISE
		car[0x1a8] = std::max<uint8_t>(car[0x1a8], 25);
		api.Say(ped, 120);
	} else if (response == intimidation::Response::Surrender && car) {
		api.Say(ped, 118);
		api.SetObjective(ped, 16, car); // native leave-car controller, then on-foot perception
	} else {
		// This native entry point sets hands-up/cower animation, registers the
		// threat with gangs and records a gun threat against a police officer.
		if (gPointGun) gPointGun(ped, player);
		if (response == intimidation::Response::Fight || response == intimidation::Response::Flee) {
			if (gClearWait) gClearWait(ped);
			api.SetObjective(ped, response == intimidation::Response::Fight ? 8 : 10, player);
		}
	}
}
} // namespace

void Frame()
{
	if (!gConfig.driverReactions) { Reset(); return; }
	if (!gVehicles || !*gVehicles || !gRef || !gPedRef || !gCutscene || !gArea) return;
	if (*gCutscene || *gArea != 0) { Reset(); return; }
	const uint32_t now = *api.timeMs;
	if (now < gLast || now - gLast > 10000) Reset();
	if (now >= gLast && now - gLast < 100) return;
	const float dt = std::min(0.25f, float(now - gLast) / 1000.0f);
	gLast = now;
	// Feed absence before any no-aim early return, including pool generations.
	for (auto *records : {&gDrivers, &gPeople}) {
		for (auto it = records->begin(); it != records->end();) {
			uint8_t *entity = records == &gDrivers ? api.PoolsGetVehicle(it->first) : api.PoolsGetPed(it->first);
			if (now - it->second.seen > 5000 || !entity) it = records->erase(it);
			else {
				it->second.rule.Update({}, dt);
				if (now - it->second.seen > 2000) { it->second.applied = intimidation::Response::None; it->second.last = 0; }
				++it;
			}
		}
	}
	uint8_t *player = api.FindPlayerPed();
	if (!player || Health(player) <= 0 || PadControlsDisabled(api.GetPad(0))) return;
	const int slot = PedCurrentWeaponSlot(player);
	if (slot >= kWeaponSlots || !Gun(WeaponType(PedWeapon(player, slot))) ||
	    !(aimcam::Aiming() || driveby::Aiming() || api.PadGetTarget(api.GetPad(0)))) return;
	const CVector eye = *reinterpret_cast<const CVector *>(api.theCamera + 0x30), aim = camera::ViewForward();
	const uint8_t *pool = *gVehicles;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(pool + 8);
	const int count = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < count; i++) {
		if (flags[i] & 0x80) continue;
		uint8_t *car = entries + size_t(i) * 0x6f0;
		if (api.VehicleFireService && api.VehicleFireService(car)) continue;
		if (IsAircraft(car) || Wrecked(car) || (PedInVehicle(player) && PedVehicle(player) == car) || car[0x294] != 1) continue;
		uint8_t *ped = *reinterpret_cast<uint8_t **>(car + 0x218);
		if (!Available(ped, player) || !PedInVehicle(ped) || PedVehicle(ped) != car || PedState(ped) != kPedDriving ||
		    (PedObjective(ped) != 0 && PedObjective(ped) != 38)) continue;
		float range;
		if (!Perceived(ped, car, eye, aim, range)) continue;
		const int handle = gRef(car), pedHandle = gPedRef(ped);
		Driver &d = gDrivers[handle];
		if (d.ped != pedHandle) d = {};
		d.ped = pedHandle; d.seen = now;
		Respond(d, ped, car, player, range, dt, now);
	}
	if (!gPedPool || !*gPedPool || !gPointGun || !api.IsPedInControl) return;
	pool = *gPedPool;
	entries = *reinterpret_cast<uint8_t *const *>(pool);
	flags = *reinterpret_cast<const uint8_t *const *>(pool + 8);
	const int peds = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int i = 0; i < peds; i++) {
		if (flags[i] & 0x80) continue;
		uint8_t *ped = entries + size_t(i) * 0x8d0;
		if (!Available(ped, player) || PedInVehicle(ped) || !api.IsPedInControl(ped) || PedObjective(ped) != 0) continue;
		float range;
		if (!Perceived(ped, nullptr, eye, aim, range)) continue;
		const int handle = gPedRef(ped);
		Driver &d = gPeople[handle];
		d.seen = now;
		Respond(d, ped, nullptr, player, range, dt, now);
	}
}

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gVehicles = static_cast<uint8_t **>(sym("_ZN6CPools15ms_pVehiclePoolE"));
	gPedPool = static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gRef = reinterpret_cast<decltype(gRef)>(sym("_ZN6CPools13GetVehicleRefEP8CVehicle"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	gArea = static_cast<int32_t *>(sym("_ZN5CGame8currAreaE"));
	gPointGun = reinterpret_cast<decltype(gPointGun)>(sym("_ZN4CPed15ReactToPointGunEP7CEntity"));
	gClearWait = reinterpret_cast<decltype(gClearWait)>(sym("_ZN4CPed14ClearWaitStateEv"));
	gSwitchPhysics = reinterpret_cast<decltype(gSwitchPhysics)>(sym("_ZN8CCarCtrl26SwitchVehicleToRealPhysicsEP8CVehicle"));
	gRam = reinterpret_cast<decltype(gRam)>(sym("_ZN6CCarAI20TellCarToRamOtherCarEP8CVehicleS1_"));
	gWantedThreat = reinterpret_cast<decltype(gWantedThreat)>(sym("_ZN7CWanted20SetWantedLevelNoDropEi"));
	const bool ready = gVehicles && gPedPool && gRef && gPedRef && gCutscene && gArea &&
	                   gPointGun && gClearWait && gSwitchPhysics && gRam && gWantedThreat;
	if (!ready) { gVehicles = gPedPool = nullptr; Reset(); }
	return ready;
}
} // namespace drivers
