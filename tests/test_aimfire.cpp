// Third-person aimed fire while moving (#20), through the actual aim adapter:
// the first shot starts where the standing aim parks the fire animation, so
// native CPed::Attack fires as soon as it does standing; native cadence, hip
// fire and the crouch stay native; the arm-pose learning never holds a moving
// player still; moving adds a slight spread; sprint can't block the trigger.
#include "../src/aimcam.cpp"
#include "../src/body.cpp"
#include <cstdio>
#include <cstring>
namespace game
{
Api api{};
float CameraHeading() { return 0; }
void FaceHeading(uint8_t *, float) {}
} // namespace game
Config gConfig;
void Log(const char *, ...) {}
namespace hooks
{
void *Install(const char *, void *) { return nullptr; }
volatile uint64_t *Flag(const char *) { return nullptr; }
} // namespace hooks
namespace camera
{
bool FirstPersonOnFoot() { return false; }
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

static uint8_t ped[0x900]{}, cam[0x900]{}, pad[0x200]{}, clump[8]{}, info[0x80]{}, fire[0x40]{};
static uint32_t now = 1000;
static int failures, stands, times;
static bool firing, fireAnim = true;
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
static bool Firing(uint8_t *) { return firing; }
static uint8_t *Association(uint8_t *c, uint32_t anim) { return c == clump && anim == 0xcd && fireAnim ? fire : nullptr; }
static void SetTime(uint8_t *assoc, float t)
{
	*reinterpret_cast<float *>(assoc + 0x30) = t;
	++times;
}
static void Stand(uint8_t *p, uint8_t *) { ++stands; game::SetPedState(p, game::kPedAimGun); }
static void Unstand(uint8_t *p) { game::SetPedState(p, game::kPedIdle); }
static void Look(uint8_t *, float, bool, bool) {}
static void Aim(uint8_t *, float) {}
static void Timer(uint8_t *, int) {}
static float Pitch(uint8_t *) { return 0; }
static float &Time() { return *reinterpret_cast<float *>(fire + 0x30); }
constexpr float kColtLoop = 11 / 30.0f; // weapon.dat column R for the Colt45

static void Setup(int weapon, bool walking, bool aimed)
{
	std::memset(pad, 0, sizeof pad);
	*reinterpret_cast<int16_t *>(pad + game::kPadRB) = aimed;
	*reinterpret_cast<int16_t *>(pad + game::kPadLeftStickY) = walking ? -128 : 0;
	*reinterpret_cast<int32_t *>(game::PedWeapon(ped, 5)) = weapon;
	*reinterpret_cast<uint64_t *>(ped + 0x188) = 0;
	*reinterpret_cast<int32_t *>(ped + 0x304) = walking ? 2 : 1; // CPed::m_nMoveState: walk / still
	*reinterpret_cast<float *>(info + 0x38) = kColtLoop;
	*reinterpret_cast<uint32_t *>(info + 0x5c) = weapon == 17 ? 0x80 : 0; // one-handed Colt; M4 two-handed
	std::memset(fire, 0, sizeof fire);
	*reinterpret_cast<uint16_t *>(fire + 0x3e) = 1; // running
	fireAnim = true;
	firing = false;
	stands = times = 0;
}

int main()
{
	using namespace game;
	api.FindPlayerPed = Player;
	api.GetPad = Pad;
	api.theCamera = cam;
	api.timeMs = &now;
	api.PadGetWeapon = Firing;
	api.GetAssociation = Association;
	api.SetPointGunAt = Stand;
	api.ClearPointGunAt = Unstand;
	api.SetLookFlag = Look;
	api.SetAimFlag = Aim;
	api.SetLookTimer = Timer;
	api.Find3rdPersonQuickAimPitch = Pitch;
	static uint8_t mouse = 1;
	api.useMouse3rdPerson = &mouse;
	gConfig.aimCamera = true;
	*reinterpret_cast<uint8_t **>(ped + 0x58) = clump;
	*reinterpret_cast<float *>(ped + 0x44c) = 100;
	ped[0x64c] = 5;
	aimcam::gInstalled = true;
	aimcam::gWeaponInfo = Info;
	aimcam::gSetAnimTime = SetTime;

	// Walking aimed, RT pulled: the attack's fresh fire animation starts at
	// the loop start, as standing it would, still running for native Attack.
	Setup(17, true, true);
	firing = true;
	SetPedState(ped, kPedAttack);
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(std::fabs(Time() - kColtLoop) < 1e-6f && times == 1);
	CHECK(*reinterpret_cast<uint16_t *>(fire + 0x3e) & 1);
	// The next frames of that attack, and a burst through the loop: native cadence.
	aimcam::AfterPlayerWeapon(ped, pad);
	Time() = kColtLoop + .05f;
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(times == 1 && std::fabs(Time() - kColtLoop - .05f) < 1e-6f);
	// Standing aimed: native PointGunAt already parked it there.
	Setup(17, false, true);
	firing = true;
	SetPedState(ped, kPedAttack);
	Time() = kColtLoop;
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(times == 0);
	// Hip fire without LT, the crouch's own fire pose, and no animation: native.
	Setup(17, true, false);
	firing = true;
	SetPedState(ped, kPedAttack);
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(times == 0 && Time() == 0);
	Setup(17, true, true);
	firing = true;
	SetPedState(ped, kPedAttack);
	*reinterpret_cast<uint64_t *>(ped + 0x188) = uint64_t(3) << 35;
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(times == 0 && Time() == 0);
	Setup(17, true, true);
	firing = true;
	fireAnim = false;
	SetPedState(ped, kPedAttack);
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(times == 0);

	// A two-handed gun's arm pose not yet learnt: walking, he is never held
	// still to learn it; standing, it is learnt as before.
	Setup(26, true, true);
	aimcam::gGunRaised = false;
	SetPedState(ped, kPedIdle);
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(stands == 0);
	Setup(26, false, true);
	aimcam::gGunRaised = false;
	SetPedState(ped, kPedIdle);
	aimcam::AfterPlayerWeapon(ped, pad);
	CHECK(stands == 1);

	// Moving adds a slight spread, from first person's model; standing and
	// unaimed stay the game's exact aim.
	Setup(26, true, true);
	CHECK(std::fabs(aimcam::Spread() - fpfire::AimedMoving(fpfire::KindOf(26), fpfire::Moving::kWalking)) < 1e-7f);
	CHECK(aimcam::Spread() > 0);
	Setup(26, false, true);
	CHECK(aimcam::Spread() == 0);
	Setup(26, true, false);
	CHECK(aimcam::Spread() == 0);
	Setup(26, true, true);
	*reinterpret_cast<uint64_t *>(ped + 0x188) = kPedInTheAir;
	CHECK(aimcam::Spread() > fpfire::AimedMoving(fpfire::KindOf(26), fpfire::Moving::kWalking));

	// Knocked down (#25): no aiming at all till he's up, so nothing turns,
	// raises or spreads against his ragdoll.
	Setup(26, true, true);
	reactions::gStubDown = true;
	CHECK(!aimcam::Aiming() && aimcam::Spread() == 0);
	reactions::gStubDown = false;
	CHECK(aimcam::Aiming());

	// The pad field native CPad::GetSprint reads (0x100109448): Cross unless a
	// controller in mode 2 (Circle); a controller in a mode above 3 has none.
	CHECK(aimcam::SprintField(0, false) == kPadCross && aimcam::SprintField(2, false) == kPadCross);
	CHECK(aimcam::SprintField(0, true) == kPadCross && aimcam::SprintField(1, true) == kPadCross &&
	      aimcam::SprintField(3, true) == kPadCross);
	CHECK(aimcam::SprintField(2, true) == 0x22 && aimcam::SprintField(4, true) < 0);

	if (failures)
		return 1;
	std::puts("aimfire: all third-person aimed-fire tests passed");
}
