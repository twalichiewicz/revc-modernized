// Actual FP velocity producer -> frozen native pedestrian fall consumer.
// Reuse the pool/rig/contact ABI fixture without changing its independent tests.
#define main ContactHookRegressionMain
#define Aiming ContactFixtureAiming
#include "test_pedcontacthook.cpp"
#undef Aiming
#undef main
#include <limits>

static bool gWalkingAim;
namespace aimcam { bool Aiming() { return gWalkingAim; } }

static void *WalkSymbol(void *, const char *);
#define dlsym WalkSymbol
#include "../src/fpwalk.cpp"
#undef dlsym

static uint8_t gFreeCamera = 1, gWalkWeaponInfo[0x64];
static bool gFirstPerson = true, gScoped;
namespace camera { bool FirstPersonOnFoot() { return gFirstPerson; } }
namespace sniper {
bool Wanted() { return gScoped; }
bool Active() { return gScoped; }
}
namespace game {
float CameraHeading() { return 0; }
void FaceHeading(uint8_t *, float) {}
}
static bool InControl(uint8_t *) { return true; }
static uint8_t *WeaponInfo(int) { return gWalkWeaponInfo; }
static void *WalkSymbol(void *, const char *name)
{
	if (!std::strcmp(name, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType"))
		return reinterpret_cast<void *>(WeaponInfo);
	if (!std::strcmp(name, "_ZN7CCamera8bFreeCamE"))
		return &gFreeCamera;
	return FixtureSymbol(nullptr, name);
}

// macVC PlayerControl1stPersonRunAround 0x1d28e0..0x1d2910:
// GetPedWalk magnitude /60 is the animation move amount. SetRealMoveAnim
// 0x1d19e8..0x1d1abc selects native Walk2 below1, Run4 above it.
// CalculateNewVelocity 0x191a48..0x191aa0 publishes animation displacement
// into +464; UpdatePosition 0x1922dc..0x1922ec copies that velocity on static
// ground. The actual WALK_player root track averages .786m/s; other walking
// groups/strafe tracks can be quicker. Keep those native paces as inputs.
static void NativeWalkingControl(float pace, bool sprint)
{
	const int x = game::PadNew(gPad, game::kPadLeftStickX);
	const int y = game::PadNew(gPad, game::kPadLeftStickY);
	const float amount = std::hypot(float(x), float(y)) / 60.0f;
	*reinterpret_cast<float *>(gPlayer + 0x788) = amount;
	*reinterpret_cast<int *>(gPlayer + 0x304) = sprint ? 5 : amount < 1 ? 2 : 4;
	*reinterpret_cast<CVector *>(gPlayer + 0x2a8) = {pace * gStep / 50, 0, 0};
	*reinterpret_cast<Vec2 *>(gPlayer + 0x464) = {pace / 50, 0};
	game::EntitySpeed(gPlayer) = {pace / 50, 0, .013f};
}
static void MovementFrame(float nativePace, bool sprint = false)
{
	gNow += 17;
	fpwalk::TakeStick(gPad);
	const bool strafed = fpwalk::BeforeControl();
	NativeWalkingControl(nativePace, sprint);
	fpwalk::AfterControl(gPlayer, strafed);
}
static float Speed()
{
	return std::hypot(game::EntitySpeed(gPlayer).x, game::EntitySpeed(gPlayer).y) * 50;
}
static float WalkFrames(int push, float nativePace, int weapon = 0, bool sprint = false)
{
	Reset();
	gConfig.firstPersonMovement = true;
	gConfig.fpAccel = 5;
	gConfig.fpFriction = 4;
	gStep = 50.0f / 60;
	gFreeCamera = 1;
	gFirstPerson = true;
	gScoped = false;
	gWalkingAim = false;
	game::api.IsPedInControl = InControl;
	game::PedFlags(gPlayer) |= game::kPedStanding;
	*reinterpret_cast<CVector *>(gCam + 0x10) = {1, 0, 0};
	game::PadNew(gPad, game::kPadLeftStickY) = int16_t(-push);
	*reinterpret_cast<int *>(game::PedWeapon(gPlayer, 0)) = weapon;
	std::memset(gWalkWeaponInfo, 0, sizeof gWalkWeaponInfo);
	for (int frame = 0; frame < 180; ++frame) {
		MovementFrame(nativePace, sprint);
		CHECK(fpwalk::Active() && gFreeCamera == 1);
		CHECK(game::EntitySpeed(gPlayer).z == .013f);
	}
	return Speed();
}
int main()
{
	CHECK(ContactHookRegressionMain() == 0);
	CHECK(fpwalk::Install());
	for (bool reversed : {false, true})
		for (float nativeWalk : {.786f, 1.7f, 2.1f})
			for (int weapon : {0, 26}) {
				const float speed = WalkFrames(58, nativeWalk, weapon);
				CHECK(game::PedMoveState(gPlayer) == 2);
				CHECK(speed > nativeWalk * .95f && speed <= nativeWalk + .001f);
				DispatchPedCollision(reversed ? gPed : gPlayer, reversed ? gPlayer : gPed);
				CHECK(gNativeFalls == 0 && game::PedState(gPlayer) == game::kPedIdle &&
				      game::PedState(gPed) == game::kPedIdle);
			}
	// Ordinary run speed and its native collision fall remain native; sprint
	// still produces the accepted reciprocal player/NPC fall.
	for (bool sprint : {false, true}) {
		const float speed = WalkFrames(128, 1.7f, 0, sprint);
		CHECK(std::fabs(speed - (sprint ? 9.8f : 6.4f)) < .01f);
		CHECK(game::PedMoveState(gPlayer) == (sprint ? 5 : 4));
		DispatchPedCollision(gPlayer, gPed);
		CHECK(gPlayerFalls == (sprint ? 1 : 0) && gNpcFalls == 1);
	}
	// A run-to-walk transition retains real incoming momentum; the new pace
	// changes its wish/stop friction, without turning contact resistance into
	// an instantaneous speed clamp. It then settles at the native walking pace.
	WalkFrames(128, 1.7f);
	game::PadNew(gPad, game::kPadLeftStickY) = -58;
	MovementFrame(1.7f);
	CHECK(Speed() > 5 && Speed() < 6.4f);
	for (int frame = 0; frame < 180; ++frame)
		MovementFrame(1.7f);
	CHECK(std::fabs(Speed() - 1.7f) < .001f);
	// Air control keeps incoming velocity and scopes keep their separate creep.
	game::PedFlags(gPlayer) |= game::kPedInTheAir;
	MovementFrame(.1f);
	CHECK(Speed() > 1.6f);
	game::PedFlags(gPlayer) &= ~game::kPedInTheAir;
	gScoped = true;
	for (int frame = 0; frame < 180; ++frame)
		MovementFrame(1.7f);
	CHECK(std::fabs(Speed() - .6f * 58 / 128) < .001f);
	gScoped = false;
	gWalkingAim = true;
	for (int frame = 0; frame < 180; ++frame)
		MovementFrame(.1f);
	CHECK(std::fabs(Speed() - 6.4f * .52f * 58 / 128) < .001f);
	gWalkingAim = false;
	gPlayer[0x18c] |= 0x10;
	for (int frame = 0; frame < 180; ++frame)
		MovementFrame(.1f);
	CHECK(std::fabs(Speed() - 1.6f * 58 / 128) < .001f);
	gPlayer[0x18c] &= ~uint8_t(0x10);
	// Third person leaves native movement entirely in charge.
	gScoped = false;
	gFirstPerson = false;
	MovementFrame(1.2f);
	CHECK(!fpwalk::Active() && std::fabs(Speed() - 1.2f) < .001f);
	CHECK(WalkFrames(58, 0) == 0); // zero native root motion cannot become a run
	for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		WalkFrames(58, .786f);
		MovementFrame(bad);
		CHECK(std::isfinite(Speed())); // invalid native pace cannot poison a valid captured velocity
	}
	for (float tick : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		WalkFrames(58, .786f);
		gStep = tick;
		MovementFrame(1.2f);
		CHECK(!fpwalk::Active() && std::fabs(Speed() - 1.2f) < .001f && gFreeCamera == 1);
	}
	if (!failures)
		std::puts("fpwalk contact hook: all passed");
	return failures ? 1 : 0;
}
