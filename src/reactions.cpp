#include "reactions.h"
#include "body.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "playerreact.h"
#include "reactcontact.h"
#include "reactcrowd.h"
#include "reactimpact.h"
#include "reactphysics.h"
#include "reactrig.h"
#include "shotimpact.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace rp = reactphysics;
namespace reactions {
namespace {
constexpr int kMost = 12;
constexpr int kTags[body::kBones] = {0, 1, 2, 3, 4, 5, 31, 32, 33, 34, 35, 21, 22, 23, 24, 25, 41, 42, 43, 51, 52, 53};
constexpr int kPhysicalTags[rp::Count] = {1, 3, 5, 32, 33, 34, 22, 23, 24, 41, 42, 43, 51, 52, 53};
using Matrix = std::array<float, 16>;
using Matrices = std::array<Matrix, body::kBones>;
int (*gRef)(uint8_t *);
void (*gUpdateMatrix)(uint8_t *), (*gUpdateFrame)(uint8_t *), (*gReindex)(uint8_t *);
void (*gSetFall)(uint8_t *, int, int, uint8_t);
void (*gGetUp)(uint8_t *);
void (*orig_Control)(uint8_t *);
void (*orig_WorldCollision)(uint8_t *);
void (*orig_WorldProcess)();
int (*orig_Collision)(uint8_t *, uint8_t *, uint8_t *);
void (*orig_PedCollision)(uint8_t *, uint8_t *);
void (*orig_SetFall)(uint8_t *, int, int, uint8_t);
bool *gCutscene;
uint8_t *gReplay;
uint8_t **gWindPool;
uint32_t *gWindFrame;
bool gInstalled;
uint32_t gLastFrame;
uint32_t gCrowdAt;
bool gCrowdClock;

struct Person {
	int handle = -1;
	uint8_t *clump = nullptr;
	std::array<int, body::kBones> indices{};
	std::array<int, rp::Count> joints{};
	Matrices original{};
	rp::Body physical;
	Vec3 pelvisOffset{};
	Vec3 publishedVelocity{};
	uint32_t stepped = 0, hitAt = 0, publishedAt = 0;
	uint32_t savedGetUpTimer = 0;
	rp::Mode nativeMode = rp::Mode::Balanced;
	bool saved = false, heldFall = false, pendingVehicle = false;
	// Tommy's view in his head's frame, from the knockdown (#25): view axis j is
	// sum_k headView[j][k] * head axis k.
	std::array<std::array<float, 3>, 3> headView{};
	bool headViewReady = false;
};
std::vector<Person> gPeople;
struct ContactStamp {
	int handle = -1;
	uint8_t *clump = nullptr;
	uint32_t at = 0;
};
std::array<ContactStamp, kMost> gContacts{};
// Native standing UpdatePosition replaces XY after CPhysical::ProcessControl
// (which only damps/applies gravity). Deliver wash after that replacement and
// before the real world collision integrates movement. Keep this separate
// from Person: articulated body/probe layouts remain stable.
struct WindPool {uint8_t *header=nullptr,*entries=nullptr,*flags=nullptr;int count=0;};
WindPool WindWorld(){
 uint8_t *p=gWindPool?*gWindPool:nullptr;if(!p)return {};
 WindPool world{p,*reinterpret_cast<uint8_t **>(p),*reinterpret_cast<uint8_t **>(p+8),*reinterpret_cast<int32_t *>(p+16)};
 return world.entries && world.flags && world.count>0 && world.count<=4096?world:WindPool{};
}
bool SameWindWorld(const WindPool &a,const WindPool &b){return a.header && a.header==b.header && a.entries==b.entries && a.flags==b.flags && a.count==b.count;}
struct WindStamp {int handle=-1;uint8_t *clump=nullptr;uint32_t at=0;Vec3 delta{};WindPool world{};};
std::array<WindStamp,64> gWind{};
WindPool gWindWorld{};uint32_t gWindTime=0,gWindNumber=0;bool gWindClock=false;
bool ObserveWindWorld(){
 const WindPool world=WindWorld();const uint32_t now=api.timeMs?*api.timeMs:0,frame=gWindFrame?*gWindFrame:0;
 const bool suspended=(gCutscene && *gCutscene)||(gReplay && *gReplay==1);
 if(!SameWindWorld(world,gWindWorld)||suspended||(gWindClock&&(now<gWindTime||frame<gWindNumber)))gWind={};
 gWindWorld=world;gWindTime=now;gWindNumber=frame;gWindClock=true;
 for(WindStamp &wind:gWind)if(wind.handle>=0&&(now<wind.at||now-wind.at>100))wind={};
 return world.header && !suspended;
}
struct PedBumpScope {
	uint8_t *player = nullptr, *ped = nullptr;
	bool resist = false;
	float closing = 0;
	bool sprint = false;
};
PedBumpScope *gPedBump;
unsigned gResistedBumpLogs, gSprintBumpLogs;
struct PedBumpStamp {
	int player = -1, ped = -1;
	uint8_t *playerClump = nullptr, *pedClump = nullptr;
	uint32_t seen = 0;
};
std::array<PedBumpStamp, kMost> gPedBumps{};
struct PedContactMotion {
	uint8_t *player = nullptr, *ped = nullptr, *playerClump = nullptr, *pedClump = nullptr;
	int playerRef = -1, pedRef = -1;
	uint32_t at = 0;
	Vec3 playerVelocity{}, pedVelocity{}, playerOrigin{}, pedOrigin{}, direction{};
	float closing = 0;
	int piece = 1;
};
std::array<PedContactMotion, kMost> gPedMotions{};

Vec3 V(CVector p)
{
	return {p.x, p.y, p.z};
}
CVector C(Vec3 p)
{
	return {p.x, p.y, p.z};
}
Vec3 Sub(Vec3 a, Vec3 b)
{
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3 Add(Vec3 a, Vec3 b)
{
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Vec3 Unit(Vec3 v)
{
	const float length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	return length > 1e-6f ? Vec3{v.x / length, v.y / length, v.z / length} : Vec3{};
}
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 Mul(Vec3 a, float v)
{
	return {a.x * v, a.y * v, a.z * v};
}
float Dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
float Length(Vec3 p)
{
	return std::sqrt(Dot(p, p));
}
Vec3 Position(const Matrix &m)
{
	return {m[12], m[13], m[14]};
}

// Side-effect free: unlike Find, never runs the timeline check that can clear
// gPeople underneath a caller's Person pointer.
bool Articulated(const uint8_t *ped)
{
	if (!gRef || !api.PoolsGetPed)
		return false;
	uint8_t *entity = const_cast<uint8_t *>(ped);
	const int ref = gRef(entity);
	for (const Person &p : gPeople)
		if (p.handle == ref && api.PoolsGetPed(ref) == ped && p.clump == EntityClump(entity))
			return true;
	return false;
}
bool Eligible(const uint8_t *ped)
{
	if (!ped)
		return false;
	const int state = PedState(ped);
	// Tommy only once a native knockdown has articulated him (#25): his own
	// controller owns everything else.
	if (ped == api.FindPlayerPed() && !Articulated(ped))
		return false;
	// Native SetDead clears UsesCollision on every corpse (drowning aside).
	// A body this module already articulates keeps its reaction and its root;
	// a corpse that died natively stays entirely native.
	const bool collision = *reinterpret_cast<const uint32_t *>(ped + 0x60) & kEntityUsesCollision;
	// (The script's CREATE_PLAYER marks Tommy MISSION_CHAR, 0x10008d8a4: mission
	// ownership is about other peds.)
	if ((!collision && !(state == kPedDead && Articulated(ped))) || (PedFromMission(ped) && ped != api.FindPlayerPed()) ||
		PedInVehicle(ped) || *reinterpret_cast<const void *const *>(ped + 0x720) || (gCutscene && *gCutscene))
		return false;
	return state < 0x30 || state == kPedDie || state == kPedDead;
}
bool CheckTimeline()
{
	if (!api.timeMs)
		return false;
	const uint32_t now = *api.timeMs;
	const bool changed = now < gLastFrame || (gLastFrame && now - gLastFrame > 10000);
	gLastFrame = now;
	if (changed) {
		gCrowdClock = false;
		gPeople.clear();
		gContacts = {};
		gWind = {};
		gPedBumps = {};
		gPedMotions = {};
		gResistedBumpLogs = gSprintBumpLogs = 0;
	}
	return changed;
}
Person *Find(uint8_t *ped)
{
	// NPC control/render may precede the player's frame callback after a
	// load. Discard old-world handles before any entry point can acquire one.
	CheckTimeline();
	if (!gRef || !ped)
		return nullptr;
	const int ref = gRef(ped);
	for (Person &p : gPeople)
		if (p.handle == ref && api.PoolsGetPed(ref) == ped && p.clump == EntityClump(ped))
			return &p;
	return nullptr;
}
float *BoneMatrices(uint8_t *ped)
{
	uint8_t *clump = EntityClump(ped);
	uint8_t *hier = clump ? api.GetAnimHierarchyFromSkinClump(clump) : nullptr;
	return hier ? api.HAnimMatrixArray(hier) : nullptr;
}
enum class Capture { Animation, PreRendered };
bool Read(Person &p, uint8_t *ped, rp::Pose &pose, Capture capture = Capture::Animation)
{
	if (!p.clump || EntityClump(ped) != p.clump || (capture == Capture::Animation && !api.UpdateRpHAnim))
		return false;
	// Control/physics captures rebuild raw keyframes. CPed::PreRender already
	// does that before its head-removal/look/IK edits; retain those edits when
	// capturing its result rather than rebuilding the hierarchy a second time.
	if (capture == Capture::Animation)
		api.UpdateRpHAnim(ped);
	float *matrices = BoneMatrices(ped);
	if (!matrices)
		return false;
	float groundOffset = 0;
	if (p.physical.Active() && p.physical.State() == rp::Mode::Balanced && api.ProcessVerticalLine) {
		const CVector root = EntityPos(ped);
		alignas(16) uint8_t point[0x30]{};
		uint8_t *entity = nullptr;
		if (api.ProcessVerticalLine(root, root.z - 1.5f, point, entity, true, false, false, false, false, false, nullptr) &&
			reinterpret_cast<const CVector *>(point + 0x10)->z > .4f &&
			std::isfinite(reinterpret_cast<const CVector *>(point)->z))
			groundOffset = std::max(0.0f, reinterpret_cast<const CVector *>(point)->z + 1.04f - root.z);
	}
	for (int i = 0; i < body::kBones; i++) {
		std::memcpy(p.original[size_t(i)].data(), matrices + p.indices[size_t(i)] * 16, 16 * sizeof(float));
		// Balanced goals and the native side of the render fade share the
		// standing support height. Otherwise a lowered physical root feeds
		// underground idle feet back into both the solver and its fade.
		p.original[size_t(i)][14] += groundOffset;
		const Vec3 position = Position(p.original[size_t(i)]);
		if (!std::isfinite(Length(position)))
			return false;
	}
	for (int i = 0; i < rp::Count; i++)
		pose[size_t(i)] = Position(p.original[size_t(p.joints[size_t(i)])]);
	return true;
}
void Release(Person &p, uint8_t *ped, bool lift = true);
// Corpses keep their articulation, so a full set gives up the settled corpse
// hit longest ago rather than refusing a new reaction. Moving bodies stay.
Person *Releasable()
{
	const uint32_t now = api.timeMs ? *api.timeMs : 0;
	Person *oldest = nullptr;
	for (Person &p : gPeople)
		if (p.physical.Dead() && p.physical.State() == rp::Mode::Settled &&
			(!oldest || now - p.hitAt > now - oldest->hitAt))
			oldest = &p;
	return oldest;
}
Person *Track(uint8_t *ped)
{
	if (Person *p = Find(ped))
		return p->clump == EntityClump(ped) ? p : nullptr;
	if ((gPeople.size() >= kMost && !Releasable()) || !gRef || !api.PoolsGetPed || !api.GetAnimHierarchyFromSkinClump ||
		!api.HAnimIDGetIndex || !api.HAnimMatrixArray || !api.UpdateRpHAnim || !gUpdateMatrix || !gUpdateFrame)
		return nullptr;
	Person fresh;
	fresh.handle = gRef(ped);
	if (fresh.handle < 0 || api.PoolsGetPed(fresh.handle) != ped)
		return nullptr;
	fresh.clump = EntityClump(ped);
	uint8_t *hier = fresh.clump ? api.GetAnimHierarchyFromSkinClump(fresh.clump) : nullptr;
	if (!hier)
		return nullptr;
	for (int i = 0; i < body::kBones; i++) {
		fresh.indices[size_t(i)] = api.HAnimIDGetIndex(hier, kTags[i]);
		if (fresh.indices[size_t(i)] < 0)
			return nullptr;
	}
	for (int i = 0; i < rp::Count; i++) {
		fresh.joints[size_t(i)] = -1;
		for (int bone = 0; bone < body::kBones; bone++)
			if (kTags[bone] == kPhysicalTags[i])
				fresh.joints[size_t(i)] = bone;
		if (fresh.joints[size_t(i)] < 0)
			return nullptr;
	}
	rp::Pose initial;
	gUpdateMatrix(ped + 8);
	gUpdateFrame(ped);
	if (!Read(fresh, ped, initial))
		return nullptr;
	fresh.pelvisOffset = Sub(initial[rp::Pelvis], V(EntityPos(ped)));
	fresh.publishedVelocity = Mul(V(EntitySpeed(ped)), 50);
	const Vec3 neck = Position(fresh.original[4]);
	const Vec3 headAxis{fresh.original[5][0], fresh.original[5][1], fresh.original[5][2]};
	fresh.physical.Reset(initial, fresh.publishedVelocity, &neck, &headAxis);
	fresh.stepped = *api.timeMs;
	if (gPeople.size() >= kMost) {
		// Only after the new body is known valid: give up one settled corpse.
		Person *corpse = Releasable();
		if (!corpse)
			return nullptr;
		Person released = *corpse;
		gPeople.erase(gPeople.begin() + (corpse - gPeople.data()));
		Release(released, api.PoolsGetPed(released.handle));
	}
	gPeople.push_back(fresh);
	return &gPeople.back();
}
void Restore(Person &p, uint8_t *ped)
{
	if (!p.saved || EntityClump(ped) != p.clump)
		return;
	if (float *matrices = BoneMatrices(ped))
		for (int i = 0; i < body::kBones; i++)
			std::memcpy(matrices + p.indices[size_t(i)] * 16, p.original[size_t(i)].data(), 16 * sizeof(float));
	p.saved = false;
}

rp::Joint Piece(int piece)
{
	// ePedPieceTypes: mid/torso0/1, left/right arm2/3, leg4/5, head6.
	switch (piece) {
	case 2:
		return rp::ShoulderL;
	case 3:
		return rp::ShoulderR;
	case 4:
		return rp::KneeL;
	case 5:
		return rp::KneeR;
	case 6:
		return rp::Head;
	default:
		return rp::Chest;
	}
}
void ContactReaction(uint8_t *ped, const uint8_t *points, int count, Vec3 relativeVelocity, Vec3 ownVelocity,
					 bool reverse)
{
	if (!gInstalled || !gConfig.npcReactions || !ped || (EntityFlags(ped) & 7) != 3 || !Eligible(ped) ||
		Health(ped) <= 0 || !points || count <= 0 || !api.timeMs || !gRef || !api.PoolsGetPed)
		return;
	uint8_t *player = api.FindPlayerPed();
	if (!player || Length(Sub(V(EntityPos(ped)), V(EntityPos(player)))) > 60)
		return;
	const int ref = gRef(ped);
	if (ref < 0 || api.PoolsGetPed(ref) != ped || !std::isfinite(Length(relativeVelocity)))
		return;
	Person *existing = Find(ped);
	if (existing && (existing->physical.Dead() || existing->physical.State() != rp::Mode::Balanced ||
					 existing->hitAt == *api.timeMs))
		return;
	const uint32_t now = *api.timeMs;
	ContactStamp *stamp = nullptr;
	for (ContactStamp &entry : gContacts) {
		if (entry.handle == ref && entry.clump == EntityClump(ped)) {
			if (now - entry.at < 250)
				return;
			stamp = &entry;
			break;
		}
	}
	reactcontact::NativePoint best{};
	float closing = .35f;
	for (int i = 0; i < std::min(count, 8); i++) {
		reactcontact::NativePoint point;
		std::memcpy(&point, points + size_t(i) * sizeof point, sizeof point);
		const float normalLength = Length(point.normal);
		if (!std::isfinite(Length(point.point)) || !std::isfinite(normalLength) || normalLength < .5f ||
			normalLength > 1.5f || !std::isfinite(point.depth) || point.depth < 0 || point.depth > 1 ||
			Length(Sub(point.point, V(EntityPos(ped)))) > 2)
			continue;
		point.normal = Mul(point.normal, (reverse ? -1.0f : 1.0f) / normalLength);
		if (point.normal.z > .55f)
			continue; // ordinary weight-bearing floor/slope contact
		const float incoming = -Dot(relativeVelocity, point.normal);
		if (incoming > closing) {
			closing = incoming;
			best = point;
		}
	}
	if (closing <= .35f)
		return;
	Person *p = Track(ped);
	if (!p)
		return;
	const rp::Pose &pose = p->physical.Positions();
	const int piece = reverse ? best.pieceB : best.pieceA;
	rp::Joint joint = Piece(piece);
	if (piece == 4 || piece == 5) {
		const rp::Joint foot = piece == 4 ? rp::FootL : rp::FootR;
		if (Length(Sub(best.point, pose[foot])) < Length(Sub(best.point, pose[joint])))
			joint = foot;
	} else if (piece < 1 || piece > 6) {
		float nearest = 100;
		for (int i = 0; i < rp::Count; i++) {
			const float distance = Length(Sub(best.point, pose[size_t(i)]));
			if (distance < nearest) { nearest = distance; joint = rp::Joint(i); }
		}
	}
	// This is an observation of native locomotion collision, not a new force.
	// Brace locally while the world pass retains accepted root movement. Real
	// damage, vehicle impacts and wind have their separate physical entry paths.
	p->physical.ContactResponse(joint, Mul(best.normal, std::min(closing, 8.0f)), best.normal, ownVelocity,
	                            rp::ContactControl::Native);
	if (!stamp)
		stamp = &*std::max_element(gContacts.begin(), gContacts.end(), [now](const ContactStamp &a, const ContactStamp &b) {
			return (a.handle < 0 ? UINT32_MAX : now - a.at) < (b.handle < 0 ? UINT32_MAX : now - b.at);
		});
	*stamp = {ref, EntityClump(ped), now};
}
void Place(Person &p, uint8_t *ped)
{
	const Vec3 origin = Sub(p.physical.Positions()[rp::Pelvis], p.pelvisOffset);
	*reinterpret_cast<float *>(ped + 0x138) = Length(Sub(origin, V(EntityPos(ped))));
	*reinterpret_cast<CVector *>(ped + 0x38) = C(origin);
	EntitySpeed(ped) = C(Mul(p.physical.Velocity(rp::Pelvis), 1.0f / 50));
	p.publishedVelocity = p.physical.Velocity(rp::Pelvis);
	p.publishedAt = *api.timeMs;
	gReindex(ped);
	gUpdateMatrix(ped + 8);
	gUpdateFrame(ped);
}
bool Physical(const Person *p)
{
	return p && p->physical.Active() && (p->physical.State() == rp::Mode::Falling || p->physical.State() == rp::Mode::Settled);
}
bool OwnsRoot(const Person *p)
{
	return p && p->physical.Active() && p->physical.State() != rp::Mode::Balanced;
}
int gCorpseReleaseLogs;
// Hand an articulated body back to native. A ragdoll root sits at its pelvis,
// but native's corpse pose (ped.ifp ko_*/kd_* final frames) lies ~0.9m below
// a standing-height root. Restore that convention above the support under the
// body (as StartGetUp and Read do), or the native corpse is drawn underground.
void Release(Person &p, uint8_t *ped, bool lift)
{
	if (!ped || EntityClump(ped) != p.clump)
		return;
	Restore(p, ped);
	if (p.heldFall && *reinterpret_cast<const uint32_t *>(ped + 0x68c) == UINT32_MAX)
		*reinterpret_cast<uint32_t *>(ped + 0x68c) = p.savedGetUpTimer;
	// Not OwnsRoot: the solver can drop Active (no certified pose under moving
	// geometry) after Place last lowered the root. Setting native's convention
	// is also harmless for a root that was never lowered.
	// A root it owned, dead or alive: the game's corpse, fall and get-up poses
	// all expect a standing-height root. Not where the game itself has put him
	// (a respawn, a teleport, the swim): `lift` false.
	if (p.physical.State() == rp::Mode::Balanced || !lift)
		return;
	const rp::Pose &pose = p.physical.Positions();
	// Its root no longer where the ragdoll left it: the game has placed it
	// since (a teleport, a script), and that placement stands.
	if (Length(Sub(V(EntityPos(ped)), Sub(pose[rp::Pelvis], p.pelvisOffset))) > 2.0f)
		return;
	float support = INFINITY;
	for (int i = 0; i < rp::Count; i++)
		support = std::min(support, pose[size_t(i)].z - rp::Body::Radius(rp::Joint(i)));
	alignas(16) uint8_t point[0x30]{};
	uint8_t *entity = nullptr;
	const Vec3 pelvis = pose[rp::Pelvis];
	if (api.ProcessVerticalLine &&
		api.ProcessVerticalLine(C(pelvis), pelvis.z - 2.5f, point, entity, true, false, false, false, false, false, nullptr) && // static support, as Read/StartGetUp
		reinterpret_cast<const CVector *>(point + 0x10)->z > .4f && std::isfinite(reinterpret_cast<const CVector *>(point)->z))
		support = reinterpret_cast<const CVector *>(point)->z;
	if (!std::isfinite(support))
		return;
	CVector &root = *reinterpret_cast<CVector *>(ped + 0x38);
	const float raised = support + 1.04f - root.z;
	root.z = support + 1.04f;
	EntitySpeed(ped) = {};
	gReindex(ped);
	gUpdateMatrix(ped + 8);
	gUpdateFrame(ped);
	if (gCorpseReleaseLogs < 8) {
		++gCorpseReleaseLogs;
		Log(Health(ped) > 0 ? "reactions: body returned to native mid-fall, root lifted %.2fm to standing height above its support"
		                     : "reactions: corpse returned to native, root lifted %.2fm to native corpse height above its support",
		    raised);
	}
}
void StartGetUp(uint8_t *ped)
{
	CVector &root = *reinterpret_cast<CVector *>(ped + 0x38);
	const CVector physicalRoot = root;
	alignas(16) uint8_t point[0x30]{};
	uint8_t *entity = nullptr;
	if (api.ProcessVerticalLine &&
		api.ProcessVerticalLine(physicalRoot, root.z - 2.5f, point, entity, true, false, false, false, false, false, nullptr) &&
		reinterpret_cast<const CVector *>(point + 0x10)->z > .4f) {
		// Native IsPositionClearOfCars tests a .25 m sphere against buildings
		// too. A prone physical root intersects its floor. Check the native
		// standing root above static support BELOW the body, so a car roof or
		// overhead slab cannot move that check above a genuine obstruction.
		root.z = reinterpret_cast<const CVector *>(point)->z + 1.04f;
	}
	gGetUp(ped);
	root = physicalRoot;
}
void NativeMode(Person &p, uint8_t *ped)
{
	const rp::Mode mode = p.physical.State();
	if (mode == rp::Mode::Falling && p.nativeMode != rp::Mode::Falling && !p.physical.Dead()) {
		if (!p.heldFall)
			p.savedGetUpTimer = *reinterpret_cast<const uint32_t *>(ped + 0x68c);
		p.heldFall = true;
		gSetFall(ped, -1, 27, 1); // KO_skid_back; -1 holds native Fall until physical recovery
	}
	if (mode == rp::Mode::Recovering && Health(ped) > 0) {
		// Its get-up only while the game still has him down: held in its fall, or
		// in PED_GETUP with the animation not started (bGetUpAnimStarted stays
		// clear while a real obstruction prevents it). Its PedGetupCB clears that
		// same flag when the get-up finishes, after restoring his state: then the
		// knockdown's over. Starting it again would replay it from the lying pose
		// (or, a car beside him, leave him standing in PED_GETUP), and his body
		// would keep his root and, Tommy's, his controls (#25).
		const int32_t state = PedState(ped);
		if (state == kPedFall || (state == kPedGetUp && !(ped[0x18c] & 0x20)))
			StartGetUp(ped);
		else if (state != kPedGetUp)
			p.physical.Recovered();
	}
	p.nativeMode = p.physical.State();
}
// The side he was hit, as the game's knockdown animations name it (AnimationId:
// KO front/left/back/right 13-16, shot face/stomach 17-18, spin forward 23-24,
// high impact front/left/back/right 25-28), as a push away from it in his frame.
Vec3 KnockedAway(uint8_t *ped, int animation)
{
	const Vec3 forward = V(EntityForward(ped)), right = V(*reinterpret_cast<const CVector *>(ped + 0x08));
	switch (animation) {
	case 15: case 27: case 23: case 24: return forward;
	case 14: case 26: return right;
	case 16: case 28: return Mul(right, -1);
	default: return Mul(forward, -1); // from the front: 13, 17, 18, 25 and the rest
	}
}
// The game has decided Tommy falls (CPed::SetFall): his ragdoll takes over
// with the velocity he had (a car's, a blast's, a punch's); a trip or bump
// with none falls away from where it came from.
void KnockDown(uint8_t *ped, int animation, Vec3 incoming)
{
	Person *p = Track(ped);
	if (!p)
		return;
	p->hitAt = *api.timeMs;
	Vec3 push = Sub(incoming, p->publishedVelocity);
	if (Length({incoming.x, incoming.y, 0}) < 1.0f)
		push = Add(push, Mul(KnockedAway(ped, animation), 1.5f));
	p->physical.Knockdown(push);
	p->publishedVelocity = incoming;
	// His view (right, facing, up) in his head's frame, the head as the composed
	// pose draws it (as PlayerHeadView reads it): first person rides his head
	// from here as it looked (#25).
	const Vec3 neck = p->physical.Neck(), headAxis = p->physical.HeadAxis();
	const auto pose = reactrig::Apply(p->original, p->physical.Positions(), p->physical.Weight(), &neck, &headAxis);
	const Matrix &head = pose[5];
	const Vec3 axes[3] = {Unit({head[0], head[1], head[2]}), Unit({head[4], head[5], head[6]}), Unit({head[8], head[9], head[10]})};
	const Vec3 view[3] = {V(*reinterpret_cast<const CVector *>(ped + 0x08)), V(EntityForward(ped)),
	                      V(*reinterpret_cast<const CVector *>(ped + 0x28))};
	p->headViewReady = true;
	for (int j = 0; j < 3; ++j)
		for (int k = 0; k < 3; ++k) {
			p->headView[size_t(j)][size_t(k)] = Dot(Unit(view[j]), axes[k]);
			p->headViewReady &= std::isfinite(p->headView[size_t(j)][size_t(k)]);
		}
}
void Hook_SetFall(uint8_t *ped, int duration, int animation, uint8_t direction)
{
	// Only the direct native pedestrian-contact fall is gated. Weapon,
	// environmental and physical-reaction calls outside this scope stay native.
	const bool nativeBump = duration == 3000 && direction == 0 &&
							(animation == 23 || animation == 24 || animation == 26 || animation == 28);
	if (gInstalled && nativeBump && gPedBump && gPedBump->resist &&
		(ped == gPedBump->player || ped == gPedBump->ped)) {
		if (!gPedBump->sprint && gResistedBumpLogs < 8) {
			++gResistedBumpLogs;
			Log("reactions: ped bump resisted closing=%.2fm/s player_move=%d native_anim=%d recipient=%s",
				gPedBump->closing, PedMoveState(gPedBump->player), animation,
				ped == gPedBump->player ? "player" : "npc");
		}
		return;
	}
	// Tommy knocked down (#25): the game's own knockdown first, then his ragdoll.
	// Every one, but one while he's already down (Falling, Settled: its own held
	// fall, -1, re-enters here): getting up, or his ragdoll fading out after,
	// a knockdown is a whole new one, not the game's animation under his ragdoll.
	if (gInstalled && gConfig.npcReactions && api.FindPlayerPed && ped == api.FindPlayerPed() && !Physical(Find(ped)) &&
	    !(gCutscene && *gCutscene) && !PedInVehicle(ped) && playerreact::CanRagdoll(ped)) {
		const Vec3 incoming = Mul(V(EntitySpeed(ped)), 50);
		orig_SetFall(ped, duration, animation, direction);
		if (PedState(ped) == kPedFall) // the game can decline (dying, head under water...)
			KnockDown(ped, animation, incoming);
		return;
	}
	orig_SetFall(ped, duration, animation, direction);
}
void Hook_PedCollision(uint8_t *ped, uint8_t *other)
{
	// macVC CPed::ProcessControl+0x7c8 reaches this through its actual
	// collision entity (+0x140)/force (+0x13c), not an AI proximity scan.
	// Native CollideWithPed+0x770 falls only `other`, using animation speed
	// (+0x788), move state and CPedStats rather than measured closing speed.
	uint8_t *player = api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	uint8_t *npc = ped == player ? other : other == player ? ped : nullptr;
	if (!gInstalled || !gConfig.npcReactions || !player || !npc || (EntityFlags(npc) & 7) != 3 || !Eligible(npc) ||
		!std::isfinite(Health(npc)) || Health(npc) <= 0 || !api.timeMs || !gRef || !api.PoolsGetPed) {
		orig_PedCollision(ped, other);
		return;
	}
	CheckTimeline();
	const int playerRef = gRef(player), npcRef = gRef(npc);
	if (playerRef < 0 || npcRef < 0 || api.PoolsGetPed(playerRef) != player ||
		api.PoolsGetPed(npcRef) != npc) {
		orig_PedCollision(ped, other);
		return;
	}
	const uint32_t now = *api.timeMs;
	uint8_t *const playerClump = EntityClump(player), *const npcClump = EntityClump(npc);
	PedBumpStamp *stamp = nullptr;
	for (auto &entry : gPedBumps)
		if (entry.player == playerRef && entry.ped == npcRef && entry.playerClump == EntityClump(player) &&
			entry.pedClump == EntityClump(npc)) {
			stamp = &entry;
			break;
		}
	const bool repeated = stamp && now >= stamp->seen && now - stamp->seen < 500;
	const Vec3 separation = Sub(V(EntityPos(npc)), V(EntityPos(player)));
	const float distance = Length({separation.x, separation.y, 0});
	Vec3 velocity = Mul(V(EntitySpeed(player)), 50);
	Vec3 npcVelocity = Mul(V(EntitySpeed(npc)), 50);
	const Vec3 relative = Sub(velocity, npcVelocity);
	Vec3 direction = distance > .05f ? Mul({separation.x, separation.y, 0}, 1 / distance) : Vec3{};
	float closing = Dot(relative, direction);
	int contactPiece = 1;
	for (const auto &motion : gPedMotions)
		if (motion.playerRef == playerRef && motion.pedRef == npcRef && motion.playerClump == playerClump &&
			motion.pedClump == npcClump && now >= motion.at && now - motion.at <= 100 &&
			Length(Sub(motion.playerOrigin, V(EntityPos(player)))) < .75f &&
			Length(Sub(motion.pedOrigin, V(EntityPos(npc)))) < .75f) {
			velocity = motion.playerVelocity;
			npcVelocity = motion.pedVelocity;
			closing = motion.closing;
			direction = motion.direction;
			contactPiece = motion.piece;
			break;
		}
	const bool ordinary = playerreact::CanResistContact(player) && PedState(npc) > 0 &&
						  PedState(npc) < kPedJump && std::isfinite(closing) && distance <= 1.2f &&
						  std::fabs(separation.z) < .8f;
	const bool sprint = ordinary && playerreact::CanKnockdown(player) && !repeated &&
						PedMoveState(player) == 5 && closing >= 3.5f &&
						Length({velocity.x, velocity.y, 0}) >= 3.5f;
	const bool walking =
		Length({velocity.x, velocity.y, 0}) <= 2.5f && Length({npcVelocity.x, npcVelocity.y, 0}) <= 2.5f;
	PedBumpScope scope{player, npc, ordinary && (repeated || walking || closing < 2.5f || sprint), closing,
					   sprint};
	{
		struct Binding {
			PedBumpScope *previous;
			explicit Binding(PedBumpScope *value) : previous(gPedBump)
			{
				gPedBump = value;
			}
			~Binding()
			{
				gPedBump = previous;
			}
		} binding(&scope);
		orig_PedCollision(ped, other);
	}
	if (repeated && *api.timeMs == now && gRef(npc) == npcRef && api.PoolsGetPed(npcRef) == npc &&
		EntityClump(npc) == npcClump && EntityClump(player) == playerClump)
		stamp->seen = now;
	if (!sprint || *api.timeMs != now || !playerreact::CanKnockdown(player) || !Eligible(npc) ||
		!std::isfinite(Health(npc)) || Health(npc) <= 0 || PedState(npc) <= 0 || PedState(npc) >= kPedJump ||
		gRef(npc) != npcRef || api.PoolsGetPed(npcRef) != npc || EntityClump(npc) != npcClump ||
		EntityClump(player) != playerClump)
		return;
	if (!stamp)
		stamp = &*std::max_element(gPedBumps.begin(), gPedBumps.end(), [now](const auto &a, const auto &b) {
			return (a.ped < 0 ? UINT32_MAX : now - a.seen) < (b.ped < 0 ? UINT32_MAX : now - b.seen);
		});
	*stamp = {playerRef, npcRef, playerClump, npcClump, now};
	Person *body = Track(npc);
	// A head-on sprint is an actual body collision, independent of which
	// actor's native control found it first. Native owns Tommy's finite fall;
	// the existing articulated controller owns the ambient pedestrian's fall.
	if (body) {
		body->physical.Impulse(Piece(contactPiece), Mul(direction, std::min(7.0f, closing * 1.5f)));
		body->hitAt = now;
		NativeMode(*body, npc);
	} else {
		gSetFall(npc, 1500, 24, 0); // native fallback when the bounded articulated set is full
	}
	gSetFall(player, 1500, 27, 0);
	if (gSprintBumpLogs < 8) {
		++gSprintBumpLogs;
		Log("reactions: reciprocal sprint bump closing=%.2fm/s player_ref=%d npc_ref=%d articulated=%d",
			closing, playerRef, npcRef, body != nullptr);
	}
}
PedContactMotion CapturePedMotion(uint8_t *ped, uint8_t *other, Vec3 velocity, Vec3 otherVelocity)
{
	PedContactMotion result;
	uint8_t *player = api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	uint8_t *npc = ped == player ? other : other == player ? ped : nullptr;
	if (!gInstalled || !gConfig.npcReactions || !player || !npc || (EntityFlags(npc) & 7) != 3 ||
		!Eligible(npc) || !api.timeMs || !gRef || !api.PoolsGetPed)
		return result;
	CheckTimeline();
	result.playerRef = gRef(player);
	result.pedRef = gRef(npc);
	if (result.playerRef < 0 || result.pedRef < 0 || api.PoolsGetPed(result.playerRef) != player ||
		api.PoolsGetPed(result.pedRef) != npc)
		return {};
	result.player = player;
	result.ped = npc;
	result.playerClump = EntityClump(player);
	result.pedClump = EntityClump(npc);
	result.playerOrigin = V(EntityPos(player));
	result.pedOrigin = V(EntityPos(npc));
	result.playerVelocity = ped == player ? velocity : otherVelocity;
	result.pedVelocity = ped == player ? otherVelocity : velocity;
	result.at = *api.timeMs;
	return result;
}
void RecordPedMotion(PedContactMotion motion, uint8_t *ped, const uint8_t *points, int count)
{
	if (!motion.player || !points || count <= 0 || *api.timeMs != motion.at ||
		gRef(motion.player) != motion.playerRef || gRef(motion.ped) != motion.pedRef ||
		api.PoolsGetPed(motion.playerRef) != motion.player || api.PoolsGetPed(motion.pedRef) != motion.ped ||
		EntityClump(motion.player) != motion.playerClump || EntityClump(motion.ped) != motion.pedClump)
		return;
	PedContactMotion *slot = nullptr;
	for (auto &entry : gPedMotions)
		if (entry.playerRef == motion.playerRef && entry.pedRef == motion.pedRef &&
			entry.playerClump == motion.playerClump && entry.pedClump == motion.pedClump) {
			slot = &entry;
			break;
		}
	// CPhysical visits each actor independently. Its second visit sees motion
	// already altered by the first native response; the first record wins.
	if (slot && slot->at == motion.at)
		return;
	bool found = false;
	for (int i = 0; i < std::min(count, 8); ++i) {
		reactcontact::NativePoint point;
		std::memcpy(&point, points + size_t(i) * sizeof point, sizeof point);
		const float length = Length(point.normal);
		if (!std::isfinite(length) || length < .5f || length > 1.5f || !std::isfinite(point.depth) ||
			point.depth < 0 || point.depth > 1 || !std::isfinite(Length(point.point)) ||
			Length(Sub(point.point, motion.pedOrigin)) > 2 ||
			Length(Sub(point.point, motion.playerOrigin)) > 2.5f)
			continue;
		Vec3 normal = Mul(point.normal, (ped == motion.player ? 1.0f : -1.0f) / length);
		if (std::fabs(normal.z) > .55f)
			continue;
		normal = Mul({normal.x, normal.y, 0}, 1 / Length({normal.x, normal.y, 0}));
		const float closing = -Dot(Sub(motion.playerVelocity, motion.pedVelocity), normal);
		if (!std::isfinite(closing) || (found && closing <= motion.closing))
			continue;
		found = true;
		motion.closing = closing;
		motion.direction = Mul(normal, -1);
		motion.piece = ped == motion.ped ? point.pieceA : point.pieceB;
	}
	if (!found)
		return;
	// Retain the incoming contact for at most 100ms after native
	// response has stopped its participants. A genuinely stronger new contact wins.
	if (slot && motion.at >= slot->at && motion.at - slot->at <= 100 && motion.closing <= slot->closing)
		return;
	if (!slot)
		slot = &*std::max_element(gPedMotions.begin(), gPedMotions.end(), [&](const auto &a, const auto &b) {
			return (a.pedRef < 0 ? UINT32_MAX : motion.at - a.at) <
				   (b.pedRef < 0 ? UINT32_MAX : motion.at - b.at);
		});
	*slot = motion;
}
void Advance(Person &p, uint8_t *ped)
{
	if (p.pendingVehicle) {
		// Nonfatal KillPedWithCar writes launch velocity after InflictDamage,
		// and the surrounding native control can apply another move force.
		// Reconcile once that callback returns, before Place can overwrite it.
		const Vec3 native = Mul(V(EntitySpeed(ped)), 50);
		p.physical.Momentum(Sub(native, p.publishedVelocity), Health(ped) <= 0);
		p.publishedVelocity = native;
		p.pendingVehicle = false;
	}
	const uint32_t now = *api.timeMs;
	if (p.stepped == now)
		return;
	const float dt = std::min(.067f, float(now - p.stepped) / 1000);
	p.stepped = now;
	Restore(p, ped);
	gUpdateMatrix(ped + 8);
	gUpdateFrame(ped);
	rp::Pose target;
	if (!Read(p, ped, target))
		return;
	if (Health(ped) <= 0 && !p.physical.Dead())
		p.physical.Impulse(rp::Chest, {}, true);
	uint8_t *ignored = *api.ignoreEntity;
	*api.ignoreEntity = ped;
	p.physical.Advance(dt, target, reactcontact::Sweep, ped);
	*api.ignoreEntity = ignored;
	if (!p.physical.Active()) {
		if (p.heldFall && *reinterpret_cast<const uint32_t *>(ped + 0x68c) == UINT32_MAX)
			*reinterpret_cast<uint32_t *>(ped + 0x68c) = p.savedGetUpTimer;
		p.heldFall = false;
		return;
	}
	NativeMode(p, ped);
	if (OwnsRoot(&p))
		Place(p, ped);
}
bool WindRecipient(uint8_t *ped)
{
	return ped && (EntityFlags(ped)&7)==3 && (EntityFlags(ped)&kEntityUsesCollision) &&
		!PedFromMission(ped) && !PedInVehicle(ped) && !*reinterpret_cast<void **>(ped+0x720) &&
		!(gCutscene && *gCutscene) && !(gReplay && *gReplay==1) &&
		std::isfinite(Health(ped)) && Health(ped)>0 && PedState(ped)>0 && PedState(ped)<0x30;
}
void PublishWind(uint8_t *ped,CVector beforeControl)
{
	if (!gInstalled || !api.timeMs || !api.PoolsGetPed)
		return;
	CheckTimeline();
	if (!ObserveWindWorld()) return;
	const uint32_t now=*api.timeMs;
	for (WindStamp &wind:gWind) {
		if (wind.handle<0 || !SameWindWorld(wind.world,WindWorld()) || api.PoolsGetPed(wind.handle)!=ped)
			continue;
		const WindStamp pending=wind;
		wind={}; // collision/control retries never deliver it twice
		if (now<pending.at || now-pending.at>100 || EntityClump(ped)!=pending.clump || !WindRecipient(ped))
			return;
		// UpdatePosition1920b0..1922ec overwrites standing, unattached XY;
		// airborne actors retain the native impulse already published by Wind.
		if (!(PedFlags(ped)&kPedStanding) || *reinterpret_cast<void **>(ped+0x480) ||
			(ped[0x158]==0x1a && (*reinterpret_cast<const float *>(ped+0x148)!=0 || *reinterpret_cast<const float *>(ped+0x14c)!=0)))
			return;
		if (Person *body=Find(ped); gConfig.npcReactions && body && OwnsRoot(body))
			return; // Advance/Place publishes the full articulated velocity
		// Match the actual result, not just eligibility for UpdatePosition:
		// native early returns retain the already-published impulse.
		const CVector after=EntitySpeed(ped);
		const CVector desired{*reinterpret_cast<const float *>(ped+0x464),*reinterpret_cast<const float *>(ped+0x468),0};
		if ((after.x==beforeControl.x && after.y==beforeControl.y) ||
			std::fabs(after.x-desired.x)>1e-6f || std::fabs(after.y-desired.y)>1e-6f)
			return;
		EntitySpeed(ped).x+=pending.delta.x/50;
		EntitySpeed(ped).y+=pending.delta.y/50;
		return;
	}
}
// Tommy put somewhere else by the game (a respawn after wasted, a script's
// teleport): his ragdoll lets go, and he stays where the game put him (#25).
bool Displaced(const Person &p, uint8_t *ped)
{
	if (!ped || !api.FindPlayerPed || ped != api.FindPlayerPed())
		return false;
	const Vec3 root = Sub(p.physical.Positions()[rp::Pelvis], p.pelvisOffset);
	return (p.physical.Dead() && Health(ped) > 0) || Length(Sub(V(EntityPos(ped)), root)) > 10 ||
	       !playerreact::CanStayDown(ped);
}
void Hook_Control(uint8_t *ped)
{
	{
		Person *p = Find(ped);
		const bool displaced = p && Displaced(*p, ped);
		if (p && (!gConfig.npcReactions || !Eligible(ped) || displaced)) {
			// Native control may finish a vehicle transition before returning.
			// Retire the old body first: eligibility at the end of that call
			// cannot grant it ownership of the newly placed on-foot actor.
			Person retired = *p;
			gPeople.erase(gPeople.begin() + (p - gPeople.data()));
			Release(retired, ped, !displaced);
		}
	}
	const CVector beforeControl=EntitySpeed(ped);
	if (gConfig.npcReactions && Eligible(ped))
		if (Person *p = Find(ped); Physical(p) && !p->physical.Dead()) {
			if (!p->heldFall) {
				p->savedGetUpTimer = *reinterpret_cast<const uint32_t *>(ped + 0x68c);
				p->heldFall = true;
			}
			*reinterpret_cast<uint32_t *>(ped + 0x68c) = UINT32_MAX;
		}
	orig_Control(ped);
	PublishWind(ped,beforeControl);
	Person *p = Find(ped);
	if (!p || !gConfig.npcReactions || !Eligible(ped))
		return;
	// Supported actors retain controller velocity until native ProcessCollision
	// has accepted its move and wall/car separation. Advancing and publishing
	// here would erase that velocity before the native world pass can use it.
	if (OwnsRoot(p) || p->pendingVehicle)
		Advance(*p, ped);
}
int Hook_Collision(uint8_t *ped, uint8_t *other, uint8_t *points)
{
	// Keep collision enabled for bullets/targeting. Only the native physical
	// contact response is replaced while the articulated body owns a fall.
	if (gConfig.npcReactions && Eligible(ped) && Physical(Find(ped)))
		return 0;
	const Vec3 incoming = Mul(V(EntitySpeed(ped)), 50);
	const int otherType = other ? EntityFlags(other) & 7 : 0;
	const Vec3 otherVelocity = otherType >= 2 && otherType <= 4 ? Mul(V(EntitySpeed(other)), 50) : Vec3{};
	const PedContactMotion motion=CapturePedMotion(ped,other,incoming,otherVelocity);
	const int count = orig_Collision(ped, other, points);
	RecordPedMotion(motion,ped,points,count);
	uint8_t *player = api.FindPlayerPed();
	if (points && player && (ped == player || other == player))
		for (int i = 0; i < std::min(count, 8); i++) {
			reactcontact::NativePoint point;
			std::memcpy(&point, points + size_t(i) * sizeof point, sizeof point);
			const bool reverse = other == player;
			playerreact::Contact(player, point.point, Mul(point.normal, reverse ? -1.0f : 1.0f),
							 reverse ? Sub(otherVelocity, incoming) : Sub(incoming, otherVelocity));
		}
	ContactReaction(ped, points, count, Sub(incoming, otherVelocity), incoming, false);
	if (otherType == 3)
		ContactReaction(other, points, count, Sub(otherVelocity, incoming), otherVelocity, true);
	return count;
}
void Hook_WorldCollision(uint8_t *entity)
{
	if (gConfig.npcReactions && (EntityFlags(entity) & 7) == 3 && Eligible(entity))
		if (Person *p = Find(entity); OwnsRoot(p)) {
			// CWorld retries unsafe bodies, then ProcessShift integrates them
			// again. Match successful native ProcessCollision bookkeeping while
			// retaining UsesCollision for bullets and entity queries.
			EntityFlags(entity) = (EntityFlags(entity) & ~0x2000u) | 0x4000u;
			*reinterpret_cast<uint16_t *>(entity + 0x156) &= ~uint16_t(0x4c0);
			if (p->publishedAt != *api.timeMs)
				*reinterpret_cast<float *>(entity + 0x138) = 0;
			return;
		}
	orig_WorldCollision(entity);
	if (gConfig.npcReactions && (EntityFlags(entity) & 7) == 3 && Eligible(entity))
		if (Person *p = Find(entity); p && p->physical.Active()) {
			// Native root movement already happened once. A supported reaction
			// only poses the body around that accepted animation target; it never
			// republishes a second root or replaces the native slide velocity.
			if (!OwnsRoot(p) && !p->pendingVehicle)
				p->publishedVelocity = Mul(V(EntitySpeed(entity)), 50);
			Advance(*p, entity);
		}
}

void Hook_WorldProcess()
{
	orig_WorldProcess();
	if (!gInstalled || !gConfig.npcReactions || !api.timeMs || !api.PoolsGetPed ||
		(gCutscene && *gCutscene) || (gReplay && *gReplay == 1))
		return;
	if (CheckTimeline())
		return;
	const uint32_t now = *api.timeMs;
	if (gCrowdClock && gCrowdAt == now)
		return;
	gCrowdClock = true;
	gCrowdAt = now;
	struct Actor {
		int handle = -1;
		uint8_t *ped = nullptr, *clump = nullptr;
		uint32_t revision = 0;
		rp::Body body;
	};
	std::array<Actor, kMost> actors{};
	size_t count = 0;
	uint8_t *player = api.FindPlayerPed();
	for (const Person &person : gPeople) {
		uint8_t *ped = api.PoolsGetPed(person.handle);
		if (count == actors.size() || !ped || EntityClump(ped) != person.clump || !Eligible(ped) ||
			!OwnsRoot(&person) || person.stepped != now || !player ||
			Length(Sub(V(EntityPos(ped)), V(EntityPos(player)))) > 100)
			continue;
		actors[count++] = {person.handle, ped, person.clump, person.physical.Revision(), person.physical};
	}
	if (count < 2)
		return;
	std::sort(actors.begin(), actors.begin() + count, [](const Actor &a, const Actor &b) { return a.handle < b.handle; });
	std::array<reactcrowd::Entry, kMost> entries{};
	for (size_t i = 0; i < count; ++i)
		entries[i] = {&actors[i].body, actors[i].ped};
	// Native collision queries can be wrapped by another mod. Solve copies,
	// then validate every participant before committing any part of the batch.
	// A recycled pool slot or nested damage event cannot receive stale state.
	reactcrowd::Solve(entries.data(), count, reactcontact::Sweep);
	for (size_t i = 0; i < count; ++i) {
		const Actor &actor = actors[i];
		if (!gConfig.npcReactions || (gReplay && *gReplay == 1) || *api.timeMs != now || api.PoolsGetPed(actor.handle) != actor.ped ||
			EntityClump(actor.ped) != actor.clump || !Eligible(actor.ped))
			return;
		Person *person = Find(actor.ped);
		if (!person || !OwnsRoot(person) || person->physical.Revision() != actor.revision)
			return;
	}
	for (size_t i = 0; i < count; ++i) {
		const Actor &actor = actors[i];
		if (!gConfig.npcReactions || (gReplay && *gReplay == 1) || *api.timeMs != now ||
			api.PoolsGetPed(actor.handle) != actor.ped || EntityClump(actor.ped) != actor.clump || !Eligible(actor.ped))
			return;
		Person *live = Find(actor.ped);
		if (!live || !OwnsRoot(live) || live->physical.Revision() != actor.revision)
			return;
		Person &person = *live;
		if (person.physical.Revision() == actors[i].body.Revision())
			continue;
		const Vec3 oldPosition = person.physical.Positions()[rp::Pelvis];
		const Vec3 oldVelocity = person.physical.Velocity(rp::Pelvis);
		person.physical = actors[i].body;
		if (Length(Sub(oldPosition, person.physical.Positions()[rp::Pelvis])) > 1e-7f ||
			Length(Sub(oldVelocity, person.physical.Velocity(rp::Pelvis))) > 1e-7f)
			Place(person, actors[i].ped);
	}
}
} // namespace

bool PlayerDown()
{
	uint8_t *player = gInstalled && api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	const Person *p = player ? Find(player) : nullptr;
	return p && p->physical.Active() && p->physical.State() != rp::Mode::Balanced;
}

bool PlayerHeadView(Vec3 &eye, Vec3 &right, Vec3 &forward, Vec3 &up)
{
	uint8_t *player = gInstalled && api.FindPlayerPed ? api.FindPlayerPed() : nullptr;
	const Person *p = player ? Find(player) : nullptr;
	if (!p || !p->headViewReady || !p->physical.Active() || p->physical.State() == rp::Mode::Balanced)
		return false;
	// The same composed pose that's drawn (and shot at), its head bone.
	const Vec3 neck = p->physical.Neck(), axis = p->physical.HeadAxis();
	const auto pose = reactrig::Apply(p->original, p->physical.Positions(), p->physical.Weight(), &neck, &axis);
	const Matrix &head = pose[5];
	const Vec3 axes[3] = {Unit({head[0], head[1], head[2]}), Unit({head[4], head[5], head[6]}), Unit({head[8], head[9], head[10]})};
	Vec3 view[3];
	for (int j = 0; j < 3; ++j)
		view[j] = Add(Add(Mul(axes[0], p->headView[size_t(j)][0]), Mul(axes[1], p->headView[size_t(j)][1])),
		              Mul(axes[2], p->headView[size_t(j)][2]));
	// Kept a proper frame: facing, then up square to it, then right.
	forward = Unit(view[1]);
	up = Unit(Sub(view[2], Mul(forward, Dot(view[2], forward))));
	right = Cross(forward, up);
	if (!std::isfinite(Length(forward)) || Length(forward) < .5f || Length(up) < .5f)
		return false;
	eye = Add(Position(head), Add(Mul(up, .08f), Mul(forward, .06f))); // as camera.cpp's eye from his head
	return true;
}

bool Wind(uint8_t *ped, Vec3 deltaVelocity)
{
	if (!gInstalled || !api.FindPlayerPed || !api.timeMs || !gRef || !api.PoolsGetPed ||
		!WindRecipient(ped) || !std::isfinite(Length(deltaVelocity)) || Length(deltaVelocity)<=0 || Length(deltaVelocity)>.8f)
		return false;
	CheckTimeline();
	if (!ObserveWindWorld()) return false;
	const WindPool world=WindWorld();
	if (!world.header) return false;
	const int ref=gRef(ped);
	if (ref<0 || api.PoolsGetPed(ref)!=ped || !SameWindWorld(world,WindWorld()))
		return false;
	WindStamp *queued=nullptr;
	for (WindStamp &wind:gWind)
		if (wind.handle==ref) {queued=&wind;break;}
	if (!queued)
		for (WindStamp &wind:gWind)
			if (wind.handle<0 || !SameWindWorld(wind.world,world) || *api.timeMs<wind.at || *api.timeMs-wind.at>100 || api.PoolsGetPed(wind.handle)==nullptr) {queued=&wind;break;}
	if (!queued)
		return false;
	Person *p=gConfig.npcReactions && Eligible(ped)?Track(ped):nullptr;
	if (!SameWindWorld(world,WindWorld()) || api.PoolsGetPed(ref)!=ped || (p && (p->handle!=ref || p->clump!=EntityClump(ped) || p->physical.Dead())))
		return false;
	const Vec3 native=Mul(V(EntitySpeed(ped)),50);
	if (!std::isfinite(Length(native)))
		return false;
	if (p) {
		// Reconcile a vehicle's late native publication before adding wash;
		// Advance must never import our newly published wind a second time.
		if (p->pendingVehicle) {
			p->physical.Momentum(Sub(native,p->publishedVelocity),false);
			p->pendingVehicle=false;
		}
		p->physical.Momentum(deltaVelocity,false);
	}
	EntitySpeed(ped)=C(Mul({native.x+deltaVelocity.x,native.y+deltaVelocity.y,native.z+deltaVelocity.z},1.0f/50));
	const Vec3 previous=queued->handle==ref && queued->clump==EntityClump(ped) && SameWindWorld(queued->world,world) && *api.timeMs>=queued->at && *api.timeMs-queued->at<=100?queued->delta:Vec3{};
	*queued={ref,EntityClump(ped),*api.timeMs,{previous.x+deltaVelocity.x,previous.y+deltaVelocity.y,previous.z+deltaVelocity.z},world};
	if (p) {
		p->publishedVelocity=Mul(V(EntitySpeed(ped)),50);
		p->publishedAt=*api.timeMs;
		NativeMode(*p,ped); // accumulated real momentum decides the fall
	}
	return true;
}

void Hit(uint8_t *ped, uint8_t *by, int weapon, int piece)
{
	if (!gInstalled || !gConfig.npcReactions || !Eligible(ped) || weapon == 43 || weapon == 31 || weapon == 14)
		return;
	Person *p = Find(ped);
	const bool created = !p;
	if (!p)
		p = Track(ped);
	if (!p)
		return;
	p->hitAt = *api.timeMs;
	const bool fatal = Health(ped) <= 0 || PedState(ped) == kPedDie || PedState(ped) == kPedDead;
	float strength = weapon < 12 ? 1.3f : weapon >= 19 && weapon <= 21 ? 2.5f : weapon == 18 ? 1.2f : .65f;
	if (weapon == 30)
		strength = 3.0f;
	shotimpact::Event impact;
	if (shotimpact::Current(ped, weapon, impact)) {
		const reactimpact::Selection selected = reactimpact::Resolve(p->physical.Positions(), impact.position, piece);
		const float rayLength = Length(impact.direction);
		if (selected.valid && std::isfinite(rayLength) && rayLength > .001f) {
			// Native bullets can retain a vehicle as their shooter when no
			// driver is present. Its scoped ray takes precedence over generic
			// vehicle damage, whose momentum arrives after the callback.
			p->physical.Impact(selected.a, selected.b, selected.along, Mul(impact.direction, strength / rayLength), fatal);
			return;
		}
	}
	if (by && IsVehicle(by)) {
		p->pendingVehicle = true;
		p->physical.Momentum({}, fatal);
		return;
	}
	if (weapon == 41) {
		// TriggerExplosionSectorList applies native force BEFORE InflictDamage
		// (weapon41); `by` is the creator, not the blast's position. Import its
		// actual momentum delta, including repeated blasts on an active body.
		const Vec3 native = Mul(V(EntitySpeed(ped)), 50);
		p->physical.Momentum(created ? Vec3{} : Sub(native, p->publishedVelocity), fatal);
		p->publishedVelocity = native;
		return;
	}
	// Melee, environmental damage, and bullets with no matching scoped native
	// event retain an explicit coarse fallback; attacker position never replaces
	// a verified bullet ray or its actual point along the struck limb.
	Vec3 direction = by ? Sub(V(EntityPos(ped)), V(EntityPos(by))) : V(EntitySpeed(ped));
	float length = Length(direction);
	if (!std::isfinite(length) || length < .001f)
		direction = Mul(V(EntityForward(ped)), -1);
	else
		direction = Mul(direction, 1 / length);
	if (weapon == 44) {
		strength = .2f;
		direction = {0, 0, -1};
	}
	p->physical.Impulse(Piece(piece), Mul(direction, strength), fatal);
}

void BeforePreRender(uint8_t *ped)
{
	if (Person *p = Find(ped))
		Restore(*p, ped);
}

bool CopyHitPose(uint8_t *ped, reactrig::Matrices &pose)
{
	Person *p = Find(ped);
	if (!gInstalled || !gConfig.npcReactions || !p || !Eligible(ped) || !p->physical.Active())
		return false;
	// Copy before composing: no native animation rebuild, matrix writes, or
	// ownership changes belong in a collision query.
	const auto physical = p->physical;
	const auto original = p->original;
	const int handle = p->handle;
	const auto *clump = p->clump;
	const Vec3 neck = physical.Neck(), axis = physical.HeadAxis();
	auto composed = reactrig::Apply(original, physical.Positions(), physical.Weight(), &neck, &axis);
	Person *current = Find(ped);
	if (!current || current->handle != handle || current->clump != clump ||
	    current->physical.Revision() != physical.Revision() || !Eligible(ped))
		return false;
	pose = composed;
	return true;
}

void AfterPreRender(uint8_t *ped)
{
	Person *p = Find(ped);
	if (!gConfig.npcReactions || !p || !Eligible(ped) || !p->physical.Active())
		return;
	rp::Pose target;
	if (!Read(*p, ped, target, Capture::PreRendered))
		return;
	float *matrices = BoneMatrices(ped);
	if (!matrices)
		return;
	const Vec3 neck = p->physical.Neck();
	const Vec3 headAxis = p->physical.HeadAxis();
	const reactrig::Matrices posed = reactrig::Apply(p->original, p->physical.Positions(), p->physical.Weight(), &neck, &headAxis);
	for (int b = 0; b < body::kBones; b++)
		std::memcpy(matrices + p->indices[size_t(b)] * 16, posed[size_t(b)].data(), sizeof(float) * 16);
	p->saved = true;
}

void Frame()
{
	if (!gInstalled)
		return;
	ObserveWindWorld();
	if (CheckTimeline()) {
		// A load reconstructs pools and may reuse both generations and clump
		// addresses. Never dereference an old-world handle to restore its state.
		gPeople.clear();
		return;
	}
	const bool reset = !gConfig.npcReactions || (gCutscene && *gCutscene);
	uint8_t *player = api.FindPlayerPed();
	for (auto it = gPeople.begin(); it != gPeople.end();) {
		uint8_t *ped = api.PoolsGetPed(it->handle);
		const float distance = ped && player ? Length(Sub(V(EntityPos(ped)), V(EntityPos(player)))) : 1e9f;
		if (reset || !ped || EntityClump(ped) != it->clump || !Eligible(ped) || !it->physical.Active() ||
			distance > 100 || Displaced(*it, ped)) {
			Release(*it, ped, !Displaced(*it, ped));
			it = gPeople.erase(it);
		} else
			++it;
	}
}

bool Install()
{
	// The entity contact observer also feeds Tommy's protective response when
	// the optional NPC controller starts disabled.
	if (!api.UpdateRpHAnim || !api.GetAnimHierarchyFromSkinClump || !api.HAnimIDGetIndex || !api.HAnimMatrixArray ||
	    !api.PoolsGetPed || !api.ProcessVerticalLine || !api.TestSphereAgainstWorld || !api.ignoreEntity)
		return false;
	auto sym = [](const char *name) { return dlsym(RTLD_MAIN_ONLY, name); };
	gRef = reinterpret_cast<decltype(gRef)>(sym("_ZN6CPools9GetPedRefEP4CPed"));
	gReplay = reinterpret_cast<uint8_t *>(sym("_ZN7CReplay4ModeE"));
	gWindPool = reinterpret_cast<uint8_t **>(sym("_ZN6CPools11ms_pPedPoolE"));
	gWindFrame = reinterpret_cast<uint32_t *>(sym("_ZN6CTimer14m_FrameCounterE"));
	gUpdateMatrix = reinterpret_cast<decltype(gUpdateMatrix)>(sym("_ZN7CMatrix8UpdateRWEv"));
	gUpdateFrame = reinterpret_cast<decltype(gUpdateFrame)>(sym("_ZN7CEntity13UpdateRwFrameEv"));
	gReindex = reinterpret_cast<decltype(gReindex)>(sym("_ZN9CPhysical12RemoveAndAddEv"));
	gSetFall = reinterpret_cast<decltype(gSetFall)>(sym("_ZN4CPed7SetFallEi11AnimationIdh"));
	gGetUp = reinterpret_cast<decltype(gGetUp)>(sym("_ZN4CPed8SetGetUpEv"));
	gCutscene = static_cast<bool *>(sym("_ZN12CCutsceneMgr10ms_runningE"));
	if (!gRef || !gUpdateMatrix || !gUpdateFrame || !gReindex || !gSetFall || !gGetUp || !gCutscene)
		return false;
	if (!reactcontact::Install())
		return false;
	gPeople.reserve(kMost);
	gInstalled =
		hooks::Install("_ZN4CPed7SetFallEi11AnimationIdh", Hook_SetFall, orig_SetFall) &&
		hooks::Install("_ZN4CPed14CollideWithPedEPS_", Hook_PedCollision, orig_PedCollision) &&
		hooks::Install("_ZN4CPed14ProcessControlEv", Hook_Control, orig_Control) &&
		hooks::Install("_ZN4CPed22ProcessEntityCollisionEP7CEntityP9CColPoint", Hook_Collision, orig_Collision) &&
		hooks::Install("_ZN9CPhysical16ProcessCollisionEv", Hook_WorldCollision, orig_WorldCollision) &&
		hooks::Install("_ZN6CWorld7ProcessEv", Hook_WorldProcess, orig_WorldProcess);
	if (gInstalled)
		Log("reactions: articulated NPC reactions ready, at most %d, four fixed steps per frame", kMost);
	return gInstalled;
}
} // namespace reactions
