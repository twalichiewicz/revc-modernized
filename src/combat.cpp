#include "combat.h"
#include "aimcam.h"
#include "config.h"
#include "hooks.h"
#include "log.h"
#include "wanted.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace combat {
namespace {

// CEntity / CPed (macVC 1.0.5217).
constexpr size_t kEntityType = 0x60;   // CEntity::m_type, in its low 3 bits
constexpr uint8_t kTypePed = 3;
constexpr size_t kPedIK = 0x2c0;       // CPed::m_pedIK
constexpr size_t kPointGunAt = 0x650;  // CPed::m_pPointGunAt: what it's shooting at
constexpr uint32_t kPedMid = 1, kPedHead = 2; // PedNode
// Guns (eWeaponType): the Colt to the minigun; of them, the ones a gunman
// keeps where he is with (the sniper rifles, the flamethrower, the minigun).
constexpr int32_t kFirstGun = 17, kLastGun = 33;
constexpr int32_t kSniper = 28, kLaserScope = 29, kFlamethrower = 31, kMinigun = 33;
// The fight (CPed::KillCharOnFootArmed).
constexpr size_t kTarget = 0x1a8;      // m_pedInObjective
constexpr size_t kFlags = 0x188;       // its first 64 flags
constexpr uint64_t kRunning = 1ull << 13, kStayPut = 1ull << 32, kDucksForCover = 1ull << 34,
                   kCrouchShooting = 1ull << 35, kDucking = 1ull << 36; // bIsRunning .. bIsDucking
constexpr size_t kState = 0x2fc, kAttackTimer = 0x698, kRotationDest = 0x470;
constexpr size_t kAccuracy = 0x64f;   // CPed::m_wepAccuracy: how far off its shots go (100: not at all)
constexpr size_t kWeaponState = 0x4;  // CWeapon::m_eWeaponState (2: reloading)
constexpr size_t kWanted = 0x778, kStars = 0x20; // CPlayerPed::m_pWanted, CWanted::m_nWantedLevel
constexpr int32_t kSeekPos = 6, kAttack = 16; // ePedState
constexpr int32_t kMoveStill = 1;             // eMoveState
constexpr int32_t kInProgress = 0, kCantAttack = 1; // its result
constexpr float kFeet = 1.04f;                // a ped's position above its feet
// His crouch's head and middle, and a standing gunman's eyes, above the ground.
constexpr float kCrouchedHead = 0.95f, kCrouchedMiddle = 0.5f, kStandingEyes = 1.6f;

bool (*gLineClear)(const CVector &, const CVector &, bool buildings, bool vehicles, bool peds, bool objects,
                   bool dummies, bool ignoreSeeThrough, bool ignoreSomeObjects);
void (*gComponentPosition)(void *ik, CVector *pos, uint32_t node);
uint8_t *(*gWeaponInfo)(int32_t type);
float (*gGroundZ)(float x, float y, float z, bool *found);
uint8_t *(*gSphereHits)(CVector centre, float radius, uint8_t *ignore, bool buildings, bool vehicles, bool peds,
                        bool objects, bool dummies, bool ignoreSomeObjects);
void (*gSetSeek)(uint8_t *ped, CVector at, float near);
void (*gSetIdle)(uint8_t *ped);
void (*gSetMoveState)(uint8_t *ped, int32_t state);
void (*gSetDuck)(uint8_t *ped, uint32_t ms, bool anyway);
void (*gClearDuck)(uint8_t *ped, bool now);
void (*gSetLookFlag)(uint8_t *ped, uint8_t *at, bool keep, bool always);
bool (*gLineHit)(const CVector &a, const CVector &b, uint8_t *point, uint8_t *&entity, bool buildings, bool vehicles,
                 bool peds, bool objects, bool dummies, bool ignoreSeeThrough, bool ignoreSomeObjects,
                 bool ignoreShootThrough);
bool (*gBestCoords)(uint8_t *ped, CVector unused, CVector *best);
int32_t (*orig_KillArmed)(uint8_t *ped, CVector &ours, CVector &theirs, CVector &between);
int (*gPedRef)(uint8_t *ped); // CPools::GetPedRef
uint8_t **gVehiclePool; // CPools::ms_pVehiclePool
constexpr size_t kVehicleSize = 0x6f0;
bool gReady, gTactics;

bool IsPed(const uint8_t *entity) { return entity && (entity[kEntityType] & 7) == kTypePed; }
uint64_t &Flags(uint8_t *ped) { return *reinterpret_cast<uint64_t *>(ped + kFlags); }
int32_t &Field(uint8_t *ped, size_t at) { return *reinterpret_cast<int32_t *>(ped + at); }

CVector Part(uint8_t *ped, uint32_t node)
{
	CVector at{0, 0, 0};
	gComponentPosition(ped + kPedIK, &at, node);
	return at;
}

CVector Above(const CVector &ground, float h) { return {ground.x, ground.y, ground.z + h}; }
float Flat(const CVector &a, const CVector &b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Walls and things in the way of a shot or a look (not glass or fences, which shots go through).
bool Clear(const CVector &a, const CVector &b) { return gLineClear(a, b, true, false, false, true, false, true, false); }
// ... and cars: what hides a gunman.
bool Open(const CVector &a, const CVector &b) { return gLineClear(a, b, true, true, false, true, false, true, false); }
// Nothing to walk into (fences and glass too).
bool Walkable(const CVector &a, const CVector &b) { return gLineClear(a, b, true, true, false, true, false, false, false); }

int gLogged;

// --- Cover and moving ---------------------------------------------------------------------

// Cover: something solid this close in front of a crouched gunman (towards
// him), and this wide either side of its middle; a step this long out round
// a corner to shoot. A shot this near, or a ped hit this near, puts a gunman
// under fire for a while. He's watching a gunman he can see within this
// angle of where he looks (wider while aiming), once he has for this long.
constexpr float kReach = 1.8f, kShoulders = 0.35f, kCorner = 0.9f;
constexpr float kShotNear = 3.5f, kHitNear = 10.0f, kKillNear = 15.0f;
constexpr uint32_t kUnderFireFor = 1500;
constexpr float kWatchCos = 0.978f, kWatchAimingCos = 0.94f; // 12 and 20 degrees
constexpr uint32_t kWatchedAfter = 350;
constexpr uint32_t kForget = 2000; // a fighter not seen for this long is a new fight
// The game's paths for peds (CPed::FindBestCoordsFromNodes): its target, its
// next node, and the flag that has it use them.
constexpr size_t kSeekTarget = 0x4a0, kNextNode = 0x430;
// CPed::m_attachedTo (CPed::AttachPedToEntity: a helicopter's door gunner).
constexpr size_t kAttachedTo = 0x720;
constexpr int32_t kCopType = 6; // ePedType
constexpr uint64_t kNodeSeek = 1ull << 29; // bUsePedNodeSeek

// A place to fight from.
struct Spot {
	CVector at;   // on the ground
	CVector peek; // where it stands to shoot: at, or a step out round a corner
	CVector via;  // where it goes first, round what's in the way (when `round`)
	bool round;
	bool cover;   // crouched at `at`, something solid and wide is between it and him
	bool shoots;  // it can shoot him from there (cover it can't is only to get closer behind)
};

enum class Phase : uint8_t { Fight, Move, Hide, Peek };
// What a new spot is for: getting behind something now; getting closer or
// round his side, cover to cover; closing in from out of range (across the
// open if it must, while he isn't looking); getting him back in sight;
// getting clear of a car on fire (to cover if there's some, the open if not).
enum class Want : uint8_t { Cover, Advance, Close, Sight, Away };

struct Fighter {
	uint8_t *ped;
	uint32_t seen;         // when it last fought (game ms)
	CVector pos;           // where it was then
	Phase phase;
	uint32_t since, until; // when its phase began, and ends
	Spot spot;             // where it's going, or where it is
	int peeks;             // looks over (or round) its cover left before it moves on
	uint32_t blind;        // since when it can't see him (0: it can)
	uint32_t alarm;        // when it last came under fire (0: never)
	uint32_t watched;      // since when he's been looking at it (0: he isn't)
	bool covering;         // shooting at him from where it is: the others can move
	bool rounded;          // it's been round the corner on its way to the spot
	uint32_t first;        // when it joined the fight
	uint32_t sighted;      // since when it's had him in sight (0: it hasn't)
	uint8_t aim;           // its own accuracy (CPed::m_wepAccuracy), ours meanwhile
	int handle;            // its pool handle (to give its aim back once it's gone from the fight)
};
std::array<Fighter, 16> gFighters;
uint32_t gPlanned; // when a fighter last looked for a spot (one a frame)
uint32_t gRandom = 0x2545f491;

float Random(float lo, float hi)
{
	gRandom = gRandom * 1664525u + 1013904223u;
	return lo + (hi - lo) * static_cast<float>(gRandom >> 8) / static_cast<float>(1u << 24);
}

bool Active(const Fighter &f, uint32_t now) { return f.ped && now - f.seen <= 1000; }

// Its aim back as it was, if it's still about (it's left the fight, or the fight it).
void Leave(Fighter &f)
{
	if (f.ped && f.aim && gPedRef && api.PoolsGetPed(f.handle) == f.ped)
		f.ped[kAccuracy] = f.aim;
	f.ped = nullptr;
}

// Those not in a fight for a while: out of it (their aim back).
void Forget(uint32_t now)
{
	for (Fighter &f : gFighters)
		if (f.ped && now - f.seen > kForget)
			Leave(f);
}

Fighter *Find(uint8_t *ped, uint32_t now, bool add)
{
	Fighter *free = nullptr;
	for (Fighter &f : gFighters) {
		if (f.ped == ped)
			return &f;
		if (!free && !f.ped)
			free = &f;
	}
	if (!add || !free || !gPedRef)
		return nullptr;
	// It fires a few shots from where it is, then looks for cover.
	*free = Fighter{ped, now, EntityPos(ped), Phase::Fight, now, now + static_cast<uint32_t>(Random(700, 1600)), {},
	                0, 0, 0, 0, false, false, now, 0, ped[kAccuracy], gPedRef(ped)};
	return free;
}

// One the tactics are for: a gunman after the player on foot.
bool Tactical(uint8_t *ped, uint8_t *target)
{
	if (!gTactics || !gConfig.smarterEnemies || target != api.FindPlayerPed() || PedInVehicle(target) ||
	    Health(target) <= 0.0f)
		return false;
	// Kept in place by the mission, or ducking behind a car as the game has cops do;
	// sat in a helicopter's door.
	if ((Flags(ped) & kStayPut) || *reinterpret_cast<void *const *>(ped + kAttachedTo))
		return false;
	const int32_t type = WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped)));
	return type >= kFirstGun && type <= kLastGun && type != kSniper && type != kLaserScope &&
	       type != kFlamethrower && type != kMinigun;
}

