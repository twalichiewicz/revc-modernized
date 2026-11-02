#include "heli.h"
#include "aircraft.h"
#include "config.h"
#include "game.h"
#include "helizones.h"
#include "hooks.h"
#include "hunter.h"
#include "log.h"
#include "nativebullet.h"
#include "shaders.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <strings.h>
#include <vector>

using namespace game;

namespace heli {
namespace {

// Its open doors, where a shot reaches the gunner kneeling there (its own frame).
constexpr float kDoorSide = 0.6f, kDoorBack = -0.7f, kDoorFront = 1.4f, kDoorLow = -0.7f, kDoorHigh = 1.3f;
// A hit counts this much (the game's 4, four times over); on the rotor's mast,
// where the blades meet the cabin, three times that. What shoots it down at once
// (the pilot dead, the tail rotor gone): more than it can take.
constexpr int32_t kHitDamage = 16, kMastTimes = 3, kDownDamage = 1000;
// Hits the searchlight and the tail rotor take before they go.
constexpr int kLampHits = 1, kTailRotorHits = 6;
// CHeli (macVC 1.0.5217): damage taken (it's shot down at 701, or 400 for
// the lighter kind), its state (3: shot down, spinning); the searchlight's
// spot on the ground (x, y) and how bright it is (0: off, by day).
constexpr size_t kDamage = 0x3f4, kStatus = 0x390, kKind = 0x3b1;
constexpr size_t kSearchLightX = 0x394, kSearchLightY = 0x398, kSearchLight = 0x3ac;
constexpr size_t kBreakupAt = 0x39c, kMoveSpeed = 0x84;
// When it last had the player in its sights: it shoots once that's long enough ago.
constexpr size_t kShootTimer = 0x3ec;
constexpr uint8_t kShotDown = 3, kHeavy = 3;
constexpr int kHelis = 4; // CHeli::pHelis
constexpr int kChopper = 165; // MI_CHOPPER: the police helicopter's model
constexpr uint32_t kPedEntity = 3;         // CEntity's type (bits 0-2 of +0x60)
constexpr size_t kAttachedToPed = 0x720;   // CPed::m_attachedTo
// tParticleType (particle.cfg's order).
constexpr int32_t kSpark = 0, kShard = 25, kSteam = 0x21, kSmoke = 0x39, kBlackSmoke = 0x3a, kFlame = 0x1b,
                  kFlameSmoke = 0x3b, kHeliDebris = 66;
constexpr uint8_t kGlassBreak = 57; // PlayOneShotScriptObject: a small pane breaking
// A shot's CColPoint: the point, the normal, the surfaces and pieces (the
// victim's piece last: a ped's body part, ePedPieceTypes), its depth.
constexpr int kPieceTorso = 0, kPieceHead = 6;
// Its searchlight's corona (id: the helicopter) and the red light blinking on
// its tail (the helicopter + 2), as far off as the game shows them (m).
constexpr float kCoronaReach = 60.0f;

// The beam out of the lamp at night (helizones::LampMesh is the lamp), from
// the lens to the spot on the ground, faint and fading along it.
constexpr int kBeamSides = 14;
constexpr uint8_t kBeamColour[3] = {255, 246, 222};
constexpr float kBeamFar = 4.0f, kBeamNear = 0.1f, kBeamMost = 0.085f; // radius at the spot; its brightest
constexpr float kNightLight = 0.6f; // at night, how far a helicopter sees with its searchlight shot out

uint8_t **gHelis;
bool (*orig_TestBulletCollision)(CVector *from, CVector *to, CVector *at, int32_t damage);
void (*orig_DoBulletImpact)(uint8_t *weapon, uint8_t *shooter, uint8_t *victim, CVector *source, CVector *target,
                            uint8_t *point, CVector2D ahead);
void (*orig_PreRenderAlways)(uint8_t *heli);
void (*orig_Render)(uint8_t *heli);
void *(*gAddParticle)(int32_t type, const CVector &at, const CVector &dir, uint8_t *entity, float size, int32_t spin,
                      int32_t rotation, int32_t frame, int32_t life);
void (*gUpdateCorona)(uint32_t id, const CVector &at, float reach, float angle);
void (*gPlaySound)(uint8_t sound, const CVector &at);
int (*gVehicleRef)(uint8_t *vehicle);
void (*orig_UpdateHelis)();
void (*orig_BulletUpdate)();
uint8_t *gBullets;
void (*gRemoveAndAdd)(uint8_t *);
void (*gUpdateRwFrame)(uint8_t *);
void (*gMatrixUpdate)(void *);
void (*gRemovePed)(uint8_t *);
struct PointLight {
	CVector position, direction;
	float range, red, green, blue;
	uint8_t type, fog, extra, pad;
};
static_assert(sizeof(PointLight) == 0x2c, "macVC CPointLights::aLights stride");
PointLight *gPointLights;
int16_t *gNumPointLights;
void (*gRegisterCorona)(uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, const CVector &, float, float, uint8_t, int8_t,
                         uint8_t, uint8_t, uint8_t, float, bool, float);
bool gPhysicsReady;
uint32_t gFrame;

CVector Row(const uint8_t *entity, size_t at) { return *reinterpret_cast<const CVector *>(entity + at); }
CVector Plus(const CVector &a, const CVector &d, float k) { return {a.x + d.x * k, a.y + d.y * k, a.z + d.z * k}; }
CVector Minus(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
CVector V(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 W(const CVector &v) { return {v.x, v.y, v.z}; }

CVector World(const uint8_t *heli, const CVector &p)
{
	return Plus(Plus(Plus(Row(heli, 0x38), Row(heli, 0x08), p.x), Row(heli, 0x18), p.y), Row(heli, 0x28), p.z);
}
CVector WorldDir(const uint8_t *heli, const CVector &d)
{
	return Plus(Plus(Plus(CVector{0, 0, 0}, Row(heli, 0x08), d.x), Row(heli, 0x18), d.y), Row(heli, 0x28), d.z);
}
CVector Local(const uint8_t *heli, const CVector &w)
{
	const CVector d = Minus(w, Row(heli, 0x38));
	return {Dot(d, Row(heli, 0x08)), Dot(d, Row(heli, 0x18)), Dot(d, Row(heli, 0x28))};
}
CVector LocalDir(const uint8_t *heli, const CVector &d) { return {Dot(d, Row(heli, 0x08)), Dot(d, Row(heli, 0x18)), Dot(d, Row(heli, 0x28))}; }

// --- Its parts: what each helicopter has left of them --------------------------------------

struct Wear {
	int handle = -1; // which helicopter it's for (a pool handle: a new one where an old one was starts afresh)
	int lampHits = 0, rotorHits = 0;
	bool lampOut = false, rotorOut = false;
	helizones::Lamp lamp = helizones::Aim({helizones::kLampPivot.x, 30.0f, -30.0f});
	CVector spot{0, 0, 0}; // where its light is on the ground
	float light = 0.0f;    // how bright it is this frame
	aircraft::Crash crash;
	CVector escape{};
	uint32_t escapeUntil = 0;
	void *tailRotor = nullptr;
	bool rotorLookedUp = false;
	uint32_t arrivalAt = 0;
	uint32_t setupSamples = 0;
	double setupMaxMs = 0.0, setupTotalMs = 0.0;
	bool setupLogged = false;
};
Wear gWear[kHelis];
void CheckTimeline();

int SlotOf(const uint8_t *heli)
{
	for (int i = 0; gHelis && i < kHelis; i++)
		if (gHelis[i] == heli)
			return i;
	return -1;
}
bool Police(const uint8_t *heli) { return EntityModel(heli) == kChopper; }
Wear *WearOf(uint8_t *heli)
{
	CheckTimeline();
	const int slot = SlotOf(heli);
	if (slot < 0 || slot > 1 || !Police(heli) || heli[kKind] != 0 || !gVehicleRef)
		return nullptr;
	Wear &w = gWear[slot];
	const int handle = gVehicleRef(heli);
	if (w.handle != handle) {
		w = Wear{};
		w.handle = handle;
	}
	return &w;
}

uint8_t *PilotOf(const uint8_t *heli); // (the crew, below)
void AimLamp(uint8_t *heli, Wear &w);

helizones::Parts PartsOf(uint8_t *heli, Wear &w)
{
	AimLamp(heli,w); // damage uses the current visible can orientation, including before PreRender
	helizones::Parts p;
	p.lamp = w.lamp.middle;
	p.lampForward = w.lamp.forward;
	p.haveLamp = !w.lampOut;
	p.haveTailRotor = !w.rotorOut;
	p.havePilot = PilotOf(heli) != nullptr;
	return p;
}

// The nearest police helicopter a shot from a to b hits (not through a
// wall), what of it, where, and the way out of it there.
uint8_t *Hit(const CVector &a, const CVector &b, CVector &at, CVector &normal, helizones::Part &part)
{
	const CVector d = Minus(b, a);
	const float len = std::sqrt(Dot(d, d));
	if (len < 1e-3f)
		return nullptr;
	const CVector rd = Plus({0, 0, 0}, d, 1.0f / len);
	uint8_t *hit = nullptr;
	float nearest = len;
	for (int i = 0; i < kHelis; i++) {
		uint8_t *h = gHelis[i];
		Wear *w = h ? WearOf(h) : nullptr;
		if (!w)
			continue;
		const helizones::Hit found = helizones::Trace(W(Local(h, a)), W(LocalDir(h, rd)), nearest, PartsOf(h, *w));
		if (found.part == helizones::Part::kNone || found.t >= nearest)
			continue;
		const CVector p = World(h, V(found.at));
		if (!api.GetIsLineOfSightClear(a, p, true, false, false, true, false, true, false))
			continue;
		hit = h;
		nearest = found.t;
		at = p;
		normal = WorldDir(h, V(found.normal));
		part = found.part;
	}
	return hit;
}

// In through one of its open doors (a gunner kneeling there takes it, not the helicopter).
bool ThroughTheDoor(const uint8_t *heli, const CVector &at)
{
	const CVector p = Local(heli, at);
	return std::fabs(p.x) >= kDoorSide && p.y >= kDoorBack && p.y <= kDoorFront && p.z >= kDoorLow && p.z <= kDoorHigh;
}

int gHits[kHelis];
uint32_t gHitsLogged[kHelis];
const uint8_t *gHitsOn[kHelis]; // (which helicopter the count is for)

void BeginCrash(uint8_t *heli, Wear &w, aircraft::Failure failure)
{
	w.crash.Disable(failure, W(Plus({0,0,0}, Row(heli, kMoveSpeed), 50.0f)));
	heli[kStatus] = kShotDown;
	// CHeli::UpdateHelis otherwise spawns airborne parts seven seconds before this timer.
	*reinterpret_cast<uint32_t *>(heli + kBreakupAt) = *api.timeMs + 15000;
}

// Count only the aircraft the trace actually reached. The original loops over all nearby helis.
void Count(uint8_t *heli, int32_t damage)
{
	int32_t &hurt = *reinterpret_cast<int32_t *>(heli + kDamage);
	hurt += damage;
	// Exact macVC branches: kind 3 fails above 400; the other kinds at 701.
	if ((heli[kKind] == kHeavy ? hurt > 400 : hurt >= 701) && heli[kStatus] != kShotDown) {
		if (Wear *w = WearOf(heli); w && gPhysicsReady)
			BeginCrash(heli, *w, aircraft::Failure::Hull);
		else {
			heli[kStatus] = kShotDown;
			*reinterpret_cast<uint32_t *>(heli + kBreakupAt) = *api.timeMs + 10000;
		}
	}
}

void Burst(int32_t type, const CVector &at, int count, float speed, float size = 0.0f)
{
	for (int i = 0; i < count; i++) {
		const float a = 6.2831853f * float(rand()) / float(RAND_MAX), up = float(rand()) / float(RAND_MAX) - 0.3f;
		gAddParticle(type, at, {std::cos(a) * speed, std::sin(a) * speed, up * speed}, nullptr, size, 0, 0, 0, 0);
	}
}

// The searchlight shot out: its glass in pieces, sparks, and dark from now on.
void LampGoes(uint8_t *heli, Wear &w)
{
	w.lampOut = true;
	const CVector at = World(heli, V(w.lamp.middle));
	Burst(kShard, at, 12, 0.06f);
	Burst(kSpark, at, 8, 0.08f);
	if (gPlaySound)
		gPlaySound(kGlassBreak, at);
	Log("heli: its searchlight shot out");
}

void LampHit(uint8_t *heli,Wear &w)
{
	if(++w.lampHits>=kLampHits)LampGoes(heli,w);
}

// The tail rotor shot away: in pieces, and with nothing to hold it straight it spins down.
void TailRotorGoes(uint8_t *heli, Wear &w)
{
	w.rotorOut = true;
	const CVector at = World(heli, V(helizones::kTailRotor));
	Burst(kHeliDebris, at, 14, 0.12f);
	Burst(kSpark, at, 10, 0.1f);
	gAddParticle(kBlackSmoke, at, {0.0f, 0.0f, 0.03f}, nullptr, 0.0f, 0, 0, 0, 0);
	if (gPhysicsReady)
		BeginCrash(heli, w, aircraft::Failure::Rotor);
	else if (heli[kStatus] != kShotDown)
		Count(heli, kDownDamage);
	Log("heli: its tail rotor shot away");
}

// The game's own hits (anything within 5 m, through walls) don't count; ours do.
bool Hook_TestBulletCollision(CVector *from, CVector *to, CVector *at, int32_t damage)
{
	return !gConfig.heliDamage && orig_TestBulletCollision(from,to,at,damage);
}

// A shot's end: on the helicopter, if it passes it before anything else,
// with the game's hit on a vehicle there; and the hit counted (by the
// game's own count, with its shooting down at the end of it). The rotor's
// mast takes three times as much; the searchlight and the tail rotor go
// after a few hits (the tail rotor shoots it down); a shot through the
// cockpit's glass that finds the pilot is his (the game's hit on a ped).
void Hook_DoBulletImpact(uint8_t *weapon, uint8_t *shooter, uint8_t *victim, CVector *source, CVector *target,
                         uint8_t *point, CVector2D ahead)
{
	if (!gConfig.heliDamage) {
		orig_DoBulletImpact(weapon,shooter,victim,source,target,point,ahead);
		return;
	}
	CVector at, normal;
	helizones::Part part = helizones::Part::kNone;
	uint8_t *heli = source && target && point
	                    ? Hit(*source, victim ? *reinterpret_cast<CVector *>(point) : *target, at, normal, part)
	                    : nullptr;
	// (A gunner the shot reached in through his door is his own hit.)
	if (heli && part == helizones::Part::kBody && victim && (EntityFlags(victim) & 7) == kPedEntity &&
	    *reinterpret_cast<uint8_t *const *>(victim + kAttachedToPed) == heli && ThroughTheDoor(heli, at))
		heli = nullptr;
	if (heli) {
		*reinterpret_cast<CVector *>(point) = at; // CColPoint: point, normal, surfaces and pieces, depth
		*reinterpret_cast<CVector *>(point + 0x10) = normal;
		point[0x20] = point[0x21] = point[0x22] = point[0x23] = 0;
		*reinterpret_cast<float *>(point + 0x24) = 0.0f;
		if (uint8_t *pilot = part == helizones::Part::kPilot ? PilotOf(heli) : nullptr) {
			point[0x23] = Local(heli, at).z > helizones::kPilotChest.z + 0.25f ? kPieceHead : kPieceTorso;
			orig_DoBulletImpact(weapon, shooter, pilot, source, target, point, ahead);
			return;
		}
		victim = heli;
		Wear *w = WearOf(heli);
		if (heli[kStatus] != kShotDown && w) {
			switch (part) {
			case helizones::Part::kLamp:
				LampHit(heli,*w);
				break;
			case helizones::Part::kTailRotor:
				if (++w->rotorHits >= kTailRotorHits)
					TailRotorGoes(heli, *w);
				break;
			default:
				Count(heli, part == helizones::Part::kMast ? kHitDamage * kMastTimes : kHitDamage);
				break;
			}
			// (Now and then, how it's going: for the log.)
			for (int i = 0; i < kHelis; i++)
				if (gHelis[i] == heli) {
					if (gHitsOn[i] != heli)
						gHits[i] = 0;
					gHitsOn[i] = heli;
					gHits[i]++;
					const uint32_t now = *api.timeMs;
					if (now - gHitsLogged[i] > 5000) {
						gHitsLogged[i] = now;
						Log("heli: hit %d times so far (damage %d of %d)", gHits[i],
						    *reinterpret_cast<const int32_t *>(heli + kDamage), heli[kKind] == kHeavy ? 701 : 400);
					}
				}
		}
	}
	orig_DoBulletImpact(weapon, shooter, victim, source, target, point, ahead);
}

// FireSniper's deferred Update damages the world inline and only afterwards
// calls CHeli::TestSniperCollision. DoBulletImpact is never called there, so a
// lamp must consume its bullet before that native pass can hit something behind
// it. Other helicopter parts deliberately retain their native sniper behavior.
void Hook_BulletUpdate()
{
	if(gConfig.heliDamage && gBullets && gHelis && api.FindPlayerPed && api.ProcessLineOfSight &&
	   api.GetIsLineOfSightClear && api.ignoreEntity && api.timeStep && orig_DoBulletImpact){
		uint8_t *player=api.FindPlayerPed();
		const float step=*api.timeStep*.5f;
		for(size_t i=0;player && std::isfinite(step) && step>0 && i<nativebullet::kCount;++i){
			const auto bullet=nativebullet::Read(gBullets,i);
			if(bullet.active!=1 || bullet.creator!=player || (bullet.type!=28 && bullet.type!=29))continue;
			CVector from=V(bullet.position),to=Plus(from,V(bullet.velocity),step),at{},normal{};
			if(!std::isfinite(from.x) || !std::isfinite(from.y) || !std::isfinite(from.z) ||
			   !std::isfinite(to.x) || !std::isfinite(to.y) || !std::isfinite(to.z) ||
			   to.x<=-2400 || to.x>=1600 || std::fabs(to.y)>=2000)continue; // native map bounds
			helizones::Part part=helizones::Part::kNone;
			uint8_t *heli=Hit(from,to,at,normal,part);
			Wear *wear=heli?WearOf(heli):nullptr;
			if(!wear || part!=helizones::Part::kLamp || heli[kStatus]==kShotDown)continue;
			const int handle=wear->handle;
			alignas(16) uint8_t point[0x30]{};uint8_t *victim=nullptr;
			uint8_t *savedIgnore=*api.ignoreEntity;*api.ignoreEntity=player;
			const bool world=api.ProcessLineOfSight(from,to,point,victim,true,true,true,true,true,false,false,true);
			*api.ignoreEntity=savedIgnore;
			// Actual nearer world ownership (including another aircraft) wins.
			// The managed helicopter's own approximate native shape cannot hide
			// its visible can: Hit already resolved its body/rotor/can ordering.
			const CVector worldDelta=Minus(*reinterpret_cast<CVector *>(point),from),lampDelta=Minus(at,from);
			if(world && victim!=heli && Dot(worldDelta,worldDelta)<=Dot(lampDelta,lampDelta))continue;
			const auto current=nativebullet::Read(gBullets,i);
			if(std::memcmp(&current,&bullet,sizeof bullet) || api.FindPlayerPed()!=player ||
			   WearOf(heli)!=wear || wear->handle!=handle || wear->lampOut)continue;
			LampHit(heli,*wear);
			// LampGoes already supplies glass/spark/audio effects. Calling the
			// native vehicle impact here would also InflictDamage on the hull.
			// Effects can mutate the world or recycle a bullet row. Only consume
			// the same record we confirmed; position/damage/lifetime stay native.
			const auto after=nativebullet::Read(gBullets,i);
			if(!std::memcmp(&after,&bullet,sizeof bullet))gBullets[i*nativebullet::kStride+offsetof(nativebullet::Record,active)]=0;
		}
	}
	orig_BulletUpdate();
}

// --- Its parts each frame: the lamp turned to its light, the coronas on the model ---------------

struct Rotor {
	void *atomic = nullptr;
};
void *(*gForAllAtomics)(void *clump, void *(*cb)(void *atomic, void *data), void *data);
void *(*gAtomicFrame)(const void *atomic);
const char *(*gFrameName)(void *frame);
uint32_t (*gAtomicFlags)(const void *atomic);
void (*gSetAtomicFlags)(void *atomic, uint32_t flags);
constexpr uint32_t kAtomicRender = 0x04; // rpATOMICRENDER

void *FindTailRotor(void *atomic, void *data)
{
	void *frame = gAtomicFrame(atomic);
	const char *name = frame ? gFrameName(frame) : nullptr;
	if (name && !strcasecmp(name, "backrotor"))
		static_cast<Rotor *>(data)->atomic = atomic;
	return atomic;
}

// The lamp turned to its spot; its glow (the game's, 7 m out in front of the
// nose) on its lens; the red light (the game's, 9 m behind it) on its tail
// fin; and a tail rotor shot away not drawn.
void AimLamp(uint8_t *heli, Wear &w)
{
	w.light = w.lampOut || w.crash.phase != aircraft::Phase::Flying ? 0.0f :
	    aircraft::Spotlight(shaders::Ready()?shaders::CurrentSky().daylight:
	        std::clamp(std::cos((float(*api.clockHours)+float(*api.clockMinutes)/60-12)*3.14159265f/12)*2+0.3f,0.0f,1.0f),
	        *reinterpret_cast<const float *>(heli + kSearchLight));
	const uint8_t *player = api.FindPlayerPed();
	const float ground = player ? EntityPos(PedInVehicle(player) ? PedVehicle(player) : player).z : Row(heli, 0x38).z - 35.0f;
	w.spot = {*reinterpret_cast<const float *>(heli + kSearchLightX), *reinterpret_cast<const float *>(heli + kSearchLightY),
	          ground};
	w.lamp = helizones::Aim(W(Local(heli, w.spot)));
	const CVector lens = World(heli, V(helizones::Lens(w.lamp))), direction = WorldDir(heli, V(w.lamp.forward));
	// The physical lamp's direction, not an independently aimed line to Tommy.
	const float reach = std::clamp(std::sqrt(Dot(Minus(w.spot,lens),Minus(w.spot,lens))), 5.0f, 110.0f);
	w.spot = Plus(lens, direction, reach);
	if (api.ProcessLineOfSight && w.light>0.001f) {
		alignas(16) uint8_t point[0x30]{};
		uint8_t *entity=nullptr;
		if (api.ProcessLineOfSight(lens,w.spot,point,entity,true,false,false,true,false,false,false,false))
			w.spot=*reinterpret_cast<const CVector *>(point);
	}
}

void Parts(uint8_t *heli, Wear &w)
{
	AimLamp(heli, w);
	const CVector lens = World(heli, V(helizones::Lens(w.lamp))), direction = WorldDir(heli, V(w.lamp.forward));
	if (gRegisterCorona) {
		const uint32_t id = uint32_t(reinterpret_cast<uintptr_t>(heli));
		const uint8_t light = uint8_t(std::clamp(w.light,0.0f,1.0f) * 220);
		gRegisterCorona(id, light, uint8_t(light*0.96f), uint8_t(light*0.87f), 255, lens,
		                0.32f + 0.35f*w.light, kCoronaReach, 0, 2, 0, 0, 0, 0.0f, false, 1.5f);
		gRegisterCorona(id + 2, (*api.timeMs & 0x100) ? 220 : 0, 0, 0, 255,
		                World(heli,V(helizones::kTailFin)), 1.0f, kCoronaReach, 0, 0, 0, 0, 0, 0.0f, false, 1.5f);
	} else if (gUpdateCorona) {
		const uint32_t id = uint32_t(reinterpret_cast<uintptr_t>(heli));
		if (w.light > 0.0f)
			gUpdateCorona(id, lens,
			              kCoronaReach, 0.0f);
		gUpdateCorona(id + 2, World(heli, V(helizones::kTailFin)), kCoronaReach, 0.0f);
	}
	// CPointLights::AddLight rejects every light >22 m from the camera, including a lamp
	// over a normal 35 m police hover. Its verified table supports the actual source/range.
	if (gPointLights && gNumPointLights && *gNumPointLights >= 0 && *gNumPointLights < 64 && w.light > 0.001f &&
	    Dot(Minus(lens,shaders::CameraPosition()),Minus(lens,shaders::CameraPosition())) < 160.0f*160.0f) {
		gPointLights[(*gNumPointLights)++] = {lens,direction,110.0f,w.light*0.9f,w.light*0.86f,w.light*0.78f,1,0,0,0};
	}
	if (w.rotorOut && gForAllAtomics) {
		if (!w.rotorLookedUp) {
			Rotor r;
			if (void *clump = *reinterpret_cast<void *const *>(heli + 0x58)) {
				gForAllAtomics(clump, FindTailRotor, &r);
				w.tailRotor = r.atomic;
				w.rotorLookedUp = true;
			}
		}
		if (w.tailRotor && (gAtomicFlags(w.tailRotor) & kAtomicRender))
			gSetAtomicFlags(w.tailRotor, gAtomicFlags(w.tailRotor) & ~kAtomicRender);
	}
}

// CRTShadows::Render has a separate searchlight generator; PreRenderAlways does
// not own it. Its optimized private push_back takes only Candidate* in x0.
// Exact macVC 1.0.5217 layout and call edges: heli-native-rtshadows.disasm.
struct RtCandidate {
	CVector position;
	float range, intensity;
	CVector direction;
	float directional;
	CVector colour;
	float emissive, sourceRadius, coneCos, volumetric, score;
};
static_assert(sizeof(RtCandidate) == 0x44, "macVC RT candidate stride");
constexpr const char *kRtCandidateSymbol =
    "_ZNSt3__16vectorIZN10CRTShadows6RenderEvE9CandidateNS_9allocatorIS2_EEE9push_backB9nqe210106ERKS2_";
constexpr uint32_t kRtHeliReturn[4] = {0x16a55c, 0x16a6b4, 0x16a80c, 0x16a964};
bool gRtHeliCall[4]{};
void (*orig_RtCandidate)(const RtCandidate *);

void Hook_RtCandidate(const RtCandidate *input)
{
	const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
	const uintptr_t base = reinterpret_cast<uintptr_t>(hooks::ImageBase());
	int slot = -1;
	for (int i = 0; i < 4; ++i)
		if (gRtHeliCall[i] && caller >= base && caller - base == kRtHeliReturn[i]) slot = i;
	uint8_t *h = slot >= 0 && gHelis ? gHelis[slot] : nullptr;
	Wear *w = h && gConfig.heliDamage ? WearOf(h) : nullptr;
	if (!w) {
		orig_RtCandidate(input);
		return;
	}
	AimLamp(h, *w); // use the current matrix even if this render pass preceded PreRenderAlways
	if (w->light <= 0.001f) return;
	RtCandidate corrected = *input;
	corrected.position = World(h, V(helizones::Lens(w->lamp)));
	corrected.direction = WorldDir(h, V(w->lamp.forward));
	corrected.range = 110.0f;
	corrected.intensity = w->light * 3.2f;
	corrected.volumetric = w->light * 0.08f;
	orig_RtCandidate(&corrected);
}

// Smoke from its engine (above the cabin, behind the rotor) as it's hurt. And
// going down, it doesn't shoot (the game's CHeli::ProcessControl goes on
// shooting as it spins): its sights are kept off the player. Its searchlight
// shot out stays dark.
void Hook_PreRenderAlways(uint8_t *heli)
{
	Wear *w = gConfig.heliDamage ? WearOf(heli) : nullptr;
	// The native pass draws its ground shadow and corona from the old virtual light.
	// Replace that pass only for our police helicopter; other heli models retain it.
	if (!w || !gRegisterCorona || !gPointLights || !gNumPointLights)
		orig_PreRenderAlways(heli);
	if (w)
		Parts(heli, *w);
	const bool down = heli[kStatus] == kShotDown;
	if (down)
		*reinterpret_cast<uint32_t *>(heli + kShootTimer) = *api.timeMs;
	if (++gFrame % 2)
		return;
	const float hurt = down ? 1.0f
	                        : static_cast<float>(*reinterpret_cast<const int32_t *>(heli + kDamage)) /
	                              (heli[kKind] == kHeavy ? 400.0f : 701.0f);
	if (w && w->rotorOut) // its tail, where the rotor was
		gAddParticle(kSmoke, World(heli, V(helizones::kTailRotor)), {0.0f, 0.0f, 0.02f}, nullptr, 0.0f, 0, 0, 0, 0);
	if (hurt < 0.2f)
		return;
	const CVector engine = Plus(Plus(Row(heli, 0x38), Row(heli, 0x28), 1.1f), Row(heli, 0x18), -0.8f);
	const CVector rise{0.0f, 0.0f, 0.03f};
	gAddParticle(hurt < 0.45f ? kSteam : hurt < 0.7f ? kSmoke : kBlackSmoke, engine, rise, nullptr, 0.0f, 0, 0, 0, 0);
	if (hurt >= 0.85f) {
		gAddParticle(kFlame, engine, rise, nullptr, 0.5f, 0, 0, 0, 0);
		gAddParticle(kFlameSmoke, engine, rise, nullptr, 0.0f, 0, 0, 0, 0);
	}
}

// --- The lamp, drawn ---------------------------------------------------------------------

struct Im3DVertex { // rw::gl3::Im3DVertex
	float x, y, z, nx, ny, nz;
	uint8_t r, g, b, a;
	float u, v;
};
static_assert(sizeof(Im3DVertex) == 0x24, "librw's Im3D vertex");
void *(*gIm3DTransform)(Im3DVertex *vertices, uint32_t count, float *world, uint32_t flags);
int (*gIm3DRenderIndexed)(int primitive, uint16_t *indices, int count);
int (*gIm3DEnd)();
int (*gStateGet)(int state, void *value);
constexpr uint32_t kIm3DLighting = 0x20; // lit by the scene's lights, as the helicopter is
constexpr int kPrimTriList = 3, kStateCull = 20, kCullNone = 1;

struct Mesh {
	std::vector<Im3DVertex> v;
	std::vector<uint16_t> i;
	void Vertex(Vec3 p, Vec3 n, const uint8_t c[3], uint8_t a = 255)
	{
		v.push_back({p.x, p.y, p.z, n.x, n.y, n.z, c[0], c[1], c[2], a, 0.0f, 0.0f});
	}
	void Tri(int a, int b, int c)
	{
		i.push_back(uint16_t(a));
		i.push_back(uint16_t(b));
		i.push_back(uint16_t(c));
	}
	void Clear()
	{
		v.clear();
		i.clear();
	}
	void Draw(float *world, uint32_t flags)
	{
		if (i.empty())
			return;
		gIm3DTransform(v.data(), uint32_t(v.size()), world, flags);
		gIm3DRenderIndexed(kPrimTriList, i.data(), int(i.size()));
		gIm3DEnd();
	}
};
Mesh gLit, gGlow, gBeam;
helizones::Mesh gLampLit, gLampGlow;

Vec3 Add3(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul3(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 Cross3(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Norm3(Vec3 a)
{
	const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
	return l > 1e-6f ? Mul3(a, 1.0f / l) : Vec3{0, 0, 1};
}

void From(const helizones::Mesh &in, Mesh &out)
{
	out.Clear();
	for (const helizones::MeshVertex &v : in.v) {
		const uint8_t c[3] = {v.r, v.g, v.b};
		out.Vertex(v.p, v.n, c);
	}
	out.i = in.i;
}

// The beam, in the world: from the lens to the spot, faint and fading out along it.
void BuildBeam(const uint8_t *heli, const Wear &w, Mesh &m)
{
	m.Clear();
	const CVector lens = World(heli, V(helizones::Lens(w.lamp)));
	const CVector to = Minus(w.spot, lens);
	const float len = std::sqrt(Dot(to, to));
	if (len < 1.0f)
		return;
	const Vec3 f = Norm3(W(to)), side = Norm3(Cross3(f, std::fabs(f.z) > 0.95f ? Vec3{0, 1, 0} : Vec3{0, 0, 1})),
	           up = Cross3(side, f);
	const float strength = std::clamp(w.light, 0.0f, 1.0f) * kBeamMost;
	uint8_t near[3], far[3] = {0, 0, 0};
	for (int c = 0; c < 3; c++)
		near[c] = uint8_t(kBeamColour[c] * strength);
	for (int k = 0; k < kBeamSides; k++) {
		const float a0 = 6.2831853f * float(k) / kBeamSides, a1 = 6.2831853f * float(k + 1) / kBeamSides;
		const Vec3 r0 = Add3(Mul3(side, std::cos(a0)), Mul3(up, std::sin(a0))), r1 = Add3(Mul3(side, std::cos(a1)), Mul3(up, std::sin(a1)));
		const int base = int(m.v.size());
		const Vec3 l = W(lens), s = W(w.spot);
		m.Vertex(Add3(l, Mul3(r0, kBeamNear)), r0, near);
		m.Vertex(Add3(s, Mul3(r0, kBeamFar)), r0, far);
		m.Vertex(Add3(s, Mul3(r1, kBeamFar)), r1, far);
		m.Vertex(Add3(l, Mul3(r1, kBeamNear)), r1, near);
		m.Tri(base, base + 1, base + 2);
		m.Tri(base, base + 2, base + 3);
	}
}

// After the game has drawn it: the lamp under its chin, lit as it is; at
// night the lens glowing and its beam down to the spot.
void Hook_Render(uint8_t *heli)
{
	orig_Render(heli);
	Wear *w = gConfig.heliDamage && gIm3DTransform ? WearOf(heli) : nullptr;
	if (!w)
		return;
	AimLamp(heli, *w);
	float world[16];
	std::memcpy(world, heli + 0x08, sizeof world); // its CMatrix is an rw::Matrix: right, forward, up, position
	world[3] = world[7] = world[11] = world[15] = 0.0f;
	{
		helizones::LampMesh(w->lamp, w->light > 0.0f, w->lampOut, gLampLit, gLampGlow);
		From(gLampLit, gLit);
		From(gLampGlow, gGlow);
		for (Im3DVertex &v : gGlow.v) {
			v.r = uint8_t(v.r * w->light); v.g = uint8_t(v.g * w->light); v.b = uint8_t(v.b * w->light);
		}
	}
	const shaders::SavedStates saved;
	void *cull = nullptr;
	gStateGet(kStateCull, &cull);
	api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(kCullNone));
	api.RwRenderStateSet(kStateTextureRaster, nullptr);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateVertexAlpha, nullptr);
	gLit.Draw(world, kIm3DLighting);
	gGlow.Draw(world, 0);
	if (w->light > 0.0f && !w->lampOut) {
		BuildBeam(heli, *w, gBeam);
		float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
		api.RwRenderStateSet(kStateZWrite, nullptr);
		api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
		api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendOne));
		api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendOne));
		api.RwRenderStateSet(kStateFog, nullptr);
		gBeam.Draw(identity, 0);
	}
	api.RwRenderStateSet(kStateCull, cull);
}

// --- Gunners (the wanted overhaul) ---------------------------------------------------------

// CHeli: its kind (0: a random police one; CHeli::UpdateHelis), the SWAT it
// has left to rope down (CHeli::SendDownSwat); its states after him (1) and
// flying off (2, then it's deleted high up).
constexpr size_t kHeliType = 0x3b1, kNumSwat = 0x3b3;
constexpr uint8_t kChasing = 1, kFlyingAway = 2;
constexpr int kSwatModel = 2, kCopModel = 1;  // MI_SWAT, MI_COP
constexpr int32_t kCopPed = 6, kCopSwat = 2;  // ePedType, eCopType COP_SWAT
// The pilot: a policeman (the street cop's model) who's no cop to the game
// (a civilian: the police's and the witnesses' own work leave him be),
// sitting in the right-hand seat (CAR_sit), facing ahead, unarmed.
constexpr int32_t kCivMale = 4, kUnarmed = 0;
constexpr uint32_t kSitAnim = 102; // ANIM_STD_CAR_SIT, in the standard group
constexpr int32_t kM4 = 26, kKillOnFoot = 8;  // eWeaponType, eObjective
constexpr size_t kAccuracy = 0x64f;           // CPed::m_wepAccuracy
constexpr size_t kAttachedTo = 0x720;         // CPed::m_attachedTo
// CPed's flags at +0x188: shooting from a crouch (bCrouchWhenShooting), ducking (bIsDucking).
constexpr size_t kPedFlags = 0x188;
constexpr uint64_t kCrouchShooting = 1ull << 35, kDucking = 1ull << 36;
constexpr uint32_t kKneelMs = 60000;
constexpr size_t kWanted = 0x778, kLevel = 0x20;
constexpr int32_t kPedDie = 0x36, kPedDead = 0x37;
// Where they kneel: in its open doors, facing out (CPed::PositionAttachedPed
// turns type 1 a quarter left of its heading, 3 a quarter right), and how far
// they can turn from that (radians). From chopper.dff: the cabin is 0.85 m
// either side of the middle mid-way along it, its floor 0.75 m below the
// model's origin; a ped's origin is 1 m over its feet (and a little more,
// kneeling: their knees went through the floor).
constexpr CVector kDoor[2] = {{-0.6f, 0.3f, 0.4f}, {0.6f, 0.3f, 0.4f}};
constexpr uint16_t kFacing[2] = {1, 3};
constexpr float kTurn = 1.4f;
constexpr uint8_t kGunnersAim = 45;
constexpr float kCrewWithin = 250.0f;        // m from him: they get in
constexpr float kSweep = 30.0f, kSweepRate = 0.3f; // m and rad/s: circling where he was seen
// CHeli::ProcessControl holds it 8 m (hovering) to 12 m (chasing) over him or
// the ground under it: this much more (some 35 m over him).
constexpr float kLift = 25.0f;
constexpr uint32_t kSecondEvery = 15000;

void (*orig_ProcessControl)(uint8_t *heli);
uint8_t *(*gAddPed)(int32_t type, uint32_t copType, const CVector &at, int32_t modifier);
void (*gAttach)(uint8_t *ped, uint8_t *to, CVector offset, uint16_t type, float turn, int32_t weapon);
void (*gDetach)(uint8_t *ped);
uint8_t *(*gGenerateHeli)(bool catalina);
int16_t *gNumRandomHelis;
int (*gPedRef)(uint8_t *ped);
uint8_t *gStreamInfo;
void *(*gBlendAnimation)(void *clump, int32_t group, uint32_t anim, float delta);
void *(*gGetAssociation)(void *clump, uint32_t anim);
void (*gSetBlendDeltas)(void *clump, uint32_t flags, float delta);
void (*gSetBlend)(void *association, float blend, float delta);
void (*gClearObjective)(uint8_t *ped);
void (*gSetDuck)(uint8_t *ped, uint32_t ms, bool anyway);
bool gGunnersReady;
uint32_t gLastTime;
bool gCrewBudget;

struct Crew {
	int heli = -1;
	int ped[2] = {-1, -1};
	bool down[2] = {false, false};
	bool logged = false;
	int pilot = -1;      // (HeliDamage) the pilot in his seat
	bool pilotShot = false;
};
Crew gCrews[2];
// Aboard, a gunner is a mission's (CPed::CanBeDeleted: CPopulation::ManagePopulation
// would cull it as soon as the helicopter's off a way); out, the game's again.
constexpr size_t kCreatedBy = 0x19a;
constexpr uint8_t kRandomChar = 1, kMissionChar = 2;

// Kneeling in the door, the rifle ready (the crouch it shoots from).
void Kneel(uint8_t *ped)
{
	uint64_t &flags = *reinterpret_cast<uint64_t *>(ped + kPedFlags);
	flags |= kCrouchShooting;
	if (!(flags & kDucking))
		gSetDuck(ped, kKneelMs, true);
}

// A gunner out of the door: let fall, and the game's to clear away.
void LetGo(uint8_t *ped)
{
	*reinterpret_cast<uint64_t *>(ped + kPedFlags) &= ~kCrouchShooting;
	if (*reinterpret_cast<void *const *>(ped + kAttachedTo))
		gDetach(ped);
	ped[kCreatedBy] = kRandomChar;
}

// A crew whose helicopter's gone: its gunners let go.
void Disband(Crew &c)
{
	for (int i = 0; i < 2; i++)
		if (uint8_t *ped = c.ped[i] >= 0 ? api.PoolsGetPed(c.ped[i]) : nullptr)
			LetGo(ped);
	if (uint8_t *pilot = c.pilot >= 0 ? api.PoolsGetPed(c.pilot) : nullptr) {
		if (gRemovePed) gRemovePed(pilot);
		else LetGo(pilot);
	}
	c = Crew{};
}
int gSecond = -1; // the second helicopter, ours
uint32_t gLastSecond;

void CheckTimeline()
{
	static uint32_t last;
	const uint32_t now=*api.timeMs;
	if (last && (now<last || now-last>10000)) {
		// Do not detach/delete handles from an old world; their slots may be new mission entities.
		for (Wear &wear:gWear) wear=Wear{};
		for (Crew &crew:gCrews) crew=Crew{};
		gSecond=-1;
		gLastSecond=0;
	}
	last=now;
}

bool Loaded(int model) { return gStreamInfo[size_t(model) * 0x20 + 0x10] == 1; }
int Level();

void WarmModels(uint32_t now)
{
	static uint32_t last;
	if (now>=last && now-last<1000) return;
	last=now;
	// Request before the three-star arrival. Never call LoadAllRequestedModels here.
	if (Level()<2) return;
	api.RequestModel(kChopper,0);
	if (gConfig.heliDamage) api.RequestModel(kCopModel,0);
	if (gConfig.heliGunners) {
		api.RequestModel(kSwatModel,0);
		const uint8_t *weapon=api.GetWeaponInfo(kM4);
		const int model=weapon?*reinterpret_cast<const int32_t *>(weapon+0x54):-1;
		if (model>=0) api.RequestModel(model,0);
	}
}

int Level()
{
	uint8_t *player = api.FindPlayerPed();
	const uint8_t *wanted = player ? *reinterpret_cast<uint8_t *const *>(player + kWanted) : nullptr;
	return wanted ? *reinterpret_cast<const int32_t *>(wanted + kLevel) : 0;
}

bool Down(const uint8_t *ped) { return Health(ped) <= 0.0f || PedState(ped) == kPedDie || PedState(ped) == kPedDead; }

// Sat in his seat (the game's idling would stand him up: it's played again when it's gone).
void Sit(uint8_t *ped)
{
	void *clump = *reinterpret_cast<void **>(ped + 0x58);
	if (!clump || !gBlendAnimation || !gGetAssociation || !gSetBlendDeltas || !gSetBlend) return;
	void *sit=gGetAssociation(clump,kSitAnim);
	if (!sit) sit=gBlendAnimation(clump,0,kSitAnim,1000.0f);
	// AttachPedToEntity initially adds the armed standing pose (animation3). Fading it
	// once on creation is insufficient: CPed idling can bring it back every frame.
	gSetBlendDeltas(clump,0,-12.0f);
	if (sit) gSetBlend(sit,1.0f,0.0f);
}

// The pilot of a police helicopter, alive in his seat (for shots through the glass).
uint8_t *PilotOf(const uint8_t *heli)
{
	for (int slot = 0; slot < 2; slot++) {
		const Crew &c = gCrews[slot];
		if (gHelis[slot] != heli || c.heli != gVehicleRef(gHelis[slot]) || c.pilot < 0 || c.pilotShot)
			continue;
		uint8_t *ped = api.PoolsGetPed(c.pilot);
		if (ped && !Down(ped) && *reinterpret_cast<uint8_t *const *>(ped + kAttachedTo) == heli)
			return ped;
	}
	return nullptr;
}

// One of the game's two police helicopters (not a mission's).
int Slot(const uint8_t *heli) { return heli == gHelis[0] ? 0 : heli == gHelis[1] ? 1 : -1; }

aircraft::Pose FlightPose(uint8_t *h)
{
	return {W(Row(h,0x38)),W(Row(h,0x08)),W(Row(h,0x18)),W(Row(h,0x28))};
}

aircraft::Contact WorldSweep(Vec3 a, Vec3 b, float radius, void *context)
{
	auto *h = static_cast<uint8_t *>(context);
	const CVector from=V(a), to=V(b), delta=Minus(to,from);
	const float length=std::sqrt(Dot(delta,delta));
	aircraft::Contact contact;
	if (length<1e-5f) return contact;
	const CVector direction=Plus({0,0,0},delta,1/length), extended=Plus(to,direction,radius);
	alignas(16) uint8_t point[0x30]{};
	uint8_t *entity=nullptr;
	// The centre ray catches thin walls between samples; offset spheres catch grazing edges.
	uint8_t *ignored=api.ignoreEntity?*api.ignoreEntity:nullptr;
	if (api.ignoreEntity) *api.ignoreEntity=h;
	const bool rayHit=api.ProcessLineOfSight(from,extended,point,entity,true,true,false,true,false,false,false,false);
	if (api.ignoreEntity) *api.ignoreEntity=ignored;
	if (rayHit) {
		CVector normal=*reinterpret_cast<CVector *>(point+0x10);
		if (Dot(delta,normal)>0) normal=Plus({0,0,0},normal,-1);
		const float denom=std::fabs(Dot(delta,normal));
		const float distance=Dot(Minus(*reinterpret_cast<CVector *>(point),from),normal);
		if (denom>1e-5f) {
			const float t=std::clamp((-distance-radius)/denom,0.0f,1.0f);
			contact={true,t,W(normal)};
		}
	}
	float water;
	if (api.GetWaterLevel && delta.z<0 && api.GetWaterLevel(to.x,to.y,to.z,&water,true) && to.z-radius<water) {
		const float t=std::clamp((from.z-radius-water)/-delta.z,0.0f,1.0f);
		if (t<contact.fraction) contact={true,t,{0,0,1}};
	}
	const int steps=std::clamp(int(std::ceil(length/0.4f)),1,24);
	float previous=0;
	for (int i=1;i<=steps;i++) {
		const float t=float(i)/steps;
		if (t>=contact.fraction) break;
		if (api.TestSphereAgainstWorld(Plus(from,delta,t),radius,h,true,true,false,true,false,false)) {
			float low=previous, high=t;
			for (int k=0;k<5;k++) {
				const float mid=(low+high)*0.5f;
				if (api.TestSphereAgainstWorld(Plus(from,delta,mid),radius,h,true,true,false,true,false,false)) high=mid;
				else low=mid;
			}
			contact={true,low,W(Plus({0,0,0},direction,-1))};
			break;
		}
		previous=t;
	}
	return contact;
}

void Place(uint8_t *h, const aircraft::Motion &motion)
{
	*reinterpret_cast<CVector *>(h+0x38)=V(motion.position);
	*reinterpret_cast<CVector *>(h+kMoveSpeed)=Plus({0,0,0},V(motion.velocity),1/50.0f);
	gRemoveAndAdd(h);
	gMatrixUpdate(h+0x08);
	gUpdateRwFrame(h);
}

void Descend(uint8_t *h, Wear &w, float dt)
{
	const aircraft::Motion result=w.crash.Step(FlightPose(h),dt,WorldSweep,h);
	// Keep the intact fuselage spinning with its momentum until physical contact.
	const float angle=dt*(w.crash.cause==aircraft::Failure::Rotor?1.3f:0.45f);
	const CVector right=Row(h,0x08), forward=Row(h,0x18);
	*reinterpret_cast<CVector *>(h+0x08)=Plus(Plus({0,0,0},right,std::cos(angle)),forward,std::sin(angle));
	*reinterpret_cast<CVector *>(h+0x18)=Plus(Plus({0,0,0},forward,std::cos(angle)),right,-std::sin(angle));
	Place(h,result);
	h[kStatus]=kShotDown;
	*reinterpret_cast<uint32_t *>(h+kShootTimer)=*api.timeMs;
	*reinterpret_cast<float *>(h+kSearchLight)=0;
	*reinterpret_cast<uint32_t *>(h+kBreakupAt)=w.crash.phase==aircraft::Phase::Impacted ? *api.timeMs-1 : *api.timeMs+15000;
}

aircraft::Contact PhysicalSweep(Vec3 from, Vec3 to, float navigationRadius, void *context)
{
	return WorldSweep(from, to, std::max(0.1f, navigationRadius - 0.5f), context);
}

void Navigate(uint8_t *h, const aircraft::Pose &before, Wear &w, float dt, int slot)
{
	const Vec3 delta=W(Minus(Row(h,0x38),V(before.position)));
	const Vec3 velocity=W(Plus({0,0,0},Row(h,kMoveSpeed),50));
	// Native ProcessControl integrates speed and banks its matrix before returning.
	// Sweep the resulting orientation from the previous position, then publish our
	// corrected speed after native control on every frame (including an escape).
	aircraft::Pose pose=FlightPose(h);
	pose.position=before.position;
	const bool escaping=*api.timeMs<w.escapeUntil;
	auto impact = [&](Vec3 travel, Vec3 momentum) {
		const aircraft::Motion physical=aircraft::Move(pose,travel,momentum,PhysicalSweep,h);
		if (physical.contact && -Dot(V(momentum),V(physical.normal))>8.0f) {
			BeginCrash(h,w,aircraft::Failure::Hull);
			w.crash.phase=aircraft::Phase::Impacted;
			w.crash.velocity={};
			Place(h,{physical.position,{},physical.normal,true});
			*reinterpret_cast<uint32_t *>(h+kBreakupAt)=*api.timeMs-1;
			*reinterpret_cast<float *>(h+kSearchLight)=0;
			*reinterpret_cast<uint32_t *>(h+kShootTimer)=*api.timeMs;
			Log("heli: hard world impact (%.1f m/s into surface); native breakup enabled at contact",
			    -Dot(V(momentum),V(physical.normal)));
			return true;
		}
		return false;
	};
	if (!escaping && impact(delta,velocity)) return;
	const float speed=std::sqrt(Dot(V(velocity),V(velocity)));
	// Look past the present frame. Waiting for contact leaves a full-speed rotor
	// strike as the first indication of a tall wall that native roof sampling misses.
	const float horizon=std::max(12.0f,speed*1.25f+6.0f);
	const Vec3 ahead=speed>0.01f?W(Plus({0,0,0},V(velocity),horizon/speed)):delta;
	const aircraft::Motion forecast=aircraft::Move(pose,ahead,velocity,WorldSweep,h);
	aircraft::Motion move=aircraft::Move(pose,delta,velocity,WorldSweep,h);
	if (forecast.contact || move.contact || escaping) {
		if (!escaping) {
			w.escape=V(aircraft::Escape(pose,ahead,slot?1:-1,WorldSweep,h,horizon));
			w.escapeUntil=*api.timeMs+1800;
		}
		const float avoidSpeed=std::clamp(speed,4.0f,12.0f);
		const Vec3 escapeDelta=W(Plus({0,0,0},w.escape,avoidSpeed*dt));
		const Vec3 escapeVelocity=W(Plus({0,0,0},w.escape,avoidSpeed));
		if (impact(escapeDelta,escapeVelocity)) return;
		move=aircraft::Move(pose,escapeDelta,escapeVelocity,WorldSweep,h);
	}
	for (int i=0;i<2;i++) {
		uint8_t *other=gHelis[i];
		if (i==slot || !other) continue;
		const aircraft::Motion separated=aircraft::Separate(before.position,move.position,move.velocity,W(Row(other,0x38)),
		    W(Plus({0,0,0},Row(other,kMoveSpeed),50)),dt);
		if (separated.contact)
			move=aircraft::Move(pose,W(Minus(V(separated.position),V(before.position))),separated.velocity,WorldSweep,h);
	}
	Place(h,move);
}

void Hook_UpdateHelis()
{
	CheckTimeline();
	if (!gConfig.heliDamage) {
		for (Wear &w:gWear) w.crash=aircraft::Crash{};
		orig_UpdateHelis();
		return;
	}
	struct Saved { uint8_t *h=nullptr; int handle=-1; } saved[kHelis];
	for (int i=0;i<kHelis;i++) {
		uint8_t *h=gHelis[i];
		Wear *w=h?WearOf(h):nullptr;
		if (!w || !gPhysicsReady) continue;
		if (h[kStatus]==kShotDown && w->crash.phase==aircraft::Phase::Flying)
			BeginCrash(h,*w,aircraft::Failure::Hull);
		if (w->crash.phase==aircraft::Phase::Descending) {
			saved[i]={h,w->handle};
			h[kStatus]=4; // not native shot-down: no timed components; retain its random-heli accounting
		}
	}
	orig_UpdateHelis();
	for (int i=0;i<kHelis;i++)
		if (saved[i].h && gHelis[i]==saved[i].h && gVehicleRef(gHelis[i])==saved[i].handle)
			gHelis[i][kStatus]=kShotDown;
}

// The police helicopter's own work, but with no belly gun (its shot timer
// kept at now: CHeli::ProcessControl fires once it's long enough ago) and
// no SWAT left to rope down; while nobody sees him, after a point circling
// where they think he is (it reads FindPlayerCoors, his car's position or
// his: that's moved there for it, and back). The game sends a second police
// helicopter off at once (CHeli::UpdateHelis wants only one): from 4 stars,
// one the game's sent off while it keeps the other is kept on him.
void Hook_ProcessControl(uint8_t *heli)
{
	const int slot = Slot(heli);
	uint8_t *player = api.FindPlayerPed();
	if (slot < 0 || heli[kHeliType] != 0 || !player) {
		orig_ProcessControl(heli);
		return;
	}
	const uint32_t now = *api.timeMs;
	Wear *wear=gConfig.heliDamage?WearOf(heli):nullptr;
	const float dt=api.timeStep?std::clamp(*api.timeStep/50.0f,0.0f,0.133f):0.02f;
	if (gPhysicsReady && wear) {
		if (heli[kStatus]==kShotDown && wear->crash.phase==aircraft::Phase::Flying)
			BeginCrash(heli,*wear,aircraft::Failure::Hull);
		if (wear->crash.phase!=aircraft::Phase::Flying) {
			Descend(heli,*wear,dt);
			return;
		}
	}
	const aircraft::Pose before=FlightPose(heli);
	uint32_t &shootTimer = *reinterpret_cast<uint32_t *>(heli + kShootTimer);
	const bool gunners = gGunnersReady && gConfig.heliGunners;
	if (gunners) {
		heli[kNumSwat] = 0;
		shootTimer = now;
		const uint8_t *other = gHelis[1 - slot];
		if (heli[kStatus] == kFlyingAway && Level() >= 4 && other && other[kStatus] != kFlyingAway)
			heli[kStatus] = kChasing;
	}
	// Where it flies to (FindPlayerCoors: his car's position, or his): over him,
	// higher than the game has it; or, while nobody sees him, circling where
	// they think he is (and with the game's belly gun, not shooting).
	CVector &at = *reinterpret_cast<CVector *>((PedInVehicle(player) ? PedVehicle(player) : player) + 0x38);
	const CVector real = at;
	const bool unseen = wanted::Active() && !wanted::OnHim();
	if (unseen) {
		const CVector t = wanted::Target();
		const float a = float(now) / 1000.0f * kSweepRate + float(slot) * 3.14159265f;
		at = {t.x + kSweep * std::cos(a), t.y + kSweep * std::sin(a), t.z};
		if (!gunners)
			shootTimer = now;
	}
	at.z += kLift;
	orig_ProcessControl(heli);
	at = real;
	if (gPhysicsReady && wear)
		Navigate(heli,before,*wear,dt,slot);
	if (gunners || unseen)
		shootTimer = now;
	static uint32_t logged[2];
	if (now - logged[slot] > 20000) {
		logged[slot] = now;
		Log("heli: the %s %.0f m over him, %.0f m off (status %d)", slot == 0 ? "first" : "second",
		    EntityPos(heli).z - real.z, std::hypot(EntityPos(heli).x - real.x, EntityPos(heli).y - real.y), heli[kStatus]);
	}
}

// From 4 stars a second police helicopter, every 15 s till there is one.
void Second(uint32_t now)
{
	if (gSecond >= 0 && !api.PoolsGetVehicle(gSecond))
		gSecond = -1;
	if (gSecond >= 0 || Level() < 4 || *gNumRandomHelis != 1 || (gHelis[0] && gHelis[1]) || !(gHelis[0] || gHelis[1]) ||
	    now - gLastSecond < kSecondEvery)
		return;
	if (!Loaded(kChopper)) {
		api.RequestModel(kChopper, 0);
		return;
	}
	gLastSecond = now;
	uint8_t *h = gGenerateHeli(false);
	if (!h)
		return;
	h[kHeliType] = 0;
	gHelis[gHelis[0] ? 1 : 0] = h;
	*gNumRandomHelis = 2;
	gSecond = gVehicleRef(h);
	Log("heli: a second helicopter up (%d stars)", Level());
}

// The pilot in his seat, made as it comes; shot dead through the glass, it goes down.
void Fly(Crew &c, uint8_t *heli, uint8_t *player, bool flying)
{
	uint8_t *ped = c.pilot >= 0 ? api.PoolsGetPed(c.pilot) : nullptr;
	if (c.pilot >= 0 && !ped)
		c.pilot = -1;
	if (ped) {
		if (Down(ped) && !c.pilotShot) {
			c.pilotShot = true;
			if (Wear *w=WearOf(heli); w && gPhysicsReady)
				BeginCrash(heli,*w,aircraft::Failure::Pilot);
			else if (heli[kStatus] != kShotDown)
				Count(heli, kDownDamage);
			// A dead attached CPed runs the ordinary prone death animation across the hull.
			// Remove that representation once; retain pilotShot so it can never respawn.
			if (gRemovePed) { gRemovePed(ped); c.pilot=-1; }
			Log("heli: its pilot shot dead; it goes down");
		} else if (!Down(ped)) {
			Sit(ped);
			if (PedObjective(ped) != 0)
				gClearObjective(ped);
		}
		return;
	}
	if (!gCrewBudget || c.pilotShot || !flying ||
	    std::hypot(EntityPos(heli).x - EntityPos(player).x, EntityPos(heli).y - EntityPos(player).y) > kCrewWithin)
		return;
	if (!Loaded(kCopModel)) {
		api.RequestModel(kCopModel, 0);
		return;
	}
	ped = gAddPed(kCivMale, kCopModel, EntityPos(heli), 0);
	if (!ped)
		return;
	gCrewBudget=false;
	ped[kCreatedBy] = kMissionChar;
	gAttach(ped, heli, V(helizones::kPilot), 0, 0.0f, kUnarmed);
	Sit(ped);
	c.pilot = gPedRef(ped);
	Log("heli: a pilot at the controls (%s)", gHelis[0] == heli ? "the first" : "the second");
}

// Two SWAT in each police helicopter's doors, made as it comes (fresh ones in
// the next); one shot falls out, and isn't replaced. (And its pilot.)
void Man(int slot, uint8_t *player, bool gunners, bool pilot)
{
	Crew &c = gCrews[slot];
	uint8_t *heli = gHelis[slot];
	const int handle = heli ? gVehicleRef(heli) : -1;
	if (handle != c.heli) {
		Disband(c);
		c.heli = handle;
	}
	if (!heli || heli[kHeliType] != 0)
		return;
	const bool shotDown = heli[kStatus] == kShotDown;
	const bool flying = !shotDown && heli[kStatus] != kFlyingAway;
	if (pilot)
		Fly(c, heli, player, flying);
	else if (c.pilot>=0) {
		if (uint8_t *p=api.PoolsGetPed(c.pilot); p && gRemovePed) gRemovePed(p);
		c.pilot=-1;
	}
	if (!gunners) {
		for (int i=0;i<2;i++) {
			if (uint8_t *p=c.ped[i]>=0?api.PoolsGetPed(c.ped[i]):nullptr) LetGo(p);
			c.ped[i]=-1;
		}
		return;
	}
	const uint8_t *weapon = api.GetWeaponInfo(kM4);
	const int gunModel = weapon ? *reinterpret_cast<const int32_t *>(weapon + 0x54) : -1;
	int made = 0;
	for (int i = 0; i < 2; i++) {
		uint8_t *ped = c.ped[i] >= 0 ? api.PoolsGetPed(c.ped[i]) : nullptr;
		if (c.ped[i] >= 0 && !ped)
			c.ped[i] = -1; // gone (not shot): another gets in
		if (ped) {
			made++;
			const bool down = Down(ped);
			if (down || shotDown) { // out of the door
				LetGo(ped);
				c.ped[i] = -1;
				c.down[i] = true;
				if (down)
					Log("heli: a gunner down (%d left)", int(!c.down[0]) + int(!c.down[1]));
				continue;
			}
			Kneel(ped);
			if (!wanted::OnHim()) { // (they shoot only once the police know him)
				if (PedObjective(ped) == kKillOnFoot)
					gClearObjective(ped);
			} else if (PedObjective(ped) != kKillOnFoot) {
				api.SetObjective(ped, kKillOnFoot, player);
			}
			continue;
		}
		if (!gCrewBudget || c.down[i] || !flying || std::hypot(EntityPos(heli).x - EntityPos(player).x,
		                                       EntityPos(heli).y - EntityPos(player).y) > kCrewWithin)
			continue;
		if (!Loaded(kSwatModel) || (gunModel >= 0 && !Loaded(gunModel))) {
			api.RequestModel(kSwatModel, 0);
			if (gunModel >= 0)
				api.RequestModel(gunModel, 0);
			continue;
		}
		ped = gAddPed(kCopPed, kCopSwat, EntityPos(heli), 0);
		if (!ped)
			continue;
		gCrewBudget=false;
		ped[kCreatedBy] = kMissionChar;
		gAttach(ped, heli, kDoor[i], kFacing[i], kTurn, kM4);
		ped[kAccuracy] = kGunnersAim;
		Kneel(ped);
		if (wanted::OnHim())
			api.SetObjective(ped, kKillOnFoot, player);
		c.ped[i] = gPedRef(ped);
		made++;
	}
	if (made == 2 && !c.logged) {
		c.logged = true;
		Log("heli: gunners aboard (%s)", slot == 0 ? "the first" : "the second");
	}
}
} // namespace

