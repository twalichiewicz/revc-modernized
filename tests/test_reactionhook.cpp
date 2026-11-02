// Drive the actual runtime collision hook with a tiny game/pool fixture.
// The native pass is a callback because this test must not launch macVC.
#include "../src/reactions.cpp"
#include "../src/shotimpact.h"
#include <cstdio>

namespace game {
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}
namespace hooks {
void *Install(const char *, void *)
{
	return nullptr;
}
} // namespace hooks
namespace playerreact {
static bool canRagdoll = true, canStayDown = true;
bool CanRagdoll(uint8_t *) { return canRagdoll; }
bool CanStayDown(uint8_t *) { return canStayDown; }
bool CanKnockdown(uint8_t *) { return true; }
bool CanResistContact(uint8_t *) { return true; }
static int contacts;
static uint8_t *lastPed;
static Vec3 lastPoint, lastNormal, lastVelocity;
void Contact(uint8_t *ped, Vec3 point, Vec3 normal, Vec3 relativeVelocity)
{
	++contacts;
	lastPed = ped;
	lastPoint = point;
	lastNormal = normal;
	lastVelocity = relativeVelocity;
}
} // namespace playerreact
namespace shotimpact {
static bool available;
static Event event;
bool Current(uint8_t *, int, Event &result)
{
	result = event;
	return available;
}
} // namespace shotimpact

static uint8_t gPed[0x8d0]{}, gOther[0x8d0]{}, gPlayer[0x8d0]{};
static uint8_t gCar[0x6f0]{};
static uint8_t windPoolHeader[32]{},windPoolFlags[2]{},*windPoolPointer=windPoolHeader;
static uint8_t *gLive = gPed;
static int gNativeCalls, gBadPoolCasts;
static uint32_t gNow = 1000,windFrameNumber=100;
static int gFalls, gGetUps, gBadDurations;
static void Fall(uint8_t *ped, int duration, int, uint8_t)
{
	gFalls++;
	gBadDurations += duration != -1;
	*reinterpret_cast<int32_t *>(ped + 0x2fc) = game::kPedFall;
	*reinterpret_cast<uint32_t *>(ped + 0x68c) = UINT32_MAX;
}
static void GetUp(uint8_t *ped)
{
	gGetUps++;
	*reinterpret_cast<int32_t *>(ped + 0x2fc) = game::kPedGetUp;
	ped[0x18c] |= 0x20; // native bGetUpAnimStarted; SetGetUp does not clear the fall timer
}
static bool gGetUpBlocked, gGetUpStarted;
static float gGetUpObstacleBottom, gGetUpObstacleTop;
static bool gGetUpObstacleVehicle;
static void PlacedGetUp(uint8_t *ped)
{
	*reinterpret_cast<int32_t *>(ped + 0x2fc) = game::kPedGetUp;
	// The actual CPedPlacement::IsPositionClearOfCars uses a .25 m sphere
	// against buildings AND vehicles. A prone entity root intersects a floor.
	const float z = game::EntityPos(ped).z;
	if (z < .25f || gGetUpBlocked ||
		(gGetUpObstacleTop > 0 && z + .25f > gGetUpObstacleBottom && z - .25f < gGetUpObstacleTop))
		return;
	ped[0x18c] |= 0x20;
	gGetUpStarted = true;
}
static bool NativeGround(const CVector &from, float toZ, uint8_t *point, uint8_t *&entity,
						 bool buildings, bool vehicles, bool, bool, bool, bool, void *)
{
	entity = nullptr;
	if (gGetUpObstacleTop > 0 && (gGetUpObstacleVehicle ? vehicles : buildings) &&
		from.z >= gGetUpObstacleTop && toZ <= gGetUpObstacleTop) {
		*reinterpret_cast<CVector *>(point) = {from.x, from.y, gGetUpObstacleTop};
		*reinterpret_cast<CVector *>(point + 0x10) = {0, 0, 1};
		return true;
	}
	if (from.z < 0 || toZ > 0)
		return false;
	*reinterpret_cast<CVector *>(point) = {from.x, from.y, 0};
	*reinterpret_cast<CVector *>(point + 0x10) = {0, 0, 1};
	return true;
}
static reactphysics::Pose Standing()
{
	return {{{0, 0, .95f},
			 {0, 0, 1.35f},
			 {0, 0, 1.7f},
			 {-.22f, 0, 1.4f},
			 {-.35f, 0, 1.1f},
			 {-.4f, 0, .8f},
			 {.22f, 0, 1.4f},
			 {.35f, 0, 1.1f},
			 {.4f, 0, .8f},
			 {-.12f, 0, .9f},
			 {-.12f, 0, .5f},
			 {-.12f, 0, .1f},
			 {.12f, 0, .9f},
			 {.12f, 0, .5f},
			 {.12f, 0, .1f}}};
}
static reactphysics::Contact Ground(Vec3, Vec3 p, float radius, void *)
{
	if (p.z >= radius)
		return {p, {}, false};
	p.z = radius;
	return {p, {0, 0, 1}, true};
}
static int Ref(uint8_t *p)
{
	if (p == gPlayer)
		return 31;
	if (p != gPed && p != gOther)
		gBadPoolCasts++;
	return 17;
}
static uint8_t *Lookup(int handle)
{
	if (handle == 31)
		return gPlayer;
	return handle == 17 ? gLive : nullptr;
}
static uint8_t *Player()
{
	return gPlayer;
}
static void Native(uint8_t *entity)
{
	gNativeCalls++;
	*reinterpret_cast<float *>(entity + 0x38) += EntitySpeed(entity).x;
}
static void Noop(uint8_t *) {}
static void LateCarControl(uint8_t *ped)
{
	reactions::Hit(ped, gCar, 39, 1);
	// The nonfatal KillPedWithCar branch publishes vehicle motion after its
	// InflictDamage callback, not before it.
	game::EntitySpeed(ped) = {.12f, .04f, .01f};
}