// How far it can usefully shoot (its gun's range, less for the sake of aim).
float Range(uint8_t *ped)
{
	const uint8_t *info = gWeaponInfo(WeaponType(PedWeapon(ped, PedCurrentWeaponSlot(ped))));
	const float range = info ? *reinterpret_cast<const float *>(info + 4) : 30.0f;
	return std::fmax(8.0f, std::fmin(range * 0.75f, 35.0f));
}

// p's distance (flat) from the segment a-b.
float FromSegment(const CVector &a, const CVector &b, const CVector &p)
{
	const float dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
	const float t = len > 0.0f ? std::fmax(0.0f, std::fmin(1.0f, ((p.x - a.x) * dx + (p.y - a.y) * dy) / len)) : 0.0f;
	return std::hypot(a.x + dx * t - p.x, a.y + dy * t - p.y);
}

CVector Towards(const CVector &a, const CVector &b)
{
	const float d = std::fmax(Flat(a, b), 1e-3f);
	return {(b.x - a.x) / d, (b.y - a.y) / d, 0.0f};
}
CVector Along(const CVector &a, const CVector &d, float k) { return {a.x + d.x * k, a.y + d.y * k, a.z + d.z * k}; }
float Bearing(const CVector &from, const CVector &to) { return std::atan2(to.y - from.y, to.x - from.x); }
float Apart(float a, float b) { return std::fabs(std::remainder(a - b, 6.2831853f)); }

