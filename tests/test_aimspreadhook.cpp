// Third person aimed and moving (#20), through the actual camera fire hook:
// the shot leaves along a slightly spread active-camera front, which native
// Find3rdPersonCamTargetVector reads (CCam::Front at +0x168), and the camera
// is exactly restored after. Standing, unaimed, NPC and first-person shots
// keep their own aim.
#define main damage_overlay_main
#include "test_damageoverlayhook.cpp"
#undef main
#include <cmath>

namespace combat
{
bool MayFire(uint8_t *, uint8_t *, CVector *) { return true; }
void PlayerShot(const CVector &, const CVector &) {}
} // namespace combat
namespace police
{
void PlayerFired(int32_t) {}
} // namespace police
namespace sniper
{
void BeforeShot() {}
void AfterShot(bool) {}
} // namespace sniper
namespace shotsound
{
void Shot(int32_t) {}
} // namespace shotsound
namespace fpshoot
{
float SpreadNow() { return 0; }
} // namespace fpshoot

static CVector gShotFront;
static int gShots;
static uint8_t weapon[0x40]{}, npc[0x900]{};
static bool NativeFire(uint8_t *, uint8_t *, CVector *)
{
	gShotFront = *reinterpret_cast<CVector *>(game::ActiveCam() + 0x168);
	++gShots;
	return true;
}
static CVector &Front() { return *reinterpret_cast<CVector *>(game::ActiveCam() + 0x168); }
static float Angle(const CVector &a, const CVector &b)
{
	const float dot = a.x * b.x + a.y * b.y + a.z * b.z;
	return std::acos(std::fmin(1.0f, std::fmax(-1.0f, dot)));
}

int main()
{
	game::api.FindPlayerPed = Player;
	game::api.theCamera = cam;
	camera::orig_Fire = NativeFire;
	camera::gOnFoot = false; // third person
	const CVector front{0, 1, 0};
	*reinterpret_cast<CVector *>(game::ActiveCam() + 0x18c) = {0, 0, 1}; // CCam::Up
	// Moving and aimed: every shot within the cone, the cone actually used,
	// the camera's front exactly as it was afterwards.
	aimcam::gStubAiming = true;
	aimcam::gStubSpread = .01f;
	float widest = 0;
	bool restored = true, inside = true;
	for (int shot = 0; shot < 400; ++shot) {
		Front() = front;
		CHECK(camera::Hook_Fire(weapon, ped, nullptr));
		const float angle = Angle(gShotFront, front);
		inside &= angle <= .01f + 1e-5f;
		widest = std::fmax(widest, angle);
		restored &= Front().x == front.x && Front().y == front.y && Front().z == front.z;
	}
	CHECK(gShots == 400 && inside && widest > .005f && restored);
	// Standing aimed (aimcam::Spread() is 0; unaimed it is 0 too, tested in
	// test_aimfire), an NPC's shot while he aims and moves, and first person
	// (its own aimed path): the camera's own front.
	for (int kind = 0; kind < 3; ++kind) {
		aimcam::gStubAiming = true;
		aimcam::gStubSpread = kind == 0 ? 0.0f : .01f;
		camera::gOnFoot = kind == 2;
		Front() = front;
		CHECK(camera::Hook_Fire(weapon, kind == 1 ? npc : ped, nullptr));
		CHECK(kind == 2 || Angle(gShotFront, front) == 0.0f);
		CHECK(Front().x == front.x && Front().y == front.y && Front().z == front.z);
	}
	camera::gOnFoot = false;
	if (failures)
		return 1;
	std::puts("aimspreadhook: third-person moving spread through the fire hook passed");
}