static uint8_t gContactClump[16]{};
static float gContactRig[body::kBones * 16]{};
static reactcontact::NativePoint gContactPoints[2]{};
static int gContactCount = 1, gCollisionCalls;
static CVector gNativeAccepted{};
static uint8_t *ContactHierarchy(uint8_t *clump) { return clump == gContactClump ? clump : nullptr; }
static float *ContactMatrices(uint8_t *) { return gContactRig; }
static int ContactBone(uint8_t *, int tag)
{
	for (int i = 0; i < body::kBones; i++)
		if (reactions::kTags[i] == tag)
			return i;
	return -1;
}
static int NativeContacts(uint8_t *ped, uint8_t *, uint8_t *points)
{
	++gCollisionCalls;
	if (points && gContactCount > 0)
		std::memcpy(points, gContactPoints, size_t(std::min(gContactCount, 2)) * sizeof gContactPoints[0]);
	// Native contact generation can already change motion (for example its
	// support pass, or a slide along the obstacle). The observer needs the
	// incoming motion, captured first; the reaction keeps what native accepted.
	game::EntitySpeed(ped) = gNativeAccepted;
	return gContactCount;
}
static bool PassiveContacts()
{
	int failures = 0;
	auto check = [&](bool good, const char *message) {
		if (!good) { printf("FAIL: %s\n", message); ++failures; }
	};
	// A routine native contact braces the pose without adding velocity. Measure
	// that brace against an identical unbraced body over the same target and
	// floor: hands (relative to the chest, so native-owned centre motion cancels
	// out) reach toward the obstacle and lift; the far foot steps with the push.
	struct Brace { Vec3 reach, step; bool balanced; };
	auto brace = [](const reactphysics::Body &reacting) {
		using namespace reactphysics;
		Body braced = reacting, control;
		const Vec3 neck = reacting.Neck(), axis = reacting.HeadAxis();
		control.Reset(reacting.Positions(), reacting.Velocity(Pelvis), &neck, &axis);
		Brace result{{}, {}, true};
		for (int frame = 0; frame < 12; frame++) {
			braced.Advance(1.0f / 60, Standing(), Ground, nullptr);
			control.Advance(1.0f / 60, Standing(), Ground, nullptr);
			result.balanced &= braced.State() == Mode::Balanced;
		}
		for (Joint hand : {HandL, HandR}) {
			const Vec3 own = reactions::Sub(braced.Positions()[hand], braced.Positions()[Chest]);
			const Vec3 still = reactions::Sub(control.Positions()[hand], control.Positions()[Chest]);
			const Vec3 shift = reactions::Mul(reactions::Sub(own, still), .5f);
			result.reach = {result.reach.x + shift.x, result.reach.y + shift.y, result.reach.z + shift.z};
		}
		for (Joint foot : {FootL, FootR}) {
			Vec3 moved = reactions::Sub(braced.Positions()[foot], control.Positions()[foot]);
			moved.z = 0;
			if (reactions::Length(moved) > reactions::Length(result.step))
				result.step = moved;
		}
		return result;
	};
	auto unforced = [](const reactphysics::Body &body, Vec3 accepted) {
		for (int joint = 0; joint < reactphysics::Count; joint++)
			if (reactions::Length(reactions::Sub(body.Velocity(reactphysics::Joint(joint)), accepted)) > 1e-4f)
				return false;
		return true;
	};
	auto reset = [&] {
		gNow += 1000;
		reactions::gLastFrame = gNow + 1;
		reactions::CheckTimeline();
		std::memset(gPed, 0, sizeof gPed);
		std::memset(gPlayer, 0, sizeof gPlayer);
		gLive = gPed;
		*reinterpret_cast<uint32_t *>(gPed + 0x60) = 3 | game::kEntityUsesCollision;
		*reinterpret_cast<uint32_t *>(gPlayer + 0x60) = 3 | game::kEntityUsesCollision;
		*reinterpret_cast<uint32_t *>(gCar + 0x60) = 2;
		*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, 1.04f};
		*reinterpret_cast<CVector *>(gPlayer + 0x38) = {0, -2, 1.04f};
		*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
		*reinterpret_cast<float *>(gPed + 0x44c) = 100;
		*reinterpret_cast<int32_t *>(gPed + 0x2fc) = game::kPedIdle;
		game::EntitySpeed(gPed) = {-.02f, 0, 0};
		game::EntitySpeed(gCar) = {};
		std::memset(gContactRig, 0, sizeof gContactRig);
		gContactRig[4 * 16 + 14] = 1.56f;
		const auto standing = Standing();
		for (int joint = 0; joint < reactphysics::Count; joint++) {
			const int bone = ContactBone(nullptr, reactions::kPhysicalTags[joint]);
			float *matrix = gContactRig + bone * 16;
			matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
			matrix[12] = standing[size_t(joint)].x;
			matrix[13] = standing[size_t(joint)].y;
			matrix[14] = standing[size_t(joint)].z;
		}
		gContactPoints[0] = {};
		gContactPoints[0].point = {-.22f, 0, 1.4f};
		gContactPoints[0].normal = {1, 0, 0};
		gContactPoints[0].pieceA = 2;
		gContactPoints[0].depth = .02f;
		gContactCount = 1;
		gNativeAccepted = {};
		gConfig.npcReactions = true;
		reactions::gInstalled = true;
		reactions::gCutscene = nullptr;
		reactions::orig_Collision = NativeContacts;
		reactions::gUpdateMatrix = Noop;
		reactions::gUpdateFrame = Noop;
		game::api.UpdateRpHAnim = Noop;
		game::api.GetAnimHierarchyFromSkinClump = ContactHierarchy;
		game::api.HAnimMatrixArray = ContactMatrices;
		game::api.HAnimIDGetIndex = ContactBone;
	};
	alignas(16) uint8_t points[2 * sizeof(reactcontact::NativePoint)]{};
	reset();
	const int beforeCalls = gCollisionCalls;
	check(reactions::Hook_Collision(gPed, gCar, points) == 1 && gCollisionCalls == beforeCalls + 1,
		  "passive observation must preserve the native contact call and return value");
	check(!std::memcmp(points, gContactPoints, sizeof gContactPoints[0]),
		  "passive observation must preserve the native 40-byte contact record");
	check(reactions::gPeople.size() == 1, "a real nondamaging contact must create a local reaction");
	if (reactions::gPeople.size() == 1) {
		const auto &body = reactions::gPeople[0].physical;
		const Brace first = brace(body);
		check(body.State() == reactphysics::Mode::Balanced && unforced(body, {}),
			  "a small bump must leave native locomotion its accepted motion instead of injecting closing speed");
		check(first.balanced && first.reach.x < -.03f && first.reach.z > .004f && first.step.x > .02f,
			  "a small bump must raise a protective brace toward the contacted side while retaining balance");
		const uint32_t braced = body.Revision();
		game::EntitySpeed(gPed) = {-.02f, 0, 0};
		gNow += 100;
		reactions::Hook_Collision(gPed, gCar, points);
		check(reactions::gPeople[0].physical.Revision() == braced,
			  "persistent contact must not brace again on every native collision retry");
		gNow += 300;
		game::EntitySpeed(gPed) = {.02f, 0, 0};
		gContactPoints[0].point = {.22f, 0, 1.4f};
		gContactPoints[0].normal = {-1, 0, 0};
		gContactPoints[0].pieceA = 3;
		reactions::Hook_Collision(gPed, gCar, points);
		const auto &later = reactions::gPeople[0].physical;
		const Brace opposite = brace(later);
		check(later.Revision() != braced && later.State() == reactphysics::Mode::Balanced && unforced(later, {}),
			  "a later contact from the opposite side must be accepted as a new native brace");
		check(opposite.balanced && opposite.reach.x > .03f && opposite.reach.z > .004f && opposite.step.x < -.02f,
			  "a later contact from the opposite side must turn the ongoing brace toward its own direction");
	}
	check(game::Health(gPed) == 100, "passive reactions must not create fake health damage");
	reset();
	game::EntitySpeed(gPed) = {};
	game::EntitySpeed(gPlayer) = {.02f, 0, 0};
	gContactPoints[0].normal = {-1, 0, 0};
	gContactPoints[0].pieceA = 1;
	gContactPoints[0].pieceB = 2;
	reactions::Hook_Collision(gPlayer, gPed, points);
	check(reactions::gPeople.size() == 1, "a player-owned native collision must create the contacted NPC's reaction");
	if (reactions::gPeople.size() == 1) {
		// Unreversed, this normal would point away from the closing player and
		// produce no reaction; reversed, the NPC braces toward the player.
		const Brace pushed = brace(reactions::gPeople[0].physical);
		check(pushed.balanced && pushed.reach.x < -.03f && pushed.step.x > .02f && unforced(reactions::gPeople[0].physical, {}),
			  "a player-owned native collision must brace the contacted NPC using the reversed contact");
		const uint32_t once = reactions::gPeople[0].physical.Revision();
		game::EntitySpeed(gPed) = {-.02f, 0, 0};
		gContactPoints[0].normal = {1, 0, 0};
		gContactPoints[0].pieceA = 2;
		reactions::Hook_Collision(gPed, gPlayer, points);
		check(reactions::gPeople[0].physical.Revision() == once,
			  "the opposite native collision ordering must not count the same actor contact twice");
	}
	check(game::Health(gPed) == 100, "a player bump must retain native health authority");
	reset();
	// Running diagonally into a low obstruction: native rejects the 3m/s normal
	// component and accepts a 1.5m/s slide along it.
	game::EntitySpeed(gPed) = {.06f, .03f, 0};
	gNativeAccepted = {0, .03f, 0};
	gContactPoints[0].point = {-.12f, 0, .1f};
	gContactPoints[0].normal = {-1, 0, 0};
	gContactPoints[0].pieceA = 4;
	reactions::Hook_Collision(gPed, gCar, points);
	check(reactions::gPeople.size() == 1 && unforced(reactions::gPeople[0].physical, {0, 1.5f, 0}),
		  "a native foot obstruction must keep the accepted slide, neither braking the foot nor replaying rejected speed");
	if (reactions::gPeople.size() == 1) {
		auto sliding = reactions::gPeople[0].physical;
		float worst = 0;
		for (int frame = 1; frame <= 30; frame++) {
			auto target = Standing();
			for (Vec3 &joint : target) joint.y += 1.5f * frame / 60;
			sliding.Advance(1.0f / 60, target, Ground, nullptr);
			worst = std::max(worst, std::fabs(sliding.Velocity(reactphysics::Pelvis).y - 1.5f));
		}
		check(sliding.State() == reactphysics::Mode::Balanced && worst < .1f &&
			  std::fabs(sliding.Positions()[reactphysics::Pelvis].y - .75f) < .03f,
			  "the accepted native trajectory must keep carrying a braced foot-obstructed body");
	}
	bool tripped = false;
	if (!reactions::gPeople.empty())
		for (int frame = 0; frame < 120; frame++) {
			reactions::gPeople[0].physical.Advance(1.0f / 60, Standing(), Ground, nullptr);
			tripped |= reactions::gPeople[0].physical.State() == reactphysics::Mode::Falling;
		}
	check(!tripped, "a nondamaging native foot collision must brace without taking over locomotion or forcing a trip");
	check(game::Health(gPed) == 100, "a trip must not inflict synthetic damage");
	for (int reject = 0; reject < 14; reject++) {
		reset();
		bool cutscene = true;
		switch (reject) {
		case 0: gContactPoints[0].normal = {0, 0, 1}; game::EntitySpeed(gPed) = {0, 0, -.1f}; break;
		case 1: gContactCount = 0; break;
		case 2: gLive = gOther; break;
		case 3: gPed[0x19a] = 2; break;
		case 4: gPed[0x4c0] = 1; break;
		case 5: *reinterpret_cast<void **>(gPed + 0x720) = gCar; break;
		case 6: reactions::gCutscene = &cutscene; break;
		case 7: *reinterpret_cast<CVector *>(gPlayer + 0x38) = {200, 0, 1}; break;
		case 8: gConfig.npcReactions = false; break;
		case 9: game::EntitySpeed(gPed) = {.02f, 0, 0}; break;
		case 10: game::EntitySpeed(gPed) = {-.001f, 0, 0}; break;
		case 11: gContactPoints[0].point.x = std::nanf(""); break;
		case 12: *reinterpret_cast<uint8_t **>(gPed + 0x58) = nullptr; break;
		case 13: *reinterpret_cast<float *>(gPed + 0x44c) = 0; break;
		}
		reactions::Hook_Collision(gPed, gCar, points);
		check(reactions::gPeople.empty(), "support, absent/outgoing contacts and excluded/stale/distant actors must not react");
		reactions::gCutscene = nullptr;
	}
	reset();
	gContactCount = 2;
	gContactPoints[1] = gContactPoints[0];
	gContactPoints[0].normal = {0, 0, 1};
	reactions::Hook_Collision(gPed, gCar, points);
	check(reactions::gPeople.size() == 1, "a second real contact must be decoded at the verified 40-byte stride");
	reset();
	reactions::gPeople.resize(12);
	reactions::Hook_Collision(gPed, gCar, points);
	check(reactions::gPeople.size() == 12, "passive contacts must respect the bounded active set");
	check(!gBadPoolCasts, "passive contacts must not cast players or physical scenery into the NPC pool");
	reset();
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = nullptr;
	return failures == 0;
}