bool FailRotor(uint8_t *vehicle,bool main)
{
	if(!vehicle || !gConfig.heliDamage)return false;
	Wear *w=WearOf(vehicle);
	if(!w)return false;
	if(!main){
		if(!w->rotorOut){
			if(w->crash.phase==aircraft::Phase::Flying)TailRotorGoes(vehicle,*w);
			else w->rotorOut=true; // later part damage cannot postpone a real impact breakup
		}
	}else if(w->crash.phase==aircraft::Phase::Flying){
		if(gPhysicsReady)BeginCrash(vehicle,*w,aircraft::Failure::Rotor);
		else Count(vehicle,kDownDamage);
	}
	return true;
}

bool Install()
{
	const auto warmStart=std::chrono::steady_clock::now();
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gHelis = static_cast<uint8_t **>(sym("_ZN5CHeli6pHelisE"));
	gAddPed = reinterpret_cast<decltype(gAddPed)>(sym("_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori"));
	gAttach = reinterpret_cast<decltype(gAttach)>(sym("_ZN4CPed17AttachPedToEntityEP7CEntity7CVectortf11eWeaponType"));
	gDetach = reinterpret_cast<decltype(gDetach)>(sym("_ZN4CPed20DettachPedFromEntityEv"));
	gGenerateHeli = reinterpret_cast<decltype(gGenerateHeli)>(sym("_ZN5CHeli12GenerateHeliEb"));
	gNumRandomHelis = static_cast<int16_t *>(sym("_ZN5CHeli14NumRandomHelisE"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gVehicleRef = reinterpret_cast<decltype(gVehicleRef)>(sym("_ZN6CPools13GetVehicleRefEP8CVehicle"));
	gStreamInfo = static_cast<uint8_t *>(sym("_ZN10CStreaming16ms_aInfoForModelE"));
	gClearObjective = reinterpret_cast<decltype(gClearObjective)>(sym("_ZN4CPed14ClearObjectiveEv"));
	gSetDuck = reinterpret_cast<decltype(gSetDuck)>(sym("_ZN4CPed7SetDuckEjb"));
	gBlendAnimation = reinterpret_cast<decltype(gBlendAnimation)>(
	    sym("_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf"));
	gGetAssociation = reinterpret_cast<decltype(gGetAssociation)>(sym("_Z30RpAnimBlendClumpGetAssociationPN2rw5ClumpEj"));
	gSetBlendDeltas=reinterpret_cast<decltype(gSetBlendDeltas)>(sym("_Z30RpAnimBlendClumpSetBlendDeltasPN2rw5ClumpEjf"));
	gSetBlend=reinterpret_cast<decltype(gSetBlend)>(sym("_ZN21CAnimBlendAssociation8SetBlendEff"));
	gRemovePed=reinterpret_cast<decltype(gRemovePed)>(sym("_ZN11CPopulation9RemovePedEP4CPed"));
	// The hook (searching where he was seen) whatever HeliGunners says; the gunners need the rest.
	const bool hooked =
	    gHelis && hooks::Install("_ZN5CHeli14ProcessControlEv", Hook_ProcessControl, orig_ProcessControl);
	gGunnersReady = hooked && gAddPed && gAttach && gDetach && gGenerateHeli && gNumRandomHelis && gPedRef &&
	                gVehicleRef && gStreamInfo && gClearObjective && gSetDuck && api.PoolsGetPed && api.PoolsGetVehicle &&
	                api.SetObjective && api.GetWeaponInfo && api.RequestModel;
	if (!hooked)
		Log("heli: CHeli::ProcessControl's hook is missing (re-run install.sh): it flies at him as the game has it");
	else if (!gGunnersReady)
		Log("heli: no gunners (a function missing): the belly gun as the game has it");
	if (!gConfig.heliDamage)
		return true;
	gRemoveAndAdd=reinterpret_cast<decltype(gRemoveAndAdd)>(sym("_ZN9CPhysical12RemoveAndAddEv"));
	gUpdateRwFrame=reinterpret_cast<decltype(gUpdateRwFrame)>(sym("_ZN7CEntity13UpdateRwFrameEv"));
	gMatrixUpdate=reinterpret_cast<decltype(gMatrixUpdate)>(sym("_ZN7CMatrix8UpdateRWEv"));
	const bool breakupHook=gHelis && hooks::Install("_ZN5CHeli11UpdateHelisEv",Hook_UpdateHelis,orig_UpdateHelis);
	gPhysicsReady=hooked && breakupHook && gRemoveAndAdd && gUpdateRwFrame && gMatrixUpdate && gVehicleRef &&
	              api.ProcessLineOfSight && api.TestSphereAgainstWorld && api.timeStep;
	if (!gPhysicsReady)
		Log("heli: physical clearance/descent unavailable; native flight/crash retained (missing hook or collision API)");
	gAddParticle = reinterpret_cast<decltype(gAddParticle)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfiiii"));
	gUpdateCorona = reinterpret_cast<decltype(gUpdateCorona)>(sym("_ZN8CCoronas17UpdateCoronaCoorsEjRK7CVectorff"));
	gRegisterCorona=reinterpret_cast<decltype(gRegisterCorona)>(sym("_ZN8CCoronas14RegisterCoronaEjhhhhRK7CVectorffhahhhfbf"));
	for (int i=0;i<4;++i)
		gRtHeliCall[i]=hooks::IsCallTo(kRtHeliReturn[i],kRtCandidateSymbol);
	if (!hooks::Install(kRtCandidateSymbol,Hook_RtCandidate,orig_RtCandidate))
		Log("heli: native RT searchlight source hook missing (re-run install.sh)");
	gPointLights=static_cast<PointLight *>(sym("_ZN12CPointLights7aLightsE"));
	gNumPointLights=static_cast<int16_t *>(sym("_ZN12CPointLights9NumLightsE"));
	gPlaySound = reinterpret_cast<decltype(gPlaySound)>(sym("_Z23PlayOneShotScriptObjecthRK7CVector"));
	gForAllAtomics = reinterpret_cast<decltype(gForAllAtomics)>(sym("_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_"));
	gAtomicFrame = reinterpret_cast<decltype(gAtomicFrame)>(sym("_Z16RpAtomicGetFramePKN2rw6AtomicE"));
	gFrameName = reinterpret_cast<decltype(gFrameName)>(sym("_Z16GetFrameNodeNamePN2rw5FrameE"));
	gAtomicFlags = reinterpret_cast<decltype(gAtomicFlags)>(sym("_Z16RpAtomicGetFlagsPKN2rw6AtomicE"));
	gSetAtomicFlags = reinterpret_cast<decltype(gSetAtomicFlags)>(sym("_Z16RpAtomicSetFlagsPN2rw6AtomicEj"));
	if (!(gForAllAtomics && gAtomicFrame && gFrameName && gAtomicFlags && gSetAtomicFlags))
		gForAllAtomics = nullptr; // (its tail rotor shot away stays drawn)
	if (!gHelis || !gAddParticle || !gVehicleRef)
		return false;
	// Our hits before the game's are turned off: without them it couldn't be shot down.
	const bool hits = hooks::Install("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D",
	                                 Hook_DoBulletImpact, orig_DoBulletImpact) &&
	                  hooks::Install("_ZN5CHeli19TestBulletCollisionEP7CVectorS1_S1_i", Hook_TestBulletCollision,
	                                 orig_TestBulletCollision) &&
	                  hooks::Install("_ZN5CHeli15PreRenderAlwaysEv", Hook_PreRenderAlways, orig_PreRenderAlways);
	if (!hits)
		return false;
	gBullets=static_cast<uint8_t *>(sym("gaBulletInfo"));
	if(gBullets)orig_BulletUpdate=reinterpret_cast<decltype(orig_BulletUpdate)>(hooks::Chain("_ZN11CBulletInfo6UpdateEv",reinterpret_cast<void *>(Hook_BulletUpdate)));
	if(!orig_BulletUpdate)Log("heli: deferred sniper lamp collision unavailable (missing bullet Update hook)");
	// Its searchlight drawn under its chin (without it, the game's light from 7 m in front of it).
	gIm3DTransform = reinterpret_cast<decltype(gIm3DTransform)>(sym("_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj"));
	gIm3DRenderIndexed = reinterpret_cast<decltype(gIm3DRenderIndexed)>(sym("_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti"));
	gIm3DEnd = reinterpret_cast<decltype(gIm3DEnd)>(sym("_Z9RwIm3DEndv"));
	gStateGet = reinterpret_cast<decltype(gStateGet)>(sym("_Z16RwRenderStateGet13RwRenderStatePv"));
	if (!gIm3DTransform || !gIm3DRenderIndexed || !gIm3DEnd || !gStateGet ||
	    !hooks::Install("_ZN5CHeli6RenderEv", Hook_Render, orig_Render)) {
		gIm3DTransform = nullptr;
		Log("heli: CHeli::Render's hook is missing (re-run install.sh): no searchlight drawn under it");
	}
	// Allocate immediate-mode storage before any aircraft arrives, including the off/broken lens cases.
	gLit.v.reserve(512); gLit.i.reserve(1024); gGlow.v.reserve(64); gGlow.i.reserve(128);
	gBeam.v.reserve(kBeamSides*4); gBeam.i.reserve(kBeamSides*6);
	for (int state=0;state<3;state++)
		helizones::LampMesh(helizones::Aim({0,30,-30}),state==0,state==2,gLampLit,gLampGlow);
	Log("heli: CPU mesh warm-up %.3f ms; assets requested asynchronously from two stars; one new crew ped per frame",
	    std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count());
	return true;
}

void Update()
{
	const bool gunners = gConfig.heliGunners;
	const bool pilots = gConfig.heliDamage && orig_DoBulletImpact && gBlendAnimation && gGetAssociation &&
	                    gSetBlendDeltas && gSetBlend && gRemovePed;
	if (!gGunnersReady)
		return;
	if (!gunners && !pilots) {
		Disband(gCrews[0]); Disband(gCrews[1]);
		return;
	}
	uint8_t *player = api.FindPlayerPed();
	if (!player)
		return;
	const uint32_t now = *api.timeMs;
	CheckTimeline();
	if (gLastTime && (now < gLastTime || now - gLastTime > 10000)) { // a game loaded: the old game's handles
		// (Not let go: after a load an old handle can name a new ped, a mission's even.)
		gCrews[0] = gCrews[1] = Crew{};
		gSecond = -1;
	}
	gLastTime = now;
	WarmModels(now);
	gCrewBudget=true;
	if (gunners)
		Second(*api.timeMs);
	for (int slot = 0; slot < 2; slot++) {
		const auto start=std::chrono::steady_clock::now();
		Man(slot, player, gunners, pilots);
		if (uint8_t *h=gHelis[slot]) {
			if (Wear *w=WearOf(h)) {
				if (!w->arrivalAt) w->arrivalAt=now;
				if (!w->setupLogged) {
					const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
					w->setupSamples++; w->setupTotalMs+=ms; w->setupMaxMs=std::max(w->setupMaxMs,ms);
					if (now-w->arrivalAt>5000) {
						w->setupLogged=true;
						Log("heli: arrival crew CPU max %.3f ms mean %.3f ms over %u updates (whole-frame/render time unmeasured)",
						    w->setupMaxMs,w->setupTotalMs/w->setupSamples,w->setupSamples);
					}
				}
			}
		}
	}
	if (gunners)
		hunter::Update(*api.timeMs);
}

bool LightOut(int slot)
{
	return slot >= 0 && slot < kHelis && gHelis && gHelis[slot] && gWear[slot].handle >= 0 && gWear[slot].lampOut &&
	       gVehicleRef && gVehicleRef(gHelis[slot]) == gWear[slot].handle;
}

float SightAtNight() { return kNightLight; }

bool Gunners(int slot, int &alive)
{
	alive = 0;
	if (slot < 0 || slot > 1 || !gHelis || !gHelis[slot] || !gVehicleRef ||
	    gCrews[slot].heli < 0 || gCrews[slot].heli != gVehicleRef(gHelis[slot]))
		return false;
	const Crew &c = gCrews[slot];
	bool any = false;
	for (int i = 0; i < 2; i++) {
		any |= c.ped[i] >= 0 || c.down[i];
		uint8_t *ped=c.ped[i]>=0?api.PoolsGetPed(c.ped[i]):nullptr;
		alive += ped && !c.down[i] && !Down(ped) && *reinterpret_cast<uint8_t *const *>(ped+kAttachedTo)==gHelis[slot];
	}
	return any;
}

} // namespace heli
