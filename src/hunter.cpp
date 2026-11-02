#include "hunter.h"
#include "config.h"
#include "game.h"
#include "log.h"
#include "wanted.h"

#include <cmath>
#include <cstring>
#include <dlfcn.h>

using namespace game;

namespace hunter {
namespace {

// --- The game (macVC 1.0.5217) -------------------------------------------------------------

constexpr int kHunter = 155;              // MI_HUNTER
constexpr int kArmy = 4, kCopTypeArmy = 4, kPedTypeCop = 6;
constexpr size_t kVehicleSize = 0x6f0;    // a vehicle pool entry
constexpr uint8_t kRandomVehicle = 1;     // eVehicleCreatedBy
constexpr uint8_t kWrecked = 5; // eEntityStatus, bits 3-7 of +0x60
// CAutoPilot: its mission (MISSION_HELI_FLYTOCOORS: CCarCtrl::SteerAIHeliTowardsTargetCoors),
// where it's going.
constexpr size_t kMission = 0x19e, kDestination = 0x1b4;
constexpr uint8_t kHeliFlyTo = 20;
// CAutomobile (SteerAIHeliTowardsTargetCoors): its rotor's speed (it steers
// once that's past 0.15), the heading it turns to (< 0: the way it's going).
constexpr size_t kRotor = 0x5f0, kTurnTo = 0x6c8;
constexpr size_t kHealth = 0x2a0, kWanted = 0x778, kLevel = 0x20;
// Its cannon and rocket pods (vecHunterGunPos, vecHunterRocketPos in CAutomobile::ProcessControl).
constexpr CVector kGun{0.0f, 4.8f, -1.3f}, kPod{2.5f, 1.0f, -0.5f};
constexpr int32_t kRocket = 16; // eWeaponType WEAPONTYPE_ROCKET, the projectile

// --- Tuning --------------------------------------------------------------------------------

constexpr float kComeFrom = 150.0f, kComeUp = 60.0f; // m from him, out of sight
constexpr float kStandOff = 30.0f, kAbove = 25.0f;   // where it hovers from him
constexpr float kCannonRange = 80.0f, kRocketRange = 150.0f;
constexpr uint32_t kCannonEvery = 90, kRocketsEvery = 4000;
constexpr float kToughness = 3000.0f; // health (a car has 1000)
constexpr uint8_t kSpeed = 40;
constexpr float kGone = 350.0f;     // m off: it's left

void *(*gNewVehicle)(size_t);
void (*gAutomobile)(void *self, int model, uint8_t createdBy);
void (*gWorldAdd)(uint8_t *entity);
uint8_t *(*gAddPed)(int32_t,unsigned,const CVector &,int32_t);
void (*gSetDriver)(uint8_t *,uint8_t *);
void (*gRegisterReference)(uint8_t *,uint8_t **);
void (*gAddInCarAnims)(uint8_t *,uint8_t *,bool);
void (*gPositionInCar)(uint8_t *);
void (*gHideWeapon)(uint8_t *);
int (*gPedRef)(uint8_t *);
void (*gGoTo)(uint8_t *heli, float x, float y, float z, uint8_t speed);
void (*gInteresting)(uint8_t *vehicle);
void (*gNotInteresting)(uint8_t *vehicle);
int (*gVehicleRef)(uint8_t *vehicle);
void (*gWeapon)(uint8_t *,int32_t,int32_t);
bool (*gFire)(uint8_t *,uint8_t *,CVector *);
bool (*gAddProjectile)(uint8_t *shooter, int32_t type, CVector at, float speed);
uint8_t *gStreamInfo;
uint8_t **gPedPool;
int32_t *gArea; // CGame::currArea: 0 outdoors
bool gReady;

int gHunter = -1;
int gSoldier = -1;
bool gLeaving;
uint32_t gLastTime;
uint32_t gLastRound, gLastRockets;
CVector &Row(uint8_t *e, size_t at) { return *reinterpret_cast<CVector *>(e + at); }
CVector Plus(const CVector &a, const CVector &d, float k) { return {a.x + d.x * k, a.y + d.y * k, a.z + d.z * k}; }
CVector Minus(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Length(const CVector &v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
CVector Unit(const CVector &v) { const float l = std::fmax(Length(v), 1e-4f); return {v.x / l, v.y / l, v.z / l}; }
CVector Cross(const CVector &a, const CVector &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
// A point given in its own frame (right, forward, up), in the world.
CVector At(uint8_t *e, const CVector &p)
{
	return Plus(Plus(Plus(Row(e, 0x38), Row(e, 0x08), p.x), Row(e, 0x18), p.y), Row(e, 0x28), p.z);
}
bool Loaded(int model) { return gStreamInfo[size_t(model) * 0x20 + 0x10] == 1; }
uint8_t Status(const uint8_t *e) { return e[0x60] >> 3; }

bool PedCapacity()
{
	const uint8_t *pool=gPedPool?*gPedPool:nullptr;
	if(!pool || !*reinterpret_cast<uint8_t *const *>(pool))return false;
	const uint8_t *flags=*reinterpret_cast<uint8_t *const *>(pool+8);
	const int capacity=*reinterpret_cast<const int32_t *>(pool+0x10);
	if(!flags || capacity<=0 || capacity>100000)return false;
	for(int i=0;i<capacity;++i)if(flags[i]&0x80)return true;
	return false;
}

void Crew(uint8_t *h)
{
	if(gSoldier>=0 || *reinterpret_cast<uint8_t **>(h+0x218))return;
	if(!Loaded(kArmy)){api.RequestModel(kArmy,0);return;}
	if(!PedCapacity())return; // AddPed assumes operator new returned a live pool slot
	uint8_t *ped=gAddPed(kPedTypeCop,kCopTypeArmy,EntityPos(h),0);
	if(!ped)return;
	// Replay SetUpDriver's native ownership/state after explicit Army creation.
	// Hunter has a single native front seat and zero passenger slots.
	gSetDriver(h,ped);
	*reinterpret_cast<uint8_t **>(ped+0x4b8)=h;
	gRegisterReference(h,reinterpret_cast<uint8_t **>(ped+0x4b8));
	ped[0x4c0]=1;SetPedState(ped,50); // PED_DRIVING
	*reinterpret_cast<uint32_t *>(ped+0x60)&=~uint32_t(0x100); // bUsesCollision
	gHideWeapon(ped);gAddInCarAnims(ped,h,true);gPositionInCar(ped);
	gSoldier=gPedRef(ped);
}

int Level(uint8_t *player)
{
	const uint8_t *wanted = *reinterpret_cast<uint8_t *const *>(player + kWanted);
	return wanted ? *reinterpret_cast<const int32_t *>(wanted + kLevel) : 0;
}

CVector Him(uint8_t *player)
{
	return PedInVehicle(player) ? EntityPos(PedVehicle(player)) : Plus(EntityPos(player), {0, 0, 1}, 0.2f);
}

void Spawn(uint8_t *player)
{
	if (!Loaded(kHunter)) {
		api.RequestModel(kHunter, 0);
		return;
	}
	if(!Loaded(kArmy)){api.RequestModel(kArmy,0);return;}
	const CVector t = wanted::Active() ? wanted::Target() : Him(player);
	// From behind the camera: out of his sight as it comes in.
	const float h = CameraHeading() + 3.14159265f;
	const CVector back = HeadingForward(h);
	const CVector at{t.x + back.x * kComeFrom, t.y + back.y * kComeFrom, t.z + kComeUp};
	auto *h8 = static_cast<uint8_t *>(gNewVehicle(kVehicleSize));
	if (!h8)
		return;
	gAutomobile(h8, kHunter, kRandomVehicle);
	const CVector fwd = Unit({t.x - at.x, t.y - at.y, 0.0f});
	Row(h8, 0x08) = {fwd.y, -fwd.x, 0.0f};
	Row(h8, 0x18) = fwd;
	Row(h8, 0x28) = {0.0f, 0.0f, 1.0f};
	Row(h8, 0x38) = at;
	gGoTo(h8, t.x, t.y, t.z + kAbove, kSpeed); // (what HELI_GOTO_COORDS does)
	*reinterpret_cast<float *>(h8 + kRotor) = 0.22f; // (spun up: it flies at once)
	*reinterpret_cast<float *>(h8 + kTurnTo) = -1.0f;
	*reinterpret_cast<float *>(h8 + kHealth) = kToughness;
	gWorldAdd(h8);
	gSoldier=-1;Crew(h8);
	gInteresting(h8); // (kept, however far off: CCarCtrl::PossiblyRemoveVehicle)
	gHunter = gVehicleRef(h8);
	gLeaving = false;
	Log("hunter: up, coming in from %.0f,%.0f", at.x, at.y);
}

void Cannon(uint8_t *h,CVector from,const CVector &target)
{
	const CVector right=Row(h,0x08),forward=Row(h,0x18),up=Row(h,0x28);
	const CVector aim=Unit(Minus(target,from)),r=Unit(Cross(aim,{0,0,1}));
	Row(h,0x08)=r;Row(h,0x18)=aim;Row(h,0x28)=Cross(r,aim);
	alignas(8) uint8_t weapon[0x18];gWeapon(weapon,35,5000); // native WEAPONTYPE_HELICANNON
	uint8_t *savedIgnore=*api.ignoreEntity;
	gFire(weapon,h,&from);
	*api.ignoreEntity=savedIgnore;
	Row(h,0x08)=right;Row(h,0x18)=forward;Row(h,0x28)=up;
}

// Its rockets fly along it (CProjectileInfo::AddProjectile): it's turned at him for them, and back.
void Rockets(uint8_t *h, const CVector &him)
{
	const CVector right = Row(h, 0x08), fwd = Row(h, 0x18), up = Row(h, 0x28);
	const CVector pods[2] = {At(h, kPod), At(h, {-kPod.x, kPod.y, kPod.z})};
	for (const CVector &from : pods) {
		const CVector aim = Unit(Minus(him, from));
		const CVector r = Unit(Cross(aim, {0, 0, 1}));
		Row(h, 0x08) = r;
		Row(h, 0x18) = aim;
		Row(h, 0x28) = Cross(r, aim);
		gAddProjectile(h, kRocket, from, 0.0f);
	}
	Row(h, 0x08) = right;
	Row(h, 0x18) = fwd;
	Row(h, 0x28) = up;
}

void Hunt(uint8_t *h, uint8_t *player, uint32_t now)
{
	const CVector t = wanted::Active() ? wanted::Target() : Him(player);
	const CVector pos = EntityPos(h);
	// Off to one side of him, above, turned to face him.
	CVector side = Unit({pos.x - t.x, pos.y - t.y, 0.0f});
	Row(h, kDestination) = {t.x + side.x * kStandOff, t.y + side.y * kStandOff, t.z + kAbove};
	float a = std::atan2(t.y - pos.y, t.x - pos.x);
	if (a < 0)
		a += 2 * 3.14159265f;
	*reinterpret_cast<float *>(h + kTurnTo) = a + 3.14159265f; // (as SteerAIHeliTowardsTargetCoors reckons it)
	// Its guns, when it can see him and the police know him (not on a look alone).
	if (!wanted::OnHim())
		return;
	const CVector him = Him(player);
	CVector gun = At(h, kGun);
	const float d = Length(Minus(him, gun));
	if (d > kRocketRange || !api.GetIsLineOfSightClear(gun, him, true, false, false, true, false, true, false))
		return;
	const CVector toHim = Unit(Minus(him, gun)), fwd = Row(h, 0x18);
	const bool facing = toHim.x * fwd.x + toHim.y * fwd.y > 0.85f * std::hypot(toHim.x, toHim.y);
	if (facing && d <= kCannonRange && now - gLastRound >= kCannonEvery) {
		gLastRound = now;
		Cannon(h,gun,him);
	}
	if (facing && now - gLastRockets >= kRocketsEvery) {
		gLastRockets = now;
		Rockets(h, him);
	}
}

// How it was lost (#30: Hunters seen crashing at six stars), for the log: its
// health, its rotor, how high it was over him and how far off.
void Lost(const uint8_t *h, const uint8_t *player, const char *why)
{
	const CVector pos = EntityPos(h), him = player ? EntityPos(player) : pos;
	Log("hunter: down, %s (health %.0f of %.0f, rotor %.2f, %.0f m over him, %.0f m off)", why,
	    *reinterpret_cast<const float *>(h + kHealth), kToughness, *reinterpret_cast<const float *>(h + kRotor),
	    pos.z - him.z, std::hypot(pos.x - him.x, pos.y - him.y));
}

} // namespace

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gNewVehicle = reinterpret_cast<decltype(gNewVehicle)>(sym("_ZN8CVehiclenwEm"));
	gAutomobile = reinterpret_cast<decltype(gAutomobile)>(sym("_ZN11CAutomobileC1Eih"));
	gWorldAdd = reinterpret_cast<decltype(gWorldAdd)>(sym("_ZN6CWorld3AddEP7CEntity"));
	gAddPed=reinterpret_cast<decltype(gAddPed)>(sym("_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori"));
	gSetDriver=reinterpret_cast<decltype(gSetDriver)>(sym("_ZN8CVehicle9SetDriverEP4CPed"));
	gRegisterReference=reinterpret_cast<decltype(gRegisterReference)>(sym("_ZN7CEntity17RegisterReferenceEPPS_"));
	gAddInCarAnims=reinterpret_cast<decltype(gAddInCarAnims)>(sym("_ZN4CPed13AddInCarAnimsEP8CVehicleb"));
	gPositionInCar=reinterpret_cast<decltype(gPositionInCar)>(sym("_ZN4CPed19SetPedPositionInCarEv"));
	gHideWeapon=reinterpret_cast<decltype(gHideWeapon)>(sym("_ZN4CPed31RemoveWeaponWhenEnteringVehicleEv"));
	gPedRef=reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gGoTo = reinterpret_cast<decltype(gGoTo)>(sym("_ZN11CAutomobile19TellHeliToGoToCoorsEfffh"));
	gInteresting = reinterpret_cast<decltype(gInteresting)>(sym("_ZN8CCarCtrl25RegisterVehicleOfInterestEP8CVehicle"));
	gNotInteresting =
	    reinterpret_cast<decltype(gNotInteresting)>(sym("_ZN8CCarCtrl32RemoveFromInterestingVehicleListEP8CVehicle"));
	gVehicleRef = reinterpret_cast<decltype(gVehicleRef)>(sym("_ZN6CPools13GetVehicleRefEP8CVehicle"));
	gWeapon=reinterpret_cast<decltype(gWeapon)>(sym("_ZN7CWeaponC1E11eWeaponTypei"));
	gFire=reinterpret_cast<decltype(gFire)>(sym("_ZN7CWeapon4FireEP7CEntityP7CVector"));
	gAddProjectile = reinterpret_cast<decltype(gAddProjectile)>(
	    sym("_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorf"));
	gStreamInfo = static_cast<uint8_t *>(sym("_ZN10CStreaming16ms_aInfoForModelE"));
	gPedPool=static_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gArea = static_cast<int32_t *>(sym("_ZN5CGame8currAreaE"));
	gReady = gNewVehicle && gAutomobile && gWorldAdd && gAddPed && gSetDriver && gRegisterReference && gAddInCarAnims &&
	         gPositionInCar && gHideWeapon && gPedRef && gGoTo && gInteresting && gNotInteresting &&
	         gVehicleRef && gWeapon && gFire && gAddProjectile && gStreamInfo && gPedPool && gArea && api.PoolsGetVehicle &&
	         api.PoolsGetPed && api.RequestModel && api.GetIsLineOfSightClear && api.ignoreEntity;
	if (!gReady)
		Log("hunter: a function is missing; no Hunter at six stars");
	return gReady;
}

void Update(uint32_t now)
{
	if (!gReady || !gConfig.heliGunners)
		return;
	uint8_t *player = api.FindPlayerPed();
	if (!player)
		return;
	if (gLastTime && (now < gLastTime || now - gLastTime > 10000))
		gHunter = gSoldier = -1; // a game loaded: the old game's handle (another vehicle's, maybe)
	gLastTime = now;
	const int level = Level(player);
	uint8_t *h = gHunter >= 0 ? api.PoolsGetVehicle(gHunter) : nullptr;
	if (gHunter >= 0 && !h) {
		gHunter = -1; // (the game's taken it)
		return;
	}
	if (!h) {
		if (level >= 6 && *gArea == 0)
			Spawn(player);
		return;
	}
	uint8_t *driver=*reinterpret_cast<uint8_t **>(h+0x218);
	if(EntityModel(h)!=kHunter || h[0x294]!=kRandomVehicle || h[kMission]!=kHeliFlyTo || driver==player ||
	   (gSoldier>=0 && driver && driver!=api.PoolsGetPed(gSoldier))){
		// A script, another driver or the player has taken ownership.
		gNotInteresting(h);gHunter=gSoldier=-1;return;
	}
	Crew(h);
	driver=*reinterpret_cast<uint8_t **>(h+0x218);
	if(!driver && gSoldier>=0){
		Lost(h, player, "its pilot is out of the seat");
		h[kMission]=0;gNotInteresting(h);gHunter=gSoldier=-1;return;
	}
	if(!driver || gSoldier<0)return; // failed/streaming crew creation retries without another aircraft
	if(PedState(driver)==kPedDie || PedState(driver)==kPedDead || !std::isfinite(Health(driver)) || Health(driver)<=0 ||
	   !PedInVehicle(driver) || PedVehicle(driver)!=h){
		// Stop the direct AI transform controller; native gravity/contact takes over.
		Lost(h, player, Health(driver) <= 0 || PedState(driver) == kPedDie || PedState(driver) == kPedDead
		                    ? "its pilot is dead" : "its pilot left it");
		h[kMission]=0;gNotInteresting(h);gHunter=gSoldier=-1;return;
	}
	if (Status(h) == kWrecked || *reinterpret_cast<const float *>(h + kHealth) <= 0.0f) {
		Lost(h, player, Status(h) == kWrecked ? "wrecked" : "out of health");
		gNotInteresting(h); // (the game clears the wreck away)
		gHunter = -1;
		return;
	}
	if (level < 6 && !gLeaving) {
		gLeaving = true;
		Log("hunter: away (%d stars)", level);
	}
	if (gLeaving) {
		const CVector pos = EntityPos(h), him = Him(player);
		const CVector away = Unit({pos.x - him.x, pos.y - him.y, 0.0f});
		Row(h, kDestination) = {pos.x + away.x * 100.0f, pos.y + away.y * 100.0f, him.z + 120.0f};
		*reinterpret_cast<float *>(h + kTurnTo) = -1.0f;
		if (std::hypot(pos.x - him.x, pos.y - him.y) > kGone) {
			gNotInteresting(h); // far enough: the game removes it
			gHunter = -1;
		}
		return;
	}
	h[kMission] = kHeliFlyTo;
	Hunt(h, player, now);
}

} // namespace hunter