static const reactphysics::Pose gCapturedIdleOffsets = {{{-.007935f, -.020996f, -.023850f},
	{-.023590f, -.057740f, .279228f}, {-.000549f, .027252f, .680600f},
	{-.210541f, .028686f, .516036f}, {-.273437f, .022979f, .227828f}, {-.259521f, .133117f, -.035996f},
	{.178040f, -.081025f, .510221f}, {.247101f, -.134827f, .228465f}, {.261017f, -.100312f, -.055330f},
	{-.115814f, .006958f, -.026221f}, {-.179321f, .041290f, -.477658f}, {-.207428f, -.103302f, -.862066f},
	{.083405f, -.040467f, -.030477f}, {.110230f, -.037842f, -.486856f}, {.099152f, -.091065f, -.894905f}}};
static void CapturedIdleMatrices(uint8_t *ped)
{
	const CVector root = game::EntityPos(ped);
	for (int bone = 0; bone < body::kBones; bone++) {
		float *matrix = gContactRig + bone * 16;
		matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
		matrix[12] = root.x; matrix[13] = root.y; matrix[14] = root.z;
	}
	for (int joint = 0; joint < reactphysics::Count; joint++) {
		float *matrix = gContactRig + ContactBone(nullptr, reactions::kPhysicalTags[joint]) * 16;
		matrix[12] += gCapturedIdleOffsets[size_t(joint)].x;
		matrix[13] += gCapturedIdleOffsets[size_t(joint)].y;
		matrix[14] += gCapturedIdleOffsets[size_t(joint)].z;
	}
	float *neck = gContactRig + 4 * 16;
	neck[12] -= .015594f; neck[13] -= .026215f; neck[14] += .549091f;
}
static bool GroundedBalancedFade()
{
	// Actual npc-bump-after before/one-second captures, translated so the
	// road is z=0. The physical foot spheres are grounded while the published
	// root has dropped from 1.040248 to .490562; raw idle goals then sink.
	const reactphysics::Pose physicalSnapshot = {{{-.008362f, -.019226f, .466431f},
		{-.022217f, -.042267f, .770765f}, {.026458f, .054535f, 1.169301f},
		{-.111420f, .160858f, .990890f}, {-.216889f, .313202f, .761277f}, {.048187f, .373688f, .850709f},
		{.141906f, -.150086f, 1.009379f}, {.243164f, -.172272f, .733142f}, {.287567f, .098724f, .652428f},
		{-.098328f, .046478f, .468503f}, {-.152466f, .397888f, .181111f}, {-.158844f, -.003235f, .088773f},
		{.077423f, -.052033f, .447981f}, {.311431f, .108734f, .089638f}, {.052673f, -.211426f, .089210f}}};
	reactions::Person p;
	p.clump = gContactClump;
	p.pelvisOffset = gCapturedIdleOffsets[reactphysics::Pelvis];
	for (int bone = 0; bone < body::kBones; bone++) p.indices[size_t(bone)] = bone;
	for (int joint = 0; joint < reactphysics::Count; joint++)
		p.joints[size_t(joint)] = ContactBone(nullptr, reactions::kPhysicalTags[joint]);
	p.physical.Reset(physicalSnapshot);
	p.physical.Impulse(reactphysics::ShoulderL, {});
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
	*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, .490562f};
	game::api.UpdateRpHAnim = CapturedIdleMatrices;
	CapturedIdleMatrices(gPed);
	reactphysics::Pose target{};
	if (!reactions::Read(p, gPed, target) || target[reactphysics::FootL].z < .13f || target[reactphysics::FootR].z < .13f) {
		puts("FAIL: Balanced capture inherited the lowered physical root and placed native foot goals underground");
		return false;
	}
	const auto first = target;
	for (int bone = 0; bone < body::kBones; bone++)
		std::memcpy(gContactRig + bone * 16, p.original[size_t(bone)].data(), 16 * sizeof(float));
	if (!reactions::Read(p, gPed, target) || std::fabs(target[reactphysics::Pelvis].z - first[reactphysics::Pelvis].z) > 1e-5f ||
		std::fabs(game::EntityPos(gPed).z - .490562f) > 1e-6f) {
		puts("FAIL: repeated grounded captures accumulated translation or moved the physical entity root");
		return false;
	}
	for (float height : {1.4f, 3.0f}) {
		*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, height};
		if (!reactions::Read(p, gPed, target) || std::fabs(target[reactphysics::Pelvis].z -
			(height + gCapturedIdleOffsets[reactphysics::Pelvis].z)) > 1e-5f) {
			puts("FAIL: Balanced grounding lowered an airborne captured pose toward a floor below it");
			return false;
		}
	}
	float previousPelvis = physicalSnapshot[reactphysics::Pelvis].z;
	for (int frame = 0; frame < 180 && p.physical.Active(); frame++) {
		const Vec3 pelvis = p.physical.Positions()[reactphysics::Pelvis];
		*reinterpret_cast<CVector *>(gPed + 0x38) = {pelvis.x - p.pelvisOffset.x, pelvis.y - p.pelvisOffset.y,
			pelvis.z - p.pelvisOffset.z};
		if (!reactions::Read(p, gPed, target)) return false;
		p.physical.Advance(1.0f / 60, target, Ground, nullptr);
		const auto rendered = reactrig::Apply(p.original, p.physical.Positions(), p.physical.Weight());
		for (int foot : {18, 21}) if (rendered[size_t(foot)][14] < .075f) {
			puts("FAIL: Balanced blend toward native animation put a captured bump foot through the floor");
			return false;
		}
		if (std::fabs(rendered[1][14] - previousPelvis) > .08f) {
			puts("FAIL: grounded Balanced handoff teleported the captured bump pelvis during its fade");
			return false;
		}
		previousPelvis = rendered[1][14];
		for (int bone = 0; bone < body::kBones; bone++)
			std::memcpy(gContactRig + bone * 16, rendered[size_t(bone)].data(), 16 * sizeof(float));
	}
	if (p.physical.Active() || p.physical.Positions()[reactphysics::Pelvis].z < .92f) {
		puts("FAIL: grounded Balanced target failed to restore standing height before releasing physical ownership");
		return false;
	}
	game::api.UpdateRpHAnim = Noop;
	return true;
}
static int gAnimationRebuilds;
static void CountedCapturedIdle(uint8_t *ped)
{
	++gAnimationRebuilds;
	CapturedIdleMatrices(ped);
}
static bool NativeRenderEdits()
{
	for (bool removedHead : {false, true}) {
		reactions::Person p;
		p.handle = 17;
		p.clump = gContactClump;
		for (int bone = 0; bone < body::kBones; bone++) p.indices[size_t(bone)] = bone;
		for (int joint = 0; joint < reactphysics::Count; joint++)
			p.joints[size_t(joint)] = ContactBone(nullptr, reactions::kPhysicalTags[joint]);
		*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, .490562f};
		*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
		*reinterpret_cast<float *>(gPed + 0x44c) = removedHead ? 0 : 100;
		*reinterpret_cast<int32_t *>(gPed + 0x2fc) = removedHead ? game::kPedDead : game::kPedIdle;
		auto pose = gCapturedIdleOffsets;
		for (Vec3 &joint : pose) joint.z += removedHead ? .490562f : 1.04f;
		p.physical.Reset(pose);
		p.physical.Impulse(reactphysics::Head, {}, removedHead);
		reactions::gPeople = {p};
		game::api.UpdateRpHAnim = CountedCapturedIdle;
		float previousPelvis = 0;
		for (int frame = 0; frame < 3; frame++) {
			reactions::BeforePreRender(gPed);
			// Real CPed::PreRender rebuilds native keyframes first, then applies
			// head removal and native post-animation look/IK/scale edits.
			CountedCapturedIdle(gPed);
			float *head = gContactRig + 5 * 16;
			float *elbow = gContactRig + 8 * 16;
			const float look[3][3] = {{0, .8f, 0}, {-.8f, 0, 0}, {0, 0, .8f}};
			for (int axis = 0; axis < 3; axis++) for (int row = 0; row < 3; row++) {
				head[axis * 4 + row] = removedHead ? 0 : look[axis][row];
				elbow[axis * 4 + row] = look[axis][row];
			}
			const int rebuilds = gAnimationRebuilds;
			reactions::AfterPreRender(gPed);
			if (gAnimationRebuilds != rebuilds) {
				puts("FAIL: reaction render capture rebuilt keyframes over native PreRender bone edits");
				return false;
			}
			for (int axis = 0; axis < 3; axis++) {
				float scale = 0;
				for (int row = 0; row < 3; row++) {
					scale += head[axis * 4 + row] * head[axis * 4 + row];
					if (std::fabs(elbow[axis * 4 + row] - look[axis][row]) > 1e-4f ||
						std::fabs(reactions::gPeople[0].original[5][axis * 4 + row] -
							(removedHead ? 0 : look[axis][row])) > 1e-4f) {
						puts("FAIL: physical render erased native captured look/IK edits");
						return false;
					}
				}
				if (std::fabs(scale - (removedHead ? 0 : .64f)) > 1e-4f) {
					puts("FAIL: physical render restored a removed head or erased its native scale");
					return false;
				}
			}
			const auto &source = reactions::gPeople[0].original;
			if ((frame && std::fabs(source[1][14] - previousPelvis) > 1e-5f) ||
				(!removedHead && (source[18][14] < .13f || source[21][14] < .13f))) {
				puts("FAIL: repeated native PreRender capture accumulated grounding or lowered its native fade feet");
				return false;
			}
			previousPelvis = source[1][14];
		}
	}
	reactions::gPeople.clear();
	game::api.UpdateRpHAnim = Noop;
	return true;
}

