// Native aim adapter + real animation-keyframe posing: walking must preserve
// the offhand's position relative to the gun hand, including torso yaw/roll.
#include "../src/aimcam.cpp"
#include "../src/body.cpp"
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}
namespace hooks
{
void *Install(const char *, void *) { return nullptr; }
volatile uint64_t *Flag(const char *) { return nullptr; }
} // namespace hooks
static bool firstPerson;
namespace camera
{
bool FirstPersonOnFoot() { return firstPerson; }
bool FirstPersonChosen() { return false; }
} // namespace camera
namespace controls
{
bool LookFrozen() { return false; }
} // namespace controls
namespace reactions
{
bool gStubDown;
bool PlayerDown() { return gStubDown; }
} // namespace reactions
namespace player
{
bool ControlHooked() { return true; }
bool PoseHooked() { return true; }
} // namespace player
static uint8_t ped[0x900]{}, cam[0x900]{}, pad[0x200]{}, clump[8]{}, info[0x80]{}, frames[body::kBones][0x30]{},
    keyframes[body::kBones][0x40]{};
static uint32_t now = 100;
static float step = 1;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
static uint8_t *Player() { return ped; }
static uint8_t *Pad(int) { return pad; }
static uint8_t *Info(int) { return info; }
static uint8_t *FindBone(uint8_t *, uint32_t tag)
{
	for (int b = 0; b < body::kBones; ++b)
		if (body::kTags[b] == int(tag))
			return frames[b];
	return nullptr;
}
static uint8_t *Hierarchy(uint8_t *) { return clump; }
static int BoneIndex(uint8_t *, int tag)
{
	for (int b = 0; b < body::kBones; ++b)
		if (body::kTags[b] == tag)
			return b;
	return -1;
}
static void RejectIncompletePose()
{
	using namespace body;
	uint8_t saved[sizeof keyframes];
	std::memcpy(saved, keyframes, sizeof saved);
	Pose pose;
	pose.Turn(kUpperArmL, X(.7f));
	Apply(ped, pose, 1);
	ApplyOver(ped, pose, 1);
	RestoreIn(ped, aimcam::gArms[26], {}, 1);
	Restore(ped, aimcam::gArms[26], 1);
	Held held;
	const Bone arm[] = {kUpperArmL, kForearmL};
	CHECK(!Hold(ped, held, kSpine1, arm, 2));
	CHECK(!HoldIn(ped, held, {}, arm, 2));
	const auto joint = JointOver(ped, {}, kHandL);
	CHECK(joint.x == 0 && joint.y == 0 && joint.z == 0);
	Pose over;
	TurnOver(ped, over, kUpperArmL, X(.7f));
	CHECK(!over.set[kUpperArmL]);
	CHECK(!std::memcmp(saved, keyframes, sizeof saved));
}
static CVector HandGap()
{
	return body::Sub(body::JointOver(ped, {}, body::kHandL), body::JointOver(ped, {}, body::kHandR));
}
int main()
{
	using namespace body;
	game::api.FindPlayerPed = Player;
	game::api.GetPad = Pad;
	game::api.theCamera = cam;
	game::api.timeMs = &now;
	game::api.timeStep = &step;
	game::api.GetAnimHierarchyFromSkinClump = Hierarchy;
	game::api.HAnimIDGetIndex = BoneIndex;
	body::gFindBone = FindBone;
	static uint8_t mouse = 1;
	game::api.useMouse3rdPerson = &mouse;
	gConfig.aimCamera = true;
	*reinterpret_cast<uint8_t **>(ped + 0x58) = clump;
	*reinterpret_cast<float *>(ped + 0x44c) = 100;
	game::SetPedState(ped, game::kPedAimGun);
	*reinterpret_cast<CVector *>(ped + 0x08) = {1, 0, 0};
	*reinterpret_cast<CVector *>(ped + 0x18) = {0, 1, 0};
	*reinterpret_cast<CVector *>(ped + 0x28) = {0, 0, 1};
	*reinterpret_cast<CVector *>(cam + 0x10) = {0, 1, 0};
	ped[0x64c] = 5;
	*reinterpret_cast<int32_t *>(game::PedWeapon(ped, 5)) = 26;
	*reinterpret_cast<int16_t *>(pad + game::kPadRB) = 1;
	aimcam::gInstalled = aimcam::gGunRaised = true;
	aimcam::gWeaponInfo = Info;
	gSkel.clump = clump;
	gSkel.ok = true;
	gSkel.hierarchy = clump;
	const int parents[kBones] = {0, 0, 1, 2, 3, 4, 3, 6, 7, 8, 9, 3, 11, 12, 13, 14, 1, 16, 17, 1, 19, 20};
	const CVector pos[kBones] = {{0, 0, 0},		  {0, 0, .9f},	       {0, 0, 1.1f},	    {0, 0, 1.35f},
				     {0, 0, 1.5f},	  {0, 0, 1.7f},	       {-.12f, 0, 1.4f},    {-.23f, 0, 1.4f},
				     {-.35f, .18f, 1.2f}, {-.25f, .45f, 1.3f}, {-.25f, .48f, 1.3f}, {.12f, 0, 1.4f},
				     {.23f, 0, 1.4f},	  {.35f, .18f, 1.2f},  {.25f, .45f, 1.3f},  {.25f, .48f, 1.3f}};
	for (int b = 0; b < kBones; ++b) {
		gSkel.parent[b] = parents[b];
		gSkel.restPos[b] = pos[b];
		gSkel.frame[b] = frames[b];
		gSkel.key[b] = keyframes[b];
		*reinterpret_cast<uint8_t **>(frames[b] + 0x10) = keyframes[b];
		KeyRotation(keyframes[b]) = {};
	}
	aimcam::PoseArms(ped);
	now = 450;
	aimcam::PoseArms(ped);
	now = 600;
	step = 20;
	aimcam::PoseArms(ped);
	const CVector target = HandGap();
	CHECK(aimcam::gArmsPose.Known(26));
	for (float yaw : {-.65f, .65f}) {
		for (int frame = 0; frame < 60; ++frame) {
			for (int b = 0; b < kBones; ++b)
				KeyRotation(keyframes[b]) = {};
			KeyRotation(keyframes[kSpine1]) = Z(yaw) * Y(.25f);
			game::SetPedState(ped, game::kPedIdle);
			now += 20;
			step = 1;
			aimcam::PoseArms(ped);
		}
		const CVector gap = HandGap();
		CHECK(std::sqrt(Dot(Sub(gap, target), Sub(gap, target))) < .005f);
	}
	// First-person, crouch, firing and seat owners keep the native keyframes.
	for (int owner = 0; owner < 4; ++owner) {
		firstPerson = owner == 0;
		*reinterpret_cast<uint64_t *>(ped + 0x188) = owner == 1 ? uint64_t(1) << 36 : 0;
		game::SetPedState(ped, owner == 2 ? game::kPedAttack : game::kPedIdle);
		ped[0x4c0] = owner == 3;
		KeyRotation(keyframes[kUpperArmL]) = X(.2f);
		aimcam::PoseArms(ped);
		CHECK(std::fabs(KeyRotation(keyframes[kUpperArmL]).x - X(.2f).x) < 1e-6f);
	}
	firstPerson = false;
	ped[0x4c0] = 0;
	game::SetPedState(ped, game::kPedIdle);
	// A native animation handoff can remove a key without replacing the
	// clump. Reject the entire pose before touching any earlier arm key.
	uint8_t savedKeys[sizeof keyframes];
	std::memcpy(savedKeys, keyframes, sizeof keyframes);
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = nullptr;
	aimcam::PoseArms(ped);
	CHECK(!std::memcmp(savedKeys, keyframes, sizeof keyframes));
	RejectIncompletePose();
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = keyframes[kFootR];
	CHECK(Prepare(ped)); // the transient handoff must not poison the cache
	CHECK(Alike(KeyRotation(keyframes[kUpperArmL]), X(.7f)) < .99f);
	Pose valid;
	valid.Turn(kUpperArmL, X(.7f));
	ApplyOver(ped, valid, 1);
	CHECK(Alike(KeyRotation(keyframes[kUpperArmL]), X(.7f)) > .99999f);
	// A replaced frame may already have been freed. Native lookup now returns
	// the live frame; no poser may dereference the quarantined cached one.
	const auto cached = gSkel;
	const size_t page = size_t(sysconf(_SC_PAGESIZE));
	void *retired = mmap(nullptr, page, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	CHECK(retired != MAP_FAILED);
	if (retired != MAP_FAILED) {
		gSkel.frame[kFootR] = static_cast<uint8_t *>(retired);
		RejectIncompletePose();
		gSkel = cached;
		munmap(retired, page);
	}
	// Even a non-null replacement key cannot receive a pose measured for the
	// old binding until Prepare has refreshed the rig.
	static uint8_t nextKey[0x40]{};
	// Mixed ready/replaced and temporarily missing bindings must prefer the
	// incomplete verdict, avoiding a failed, permanently cached measurement.
	*reinterpret_cast<uint8_t **>(frames[kUpperArmL] + 0x10) = nextKey;
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = nullptr;
	CHECK(!Prepare(ped));
	*reinterpret_cast<uint8_t **>(frames[kUpperArmL] + 0x10) = keyframes[kUpperArmL];
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = keyframes[kFootR];
	CHECK(Prepare(ped));
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = nextKey;
	RejectIncompletePose();
	*reinterpret_cast<uint8_t **>(frames[kFootR] + 0x10) = keyframes[kFootR];
	static uint8_t nextClump[8]{};
	*reinterpret_cast<uint8_t **>(ped + 0x58) = nextClump;
	aimcam::PoseArms(ped);
	CHECK(!aimcam::gArmsPose.Known(26)); // a new outfit cannot receive the old held rig
	if (failures)
		return 1;
	std::puts("all native aimed-walk keyframe tests passed");
}
