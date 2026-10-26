#include "heli.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "hunter.h"
#include "log.h"
#include "wanted.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace heli {
namespace {

// Its body, in its own frame (x right, y forward, z up; from chopper.dff):
// the cabin and the tail boom, each a capsule (a segment and a radius).
struct Part {
	CVector a, b;
	float r;
};
constexpr Part kParts[] = {{{0.0f, 2.6f, 0.35f}, {0.0f, -1.6f, 0.35f}, 1.15f}, {{0.0f, -2.0f, 1.0f}, {0.0f, -7.4f, 1.3f}, 0.55f}};
// Its open doors, where a shot reaches the gunner kneeling there (its own frame).
constexpr float kDoorSide = 0.6f, kDoorBack = -0.7f, kDoorFront = 1.4f, kDoorLow = -0.7f, kDoorHigh = 1.3f;
// A hit counts this much (the game's 4, four times over).
constexpr int32_t kHitDamage = 16;
// CHeli (macVC 1.0.5217): damage taken (it's shot down at 701, or 400 for
// the lighter kind), its state (3: shot down, spinning).
constexpr size_t kDamage = 0x3f4, kStatus = 0x390, kKind = 0x3b1;
// When it last had the player in its sights: it shoots once that's long enough ago.
constexpr size_t kShootTimer = 0x3ec;
constexpr uint8_t kShotDown = 3, kHeavy = 3;
constexpr int kHelis = 4; // CHeli::pHelis
constexpr uint32_t kPedEntity = 3;         // CEntity's type (bits 0-2 of +0x60)
constexpr size_t kAttachedToPed = 0x720;   // CPed::m_attachedTo
// tParticleType.
constexpr int32_t kSteam = 0x21, kSmoke = 0x39, kBlackSmoke = 0x3a, kFlame = 0x1b, kFlameSmoke = 0x3b;

uint8_t **gHelis;
bool (*orig_TestBulletCollision)(CVector *from, CVector *to, CVector *at, int32_t damage);
void (*orig_DoBulletImpact)(uint8_t *weapon, uint8_t *shooter, uint8_t *victim, CVector *source, CVector *target,
                            uint8_t *point, CVector2D ahead);
void (*orig_PreRenderAlways)(uint8_t *heli);
void *(*gAddParticle)(int32_t type, const CVector &at, const CVector &dir, uint8_t *entity, float size, int32_t spin,
                      int32_t rotation, int32_t frame, int32_t life);
bool gCounting; // our own call of TestBulletCollision
uint32_t gFrame;

CVector Row(const uint8_t *entity, size_t at) { return *reinterpret_cast<const CVector *>(entity + at); }
CVector Plus(const CVector &a, const CVector &d, float k) { return {a.x + d.x * k, a.y + d.y * k, a.z + d.z * k}; }
CVector Minus(const CVector &a, const CVector &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(const CVector &a, const CVector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

CVector World(const uint8_t *heli, const CVector &p)
{
	return Plus(Plus(Plus(Row(heli, 0x38), Row(heli, 0x08), p.x), Row(heli, 0x18), p.y), Row(heli, 0x28), p.z);
}
CVector Local(const uint8_t *heli, const CVector &w)
{
	const CVector d = Minus(w, Row(heli, 0x38));
	return {Dot(d, Row(heli, 0x08)), Dot(d, Row(heli, 0x18)), Dot(d, Row(heli, 0x28))};
}
CVector Middle(const uint8_t *heli) { return World(heli, Plus(kParts[0].a, Minus(kParts[0].b, kParts[0].a), 0.5f)); }

// The nearest point to p on the segment a-b.
CVector Nearest(const CVector &a, const CVector &b, const CVector &p)
{
	const CVector ab = Minus(b, a);
	const float t = std::clamp(Dot(Minus(p, a), ab) / std::fmax(Dot(ab, ab), 1e-6f), 0.0f, 1.0f);
	return Plus(a, ab, t);
}

// How far along the ray from `ro` (unit `rd`) it first meets the capsule (a,
// b, r): < 0 if it doesn't, or starts inside it (one of its own gunners).
float Meets(const CVector &ro, const CVector &rd, const CVector &a, const CVector &b, float r)
{
	const CVector off = Minus(ro, Nearest(a, b, ro));
	if (Dot(off, off) <= r * r)
		return -1.0f;
	const CVector ba = Minus(b, a), oa = Minus(ro, a);
	const float baba = Dot(ba, ba), bard = Dot(ba, rd), baoa = Dot(ba, oa), rdoa = Dot(rd, oa), oaoa = Dot(oa, oa);
	const float qa = baba - bard * bard, qb = baba * rdoa - baoa * bard, qc = baba * oaoa - baoa * baoa - r * r * baba;
	float h = qb * qb - qa * qc;
	if (h >= 0.0f && qa > 1e-6f) {
		const float t = (-qb - std::sqrt(h)) / qa, y = baoa + t * bard;
		if (y > 0.0f && y < baba)
			return t;
		const CVector oc = y <= 0.0f ? oa : Minus(ro, b); // its rounded ends
		const float cb = Dot(rd, oc), cc = Dot(oc, oc) - r * r;
		h = cb * cb - cc;
		if (h > 0.0f)
			return -cb - std::sqrt(h);
	}
	return -1.0f;
}

// The nearest heli a shot from a to b hits (not through a wall), where, and
// the way out from its body there.
uint8_t *Hit(const CVector &a, const CVector &b, CVector &at, CVector &normal)
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
		if (!h)
			continue;
		for (const Part &part : kParts) {
			const CVector pa = World(h, part.a), pb = World(h, part.b);
			const float t = Meets(a, rd, pa, pb, part.r);
			if (t < 0.0f || t >= nearest)
				continue;
			const CVector p = Plus(a, rd, t);
			if (!api.GetIsLineOfSightClear(a, p, true, false, false, true, false, true, false))
				continue;
			hit = h;
			nearest = t;
			at = p;
			const CVector out = Minus(p, Nearest(pa, pb, p));
			const float l = std::sqrt(Dot(out, out));
			normal = l > 1e-4f ? Plus({0, 0, 0}, out, 1.0f / l) : CVector{0, 0, 1};
		}
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

// The game's own hits (anything within 5 m, through walls) don't count; ours do.
bool Hook_TestBulletCollision(CVector *from, CVector *to, CVector *at, int32_t damage)
{
	return gCounting && orig_TestBulletCollision(from, to, at, damage);
}

// A shot's end: on the heli's body, if it passes through it before anything
// else, with the game's hit on a vehicle there; and the hit counted (by the
// game's own count, with its shooting down at the end of it).
void Hook_DoBulletImpact(uint8_t *weapon, uint8_t *shooter, uint8_t *victim, CVector *source, CVector *target,
                         uint8_t *point, CVector2D ahead)
{
	CVector at, normal;
	uint8_t *heli = source && target && point
	                    ? Hit(*source, victim ? *reinterpret_cast<CVector *>(point) : *target, at, normal)
	                    : nullptr;
	// (A gunner the shot reached in through his door is his own hit.)
	if (heli && victim && (EntityFlags(victim) & 7) == kPedEntity &&
	    *reinterpret_cast<uint8_t *const *>(victim + kAttachedToPed) == heli && ThroughTheDoor(heli, at))
		heli = nullptr;
	if (heli) {
		CVector middle = Middle(heli);
		*reinterpret_cast<CVector *>(point) = at; // CColPoint: point, normal, surfaces and pieces, depth
		*reinterpret_cast<CVector *>(point + 0x10) = normal;
		point[0x20] = point[0x21] = point[0x22] = point[0x23] = 0;
		*reinterpret_cast<float *>(point + 0x24) = 0.0f;
		victim = heli;
		if (heli[kStatus] != kShotDown) {
			gCounting = true;
			CVector where;
			orig_TestBulletCollision(&at, &middle, &where, kHitDamage);
			gCounting = false;
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

// Smoke from its engine (above the cabin, behind the rotor) as it's hurt. And
// going down, it doesn't shoot (the game's CHeli::ProcessControl goes on
// shooting as it spins): its sights are kept off the player.
void Hook_PreRenderAlways(uint8_t *heli)
{
	orig_PreRenderAlways(heli);
	const bool down = heli[kStatus] == kShotDown;
	if (down)
		*reinterpret_cast<uint32_t *>(heli + kShootTimer) = *api.timeMs;
	if (++gFrame % 2)
		return;
	const float hurt = down ? 1.0f
	                        : static_cast<float>(*reinterpret_cast<const int32_t *>(heli + kDamage)) /
	                              (heli[kKind] == kHeavy ? 701.0f : 400.0f);
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


// --- Gunners (the wanted overhaul) ---------------------------------------------------------

// CHeli: its kind (0: a random police one; CHeli::UpdateHelis), the SWAT it
// has left to rope down (CHeli::SendDownSwat); its states after him (1) and
// flying off (2, then it's deleted high up).
constexpr size_t kHeliType = 0x3b1, kNumSwat = 0x3b3;
constexpr uint8_t kChasing = 1, kFlyingAway = 2;
constexpr int kChopper = 165, kSwatModel = 2; // MI_CHOPPER, MI_SWAT
constexpr int32_t kCopPed = 6, kCopSwat = 2;  // ePedType, eCopType COP_SWAT
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
int (*gVehicleRef)(uint8_t *vehicle);
uint8_t *gStreamInfo;
void (*gClearObjective)(uint8_t *ped);
void (*gSetDuck)(uint8_t *ped, uint32_t ms, bool anyway);
bool gGunnersReady;
uint32_t gLastTime;

struct Crew {
	int heli = -1;
	int ped[2] = {-1, -1};
	bool down[2] = {false, false};
	bool logged = false;
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
	c = Crew{};
}
int gSecond = -1; // the second helicopter, ours
uint32_t gLastSecond;

bool Loaded(int model) { return gStreamInfo[size_t(model) * 0x20 + 0x10] == 1; }

int Level()
{
	uint8_t *player = api.FindPlayerPed();
	const uint8_t *wanted = player ? *reinterpret_cast<uint8_t *const *>(player + kWanted) : nullptr;
	return wanted ? *reinterpret_cast<const int32_t *>(wanted + kLevel) : 0;
}

bool Down(const uint8_t *ped) { return Health(ped) <= 0.0f || PedState(ped) == kPedDie || PedState(ped) == kPedDead; }

// One of the game's two police helicopters (not a mission's).
int Slot(const uint8_t *heli) { return heli == gHelis[0] ? 0 : heli == gHelis[1] ? 1 : -1; }

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

// Two SWAT in each police helicopter's doors, made as it comes (fresh ones in
// the next); one shot falls out, and isn't replaced.
void Man(int slot, uint8_t *player)
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
		if (c.down[i] || !flying || std::hypot(EntityPos(heli).x - EntityPos(player).x,
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

bool Install()
{
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
	gAddParticle = reinterpret_cast<decltype(gAddParticle)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfiiii"));
	if (!gHelis || !gAddParticle)
		return false;
	// Our hits before the game's are turned off: without them it couldn't be shot down.
	return hooks::Install("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D",
	                      Hook_DoBulletImpact, orig_DoBulletImpact) &&
	       hooks::Install("_ZN5CHeli19TestBulletCollisionEP7CVectorS1_S1_i", Hook_TestBulletCollision,
	                      orig_TestBulletCollision) &&
	       hooks::Install("_ZN5CHeli15PreRenderAlwaysEv", Hook_PreRenderAlways, orig_PreRenderAlways);
}

void Update()
{
	if (!gGunnersReady || !gConfig.heliGunners)
		return;
	uint8_t *player = api.FindPlayerPed();
	if (!player)
		return;
	const uint32_t now = *api.timeMs;
	if (gLastTime && (now < gLastTime || now - gLastTime > 10000)) { // a game loaded: the old game's handles
		gCrews[0] = gCrews[1] = Crew{};
		gSecond = -1;
	}
	gLastTime = now;
	Second(*api.timeMs);
	for (int slot = 0; slot < 2; slot++)
		Man(slot, player);
	hunter::Update(*api.timeMs);
}

bool Gunners(int slot, int &alive)
{
	alive = 0;
	if (slot < 0 || slot > 1 || gCrews[slot].heli < 0)
		return false;
	const Crew &c = gCrews[slot];
	bool any = false;
	for (int i = 0; i < 2; i++) {
		any |= c.ped[i] >= 0 || c.down[i];
		alive += c.ped[i] >= 0 && !c.down[i];
	}
	return any;
}

} // namespace heli