static int gSupportedWorldCalls;
static uint8_t gStaticFloor[0x78]{};
static void (*gWorldQueryEvent)();
// Export the exact native world ABI so reactcontact::Install exercises its
// real binding and sweep path without needing a running game.
extern "C" reactcontact::NativePoint gaTempSphereColPoints[1];
reactcontact::NativePoint gaTempSphereColPoints[1];
uint8_t *SupportedSphere(CVector, float, uint8_t *, bool, bool, bool, bool, bool, bool)
	asm("__ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb");
uint8_t *SupportedSphere(CVector center, float radius, uint8_t *, bool, bool, bool, bool, bool, bool)
{
	if (gWorldQueryEvent) {
		auto event = gWorldQueryEvent;
		gWorldQueryEvent = nullptr;
		event();
	}
	auto &point = gaTempSphereColPoints[0];
	point = {};
	if (center.z < radius) {
		point.point = {center.x, center.y, 0};
		point.normal = {0, 0, 1};
		point.depth = radius - center.z;
		game::EntityFlags(gStaticFloor)=1|game::kEntityUsesCollision;
		return gStaticFloor;
	}
	if (center.x + radius > .5f) {
		point.point = {.5f, center.y, center.z};
		point.normal = {-1, 0, 0};
		point.depth = center.x + radius - .5f;
		return gCar;
	}
	return nullptr;
}
static void SupportedControl(uint8_t *ped)
{
	// A pursuing on-foot NPC continues asking to run into the car/wall.
	game::EntitySpeed(ped) = {.02f, .01f, 0};
}
static int SupportedContacts(uint8_t *ped, uint8_t *, uint8_t *points)
{
	std::memcpy(points, gContactPoints, sizeof gContactPoints[0]);
	game::EntitySpeed(ped).x = 0; // blocked normal motion, retain the tangential motion
	return 1;
}
static void SupportedWorld(uint8_t *ped)
{
	++gSupportedWorldCalls;
	// Native CPhysical::ProcessCollision integrates controller speed ONCE,
	// calls ProcessEntityCollision, and retains the accepted blocking result.
	const CVector incoming = game::EntitySpeed(ped);
	const CVector before = game::EntityPos(ped);
	alignas(16) uint8_t points[sizeof(reactcontact::NativePoint)]{};
	if (before.x + incoming.x * .8f > .01f) {
		gContactPoints[0] = {};
		gContactPoints[0].point = {.25f, before.y, 1.4f};
		gContactPoints[0].normal = {-1, 0, 0};
		gContactPoints[0].pieceA = 2;
		gContactPoints[0].depth = .02f;
		if (reactions::Hook_Collision(ped, gCar, points))
			*reinterpret_cast<float *>(ped + 0x38) = .01f;
		else
			*reinterpret_cast<float *>(ped + 0x38) += incoming.x * .8f;
	} else
		*reinterpret_cast<float *>(ped + 0x38) += incoming.x * .8f;
	*reinterpret_cast<float *>(ped + 0x3c) += game::EntitySpeed(ped).y * .8f;
	game::EntityFlags(ped) = (game::EntityFlags(ped) & ~0x2000u) | 0x4000u;
}
static bool NativeSupportedOwnership()
{
	gNow += 1000;
	reactions::gPeople.clear();
	reactions::gContacts = {};
	gLive = gPed;
	std::memset(gPed, 0, sizeof gPed);
	*reinterpret_cast<uint32_t *>(gPed + 0x60) = 3 | game::kEntityUsesCollision;
	*reinterpret_cast<uint32_t *>(gCar + 0x60) = 2;
	*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, 1.04f};
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
	*reinterpret_cast<float *>(gPed + 0x44c) = 100;
	game::EntitySpeed(gCar) = {};
	game::api.UpdateRpHAnim = CapturedIdleMatrices;
	CapturedIdleMatrices(gPed);
	game::api.GetAnimHierarchyFromSkinClump = ContactHierarchy;
	game::api.HAnimMatrixArray = ContactMatrices;
	game::api.HAnimIDGetIndex = ContactBone;
	reactions::gUpdateMatrix = Noop;
	reactions::gUpdateFrame = Noop;
	reactions::gReindex = Noop;
	reactions::orig_Control = SupportedControl;
	reactions::orig_Collision = SupportedContacts;
	reactions::orig_WorldCollision = SupportedWorld;
	uint8_t *ignored = nullptr;
	game::api.ignoreEntity = &ignored;
	if (!reactcontact::Install()) {
		puts("FAIL: native ownership fixture could not bind its real world-query symbols");
		return false;
	}
	auto *p = reactions::Track(gPed);
	if (!p) return false;
	p->physical.Impulse(reactphysics::ShoulderR, {.1f, 0, 0});
	gSupportedWorldCalls = 0;
	gNow += 16;
	reactions::Hook_Control(gPed);
	reactions::Hook_WorldCollision(gPed);
	if (gSupportedWorldCalls != 1 || std::fabs(game::EntityPos(gPed).x - .01f) > 1e-5f ||
		std::fabs(game::EntityPos(gPed).y - .008f) > 1e-5f ||
		std::fabs(game::EntitySpeed(gPed).x) > 1e-6f || std::fabs(game::EntitySpeed(gPed).y - .01f) > 1e-6f) {
		puts("FAIL: supported reaction lost native wall/car blocking and tangential movement, or integrated twice");
		return false;
	}
	if (game::Health(gPed) != 100 || !(game::EntityFlags(gPed) & game::kEntityUsesCollision)) {
		puts("FAIL: supported native collision changed health or disabled bullet collision");
		return false;
	}
	for (int frame = 1; frame < 180; frame++) {
		gNow += 16;
		reactions::Hook_Control(gPed);
		reactions::Hook_WorldCollision(gPed);
		if (gSupportedWorldCalls != frame + 1 ||
			reactions::gPeople[0].physical.State() != reactphysics::Mode::Balanced ||
			std::fabs(game::EntityPos(gPed).y - .008f * float(frame + 1)) > .0001f ||
			std::fabs(reactions::gPeople[0].physical.Positions()[reactphysics::Pelvis].y -
				(game::EntityPos(gPed).y + gCapturedIdleOffsets[reactphysics::Pelvis].y)) > .25f ||
			reactions::gPeople[0].physical.Positions()[reactphysics::FootL].z < .084f ||
			reactions::gPeople[0].physical.Positions()[reactphysics::FootR].z < .084f) {
			puts("FAIL: repeated supported wall/car contact collapsed or permanently froze a healthy moving NPC");
			return false;
		}
	}
	const Vec3 beforeBlast = reactions::gPeople[0].physical.Velocity(reactphysics::FootL);
	game::EntitySpeed(gPed).x += .08f;
	reactions::Hit(gPed, gOther, 41, 1);
	const Vec3 afterBlast = reactions::gPeople[0].physical.Velocity(reactphysics::FootL);
	if (std::fabs(afterBlast.x - beforeBlast.x - 4) > .001f ||
		std::fabs(afterBlast.y - beforeBlast.y) > .001f) {
		puts("FAIL: supported native locomotion was counted again as explosion momentum");
		return false;
	}
	game::api.ignoreEntity = nullptr;
	reactions::gPeople.clear();
	return true;
}
static bool PlayerNativeContacts()
{
	gConfig.npcReactions = false;
	*reinterpret_cast<uint32_t *>(gPlayer + 0x60) = 3 | game::kEntityUsesCollision;
	*reinterpret_cast<uint32_t *>(gPed + 0x60) = 3 | game::kEntityUsesCollision;
	reactions::orig_Collision = NativeContacts;
	gContactCount = 1;
	gContactPoints[0] = {};
	gContactPoints[0].point = {.2f, -.1f, 1.3f};
	gContactPoints[0].normal = {-1, 0, 0};
	alignas(16) uint8_t points[sizeof(reactcontact::NativePoint)]{};
	playerreact::contacts = 0;
	game::EntitySpeed(gPlayer) = {.04f, .01f, 0};
	game::EntitySpeed(gPed) = {.01f, 0, 0};
	reactions::Hook_Collision(gPlayer, gPed, points);
	if (playerreact::contacts != 1 || playerreact::lastPed != gPlayer ||
		std::fabs(playerreact::lastPoint.z - 1.3f) > 1e-6f || playerreact::lastNormal.x != -1 ||
		std::fabs(playerreact::lastVelocity.x - 1.5f) > 1e-6f ||
		std::fabs(playerreact::lastVelocity.y - .5f) > 1e-6f) {
		puts("FAIL: real Tommy native contacts were not forwarded independently of NPC reactions");
		return false;
	}
	game::EntitySpeed(gPlayer) = {.04f, .01f, 0};
	game::EntitySpeed(gPed) = {.01f, 0, 0};
	gContactPoints[0].normal = {1, 0, 0};
	reactions::Hook_Collision(gPed, gPlayer, points);
	if (playerreact::contacts != 2 || playerreact::lastNormal.x != -1 ||
		std::fabs(playerreact::lastVelocity.x - 1.5f) > 1e-6f) {
		puts("FAIL: reversed native collision ordering inverted Tommy's protective contact");
		return false;
	}
	const auto animation = game::api.UpdateRpHAnim;
	game::api.UpdateRpHAnim = nullptr;
	const bool skippedBinding = reactions::Install();
	game::api.UpdateRpHAnim = animation;
	if (skippedBinding) {
		puts("FAIL: disabling NPC reactions skipped native observer installation required by Tommy contacts");
		return false;
	}
	gConfig.npcReactions = true;
	return true;
}
static bool AcceptedSpatialImpact()
{
	gNow += 100;
	reactions::Person p;
	p.handle = 17;
	p.clump = gContactClump;
	const auto pose = Standing();
	p.physical.Reset(pose);
	reactions::gPeople = {p};
	*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, 1.04f};
	*reinterpret_cast<CVector *>(gOther + 0x38) = {-2, 0, 1.04f};
	shotimpact::available = true;
	shotimpact::event = {pose[reactphysics::FootL], {0, -1, 0}};
	reactions::Hit(gPed, gOther, 17, 4);
	const auto &body = reactions::gPeople[0].physical;
	if (body.Velocity(reactphysics::FootL).y > -.6f ||
		std::fabs(body.Velocity(reactphysics::KneeL).y) > .01f ||
		std::fabs(body.Velocity(reactphysics::FootL).x) > .01f) {
		puts("FAIL: accepted native foot point/ray did not override coarse knee and attacker-root direction");
		return false;
	}
	p.physical.Reset(pose);
	reactions::gPeople = {p};
	// Native DoBulletImpact can retain the firing vehicle as its shooter.
	// A matching scoped ray still describes a bullet, not a car collision.
	*reinterpret_cast<uint32_t *>(gCar + 0x60) = 2;
	reactions::Hit(gPed, gCar, 17, 4);
	if (reactions::gPeople[0].pendingVehicle ||
		reactions::gPeople[0].physical.Velocity(reactphysics::FootL).y > -.6f) {
		puts("FAIL: a verified vehicle-origin bullet was mistaken for vehicle collision momentum");
		return false;
	}
	for (bool invalid : {false, true}) {
		p.physical.Reset(pose);
		reactions::gPeople = {p};
		shotimpact::available = invalid;
		shotimpact::event.direction.x = std::nanf("");
		reactions::Hit(gPed, gOther, 17, 4);
		if (reactions::gPeople[0].physical.Velocity(reactphysics::KneeL).x < .6f ||
			std::fabs(reactions::gPeople[0].physical.Velocity(reactphysics::FootL).x) > .01f) {
			puts("FAIL: absent/invalid native shot context lost its explicit coarse damage fallback");
			return false;
		}
	}
	shotimpact::available = false;
	reactions::gPeople.clear();
	return true;
}
static bool ReplacedClumpOwnership()
{
	gNow += 100;
	reactions::Person p;
	p.handle = 17;
	p.clump = gContactClump;
	p.physical.Reset(Standing());
	p.physical.Impulse(reactphysics::Chest, {6, 0, 0});
	reactions::gPeople = {p};
	// Model streaming can replace a clump before the player's Frame cleanup.
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = gOther;
	game::EntitySpeed(gPed) = {.02f, 0, 0};
	reactions::orig_WorldCollision = Native;
	const int calls = gNativeCalls;
	reactions::Hook_WorldCollision(gPed);
	*reinterpret_cast<uint8_t **>(gPed + 0x58) = gContactClump;
	reactions::gPeople.clear();
	if (gNativeCalls != calls + 1) {
		puts("FAIL: a replaced native clump retained the old body's physical root ownership");
		return false;
	}
	return true;
}

