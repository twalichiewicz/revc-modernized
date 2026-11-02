// A body killed under articulation through the actual control/collision/frame
// and render hooks. Native CPed::SetDead clears UsesCollision on every corpse
// (bar drowning) and stores PED_DEAD; the native corpse pose then lies about
// 0.9m below its root (ped.ifp ko_*/kd_* final frames), so a root left at
// ragdoll pelvis height draws the corpse underground.
#define main original_reaction_tests
#include "test_reactionhook.cpp"
#undef main

namespace {
int failures;
void Check(bool good, const char *message)
{
	if (!good) { std::printf("FAIL: %s\n", message); ++failures; }
}

constexpr int kPeds = 13, kFirst = 100;
uint8_t gPeds[kPeds][0x8d0];
int PoolRef(uint8_t *ped)
{
	if (ped == gPlayer) return 31;
	for (int i = 0; i < kPeds; i++)
		if (ped == gPeds[i]) return kFirst + i;
	return -1;
}
uint8_t *PoolPed(int handle)
{
	if (handle == 31) return gPlayer;
	return handle >= kFirst && handle < kFirst + kPeds ? gPeds[handle - kFirst] : nullptr;
}
void NativeControl(uint8_t *) {}
CVector &Root(uint8_t *ped) { return *reinterpret_cast<CVector *>(ped + 0x38); }
float &Life(uint8_t *ped) { return *reinterpret_cast<float *>(ped + 0x44c); }

void Setup()
{
	gNow += 1000;
	reactions::gLastFrame = gNow;
	reactions::gPeople.clear();
	reactions::gContacts = {};
	std::memset(gPeds, 0, sizeof gPeds);
	std::memset(gPlayer, 0, sizeof gPlayer);
	game::EntityFlags(gPlayer) = 3 | game::kEntityUsesCollision;
	Root(gPlayer) = {-6, -2, 1.04f};
	for (int i = 0; i < kPeds; i++) {
		uint8_t *ped = gPeds[i];
		game::EntityFlags(ped) = 3 | game::kEntityUsesCollision;
		game::SetPedState(ped, game::kPedIdle);
		Root(ped) = {-6 - 3.0f * i, 0, 1.04f};
		*reinterpret_cast<uint8_t **>(ped + 0x58) = gContactClump;
		Life(ped) = 100;
	}
	game::api.FindPlayerPed = Player;
	game::api.PoolsGetPed = PoolPed;
	game::api.timeMs = &gNow;
	game::api.ProcessVerticalLine = NativeGround;
	game::api.UpdateRpHAnim = CapturedIdleMatrices;
	game::api.GetAnimHierarchyFromSkinClump = ContactHierarchy;
	game::api.HAnimMatrixArray = ContactMatrices;
	game::api.HAnimIDGetIndex = ContactBone;
	static uint8_t *ignored = nullptr;
	game::api.ignoreEntity = &ignored;
	reactions::gRef = PoolRef;
	reactions::gInstalled = true;
	reactions::gCutscene = nullptr;
	reactions::gReplay = nullptr;
	gConfig.npcReactions = true;
	reactions::gUpdateMatrix = reactions::gUpdateFrame = reactions::gReindex = Noop;
	reactions::gSetFall = Fall;
	reactions::gGetUp = GetUp;
	reactions::orig_Control = NativeControl;
	reactions::orig_WorldCollision = NativeControl;
	shotimpact::available = false;
	Check(reactcontact::Install(), "corpse fixture binds the real native contact sweep");
}
// One game frame in native order: control, collision, frame, render.
void Tick(int count = kPeds)
{
	gNow += 16;
	for (int i = 0; i < count; i++) {
		reactions::Hook_Control(gPeds[i]);
		reactions::Hook_WorldCollision(gPeds[i]);
	}
	reactions::Frame();
	for (int i = 0; i < count; i++) {
		reactions::BeforePreRender(gPeds[i]);
		reactions::AfterPreRender(gPeds[i]);
	}
}
void Kill(uint8_t *ped)
{
	Life(ped) = 0;
	game::SetPedState(ped, game::kPedDie);
	reactions::Hit(ped, gPlayer, 26, 1);
}
// Exactly the native store sequence at 0x10019efe4..0x10019f018.
void NativeSetDead(uint8_t *ped)
{
	game::EntityFlags(ped) &= ~game::kEntityUsesCollision;
	Life(ped) = 0;
	game::SetPedState(ped, game::kPedDead);
}
// A ragdoll corpse handed back to native must keep native's corpse root
// convention: standing height (1.04m, as StartGetUp and Read use) above the
// support under the body, so the native lying pose lies on that support.
bool NativeCorpseRoot(uint8_t *ped, CVector before)
{
	return std::fabs(Root(ped).z - 1.04f) < .002f && std::fabs(Root(ped).x - before.x) < 1e-4f &&
	       std::fabs(Root(ped).y - before.y) < 1e-4f;
}
const reactions::Person *Tracked(uint8_t *ped) { return reactions::Find(ped); }

void KilledBodySurvivesNativeSetDead()
{
	Setup();
	uint8_t *ped = gPeds[0];
	Kill(ped);
	Check(Tracked(ped) && Tracked(ped)->physical.Dead(), "a fatal hit articulates the dying body");
	for (int frame = 0; frame < 90; frame++) Tick(1);
	const auto *p = Tracked(ped);
	Check(p && reactions::OwnsRoot(p) && Root(ped).z < .5f,
	      "precondition: the falling ragdoll owns a root lowered to its pelvis");
	NativeSetDead(ped);
	CVector previous = Root(ped);
	float jump = 0;
	bool posed = true;
	for (int frame = 0; frame < 120; frame++) {
		Tick(1);
		jump = std::max(jump, reactions::Length(reactions::Sub(reactions::V(Root(ped)), reactions::V(previous))));
		previous = Root(ped);
		p = Tracked(ped);
		posed &= p && p->saved;
	}
	Check(posed, "a body killed under articulation keeps its ragdoll pose through native SetDead");
	Check(jump < .05f, "native SetDead must not move the ragdoll corpse's root");
	reactrig::Matrices pose{};
	Check(reactions::CopyHitPose(ped, pose), "shots at a ragdoll corpse use the pose that is drawn");
	Check(game::Health(ped) == 0 && !(game::EntityFlags(ped) & game::kEntityUsesCollision) &&
	      game::PedState(ped) == game::kPedDead, "native death state and collision flags remain native");

	uint8_t *native = gPeds[1];
	NativeSetDead(native);
	reactions::Hit(native, gPlayer, 26, 1);
	Tick(2);
	Check(!Tracked(native), "a corpse that died natively stays native");
}

void ReleasedCorpseLiesOnItsSupport(bool farAway)
{
	Setup();
	uint8_t *ped = gPeds[0];
	Kill(ped);
	for (int frame = 0; frame < 90; frame++) Tick(1);
	NativeSetDead(ped);
	for (int frame = 0; frame < 90; frame++) Tick(1);
	Check(Tracked(ped) && Tracked(ped)->physical.State() == reactphysics::Mode::Settled,
	      "precondition: the corpse settled under articulation");
	const CVector before = Root(ped);
	if (farAway)
		Root(gPlayer) = {150, 0, 1.04f};
	else
		gConfig.npcReactions = false;
	Tick(1);
	Check(!Tracked(ped), "precondition: the corpse was released to native");
	Check(NativeCorpseRoot(ped, before), farAway
	      ? "a corpse released by distance gets native's corpse root height above its support"
	      : "a corpse released by disabling reactions gets native's corpse root height above its support");
}

// A car over a settled corpse leaves no certified pose, so the solver itself
// gives the body back (reactphysics Constrain) while its root is still at the
// pelvis. That release must also restore native's corpse root.
bool gOverrun;
reactphysics::Contact Overrun(Vec3 from, Vec3 to, float radius, void *context)
{
	if (gOverrun) return {from, {0, 0, 1}, true, false};
	return Ground(from, to, radius, context);
}
void DeactivatedCorpseLiesOnItsSupport()
{
	Setup();
	uint8_t *ped = gPeds[0];
	Kill(ped);
	for (int frame = 0; frame < 90; frame++) Tick(1);
	NativeSetDead(ped);
	for (int frame = 0; frame < 90; frame++) Tick(1);
	reactions::Person *p = reactions::Find(ped);
	Check(p && p->physical.State() == reactphysics::Mode::Settled, "precondition: the corpse settled under articulation");
	if (!p) return;
	gOverrun = true;
	p->physical.Advance(1.0f / 60, p->physical.Positions(), Overrun, nullptr);
	gOverrun = false;
	Check(!p->physical.Active() && p->physical.Dead(), "precondition: no certified pose remains, so the solver released the corpse");
	const CVector before = Root(ped);
	Check(before.z < .5f, "precondition: the solver released it with the ragdoll's pelvis-height root");
	Tick(1);
	Check(!Tracked(ped), "precondition: the deactivated corpse was handed back to native");
	Check(NativeCorpseRoot(ped, before), "a corpse the solver gave up under moving geometry still gets native's corpse root");
}

// disturbOldest: a blast moves the corpse hit longest ago without a new hit;
// releasing a body mid-motion would snap it into native's pose in the air.
void FullSetReleasesOldestCorpse(bool disturbOldest)
{
	Setup();
	for (int i = 0; i < kPeds - 1; i++) {
		Kill(gPeds[i]);
		for (int frame = 0; frame < 8; frame++) Tick(kPeds - 1);
	}
	for (int i = 0; i < kPeds - 1; i++) NativeSetDead(gPeds[i]);
	for (int frame = 0; frame < 400; frame++) Tick(kPeds - 1);
	int corpses = 0;
	for (int i = 0; i < kPeds - 1; i++)
		corpses += Tracked(gPeds[i]) && Tracked(gPeds[i])->physical.State() == reactphysics::Mode::Settled;
	Check(corpses == kPeds - 1, "precondition: twelve articulated corpses fill the reaction set");
	if (disturbOldest)
		reactions::Find(gPeds[0])->physical.Momentum({0, 0, 2}, true);
	const int expected = disturbOldest ? 1 : 0;
	const CVector oldest = Root(gPeds[expected]);
	uint8_t *living = gPeds[kPeds - 1];
	Life(living) = 60;
	reactions::Hit(living, gPlayer, 26, 1);
	Check(Tracked(living) != nullptr, "a full set of settled corpses still leaves a living victim its reaction");
	Check(reactions::gPeople.size() == size_t(kPeds - 1), "the reaction set stays bounded");
	Check(!Tracked(gPeds[expected]) && NativeCorpseRoot(gPeds[expected], oldest), disturbOldest
	      ? "a moving corpse keeps articulation; the least recently hit settled corpse makes room instead"
	      : "the least recently hit corpse makes room and lies on its support natively");
	bool others = true;
	for (int i = 0; i < kPeds - 1; i++) others &= i == expected || Tracked(gPeds[i]) != nullptr;
	Check(others, "only one corpse is released for one new reaction");
}
} // namespace

int main()
{
	KilledBodySurvivesNativeSetDead();
	ReleasedCorpseLiesOnItsSupport(true);
	ReleasedCorpseLiesOnItsSupport(false);
	DeactivatedCorpseLiesOnItsSupport();
	FullSetReleasesOldestCorpse(false);
	FullSetReleasesOldestCorpse(true);
	std::printf("reactcorpse: %d failures\n", failures);
	return failures ? 1 : 0;
}