// The first solid thing (not the ground) on the line a-b, and where.
bool Hits(const CVector &a, const CVector &b, CVector &at)
{
	alignas(16) uint8_t point[64] = {}; // CColPoint: where, then its normal at +0x10
	uint8_t *entity = nullptr;
	if (!gLineHit(a, b, point, entity, true, true, false, true, false, true, false, false))
		return false;
	at = *reinterpret_cast<const CVector *>(point);
	return reinterpret_cast<const CVector *>(point + 0x10)->z < 0.7f;
}

// Ground to stand on near (x, y), on the level of `feet`, with room for a ped.
bool Standable(uint8_t *ped, const CVector &feet, float x, float y, CVector &ground)
{
	bool found = false;
	const float z = gGroundZ(x, y, feet.z + 1.5f, &found);
	if (!found || std::fabs(z - feet.z) > 1.0f)
		return false;
	ground = {x, y, z};
	return !gSphereHits(Above(ground, 1.0f), 0.35f, ped, true, true, false, true, false, false);
}

// Something solid within reach in front, at height h, `lateral` to the side.
bool Shielded(const CVector &at, const CVector &front, const CVector &side, float h, float lateral)
{
	const CVector from = Above(Along(at, side, lateral), h);
	return !Open(from, Along(from, front, kReach));
}

// What (x, y) is worth against him (his middle): ground, cover, and where it
// can shoot him from. False if it's no place to stand, or neither cover nor
// anywhere to shoot from.
bool Survey(uint8_t *ped, const CVector &feet, const CVector &middle, float x, float y, Spot &s)
{
	if (!Standable(ped, feet, x, y, s.at))
		return false;
	const CVector front = Towards(s.at, middle), side{-front.y, front.x, 0.0f};
	s.cover = Shielded(s.at, front, side, kCrouchedHead, 0.0f) && Shielded(s.at, front, side, kCrouchedMiddle, 0.0f) &&
	          Shielded(s.at, front, side, kCrouchedHead, kShoulders) &&
	          Shielded(s.at, front, side, kCrouchedHead, -kShoulders);
	s.peek = s.at;
	s.shoots = Clear(Above(s.at, kStandingEyes), middle); // over it, or out in the open
	if (s.shoots || !s.cover)
		return s.shoots;
	for (float k : {kCorner, -kCorner}) { // round the side of it
		CVector out;
		if (Standable(ped, s.at, s.at.x + side.x * k, s.at.y + side.y * k, out) &&
		    Walkable(Above(s.at, 0.5f), Above(out, 0.5f)) && Clear(Above(out, kStandingEyes), middle)) {
			s.peek = out;
			s.shoots = true;
			break;
		}
	}
	return true;
}

bool Taken(const Fighter &me, const CVector &p, uint32_t now)
{
	for (const Fighter &f : gFighters)
		if (&f != &me && Active(f, now) && f.phase != Phase::Fight &&
		    (Flat(f.spot.at, p) < 1.5f || Flat(f.spot.peek, p) < 1.5f))
			return true;
	return false;
}

// How much a spot comes at him from a side of his own (0-1): apart from the
// others, round from where he's looking.
float Flank(const Fighter &me, const CVector &spot, const CVector &him, const CVector &look, uint32_t now)
{
	const float bearing = Bearing(him, spot);
	float apart = 1.5707963f;
	bool others = false;
	for (const Fighter &f : gFighters)
		if (&f != &me && Active(f, now)) {
			apart = std::fmin(apart, Apart(bearing, Bearing(him, f.pos)));
			others = true;
		}
	const float spread = others ? apart / 1.5707963f : 0.5f;
	const float off = Apart(bearing, std::atan2(look.y, look.x)); // 0: straight in front of him
	return 0.6f * spread + 0.4f * std::fmax(0.0f, std::fmin(1.0f, (off - 0.5f) / 1.0f));
}

// A way to the spot on foot: straight, or round the side of what's in the
// way (a step to one side or the other first). Its corner, if any, in s.via.
bool Reach(uint8_t *ped, const CVector &at, const CVector &feet, Spot &s)
{
	const CVector from = Above(feet, 0.5f), to = Above(s.at, 0.5f);
	s.round = false;
	if (Walkable(from, to) && Walkable(at, Above(s.at, kFeet)))
		return true;
	const CVector dir = Towards(feet, s.at), side{-dir.y, dir.x, 0.0f};
	const CVector mid{(feet.x + s.at.x) / 2, (feet.y + s.at.y) / 2, feet.z};
	for (float off : {2.5f, -2.5f, 4.5f, -4.5f}) {
		CVector corner;
		const CVector p = Along(mid, side, off);
		if (Standable(ped, feet, p.x, p.y, corner) && Walkable(from, Above(corner, 0.5f)) &&
		    Walkable(Above(corner, 0.5f), to)) {
			s.via = corner;
			s.round = true;
			return true;
		}
	}
	return false;
}

// Cars burning: one on fire blows up 5 s later, a blast 9 m about it
// (CExplosion's car blast); gunmen keep this far off one. One smoking is no
// cover to stay behind.
constexpr float kBlast = 11.0f, kBehindCar = 4.5f;
struct Danger {
	CVector at;
	bool fire; // on fire, else smoking
};
std::array<Danger, 8> gDangers;
int gNumDangers;
uint32_t gDangersAt = ~0u;

// The cars burning or smoking (not wrecks: they've blown up), once a frame.
void Dangers(uint32_t now)
{
	if (gDangersAt == now)
		return;
	gDangersAt = now;
	gNumDangers = 0;
	const uint8_t *pool = gVehiclePool ? *gVehiclePool : nullptr;
	if (!pool)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	for (int32_t i = 0; i < size && gNumDangers < int(gDangers.size()); i++) {
		if (flags[i] & 0x80)
			continue;
		uint8_t *car = entries + size_t(i) * kVehicleSize;
		if (Wrecked(car) || IsAircraft(car) || VehicleHealth(car) >= kVehicleSmoking)
			continue;
		gDangers[size_t(gNumDangers++)] = {EntityPos(car), VehicleHealth(car) < kVehicleOnFire};
	}
}