#ifdef REACTION_WIND_BASELINE
namespace reactions {bool Wind(uint8_t *,Vec3){return false;}}
#endif
// Wind reaches the real solver and its native controller consumer. In normal
// standing CPed control, UpdatePosition replaces XY with animation velocity;
// the active body must retain wind accumulated before that replacement.
static void WindNativeControl(uint8_t *ped)
{
 game::EntitySpeed(ped).x=game::EntitySpeed(ped).y=0;
}
static void WalkingWindControl(uint8_t *ped){game::EntitySpeed(ped).x=*reinterpret_cast<float *>(ped+0x464);game::EntitySpeed(ped).y=*reinterpret_cast<float *>(ped+0x468);}
static bool NondamagingWind()
{
 int failures=0;auto check=[&](bool ok,const char *text){if(!ok){std::printf("FAIL: %s\n",text);++failures;}};
 gNow+=1000;reactions::gPeople.clear();reactions::gLastFrame=gNow;gLive=gPed;
 std::memset(gPed,0,sizeof gPed);std::memset(gPlayer,0,sizeof gPlayer);
 *reinterpret_cast<uint32_t *>(gPed+0x60)=3|game::kEntityUsesCollision;
 *reinterpret_cast<int32_t *>(gPed+0x2fc)=game::kPedIdle;
 *reinterpret_cast<float *>(gPed+0x44c)=100;
 *reinterpret_cast<uint8_t **>(gPed+0x58)=gContactClump;
 *reinterpret_cast<CVector *>(gPed+0x38)={-10,0,1.04f};
 game::api.GetAnimHierarchyFromSkinClump=ContactHierarchy;game::api.HAnimMatrixArray=ContactMatrices;
 game::api.HAnimIDGetIndex=ContactBone;game::api.UpdateRpHAnim=Noop;
 reactions::gUpdateMatrix=reactions::gUpdateFrame=reactions::gReindex=Noop;
 reactions::gSetFall=Fall;reactions::gGetUp=GetUp;reactions::gInstalled=true;gConfig.npcReactions=true;
 reactions::gCutscene=nullptr;reactions::orig_Control=WindNativeControl;reactions::orig_WorldCollision=Native;
 // This tests open-field wind retention; the shared collision fixture has
 // a wall at X=.5, which the complete arm capsules now correctly block.
 const auto standing=Standing();std::memset(gContactRig,0,sizeof gContactRig);
 for(int joint=0;joint<reactphysics::Count;++joint){float *m=gContactRig+ContactBone(nullptr,reactions::kPhysicalTags[joint])*16;m[0]=m[5]=m[10]=m[15]=1;m[12]=standing[joint].x-10;m[13]=standing[joint].y;m[14]=standing[joint].z;}
 uint8_t *ignore=nullptr;game::api.ignoreEntity=&ignore;
 check(reactions::Wind(gPed,{.02f,0,0}),"small nondamaging wind must acquire a generation-valid real body");
 if(reactions::gPeople.empty())return false;
 auto &p=reactions::gPeople[0];
 check(p.physical.State()==reactphysics::Mode::Balanced,"tiny wind must retain balance");
 check(std::fabs(p.physical.Velocity(reactphysics::FootL).x-.02f)<.0001f&&std::fabs(game::EntitySpeed(gPed).x-.0004f)<.00001f,"wind is published once to every body joint and native m/50s velocity");
 game::PedFlags(gPed)=game::kPedStanding;gNow+=20;const float weakOrigin=game::EntityPos(gPed).x;reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);
 check(game::EntityPos(gPed).x>weakOrigin&&p.physical.State()==reactphysics::Mode::Balanced,"weak articulated wind must move through native standing control then collision while retaining balance");
 const float health=game::Health(gPed);const int beforeFalls=gFalls;
 for(int i=0;i<35&&p.physical.State()==reactphysics::Mode::Balanced;++i){gNow+=20;check(reactions::Wind(gPed,{.3f,0,0}),"continued wind must retain body ownership");reactions::Hook_Control(gPed);reactions::Hook_WorldCollision(gPed);}
 check(p.physical.State()==reactphysics::Mode::Falling&&gFalls==beforeFalls+1,"continued real momentum must survive native standing velocity replacement and enter a native fall once");
 check(game::Health(gPed)==health&&!p.physical.Dead(),"wind cannot damage health or mark the solver dead");
 const float before=p.physical.Velocity(reactphysics::FootL).x;
 // The existing damage reconciliation must not import the just-published
 // wind velocity again through a later explosion/vehicle notification.
 reactions::Hit(gPed,gOther,41,1);
 check(p.physical.Velocity(reactphysics::FootL).x<before+.05f,"a later native momentum notification must not import wind twice");
 gNow+=20;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x>0&&gFalls==beforeFalls+1,"physical wind fall must republish body speed after native control resets locomotion");
 const float speed=game::EntitySpeed(gPed).x;gLive=gOther;
 check(!reactions::Wind(gPed,{.4f,0,0})&&game::EntitySpeed(gPed).x==speed,"recycled native pool slot must reject wind before touching velocity");gLive=gPed;
 check(!reactions::Wind(gPed,{std::nanf(""),0,0}),"invalid wind must not poison live solver or native motion");
 for(int gate=0;gate<4;++gate){if(gate==0)gPed[0x19a]=2;if(gate==1)gPed[0x4c0]=1;if(gate==2)*reinterpret_cast<void **>(gPed+0x720)=gCar;if(gate==3)*reinterpret_cast<float *>(gPed+0x44c)=0;check(!reactions::Wind(gPed,{.4f,0,0}),"mission, seated, attached and dead recipients must reject wind");gPed[0x19a]=gPed[0x4c0]=0;*reinterpret_cast<void **>(gPed+0x720)=nullptr;*reinterpret_cast<float *>(gPed+0x44c)=100;gConfig.npcReactions=true;}
 // With optional bodies disabled, standing native control resets XY first;
 // the queued delta then reaches actual world collision/integration once.
 reactions::gPeople.clear();gConfig.npcReactions=false;game::PedFlags(gPed)=game::kPedStanding;game::EntitySpeed(gPed)={};
 *reinterpret_cast<int32_t *>(gPed+0x2fc)=game::kPedIdle;gNow+=200;
 check(reactions::Wind(gPed,{.4f,0,0}),"disabled articulated reactions must retain native wind delivery");
 reactions::Hook_Control(gPed);const float nativeOrigin=game::EntityPos(gPed).x;reactions::Hook_WorldCollision(gPed);
 check(std::fabs(game::EntityPos(gPed).x-nativeOrigin-.008f)<.00001f&&reactions::gPeople.empty(),"normal native control then collision must move a standing body without acquiring articulated ownership");
 reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"a consumed queue must not create wind after leaving the cone");
 // Airborne native control retains existing velocity: never publish twice.
 game::PedFlags(gPed)=0;game::EntitySpeed(gPed)={};reactions::orig_Control=Noop;
 check(reactions::Wind(gPed,{.4f,0,0}),"airborne exposed actor must receive its native impulse");reactions::Hook_Control(gPed);
 check(std::fabs(game::EntitySpeed(gPed).x-.008f)<.00001f,"airborne native wind cannot be counted twice");
 reactions::orig_Control=WindNativeControl;game::PedFlags(gPed)=game::kPedStanding;game::EntitySpeed(gPed)={};
 check(reactions::Wind(gPed,{.4f,0,0}),"standing exposed native actor must queue motion");gNow+=101;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"expired wind must not move an actor after a pause or loss of exposure");
 // A native early return leaves the published impulse intact even while
 // standing. Ordinary walking instead writes actual desired+464/+468 XY.
 game::EntitySpeed(gPed)={};reactions::orig_Control=Noop;check(reactions::Wind(gPed,{.4f,0,0}),"standing wind can precede native early return");reactions::Hook_Control(gPed);check(std::fabs(game::EntitySpeed(gPed).x-.008f)<.00001f,"retained standing native velocity cannot receive the same wind twice");
 game::EntitySpeed(gPed)={};*reinterpret_cast<float *>(gPed+0x464)=.012f;*reinterpret_cast<float *>(gPed+0x468)=.006f;reactions::orig_Control=WalkingWindControl;check(reactions::Wind(gPed,{.4f,0,0}),"walking native actor can receive wind");reactions::Hook_Control(gPed);check(std::fabs(game::EntitySpeed(gPed).x-.020f)<.00001f&&std::fabs(game::EntitySpeed(gPed).y-.006f)<.00001f,"wind must add once to real ordinary-walking desired XY");*reinterpret_cast<float *>(gPed+0x464)=*reinterpret_cast<float *>(gPed+0x468)=0;
 // Native support and special slope branches retain/adjust an impulse
 // themselves; the unsupported-standing replay must not add it again.
 for(int special=0;special<2;++special){game::EntitySpeed(gPed)={};reactions::orig_Control=Noop;if(special==0)*reinterpret_cast<void **>(gPed+0x480)=gCar;else{gPed[0x158]=0x1a;*reinterpret_cast<float *>(gPed+0x148)=1;}
 check(reactions::Wind(gPed,{.4f,0,0}),"native supported/slope actor receives a single wind impulse");reactions::Hook_Control(gPed);check(std::fabs(game::EntitySpeed(gPed).x-.008f)<.00001f,"moving support/special slope branch cannot receive a duplicate standing delta");*reinterpret_cast<void **>(gPed+0x480)=nullptr;gPed[0x158]=0;*reinterpret_cast<float *>(gPed+0x148)=0;}
 reactions::orig_Control=WindNativeControl;game::EntitySpeed(gPed)={};check(reactions::Wind(gPed,{.4f,0,0}),"native wind can queue before a world reset");gNow=1;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"world rewind must drop all queued wind before native collision");
 // A generation or clump change between Frame and native control must
 // invalidate the already-queued motion before it can reach collision.
 reactions::orig_Control=WindNativeControl;game::EntitySpeed(gPed)={};check(reactions::Wind(gPed,{.4f,0,0}),"wind can queue before a generation change");game::api.PoolsGetPed=[](int handle)->uint8_t *{return handle==18?gPed:handle==31?gPlayer:nullptr;};reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"queued motion cannot cross a native generation change");game::api.PoolsGetPed=Lookup;
 check(reactions::Wind(gPed,{.4f,0,0}),"wind can queue before clump replacement");*reinterpret_cast<uint8_t **>(gPed+0x58)=gOther;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"queued motion cannot cross native clump replacement");*reinterpret_cast<uint8_t **>(gPed+0x58)=gContactClump;
 // Monotonic game time cannot authorize a previous pool/epoch's delta.
 reactions::orig_Control=WindNativeControl;game::EntitySpeed(gPed)={};check(reactions::Wind(gPed,{.4f,0,0}),"native wind can queue before pool replacement");uint8_t replacement[2]{};*reinterpret_cast<uint8_t **>(windPoolHeader+8)=replacement;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"replacement pool with repeated handle/clump must not inherit queued motion");*reinterpret_cast<uint8_t **>(windPoolHeader+8)=windPoolFlags;
 check(reactions::Wind(gPed,{.4f,0,0}),"native wind can queue before cutscene");bool scene=true;reactions::gCutscene=&scene;reactions::Frame();scene=false;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"short cutscene must clear wind even if no actor control ran during it");reactions::gCutscene=nullptr;
 check(reactions::Wind(gPed,{.4f,0,0}),"native wind can queue before frame rewind");windFrameNumber=1;reactions::Hook_Control(gPed);check(game::EntitySpeed(gPed).x==0,"native frame rewind must invalidate wind even with monotonic milliseconds");
 // Tommy uses the same already-installed CPed consumer with no NPC body.
 *reinterpret_cast<uint32_t *>(gPlayer+0x60)=3|game::kEntityUsesCollision;*reinterpret_cast<float *>(gPlayer+0x44c)=100;*reinterpret_cast<int32_t *>(gPlayer+0x2fc)=game::kPedIdle;game::PedFlags(gPlayer)=game::kPedStanding;*reinterpret_cast<uint8_t **>(gPlayer+0x58)=gContactClump;
 check(reactions::Wind(gPlayer,{.4f,0,0}),"on-foot player behind a jet must receive native wind");reactions::Hook_Control(gPlayer);
 check(std::fabs(game::EntitySpeed(gPlayer).x-.008f)<.00001f&&reactions::gPeople.empty(),"player wind must survive native standing control without creating an NPC body");
 gConfig.npcReactions=true;reactions::gWind={};reactions::gPeople.clear();game::api.ignoreEntity=nullptr;return failures==0;
}

