// Real Install -> native control -> CollideWithPed -> SetFall boundaries.
// Native adapters reproduce the verified macVC call order/ABI, without launching it.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
static void *FixtureSymbol(void *, const char *);
#define dlsym FixtureSymbol
#include "../src/playerreact.cpp"
#include "../src/reactcontact.cpp"
#include "../src/reactions.cpp"
#undef dlsym
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}
namespace aimcam
{
bool Aiming()
{
	return false;
}
} // namespace aimcam
namespace fpshoot
{
bool Shooting()
{
	return false;
}
} // namespace fpshoot
static bool gSwimming, gClimbing, gDodge, gCrouch;
namespace swim
{
bool Swimming()
{
	return gSwimming;
}
} // namespace swim
namespace climb
{
bool Active()
{
	return gClimbing;
}
} // namespace climb
namespace shootdodge
{
bool Posing()
{
	return gDodge;
}
} // namespace shootdodge
namespace crouch
{
bool Posing()
{
	return gCrouch;
}
} // namespace crouch
namespace shotimpact
{
bool Current(uint8_t *, int, Event &)
{
	return false;
}
} // namespace shotimpact
static uint8_t gPed[0x900], gPlayer[0x900], gClump[8], gCam[0x900], gPad[0x200];
static uint8_t *gIgnored, *gPlayerLive = gPlayer;
static int gNpcGeneration = 17;
static uint32_t gNow = 1000;
static float gStep = 1, gRig[22 * 16];
static bool gCutscene, gRecycled, gNestedDamage;
static int gDuringNative, gContactPiece = 2;
static bool gEntityContact, gStopContact;
static uint8_t gReplacementClump[8];
static int gNativeCollisions, gNativeFalls, gPlayerFalls, gNpcFalls, gNativeControls;
static int gNativeFallDuration, gNativeFallAnimation;
static std::map<std::string, void *> gSlots;
static uint8_t *Player()
{
	return gPlayerLive;
}
static uint8_t *Pad(int)
{
	return gPad;
}
static int Ref(uint8_t *p)
{
	return p == gPed ? gNpcGeneration : p == gPlayer ? 31 : -1;
}
static uint8_t *Lookup(int h)
{
	return h == gNpcGeneration ? gPed : h == 31 && !gRecycled ? gPlayer : nullptr;
}
static uint8_t *Hierarchy(uint8_t *p)
{
	return p == gClump ? gClump : nullptr;
}
static float *Matrices(uint8_t *)
{
	return gRig;
}
static int Index(uint8_t *, int tag)
{
	for (int i = 0; i < 22; i++)
		if (reactions::kTags[i] == tag)
			return i;
	return -1;
}
static void Noop(uint8_t *) {}
static bool Vertical(const CVector &, float, uint8_t *, uint8_t *&, bool, bool, bool, bool, bool, bool,
					 void *)
{
	return false;
}
static void NativeFall(uint8_t *p, int duration, int animation, uint8_t force)
{
	// Verified SetFall early rejection: nonforced calls cannot replace a fall,
	// jump or dead controller. Force1 still rejects dying/dead.
	const int state = game::PedState(p);
	if ((!force && (state > 0x26 || game::Health(p) <= 0 || (game::PedFlags(p) & 0x1800))) ||
		(force && (state == game::kPedDie || state == game::kPedDead)))
		return;
	++gNativeFalls;
	if (p == gPlayer)
		++gPlayerFalls;
	else
		++gNpcFalls;
	gNativeFallDuration = duration;
	gNativeFallAnimation = animation;
	*reinterpret_cast<int *>(p + 0x2fc) = game::kPedFall;
	*reinterpret_cast<uint32_t *>(p + 0x68c) = duration == -1 ? UINT32_MAX : gNow + uint32_t(duration);
}
static void DispatchFall(uint8_t *p, int duration, int animation, uint8_t direction)
{
	auto it = gSlots.find("_ZN4CPed7SetFallEi11AnimationIdh");
	auto fn = it == gSlots.end() ? NativeFall : reinterpret_cast<decltype(&NativeFall)>(it->second);
	fn(p, duration, animation, direction);
}
static void NativePedCollision(uint8_t *ped, uint8_t *other)
{
	++gNativeCollisions;
	// 0x1001ca47c ApplyMoveForce(this) precedes 0x1001ca4d0
	// SetFall(other,3000,KO-side/back,0). Its animation-speed/stat branch can
	// choose this even when actual world closing speed is only a walk.
	game::EntitySpeed(ped) = {};
	if (gNestedDamage) {
		*reinterpret_cast<float *>(gPed + 0x44c) -= 5;
		reactions::Hit(gPed, gPlayer, 19, 1);
		DispatchFall(gPlayer, 800, 27, 1);
	}
	switch (gDuringNative) {
	case 1:
		*reinterpret_cast<uint16_t *>(gPad + 0xf0) = 1;
		break;
	case 2:
		gPed[0x19a] = 2;
		break;
	case 3:
		*reinterpret_cast<uint8_t **>(gPed + 0x58) = gReplacementClump;
		break;
	case 4:
		gRecycled = true;
		break;
	case 5:
		*reinterpret_cast<float *>(gPed + 0x44c) = 0;
		break;
	case 6:
		gNow -= 100;
		break;
	}
	DispatchFall(other, 3000, 24, 0);
}
static void DispatchPedCollision(uint8_t *ped, uint8_t *other)
{
	auto it = gSlots.find("_ZN4CPed14CollideWithPedEPS_");
	auto fn =
		it == gSlots.end() ? NativePedCollision : reinterpret_cast<decltype(&NativePedCollision)>(it->second);
	fn(ped, other);
}
static void NativeControl(uint8_t *ped)
{
	++gNativeControls;
	DispatchPedCollision(ped, ped == gPlayer ? gPed : gPlayer);
}
static int NativeEntityCollision(uint8_t *ped, uint8_t *, uint8_t *out)
{
	if (!gEntityContact)
		return 0;
	reactcontact::NativePoint p{};
	const Vec3 places[] = {{0, -.06f, 1.32f},	 {0, -.06f, 1.32f},		 {-.21f, .03f, 1.55f},
						   {.18f, -.08f, 1.55f}, {-.207f, -.103f, .18f}, {.099f, -.091f, .15f},
						   {0, .03f, 1.72f}};
	p.point = places[gContactPiece];
	p.normal = ped == gPlayer ? Vec3{-1, 0, 0} : Vec3{1, 0, 0};
	p.pieceA = ped == gPlayer ? 1 : gContactPiece;
	p.pieceB = ped == gPlayer ? gContactPiece : 1;
	p.depth = .02f;
	std::memcpy(out, &p, sizeof p);
	if (gStopContact)
		game::EntitySpeed(ped) = {};
	return 1;
}
static reactcontact::NativePoint gPoint;
static uint8_t gScenery[0x80];
static uint8_t *Sphere(CVector center, float radius, uint8_t *, bool, bool, bool, bool, bool, bool)
{
	if (center.z >= radius)
		return nullptr;
	gPoint = {};
	gPoint.point = {center.x, center.y, 0};
	gPoint.normal = {0, 0, 1};
	gPoint.depth = radius - center.z;
	return gScenery;
}
static void *FixtureSymbol(void *, const char *name)
{
#define SYMBOL(n, f)                                                                                         \
	if (!std::strcmp(name, n))                                                                               \
	return reinterpret_cast<void *>(f)
	SYMBOL("_ZN6CPools9GetPedRefEP4CPed", Ref);
	SYMBOL("_ZN7CMatrix8UpdateRWEv", Noop);
	SYMBOL("_ZN7CEntity13UpdateRwFrameEv", Noop);
	SYMBOL("_ZN9CPhysical12RemoveAndAddEv", Noop);
	SYMBOL("_ZN4CPed8SetGetUpEv", Noop);
	SYMBOL("_ZN4CPed7SetFallEi11AnimationIdh", DispatchFall);
	SYMBOL("_ZN12CCutsceneMgr10ms_runningE", &gCutscene);
	SYMBOL("_ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb", Sphere);
	SYMBOL("gaTempSphereColPoints", &gPoint);
	return nullptr;
#undef SYMBOL
}
namespace hooks
{
void *Install(const char *name, void *replacement)
{
	gSlots[name] = replacement;
	if (!std::strcmp(name, "_ZN4CPed14ProcessControlEv"))
		return reinterpret_cast<void *>(NativeControl);
	if (!std::strcmp(name, "_ZN4CPed22ProcessEntityCollisionEP7CEntityP9CColPoint"))
		return reinterpret_cast<void *>(NativeEntityCollision);
	if (!std::strcmp(name, "_ZN4CPed14CollideWithPedEPS_"))
		return reinterpret_cast<void *>(NativePedCollision);
	if (!std::strcmp(name, "_ZN4CPed7SetFallEi11AnimationIdh"))
		return reinterpret_cast<void *>(NativeFall);
	return reinterpret_cast<void *>(Noop);
}
} // namespace hooks
static int failures;
#define CHECK(c)                                                                                             \
	do {                                                                                                     \
		if (!(c)) {                                                                                          \
			std::printf("FAIL line%d: %s\n", __LINE__, #c);                                                  \
			++failures;                                                                                      \
		}                                                                                                    \
	} while (0)
static const reactphysics::Pose gCapturedIdleOffsets = {{{-.007935f, -.020996f, -.023850f},
														 {-.023590f, -.057740f, .279228f},
														 {-.000549f, .027252f, .680600f},
														 {-.210541f, .028686f, .516036f},
														 {-.273437f, .022979f, .227828f},
														 {-.259521f, .133117f, -.035996f},
														 {.178040f, -.081025f, .510221f},
														 {.247101f, -.134827f, .228465f},
														 {.261017f, -.100312f, -.055330f},
														 {-.115814f, .006958f, -.026221f},
														 {-.179321f, .041290f, -.477658f},
														 {-.207428f, -.103302f, -.862066f},
														 {.083405f, -.040467f, -.030477f},
														 {.110230f, -.037842f, -.486856f},
														 {.099152f, -.091065f, -.894905f}}};
static void CapturedRig(uint8_t *ped)
{
	const auto root = game::EntityPos(ped);
	for (int b = 0; b < 22; b++) {
		float *m = gRig + b * 16;
		std::memset(m, 0, 16 * sizeof(float));
		m[0] = m[5] = m[10] = m[15] = 1;
		m[12] = root.x;
		m[13] = root.y;
		m[14] = root.z;
	}
	for (int j = 0; j < reactphysics::Count; j++) {
		float *m = gRig + Index(nullptr, reactions::kPhysicalTags[j]) * 16;
		m[12] += gCapturedIdleOffsets[size_t(j)].x;
		m[13] += gCapturedIdleOffsets[size_t(j)].y;
		m[14] += gCapturedIdleOffsets[size_t(j)].z;
	}
	gRig[4 * 16 + 14] = root.z + .549091f;
}
static void Reset()
{
	gNow += 1000;
	gNpcGeneration = 17;
	reactions::gLastFrame = gNow + 1;
	reactions::CheckTimeline();
	std::memset(gPed, 0, sizeof gPed);
	std::memset(gPlayer, 0, sizeof gPlayer);
	std::memset(gPad, 0, sizeof gPad);
	std::memset(gCam, 0, sizeof gCam);
	gCam[0x63] = 1;
	gPlayerLive = gPlayer;
	gCutscene = gRecycled = gSwimming = gClimbing = gDodge = gCrouch = gNestedDamage = gEntityContact =
		gStopContact = false;
	gDuringNative = 0;
	gContactPiece = 2;
	gNativeFalls = gPlayerFalls = gNpcFalls = gNativeCollisions = gNativeControls = 0;
	gConfig.npcReactions = true;
	for (auto ped : {gPed, gPlayer}) {
		*reinterpret_cast<uint32_t *>(ped + 0x60) = 3 | game::kEntityUsesCollision;
		*reinterpret_cast<uint8_t **>(ped + 0x58) = gClump;
		*reinterpret_cast<float *>(ped + 0x44c) = 100;
		*reinterpret_cast<int *>(ped + 0x2fc) = game::kPedIdle;
		*reinterpret_cast<int *>(ped + 0x304) = 2;
		*reinterpret_cast<CVector *>(ped + 0x38) = {0, 0, 1.04f};
	}
	*reinterpret_cast<CVector *>(gPlayer + 0x38) = {-0.5f, 0, 1.04f};
	CapturedRig(gPed);
}
int main()
{
	game::api.FindPlayerPed = Player;
	game::api.PoolsGetPed = Lookup;
	game::api.timeMs = &gNow;
	game::api.timeStep = &gStep;
	game::api.GetAnimHierarchyFromSkinClump = Hierarchy;
	game::api.HAnimMatrixArray = Matrices;
	game::api.HAnimIDGetIndex = Index;
	game::api.UpdateRpHAnim = CapturedRig;
	game::api.ProcessVerticalLine = Vertical;
	game::api.TestSphereAgainstWorld = Sphere;
	game::api.ignoreEntity = &gIgnored;
	game::api.GetPad = Pad;
	game::api.theCamera = gCam;
	CHECK(reactions::Install());
	for (bool reversed : {false, true}) {
		Reset();
		game::EntitySpeed(gPlayer) = {.03f, 0, 0}; // 1.5m/s
		reactions::Hook_Control(reversed ? gPed : gPlayer);
		CHECK(gNativeControls == 1 && gNativeCollisions == 1);
		CHECK(gNativeFalls == 0 && game::PedState(gPed) == game::kPedIdle &&
			  game::PedState(gPlayer) == game::kPedIdle);
	}
	for (bool reversed : {false, true}) {
		Reset();
		game::EntitySpeed(gPlayer) = {.034f, 0, 0};
		game::EntitySpeed(gPed) = {-.034f, 0, 0};
		reactions::Hook_Control(reversed ? gPed : gPlayer);
		CHECK(gNativeFalls == 0 && game::PedState(gPed) == game::kPedIdle &&
			  game::PedState(gPlayer) == game::kPedIdle);
	}
	for (bool reversed : {false, true}) {
		Reset();
		game::EntitySpeed(gPlayer) = {.10f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		reactions::Hook_Control(reversed ? gPed : gPlayer);
		CHECK(gNativeCollisions == 1 && gPlayerFalls == 1 && gNpcFalls == 1);
		CHECK(game::PedState(gPlayer) == game::kPedFall && game::PedState(gPed) == game::kPedFall);
		CHECK(gNativeFallDuration > 0 && gNativeFallDuration < 5000);
	}
	Reset();
	game::EntitySpeed(gPlayer) = {.03f, 0, 0};
	DispatchPedCollision(gPlayer, gPed);
	gNow += 100;
	game::EntitySpeed(gPlayer) = {.10f, 0, 0};
	*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
	DispatchPedCollision(gPlayer, gPed);
	CHECK(gPlayerFalls == 1 && gNpcFalls == 1); // an earlier walk must not swallow a real sprint

	// Captured native rig and the actual sphere ABI sweep, through the whole
	// control/contact/world/render sequence. The native fall and passive local
	// response must not accumulate into a walk-induced collapse.
	for (bool reversed : {false, true})
		for (float walk : {1.2f, 1.7f, 2.1f})
			for (int piece = 0; piece <= 6; piece++) {
				Reset();
				gEntityContact = true;
				gContactPiece = piece;
				bool fell = false;
				for (int frame = 0; frame < 240; frame++) {
					gNow += 17;
					game::EntitySpeed(gPlayer) = {walk / 50, 0, 0};
					game::EntitySpeed(gPed) = {};
					reactions::Hook_Control(reversed ? gPed : gPlayer);
					game::EntitySpeed(gPlayer) = {walk / 50, 0, 0};
					alignas(16) uint8_t point[sizeof(reactcontact::NativePoint)];
					reactions::Hook_Collision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed, point);
					reactions::Hook_WorldCollision(gPed);
					reactions::BeforePreRender(gPed);
					CapturedRig(gPed);
					reactions::AfterPreRender(gPed);
					fell |= game::PedState(gPed) != game::kPedIdle;
					if (!reactions::gPeople.empty())
						fell |= reactions::gPeople[0].physical.State() != reactphysics::Mode::Balanced;
				}
				CHECK(!fell && gNativeFalls == 0 && game::Health(gPed) == 100);
			}
	// Fresh sprint for same-pair retry checks.
	Reset();
	game::EntitySpeed(gPlayer) = {.1f, 0, 0};
	*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
	DispatchPedCollision(gPlayer, gPed);
	// Retry the same native pair in both orders after the accepted sprint.
	const int oldFalls = gNativeFalls;
	const auto timer = *reinterpret_cast<uint32_t *>(gPlayer + 0x68c);
	for (int frame = 0; frame < 40; frame++) {
		gNow += 17;
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		DispatchPedCollision(frame % 2 ? gPed : gPlayer, frame % 2 ? gPlayer : gPed);
	}
	CHECK(gNativeFalls == oldFalls && *reinterpret_cast<uint32_t *>(gPlayer + 0x68c) == timer);
	// World-relative closing, not the controller that happens to run first.
	for (bool reversed : {false, true})
		for (int moving = 0; moving < 3; moving++) {
			Reset();
			game::EntitySpeed(gPlayer) = {.1f, 0, 0};
			*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
			game::EntitySpeed(gPed) = moving == 0	? CVector{.08f, 0, 0}
									  : moving == 1 ? CVector{-.02f, 0, 0}
													: CVector{.1f, 0, 0};
			DispatchPedCollision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed);
			CHECK(gPlayerFalls == (moving == 1 ? 1 : 0) && gNpcFalls == (moving == 1 ? 1 : 0));
		}
	// The actual ordering can be collision first, controller on the next
	// frame: native contact response has already stopped the incoming motion.
	for (bool reversed : {false, true}) {
		Reset();
		gEntityContact = gStopContact = true;
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		alignas(16) uint8_t point[sizeof(reactcontact::NativePoint)];
		reactions::Hook_Collision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed, point);
		game::EntitySpeed(gPlayer) = {};
		game::EntitySpeed(gPed) = {};
		gNow += 17;
		DispatchPedCollision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed);
		CHECK(gPlayerFalls == 1 && gNpcFalls == 1);
	}
	// A cached impulse is authority for its original pool generation, visual
	// owner, timeline and nearby origin only, and expires after100ms.
	for (int invalid = 0; invalid < 5; invalid++) {
		Reset();
		gEntityContact = gStopContact = true;
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		alignas(16) uint8_t point[sizeof(reactcontact::NativePoint)];
		reactions::Hook_Collision(gPlayer, gPed, point);
		game::EntitySpeed(gPlayer) = {};
		game::EntitySpeed(gPed) = {};
		switch (invalid) {
		case 0:
			gNow += 101;
			break;
		case 1:
			*reinterpret_cast<uint8_t **>(gPed + 0x58) = gReplacementClump;
			break;
		case 2:
			++gNpcGeneration;
			break;
		case 3:
			*reinterpret_cast<float *>(gPlayer + 0x38) += 10;
			*reinterpret_cast<float *>(gPed + 0x38) += 10;
			break;
		case 4:
			gNow -= 100;
			break;
		}
		DispatchPedCollision(gPlayer, gPed);
		CHECK(gPlayerFalls == 0 && gNpcFalls == 0);
	}
	// A co-moving first visit must not be replaced by the now-stopped other
	// actor's artificial closing speed during the second visit in that frame.
	for (bool reversed : {false, true}) {
		Reset();
		gEntityContact = gStopContact = true;
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		game::EntitySpeed(gPed) = {.1f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		alignas(16) uint8_t point[sizeof(reactcontact::NativePoint)];
		reactions::Hook_Collision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed, point);
		reactions::Hook_Collision(reversed ? gPlayer : gPed, reversed ? gPed : gPlayer, point);
		gNow += 17;
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		DispatchPedCollision(gPlayer, gPed);
		CHECK(gPlayerFalls == 0 && gNpcFalls == 0);
	}
	for (int exclude = 0; exclude < 16; exclude++) {
		Reset();
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		switch (exclude) {
		case 0:
			gConfig.npcReactions = false;
			break;
		case 1:
			gCutscene = true;
			break;
		case 2:
			gPed[0x19a] = 2;
			break;
		case 3:
			gCam[0x63] = 0;
			break;
		case 4:
			gCam[0x75] = 1;
			break;
		case 5:
			*reinterpret_cast<uint16_t *>(gPad + 0xf0) = 1;
			break;
		case 6:
			gSwimming = true;
			break;
		case 7:
			gClimbing = true;
			break;
		case 8:
			gDodge = true;
			break;
		case 9:
			gCrouch = true;
			break;
		case 10:
			gPlayer[0x4c0] = 1;
			break;
		case 11:
			*reinterpret_cast<int *>(gPlayer + 0x2fc) = game::kPedAttack;
			break;
		case 12:
			gRecycled = true;
			break;
		case 13:
			*reinterpret_cast<void **>(gPlayer + 0x720) = gPed;
			break;
		case 14:
			game::PedFlags(gPlayer) |= game::kPedInTheAir;
			break;
		case 15:
			game::PedFlags(gPlayer) |= game::kPedLanding;
			break;
		}
		DispatchPedCollision(gPlayer, gPed);
		CHECK(gPlayerFalls == 0 && gNpcFalls == 1 &&
			  reactions::gPeople.empty()); // native original remains authoritative
	}
	for (int change = 1; change <= 6; change++) {
		Reset();
		game::EntitySpeed(gPlayer) = {.1f, 0, 0};
		*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
		gDuringNative = change;
		DispatchPedCollision(gPlayer, gPed);
		CHECK(gPlayerFalls == 0 && reactions::gPeople.empty());
	}
	Reset();
	gNestedDamage = true;
	game::EntitySpeed(gPlayer) = {.03f, 0, 0};
	DispatchPedCollision(gPlayer, gPed);
	CHECK(game::Health(gPed) == 95 && gPlayerFalls == 1 && gNpcFalls == 0 && !reactions::gPeople.empty());
	CHECK(reactions::gPeople[0].physical.Velocity(reactphysics::Chest).x > 1);
	// Falls from weapons/landing outside contact have unchanged ABI and timing.
	Reset();
	DispatchFall(gPlayer, 800, 27, 1);
	CHECK(gPlayerFalls == 1 && gNativeFallDuration == 800 && gNativeFallAnimation == 27);
	Reset();
	game::EntitySpeed(gPlayer) = {.1f, 0, 0};
	*reinterpret_cast<int *>(gPlayer + 0x304) = 5;
	reactions::gPeople.resize(12);
	DispatchPedCollision(gPlayer, gPed);
	CHECK(gPlayerFalls == 1 && gNpcFalls == 1 &&
		  reactions::gPeople.size() == 12); // finite native fallback at body capacity
	std::printf("pedcontacthook: %d failures\n", failures);
	return failures ? 1 : 0;
}