// The nearest car on fire with p in its blast.
bool Fire(const CVector &p, CVector *at)
{
	float nearest = kBlast;
	bool found = false;
	for (int i = 0; i < gNumDangers; i++) {
		const Danger &d = gDangers[size_t(i)];
		const float r = Flat(d.at, p);
		if (d.fire && r < nearest && std::fabs(d.at.z - p.z) < 4.0f) {
			nearest = r;
			found = true;
			if (at)
				*at = d.at;
		}
	}
	return found;
}

// In a burning car's blast, or behind one smoking.
bool Unsafe(const CVector &p)
{
	if (Fire(p, nullptr))
		return true;
	for (int i = 0; i < gNumDangers; i++) {
		const Danger &d = gDangers[size_t(i)];
		if (!d.fire && Flat(d.at, p) < kBehindCar && std::fabs(d.at.z - p.z) < 4.0f)
			return true;
	}
	return false;
}

// Cars standing near it, their side away from him (and towards each end):
// what a cop gets behind when he's out of his car (not one smoking).
template <typename Consider> void BehindCars(const CVector &at, const CVector &him, float within, Consider &&consider)
{
	const uint8_t *pool = gVehiclePool ? *gVehiclePool : nullptr;
	if (!pool)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(pool);
	const uint8_t *flags = *reinterpret_cast<uint8_t *const *>(pool + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(pool + 0x10);
	int found = 0;
	for (int32_t i = 0; i < size && found < 4; i++) {
		if (flags[i] & 0x80)
			continue;
		uint8_t *car = entries + size_t(i) * kVehicleSize;
		const CVector c = EntityPos(car);
		const CVector v = EntitySpeed(car);
		if (Flat(c, at) > within || std::hypot(v.x, v.y) * 50.0f > 1.5f || IsAircraft(car) ||
		    (!Wrecked(car) && VehicleHealth(car) < kVehicleSmoking))
			continue;
		const CVector &right = *reinterpret_cast<const CVector *>(car + 0x08);
		const CVector &ahead = EntityForward(car);
		const float away = (c.x - him.x) * right.x + (c.y - him.y) * right.y >= 0.0f ? 1.0f : -1.0f;
		for (float along : {0.0f, 1.5f, -1.5f}) {
			const CVector p = Along(Along(c, right, away * 2.0f), ahead, along);
			consider(p.x, p.y);
		}
		found++;
	}
}

// The best spot within 12 m for what it wants (cover within 18). Its
// candidates: just behind whatever is between him and the ground about it
// (rays from his eyes fanned about the line to it), the far side of cars
// standing near, and rings about it; the best few it can get to (round a
// corner if it must). Cover is wanted near, but it runs further for it
// rather than dodge about in the open, never to cover nearer him than it is;
// with none, a dodge to the side. It moves up cover to cover; only closing
// in from out of range, while he isn't looking, does it cross open ground.
// Never in a burning car's blast, or behind one smoking.
// Where a cop goes by when nobody's seen him lately (the wanted overhaul): where
// the police think he is (a gang's gunmen know where he is).
CVector Known(const uint8_t *ped, uint8_t *player)
{
	if (PedType(ped) == kCopType && wanted::Active() && !wanted::OnHim())
		return wanted::Target();
	return EntityPos(player);
}

bool Plan(uint8_t *ped, uint8_t *player, Fighter &me, Want want, uint32_t now, Spot &chosen)
{
	const CVector at = EntityPos(ped), feet = Above(at, -kFeet);
	// (As though he were where it thinks he is.)
	const CVector real = EntityPos(player), him = Known(ped, player);
	const CVector shift{him.x - real.x, him.y - real.y, him.z - real.z};
	const CVector head = Part(player, kPedHead), mid = Part(player, kPedMid);
	const CVector eyes{head.x + shift.x, head.y + shift.y, head.z + shift.z};
	const CVector middle{mid.x + shift.x, mid.y + shift.y, mid.z + shift.z};
	const CVector &front = *reinterpret_cast<const CVector *>(api.theCamera + 0x10); // where he's looking
	const CVector look = Towards({0, 0, 0}, {front.x, front.y, 0});
	const float range = Range(ped), hisDistance = Flat(at, him), flankNow = Flank(me, at, him, look, now);
	const bool watched = me.watched && now - me.watched > kWatchedAfter;
	const bool onward = want == Want::Advance || want == Want::Close;
	const float within = want == Want::Cover || want == Want::Away ? 18.0f : 12.0f;
	struct Candidate {
		float score;
		Spot s;
	};
	constexpr int kBest = 6;
	Candidate best[kBest];
	int kept = 0;
	auto consider = [&](float x, float y) {
		const CVector flat{x, y, at.z};
		const float radius = Flat(flat, at), distance = Flat(flat, him);
		const bool closer = distance < hisDistance - 2.0f;
		if (radius < 1.0f || radius > within || distance < 4.0f || FromSegment(at, flat, him) < 2.5f ||
		    (distance > range + 4.0f && !(onward && closer) && want != Want::Away) || Taken(me, flat, now) ||
		    Unsafe(flat))
			return;
		// For cover, not one he's nearer than it is (it'd run at him for it).
		if (want == Want::Cover && distance < radius)
			return;
		const float flank = Flank(me, flat, him, look, now);
		if (onward && !closer && flank < flankNow + 0.15f)
			return;
		Spot s;
		if (!Survey(ped, feet, middle, x, y, s) || (want == Want::Sight && !s.shoots) ||
		    (!s.cover && ((want == Want::Cover && radius > 4.5f) || want == Want::Advance ||
		                  (want == Want::Close && watched))))
			return;
		// Running across his sight on the way there.
		const CVector halfway{(at.x + s.at.x) / 2, (at.y + s.at.y) / 2, (feet.z + s.at.z) / 2 + 1.0f};
		const float exposed = radius > 2.0f && Clear(eyes, halfway) ? 1.0f : 0.0f;
		const float worth = s.cover ? (s.shoots ? 1.0f : 0.6f) : 0.0f; // cover to shoot from, or only to hide
		float score;
		if (want == Want::Cover)
			score = s.cover ? 20.0f * worth - radius * 0.8f - exposed * 3.0f + flank
			                : 2.0f - std::fabs(radius - 3.0f) * 0.5f;
		else if (want == Want::Away) // out of the blast the nearest way, to cover if it can
			score = (s.cover ? 6.0f + 6.0f * worth : 0.0f) - radius * 0.3f - exposed * 2.0f;
		else if (onward)
			score = 8.0f * worth + std::fmin(hisDistance - distance, 6.0f) * 0.5f + flank * 5.0f - radius * 0.4f -
			        exposed * (watched ? 4.0f : 2.0f);
		else
			score = (s.cover ? 6.0f : 0.0f) + flank * 2.0f - radius * 0.6f - exposed;
		score += Random(0.0f, 0.5f);
		// Kept in order, the best first.
		int k = std::min(kept, kBest - 1);
		if (kept == kBest && score <= best[k].score)
			return;
		while (k > 0 && best[k - 1].score < score) {
			best[k] = best[k - 1];
			k--;
		}
		best[k] = {score, s};
		kept = std::min(kept + 1, kBest);
	};
	// Behind what he'd see it behind: just past each thing a ray from his eyes
	// hits near it (not too near him), at a crouched gunman's middle.
	const float bearing = Bearing(him, at), reach = hisDistance + 6.0f;
	for (int i = -7; i <= 7; i++) {
		const float angle = bearing + static_cast<float>(i) * 0.12f;
		const CVector dir{std::cos(angle), std::sin(angle), 0.0f};
		CVector hit;
		if (Hits(eyes, {him.x + dir.x * reach, him.y + dir.y * reach, feet.z + kCrouchedMiddle + 0.2f}, hit) &&
		    Flat(hit, him) > 4.0f && Flat(hit, at) < within)
			consider(hit.x + dir.x * 0.6f, hit.y + dir.y * 0.6f);
	}
	BehindCars(at, him, within, consider);
	constexpr float kRings[] = {2.0f, 4.0f, 6.5f, 9.0f, 13.0f, 16.5f};
	for (float radius : kRings) {
		if (radius > within)
			break;
		const float turn = Random(0.0f, 1.0f);
		for (int a = 0; a < 8; a++) {
			const float angle = (static_cast<float>(a) + turn) * 0.7853982f;
			consider(at.x + radius * std::cos(angle), at.y + radius * std::sin(angle));
		}
	}
	for (int k = 0; k < kept; k++)
		if (Reach(ped, at, feet, best[k].s)) {
			chosen = best[k].s;
			return true;
		}
	return false;
}

// The way round to him when there's none to see him from near: the game's
// paths for peds, the node it would take next towards him.
bool Round(uint8_t *ped, uint8_t *player, Spot &s)
{
	CVector &target = *reinterpret_cast<CVector *>(ped + kSeekTarget);
	uint8_t *&next = *reinterpret_cast<uint8_t **>(ped + kNextNode);
	const CVector targetWas = target;
	uint8_t *const nextWas = next;
	const uint64_t kept = Flags(ped) & kNodeSeek;
	target = Known(ped, player);
	next = nullptr;
	Flags(ped) |= kNodeSeek;
	CVector to;
	const bool found = gBestCoords(ped, {0, 0, 0}, &to);
	Flags(ped) = (Flags(ped) & ~kNodeSeek) | kept;
	target = targetWas;
	next = nextWas;
	if (!found)
		return false;
	s = Spot{to, to, to, false, false, false};
	return true;
}

// Another is shooting at him (so it can move), and not too many are on the move.
bool MayAdvance(const Fighter &me, uint32_t now)
{
	int others = 0, moving = 0;
	bool covered = false;
	for (const Fighter &f : gFighters) {
		if (&f == &me || !Active(f, now))
			continue;
		others++;
		moving += f.phase == Phase::Move;
		covered |= f.covering && Flat(f.pos, me.pos) < 40.0f;
	}
	return others == 0 || (covered && moving * 2 <= others);
}

// Turns to shoot (as Half-Life's grunts have them): so many shooting at him
// at once from where they are, more for more stars; the rest keep down, or
// move up while those cover them, and take a turn as soon as one's free.
int Turns(uint8_t *player)
{
	const uint8_t *wanted = *reinterpret_cast<uint8_t *const *>(player + kWanted);
	const int stars = wanted ? *reinterpret_cast<const int32_t *>(wanted + kStars) : 0;
	return stars >= 4 ? 5 : stars == 3 ? 4 : 3;
}
bool MayShoot(const Fighter &me, uint8_t *player, uint32_t now)
{
	int shooting = 0;
	for (const Fighter &f : gFighters)
		shooting += &f != &me && Active(f, now) && f.covering;
	return shooting < Turns(player);
}

// Others with it, to go in together.
int Friends(const Fighter &me, uint32_t now)
{
	int near = 0;
	for (const Fighter &f : gFighters)
		near += &f != &me && Active(f, now) && Flat(f.pos, me.pos) < 15.0f;
	return near;
}

bool Reloading(uint8_t *ped)
{
	return *reinterpret_cast<const int32_t *>(PedWeapon(ped, PedCurrentWeaponSlot(ped)) + kWeaponState) ==
	       kWeaponReloading;
}

// Its aim: wide at first sight of him, its own once it's had him a moment,
// truer still the longer it has him (one who stands about out in the open
// gets hit).
void Aim(uint8_t *ped, Fighter &f, bool sees, uint32_t now)
{
	f.sighted = sees ? (f.sighted ? f.sighted : now) : 0;
	if (!f.aim)
		return;
	float k = 0.5f;
	if (f.sighted) {
		const float t = static_cast<float>(now - f.sighted);
		k = t < 900.0f ? 0.5f + 0.5f * t / 900.0f : 1.0f + std::fmin(0.2f, (t - 900.0f) / 20000.0f);
	}
	ped[kAccuracy] = static_cast<uint8_t>(std::clamp(static_cast<int>(f.aim * k + 0.5f), 5, 95));
}

// What they did, for the log every 20 s.
struct Counts {
	int cover, dodges, none, upToCover, up, sight, paths, corners, peeks, turns, musters, reloads, moved, fled;
} gCount;
uint32_t gReported;

void Report(uint32_t now)
{
	if (now - gReported < 20000)
		return;
	gReported = now;
	const Counts &c = gCount;
	const int moves = c.cover + c.dodges + c.upToCover + c.up + c.sight + c.paths + c.fled;
	if (!moves && !c.peeks && !c.musters)
		return;
	int fighting = 0;
	for (const Fighter &f : gFighters)
		fighting += Active(f, now);
	Log("combat: 20 s, %d gunmen now: %d into cover, %d sidesteps, %d found neither, %d up to cover, %d up in the "
	    "open, %d round to see him, %d by the paths (%d of all round a corner); %d peeks, %d waited a turn to shoot, "
	    "%d waited for others, %d hid to reload, %d moved on being aimed at, %d ran from a car on fire",
	    fighting, c.cover, c.dodges, c.none, c.upToCover, c.up, c.sight, c.paths, c.corners, c.peeks, c.turns,
	    c.musters, c.reloads, c.moved, c.fled);
	gCount = {};
}

void StandUp(uint8_t *ped)
{
	Flags(ped) &= ~kCrouchShooting;
	if (Flags(ped) & kDucking)
		gClearDuck(ped, true);
}

void Begin(Fighter &f, Phase phase, uint32_t now, float lo, float hi)
{
	f.phase = phase;
	f.since = now;
	f.until = now + static_cast<uint32_t>(Random(lo, hi));
}

// Looks for a spot (one fighter a frame: the rest wait for the next) and sets
// off for it; with none to see him from, the way round on the game's paths.
bool Ready(uint32_t now) { return gPlanned != now; }

void SetOff(uint8_t *ped, Fighter &f, const Spot &s, uint32_t now)
{
	StandUp(ped);
	f.spot = s;
	f.rounded = false;
	gCount.corners += s.round;
	Begin(f, Phase::Move, now, 5000, 5000);
}

bool MoveOn(uint8_t *ped, uint8_t *player, Fighter &f, Want want, uint32_t now)
{
	gPlanned = now;
	Spot s;
	if (Plan(ped, player, f, want, now, s)) {
		SetOff(ped, f, s, now);
		if (want == Want::Advance || want == Want::Close)
			(s.cover ? gCount.upToCover : gCount.up)++;
		else if (want == Want::Cover)
			(s.cover ? gCount.cover : gCount.dodges)++;
		else if (want == Want::Away)
			gCount.fled++;
		else
			gCount.sight++;
		return true;
	}
	if (want != Want::Cover && want != Want::Away && f.blind && Round(ped, player, s)) {
		SetOff(ped, f, s, now);
		gCount.paths++;
		return true;
	}
	gCount.none += want == Want::Cover;
	return false;
}

int32_t Run(uint8_t *ped, uint8_t *player, const CVector &to, bool run)
{
	gSetSeek(ped, Above(to, kFeet), run ? 0.5f : 0.3f);
	if (run)
		Flags(ped) |= kRunning;
	else
		Flags(ped) &= ~kRunning;
	gSetLookFlag(ped, player, true, false);
	return kInProgress;
}

// To its spot, round the corner first if that's the way.
int32_t RunTo(uint8_t *ped, uint8_t *player, Fighter &f)
{
	if (f.spot.round && !f.rounded && Flat(f.pos, f.spot.via) < 1.0f)
		f.rounded = true;
	return Run(ped, player, f.spot.round && !f.rounded ? f.spot.via : f.spot.at, true);
}

// It shoots from where it is: the game would walk it closer between shots,
// and at range wait seconds between them.
int32_t Shoot(uint8_t *ped, CVector &ours, CVector &theirs, CVector &between, uint32_t now)
{
	const uint64_t kept = Flags(ped) & kStayPut;
	Flags(ped) |= kStayPut;
	const int32_t status = orig_KillArmed(ped, ours, theirs, between);
	Flags(ped) = (Flags(ped) & ~kStayPut) | kept;
	uint32_t &timer = *reinterpret_cast<uint32_t *>(ped + kAttackTimer);
	if (timer > now + 1200)
		timer = now + static_cast<uint32_t>(Random(400, 1100));
	return status;
}

// Down where it is, gun ready, facing him (not while it's knocked down).
void Crouch(uint8_t *ped, uint8_t *player, const Fighter &f, uint32_t now)
{
	if (!api.IsPedInControl(ped))
		return;
	if (Field(ped, kState) == kSeekPos)
		gSetIdle(ped);
	gSetMoveState(ped, kMoveStill);
	const CVector him = EntityPos(player);
	*reinterpret_cast<float *>(ped + kRotationDest) = HeadingOf(him.x - f.pos.x, him.y - f.pos.y);
	Flags(ped) |= kCrouchShooting; // the crouch with the gun ready
	if (!(Flags(ped) & kDucking))
		gSetDuck(ped, std::max<uint32_t>(f.until > now ? f.until - now : 0, 500) + 500, true);
}

int32_t Hook_KillArmed(uint8_t *ped, CVector &ours, CVector &theirs, CVector &between)
{
	uint8_t *player = *reinterpret_cast<uint8_t **>(ped + kTarget);
	const uint32_t now = *api.timeMs;
	Report(now);
	Forget(now);
	Dangers(now);
	if (!Tactical(ped, player)) {
		if (Fighter *f = Find(ped, now, false)) {
			if (f->phase == Phase::Hide)
				StandUp(ped);
			Leave(*f);
		}
		// Beside a car on fire all the same (he's in a car, or it's kept in
		// place but not by the mission): straight away from it.
		CVector fire;
		if (gTactics && gConfig.smarterEnemies && !PedFromMission(ped) && Fire(EntityPos(ped), &fire)) {
			StandUp(ped);
			return Run(ped, player, Along(fire, Towards(fire, EntityPos(ped)), kBlast + 3.0f), true);
		}
		return orig_KillArmed(ped, ours, theirs, between);
	}
	Fighter *slot = Find(ped, now, true);
	if (!slot) // more gunmen than places to keep them
		return orig_KillArmed(ped, ours, theirs, between);
	Fighter &f = *slot;
	f.seen = now;
	f.pos = EntityPos(ped);
	f.covering = false;
	Flags(ped) &= ~kDucksForCover; // our cover, not the game's (cops' cars)
	const float distance = Flat(f.pos, EntityPos(player)), range = Range(ped);
	const CVector head = Part(ped, kPedHead), middle = Part(player, kPedMid);
	const bool sees = Clear(head, middle);
	Aim(ped, f, sees, now);
	f.blind = sees ? 0 : f.blind ? f.blind : now;
	const bool lost = f.blind && now - f.blind > 1000;
	const bool underFire = f.alarm && now - f.alarm < kUnderFireFor;
	const bool shooting = Field(ped, kState) == kAttack;
	const bool reloading = Reloading(ped);
	// He's looking at it: it can see him, and it's near the middle of his view.
	const CVector &eye = *reinterpret_cast<const CVector *>(api.theCamera + 0x30);
	const CVector &view = *reinterpret_cast<const CVector *>(api.theCamera + 0x10);
	const CVector to{head.x - eye.x, head.y - eye.y, head.z - eye.z};
	const float away = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
	const bool looking = sees && away > 0.1f && away < 80.0f &&
	                     (to.x * view.x + to.y * view.y + to.z * view.z) / away >
	                         (aimcam::Aiming() ? kWatchAimingCos : kWatchCos);
	f.watched = looking ? (f.watched ? f.watched : now) : 0;
	const bool watched = f.watched && now - f.watched > kWatchedAfter;
	const bool pressed = underFire || watched; // it keeps its head down

	// A car on fire by it (it blows up in seconds): clear of it before
	// anything, to cover out of the blast if there's some (on its way to one
	// already, it goes on).
	CVector fire;
	if (Fire(f.pos, &fire) && !(f.phase == Phase::Move && !Unsafe(f.spot.at)) && Ready(now)) {
		if (!MoveOn(ped, player, f, Want::Away, now)) {
			const CVector away = Along(fire, Towards(fire, f.pos), kBlast + 3.0f);
			SetOff(ped, f, Spot{away, away, away, false, false, false}, now);
			gCount.fled++;
		}
		return RunTo(ped, player, f);
	}

	if (f.phase == Phase::Move) {
		if (Flat(f.pos, f.spot.at) < 0.8f) {
			if (f.spot.cover) {
				Begin(f, Phase::Hide, now, pressed ? 1500 : 700, pressed ? 2500 : 1500);
				f.peeks = Random(0, 1) < 0.5f ? 2 : 3;
			} else {
				Begin(f, Phase::Fight, now, 1500, 3000);
			}
		} else if (now > f.until) {
			Begin(f, Phase::Fight, now, 1500, 3000); // couldn't get there
		} else {
			Spot s; // shot at or looked at on the way somewhere open: into cover instead, if there's some
			if (pressed && !f.spot.cover && now - f.since > 300 && Ready(now)) {
				gPlanned = now;
				if (Plan(ped, player, f, Want::Cover, now, s) && s.cover) {
					SetOff(ped, f, s, now);
					gCount.cover++;
				}
			}
			return RunTo(ped, player, f);
		}
	}
	if (f.phase == Phase::Hide) {
		if (Flat(f.pos, f.spot.at) > 0.7f && now - f.since < 2500) // back behind it from round the corner
			return Run(ped, player, f.spot.at, false);
		// He's come round to where he sees it, or what it's behind is smoking
		// (soon on fire): cover elsewhere, or fight back.
		const bool exposed = now - f.since > 700 && (Open(head, middle) || Unsafe(f.spot.at));
		if (exposed && Ready(now) && MoveOn(ped, player, f, Want::Cover, now))
			return RunTo(ped, player, f);
		// Aimed at where it is a while: off to other cover, while another keeps him busy.
		if (watched && now - f.watched > 2000 && now - f.since > 2000 && Ready(now) && MayAdvance(f, now) &&
		    MoveOn(ped, player, f, Want::Cover, now)) {
			gCount.moved++;
			return RunTo(ped, player, f);
		}
		if (pressed) // kept down while the shots come, or he's looking, for a while
			f.until = std::min(std::max(f.until, now + 600), f.since + 5000);
		const bool due = now > f.until || exposed;
		if (due && !f.spot.shoots && !exposed && now - f.since < 6000) {
			// Nowhere to shoot from here: on, cover to cover, or round to where it sees him.
			if (Ready(now) && ((MayAdvance(f, now) && MoveOn(ped, player, f, Want::Advance, now)) ||
			                   MoveOn(ped, player, f, Want::Sight, now)))
				return RunTo(ped, player, f);
			f.until = now + 500;
		} else if (due && !exposed && !MayShoot(f, player, now)) {
			// Not its turn to shoot: up, cover to cover, while the others shoot; else it waits.
			if (Ready(now) && MayAdvance(f, now) && MoveOn(ped, player, f, Want::Advance, now))
				return RunTo(ped, player, f);
			f.until = now + 400;
			gCount.turns++;
		} else if (due) {
			StandUp(ped);
			Begin(f, Phase::Peek, now, 1400, 2400);
			f.peeks--;
			gCount.peeks++;
			*reinterpret_cast<uint32_t *>(ped + kAttackTimer) = now; // it shoots straight away
		}
		if (f.phase == Phase::Hide) {
			Crouch(ped, player, f, now);
			return kCantAttack;
		}
	}
	if (f.phase == Phase::Peek) {
		if (Flat(f.spot.peek, f.spot.at) > 0.1f && Flat(f.pos, f.spot.peek) > 0.5f && now - f.since < 1500)
			return Run(ped, player, f.spot.peek, false); // out round the corner
		// Down to reload, out of his sight.
		if (reloading && !shooting) {
			Begin(f, Phase::Hide, now, 900, 1500);
			f.peeks = std::max(f.peeks, 1);
			gCount.reloads++;
			return kCantAttack;
		}
		// Down again once he looks its way (or the shots come), after a burst.
		if ((now > f.until || lost || (pressed && now - f.since > 700)) && !shooting) {
			if (lost) {
				if (!Ready(now) || !MoveOn(ped, player, f, Want::Sight, now))
					Begin(f, Phase::Fight, now, 1000, 2000);
			} else if (f.peeks > 0 || underFire || !Ready(now) || !MayAdvance(f, now) ||
			           !MoveOn(ped, player, f, Want::Advance, now)) {
				Begin(f, Phase::Hide, now, pressed ? 1500 : 900, pressed ? 2500 : 1800);
				f.peeks = std::max(f.peeks, 1);
			}
			if (f.phase == Phase::Move)
				return RunTo(ped, player, f);
			if (f.phase != Phase::Peek)
				return kCantAttack;
		}
		f.covering = sees;
		return Shoot(ped, ours, theirs, between, now);
	}
	// In the open. Too far off to hit him: closer, cover to cover (or as the
	// game runs at him, or round on the paths if a wall's between). Not yet
	// seeing him, with few others up with it: it waits for them (8 s at most),
	// so they come at him together.
	if (distance > range + 3.0f) {
		if (!sees && Friends(f, now) < 2 && now - f.first < 8000) {
			if (Field(ped, kState) != kAttack) {
				gCount.musters += f.until != f.first; // counted once
				f.until = f.first;
				Crouch(ped, player, f, now);
				return kCantAttack;
			}
		}
		if (now > f.until && Ready(now)) {
			if (MoveOn(ped, player, f, Want::Close, now))
				return RunTo(ped, player, f);
			Begin(f, Phase::Fight, now, watched ? 500 : 1000, watched ? 900 : 2000);
		}
		StandUp(ped);
		return orig_KillArmed(ped, ours, theirs, between);
	}
	// Shot at, looked at, a burst done, blind, reloading, or not its turn: off
	// to a spot (cover first; tried again soon if there was none).
	const bool turn = MayShoot(f, player, now);
	if ((now > f.until || ((pressed || lost || reloading || !turn) && now - f.since > 400)) && !shooting &&
	    Ready(now)) {
		if (MoveOn(ped, player, f, lost ? Want::Sight : Want::Cover, now)) {
			gCount.reloads += reloading;
			return RunTo(ped, player, f);
		}
		Begin(f, Phase::Fight, now, 1000, 2000);
	}
	f.covering = sees;
	return Shoot(ped, ours, theirs, between, now);
}

} // namespace