int main()
{
	reactions::gWindPool=&windPoolPointer;reactions::gWindFrame=&windFrameNumber;*reinterpret_cast<uint8_t **>(windPoolHeader)=gPed;*reinterpret_cast<uint8_t **>(windPoolHeader+8)=windPoolFlags;*reinterpret_cast<int32_t *>(windPoolHeader+16)=2;
	game::api.FindPlayerPed = Player;
	game::api.PoolsGetPed = Lookup;
	game::api.timeMs = &gNow;
	game::api.ProcessVerticalLine = NativeGround;
	reactions::gRef = Ref;
	reactions::orig_WorldCollision = Native;
	// reactions::Install refuses without these; an installed module always has them.
	reactions::gUpdateMatrix = reactions::gUpdateFrame = reactions::gReindex = Noop;
	*reinterpret_cast<uint32_t *>(gPed + 0x60) = 3 | game::kEntityUsesCollision | 0x2000u;
	*reinterpret_cast<uint32_t *>(gOther + 0x60) = 3;
	reactions::Person p;
	p.handle = 17;
	reactphysics::Pose pose{};
	for (int i = 0; i < reactphysics::Count; i++)
		pose[size_t(i)] = {0, 0, float(i) * .1f};
	p.physical.Reset(pose);
	p.physical.Impulse(reactphysics::Chest, {6, 0, 0});
	reactions::gPeople.push_back(p);
	*reinterpret_cast<float *>(gPed + 0x38) = .25f;
	game::EntitySpeed(gPed).x = .1f;
	*reinterpret_cast<uint16_t *>(gPed + 0x156) = 0x4c0;
	*reinterpret_cast<float *>(gPed + 0x138) = .7f;
	reactions::Hook_WorldCollision(gPed);
	if (gNativeCalls || *reinterpret_cast<float *>(gPed + 0x38) != .25f) {
		puts("FAIL: native collision moved the simulated root a second time");
		return 1;
	}
	if (!(game::EntityFlags(gPed) & 0x4000u) || (game::EntityFlags(gPed) & 0x2000u) ||
		!(game::EntityFlags(gPed) & game::kEntityUsesCollision)) {
		puts("FAIL: native world retries/ProcessShift retain movement ownership, or bullets lost collision");
		return 1;
	}
	if ((*reinterpret_cast<uint16_t *>(gPed + 0x156) & 0x4c0) || *reinterpret_cast<float *>(gPed + 0x138) != 0) {
		puts("FAIL: owned collision retained transient flags or stale native travelled distance");
		return 1;
	}
	gConfig.npcReactions = false;
	reactions::Hook_WorldCollision(gPed);
	if (gNativeCalls != 1) {
		puts("FAIL: disabled feature suppressed native physics");
		return 1;
	}
	gConfig.npcReactions = true;
	gLive = gOther;
	reactions::Hook_WorldCollision(gPed);
	if (gNativeCalls != 2) {
		puts("FAIL: reused pool slot retained physical ownership");
		return 1;
	}
	gLive = gPed;
	gPed[0x19a] = 2;
	reactions::Hook_WorldCollision(gPed);
	if (gNativeCalls != 3) {
		puts("FAIL: mission controller lost native physics");
		return 1;
	}
	gPed[0x19a] = 0;
	uint8_t car[0x6f0]{};
	*reinterpret_cast<uint32_t *>(car + 0x60) = 2;
	reactions::Hook_WorldCollision(car);
	if (gNativeCalls != 4 || gBadPoolCasts) {
		puts("FAIL: non-ped physical was cast into the pedestrian pool");
		return 1;
	}
	*reinterpret_cast<float *>(gPed + 0x44c) = 100;
	reactions::gSetFall = Fall;
	reactions::gGetUp = GetUp;
	const auto standing = Standing();
	{
		reactions::Person prone;
		prone.physical.Reset(standing);
		for (int i = 0; i < 4; i++)
			prone.physical.Impulse(reactphysics::KneeL, {0, -1.4f, 0});
		for (int i = 0; i < 600 && prone.physical.State() != reactphysics::Mode::Recovering; i++)
			prone.physical.Advance(1.0f / 60, standing, Ground, nullptr);
		if (prone.physical.State() != reactphysics::Mode::Recovering) {
			puts("FAIL: native handoff fixture did not settle into recovery");
			return 1;
		}
		prone.nativeMode = reactphysics::Mode::Falling;
		prone.heldFall = true;
		game::SetPedState(gPed, game::kPedFall); // held in the game's fall (its SetFall -1)
		prone.savedGetUpTimer = 1234;
		*reinterpret_cast<uint32_t *>(gPed + 0x68c) = UINT32_MAX;
		*reinterpret_cast<CVector *>(gPed + 0x38) = {0, 0, .18f};
		gPed[0x18c] &= ~0x20;
		reactions::gGetUp = PlacedGetUp;
		reactions::NativeMode(prone, gPed);
		if (!gGetUpStarted || !(gPed[0x18c] & 0x20)) {
			puts("FAIL: prone physical root prevented native GetUp animation from starting above a clear floor");
			return 1;
		}
		for (bool vehicle : {false, true}) {
			prone.nativeMode = reactphysics::Mode::Falling;
			gPed[0x18c] &= ~0x20;
			gGetUpStarted = false;
			gGetUpObstacleVehicle = vehicle;
			gGetUpObstacleBottom = vehicle ? .3f : .75f;
			gGetUpObstacleTop = 1.0f;
			reactions::NativeMode(prone, gPed);
			if (gGetUpStarted) {
				puts("FAIL: native get-up ground query lifted above a blocking car or low ceiling");
				return 1;
			}
			gGetUpObstacleTop = 0;
			reactions::NativeMode(prone, gPed);
			if (!gGetUpStarted) {
				puts("FAIL: native get-up did not retry after the geometric obstruction cleared");
				return 1;
			}
		}
		prone.nativeMode = reactphysics::Mode::Falling;
		gPed[0x18c] &= ~0x20;
		gGetUpStarted = false;
		gGetUpBlocked = true;
		reactions::NativeMode(prone, gPed);
		if (gGetUpStarted) {
			puts("FAIL: recovery bypassed a genuine native get-up obstruction");
			return 1;
		}
		gGetUpBlocked = false;
		reactions::NativeMode(prone, gPed);
		if (!gGetUpStarted || !(gPed[0x18c] & 0x20)) {
			puts("FAIL: prone physical root prevented native GetUp animation from starting after room cleared");
			return 1;
		}
		if (std::fabs(game::EntityPos(gPed).z - .18f) > 1e-6f ||
			*reinterpret_cast<uint32_t *>(gPed + 0x68c) != UINT32_MAX) {
			puts("FAIL: native get-up placement check moved the physical root or lost fall timer restoration");
			return 1;
		}
		reactions::gGetUp = GetUp;
		gPed[0x18c] &= ~0x20;
	}
	reactions::Person cycle;
	cycle.handle = 17;
	cycle.physical.Reset(standing);
	*reinterpret_cast<uint32_t *>(gPed + 0x68c) = 1234;
	for (int i = 0; i < 4; i++)
		cycle.physical.Impulse(reactphysics::KneeL, {0, -1.4f, 0});
	reactions::NativeMode(cycle, gPed);
	bool hitAgain = false;
	for (int i = 0; i < 900; i++) {
		cycle.physical.Advance(1.0f / 60, standing, Ground, nullptr);
		reactions::NativeMode(cycle, gPed);
		if (!hitAgain && cycle.physical.State() == reactphysics::Mode::Recovering) {
			cycle.physical.Impulse(reactphysics::Chest, {6, 0, 0});
			reactions::NativeMode(cycle, gPed);
			hitAgain = true;
		}
	}
	if (gFalls != 2 || gGetUps != 2 || gBadDurations) {
		puts("FAIL: interrupted recovery lost native fall/get-up ownership");
		return 1;
	}
	cycle.physical.Impulse(reactphysics::Chest, {6, 0, 0});
	reactions::NativeMode(cycle, gPed);
	reactions::gPeople = {cycle};
	reactions::gInstalled = true;
	gNow = 5000;
	gConfig.npcReactions = false;
	reactions::Frame();
	if (*reinterpret_cast<uint32_t *>(gPed + 0x68c) != 1234) {
		puts("FAIL: feature disable left an indefinite native fall timer");
		return 1;
	}
	gConfig.npcReactions = true;
	cycle = {};
	cycle.handle = 17;
	cycle.physical.Reset(standing);
	cycle.physical.Impulse(reactphysics::Chest, {.5f, 0, 0});
	reactions::gPeople = {cycle};
	game::EntitySpeed(gPed) = {.08f, .02f, 0};
	reactions::Hit(gPed, gOther, 41, 1);
	if (reactions::gPeople[0].physical.Velocity(reactphysics::FootL).x < 3.9f) {
		puts("FAIL: native explosion momentum never reached the active physical body");
		return 1;
	}
	reactions::Hit(gPed, gOther, 41, 1);
	if (reactions::gPeople[0].physical.Velocity(reactphysics::FootL).x > 4.1f) {
		puts("FAIL: native explosion momentum was counted twice");
		return 1;
	}
	cycle = {};
	cycle.handle = 17;
	cycle.physical.Reset(standing);
	cycle.physical.Impulse(reactphysics::Chest, {.5f, 0, 0});
	reactions::gPeople = {cycle};
	*reinterpret_cast<uint32_t *>(gCar + 0x60) = 2;
	game::EntitySpeed(gPed) = {};
	reactions::orig_Control = LateCarControl;
	reactions::gUpdateMatrix = Noop;
	reactions::gUpdateFrame = Noop;
	game::api.UpdateRpHAnim = Noop;
	gNow += 100;
	reactions::Hook_Control(gPed);
	if (reactions::gPeople[0].physical.Velocity(reactphysics::FootL).x < 5.9f ||
	    reactions::gPeople[0].physical.Velocity(reactphysics::FootL).y < 1.9f) {
		puts("FAIL: vehicle momentum published after damage was overwritten by old solver state");
		return 1;
	}
	// Loading reconstructs pools, whose generation counters can repeat. An
	// old-world handle is not authority to restore into a newly loaded actor.
	cycle.heldFall = true;
	cycle.savedGetUpTimer = 1234;
	reactions::gPeople = {cycle};
	gLive = gOther;
	gOther[0x19a] = 2;
	*reinterpret_cast<uint32_t *>(gOther + 0x68c) = UINT32_MAX;
	std::array<uint8_t, sizeof gOther> loaded{};
	std::memcpy(loaded.data(), gOther, sizeof gOther);
	reactions::gLastFrame = 10000;
	gNow = 50;
	if (reactions::Find(gOther)) {
		puts("FAIL: NPC entry acquired an old-world record before the player's Frame cleanup");
		return 1;
	}
	reactions::Frame();
	if (!reactions::gPeople.empty() || std::memcmp(loaded.data(), gOther, sizeof gOther)) {
		puts("FAIL: timeline reset restored old state into a newly loaded ped");
		return 1;
	}
	if (!PassiveContacts())
		return 1;
	if (!GroundedBalancedFade())
		return 1;
	if (!NativeRenderEdits())
		return 1;
	if (!NativeSupportedOwnership())
		return 1;
	if (!PlayerNativeContacts())
		return 1;
	if (!AcceptedSpatialImpact())
		return 1;
	if (!ReplacedClumpOwnership())
		return 1;
	if (!NondamagingWind())
		return 1;
	puts("reactionhook: all passed");
	return 0;
}