void PlayerShot(const CVector &from, const CVector &dir)
{
	if (!gTactics)
		return;
	const uint32_t now = *api.timeMs;
	for (Fighter &f : gFighters) {
		if (!Active(f, now))
			continue;
		const CVector to{f.pos.x - from.x, f.pos.y - from.y, f.pos.z - from.z};
		const float along = to.x * dir.x + to.y * dir.y + to.z * dir.z;
		const float off = to.x * to.x + to.y * to.y + to.z * to.z - along * along;
		if (along > 0.0f && along < 100.0f && off < kShotNear * kShotNear)
			f.alarm = now;
	}
}

void Hurt(uint8_t *ped, uint8_t *by, bool killed)
{
	if (!gTactics || !ped || by != api.FindPlayerPed())
		return;
	const uint32_t now = *api.timeMs;
	const CVector &at = EntityPos(ped);
	for (Fighter &f : gFighters)
		if (Active(f, now) && Flat(f.pos, at) < (killed ? kKillNear : kHitNear))
			f.alarm = now;
}

bool MayFire(uint8_t *weapon, uint8_t *shooter, CVector *source)
{
	if (!gReady || !IsPed(shooter) || shooter == api.FindPlayerPed() || !source)
		return true;
	const int32_t type = WeaponType(weapon);
	if (type < kFirstGun || type > kLastGun)
		return true;
	const CVector chest = Part(shooter, kPedMid);
	// It can't see what it's shooting at: it holds its fire.
	uint8_t *target = *reinterpret_cast<uint8_t **>(shooter + kPointGunAt);
	if (target) {
		const CVector eyes = Part(shooter, kPedHead);
		const CVector at = IsPed(target) ? Part(target, kPedMid) : *reinterpret_cast<const CVector *>(target + 0x38);
		if (!Clear(eyes, at)) {
			if (gLogged++ < 5)
				Log("combat: a ped holds its fire, its target out of sight");
			return false;
		}
	}
	// Its gun's muzzle through a wall: the shot starts at its chest.
	if (!Clear(chest, *source))
		*source = chest;
	return true;
}

bool Install()
{
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gLineClear = reinterpret_cast<decltype(gLineClear)>(sym("_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb"));
	gComponentPosition = reinterpret_cast<decltype(gComponentPosition)>(sym("_ZN6CPedIK20GetComponentPositionERN2rw3V3dEj"));
	gReady = gLineClear && gComponentPosition;
	gWeaponInfo = reinterpret_cast<decltype(gWeaponInfo)>(sym("_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType"));
	gGroundZ = reinterpret_cast<decltype(gGroundZ)>(sym("_ZN6CWorld21FindGroundZFor3DCoordEfffPb"));
	gSphereHits = reinterpret_cast<decltype(gSphereHits)>(sym("_ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb"));
	gSetSeek = reinterpret_cast<decltype(gSetSeek)>(sym("_ZN4CPed7SetSeekE7CVectorf"));
	gSetIdle = reinterpret_cast<decltype(gSetIdle)>(sym("_ZN4CPed7SetIdleEv"));
	gSetMoveState = reinterpret_cast<decltype(gSetMoveState)>(sym("_ZN4CPed12SetMoveStateE10eMoveState"));
	gSetDuck = reinterpret_cast<decltype(gSetDuck)>(sym("_ZN4CPed7SetDuckEjb"));
	gClearDuck = reinterpret_cast<decltype(gClearDuck)>(sym("_ZN4CPed9ClearDuckEb"));
	gSetLookFlag = reinterpret_cast<decltype(gSetLookFlag)>(sym("_ZN4CPed11SetLookFlagEP7CEntitybb"));
	gLineHit = reinterpret_cast<decltype(gLineHit)>(
	    sym("_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb"));
	gBestCoords = reinterpret_cast<decltype(gBestCoords)>(sym("_ZN4CPed23FindBestCoordsFromNodesE7CVectorPS0_"));
	gVehiclePool = static_cast<uint8_t **>(sym("_ZN6CPools15ms_pVehiclePoolE"));
	gPedRef = reinterpret_cast<decltype(gPedRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gTactics = gReady && gWeaponInfo && gGroundZ && gSphereHits && gSetSeek && gSetIdle && api.IsPedInControl &&
	           gSetMoveState && gSetDuck && gClearDuck && gSetLookFlag && gLineHit && gBestCoords &&
	           hooks::Install("_ZN4CPed19KillCharOnFootArmedER7CVectorS1_S1_", Hook_KillArmed, orig_KillArmed);
	if (gReady && !gTactics)
		Log("combat: no cover or moving for gunmen (a function or the hook is missing)");
	return gReady;
}

} // namespace combat
