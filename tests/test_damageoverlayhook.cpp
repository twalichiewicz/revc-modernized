// Exercise accepted native InflictDamage -> real camera frame -> Im2D draw.
#include "../src/camera.cpp"
#include "../src/player.cpp"
#include <cstdio>
#include <cstring>
Config gConfig;
void Log(const char *, ...) {}
namespace hooks
{
void *Install(const char *, void *) { return nullptr; }
void *Chain(const char *, void *) { return nullptr; }
} // namespace hooks
namespace playerreact
{
void BeforeControl(uint8_t *) {}
void AfterControl(uint8_t *) {}
void BeforePreRender(uint8_t *) {}
void AfterPreRender(uint8_t *) {}
} // namespace playerreact
namespace climb
{
void BeforeControl(uint8_t *) {}
void AfterControl(uint8_t *) {}
bool Active() { return false; }
bool Posing() { return false; }
float Pose(body::Pose &, float) { return 0; }
} // namespace climb
namespace swim
{
void AfterControl(uint8_t *) {}
bool Swimming() { return false; }
bool Posing() { return false; }
float Pose(body::Pose &) { return 0; }
bool RefusesDamage(const uint8_t *, int32_t) { return false; }
bool EyePosition(uint8_t *, CVector &) { return false; }
} // namespace swim
namespace crouch
{
void AfterControl(uint8_t *) {}
bool Posing() { return false; }
float Pose(uint8_t *, body::Pose &) { return 0; }
} // namespace crouch
namespace shootdodge
{
void AfterControl(uint8_t *) {}
bool Posing() { return false; }
float Pose(uint8_t *, body::Pose &) { return 0; }
} // namespace shootdodge
namespace aimcam
{
bool BeforeControl() { return false; }
void AfterControl(uint8_t *, bool) {}
void Lower(uint8_t *) {}
void AfterPlayerWeapon(uint8_t *, uint8_t *) {}
void PoseArms(uint8_t *) {}
bool gStubAiming;
float gStubSpread;
bool Aiming() { return gStubAiming; }
float Spread() { return gStubSpread; }
} // namespace aimcam
namespace fpwalk
{
bool BeforeControl() { return false; }
void AfterControl(uint8_t *, bool) {}
bool BeforeWeapon() { return false; }
void AfterWeapon(bool) {}
} // namespace fpwalk
namespace drivers
{
void Frame() {}
} // namespace drivers
namespace reactions
{
void Frame() {}
void BeforePreRender(uint8_t *) {}
void AfterPreRender(uint8_t *) {}
void Hit(uint8_t *, uint8_t *, int, int) {}
// Knocked down (#25): controllable for first-person head-view tests.
bool gStubDown;
Vec3 gStubEye, gStubRight{1, 0, 0}, gStubForward{0, 1, 0}, gStubUp{0, 0, 1};
bool PlayerDown() { return gStubDown; }
bool PlayerHeadView(Vec3 &eye, Vec3 &right, Vec3 &forward, Vec3 &up)
{
	eye = gStubEye; right = gStubRight; forward = gStubForward; up = gStubUp;
	return gStubDown;
}
} // namespace reactions
namespace achievements { void Damaged(uint8_t *, uint8_t *, int, bool) {} }
namespace driveby
{
void PilotHolster(uint8_t *) {}
void PreRender(uint8_t *) {}
bool Aiming() { return false; }
CVector AimDirection() { return {0, 1, 0}; }
} // namespace driveby
namespace seatgun
{
void BeforePose(uint8_t *) {}
void Pose(uint8_t *) {}
} // namespace seatgun
namespace body
{
bool Prepare(uint8_t *) { return false; }
void Apply(uint8_t *, const Pose &, float) {}
void ApplyOver(uint8_t *, const Pose &, float) {}
} // namespace body
namespace steering
{
void PoseDriver(uint8_t *) {}
} // namespace steering
namespace jetboardingpose
{
bool BeforePedRender(uint8_t *) { return false; }
void AfterPedRender(uint8_t *) {}
} // namespace jetboardingpose
namespace fpshoot
{
void AfterWeapon(uint8_t *, uint8_t *) {}
bool Shooting() { return false; }
void Tick(float) {}
void Punch(float &a, float &b) { a = b = 0; }
void Jolt(float &a, float &b, float &c) { a = b = c = 0; }
} // namespace fpshoot
namespace falls
{
float Land(uint8_t *, int32_t, float &) { return 0; }
void Landed(uint8_t *, float) {}
} // namespace falls
namespace combat
{
void Hurt(uint8_t *, uint8_t *, bool) {}
} // namespace combat
namespace rumble
{
void Damaged(const uint8_t *, const uint8_t *, int32_t, float) {}
} // namespace rumble
namespace wanted
{
void Update() {}
} // namespace wanted
namespace traffic
{
void AfterCamera() {}
} // namespace traffic
namespace police
{
void Update() {}
} // namespace police
namespace heli
{
void Update() {}
} // namespace heli
namespace planeflight {
bool Owns(const uint8_t *) { return false; }
void Frame(uint32_t) {}
}
namespace flightmodel { void Frame(uint32_t) {} }
namespace airportjets { void Frame(uint32_t) {} }
namespace airportheli { void Frame(uint32_t) {} }
namespace jetwash { void Frame(uint32_t) {} }
namespace doorgrab
{
void Update() {}
} // namespace doorgrab
namespace watch
{
void Frame() {}
} // namespace watch
namespace phone
{
void AfterWorld() {}
} // namespace phone
namespace sniper
{
bool TakeLeftView(CVector &) { return false; }
bool Wanted() { return false; }
bool Active() { return false; }
void Frame(float) {}
const scope::Output &Now()
{
	static scope::Output s{};
	return s;
}
} // namespace sniper
namespace fpgun
{
void CancelFrame() {}
} // namespace fpgun
namespace controls
{
bool LookFrozen() { return false; }
bool LookTurn(float &, float &) { return false; }
} // namespace controls
namespace seateye
{
float Lift(const uint8_t *, Vec3, bool, float) { return 0; }
} // namespace seateye
static uint8_t ped[0x900]{}, attacker[0x900]{}, cam[0x1000]{}, clump[8]{};
static uint32_t now = 100;
static float step = 1;
static uint8_t *Player() { return ped; }
static int generation = 17;
static int PedRef(uint8_t *) { return generation; }
static void NativeFrame(uint8_t *) {}
static bool ignored = false, armourHit = false;
static bool NativeDamage(uint8_t *p, uint8_t *, int32_t, float amount, int32_t, uint8_t)
{
	if (!ignored)
		*reinterpret_cast<float *>(p + (armourHit ? 0x450 : 0x44c)) -= amount;
	return false;
}
static int calls, vertices, maxAlpha;
static intptr_t states[40]{};
static void State(int s, void *v) { states[s] = reinterpret_cast<intptr_t>(v); }
static void Draw(int, RwIm2DVertex *, int n)
{
	++calls;
	vertices += n;
}
static void FloatVertex(RwIm2DVertex *, float) {}
static void UV(RwIm2DVertex *, float, float) {}
static void Color(RwIm2DVertex *, uint8_t, uint8_t, uint8_t, uint8_t a) { maxAlpha = std::max(maxAlpha, int(a)); }
static uint8_t vehicle[0x900]{}, priorIgnored[8]{};
static uint8_t *ignoredEntity = priorIgnored;
static CVector fixtureHead{0, 0, -1.2f};
static int worldCasts;
static bool overheadPlane;
static void NoMatrix(void *) {}
static void NoEntity(uint8_t *) {}
static void Component(void *, CVector *out, uint32_t) { *out = fixtureHead; }
static bool Ground(const CVector &a, const CVector &b, uint8_t *point, uint8_t *&hit, bool buildings, bool vehicles,
		   bool peds, bool objects, bool dummies, bool, bool, bool)
{
	++worldCasts;
	if (!buildings || vehicles || peds || !objects || !dummies || ignoredEntity != vehicle)
		std::abort();
	const float floor = overheadPlane && a.z > 1.0f && b.z < 1.0f ? 1.0f : 0.0f;
	if (a.z < floor || b.z >= floor)
		return false;
	const float t = (a.z - floor) / (a.z - b.z);
	*reinterpret_cast<CVector *>(point) = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, floor};
	*reinterpret_cast<CVector *>(point + 0x10) = {0, 0, 1};
	hit = priorIgnored;
	return true;
}
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
int main()
{
	game::api.FindPlayerPed = Player;
	game::api.theCamera = cam;
	game::api.timeMs = &now;
	game::api.timeStep = &step;
	game::api.RwRenderStateSet = State;
	game::api.RwIm2DRenderPrimitive = Draw;
	game::api.VertSetScreenX = FloatVertex;
	game::api.VertSetScreenY = FloatVertex;
	game::api.VertSetScreenZ = FloatVertex;
	game::api.VertSetCameraZ = FloatVertex;
	game::api.VertSetRecipCameraZ = FloatVertex;
	game::api.VertSetU = UV;
	game::api.VertSetV = UV;
	game::api.VertSetIntRGBA = Color;
	static float z = 1;
	game::api.spriteRecipNearClip = game::api.spriteNearScreenZ = game::api.spriteNearCamZ = &z;
	static uint8_t rs[32]{};
	*reinterpret_cast<int32_t *>(rs + 8) = 1600;
	*reinterpret_cast<int32_t *>(rs + 12) = 900;
	game::api.rsGlobal = rs;
	*reinterpret_cast<uint8_t **>(ped + 0x58) = clump;
	*reinterpret_cast<float *>(ped + 0x44c) = 100;
	*reinterpret_cast<float *>(ped + 0x450) = 50;
	game::SetPedState(ped, game::kPedIdle);
	*reinterpret_cast<CVector *>(cam + 0x10) = {0, 1, 0};
	*reinterpret_cast<CVector *>(cam + 0x20) = {0, 0, 1};
	cam[0x63] = 1;
	gConfig.firstPerson = true;
	gConfig.cameraShake = 0;
	camera::gPedRef = PedRef;
	camera::orig_Process = NativeFrame;
	player::orig_InflictDamage = NativeDamage;
	player::gDamage = true;
	camera::Hook_Process(cam); // establish lifetime before native damage
	CHECK(!player::Hook_InflictDamage(ped, attacker, 17, 10, 0, 0));
	camera::Hook_Process(cam);
	camera::DrawDamage();
	CHECK(calls > 0 && vertices >= 144 && maxAlpha >= 60);
	calls = vertices = maxAlpha = 0;
	camera::gImpact.Reset();
	armourHit = true;
	CHECK(!player::Hook_InflictDamage(ped, attacker, 17, 10, 0, 0));
	camera::Hook_Process(cam);
	camera::DrawDamage();
	CHECK(calls > 0 && maxAlpha >= 40);
	calls = 0;
	camera::gImpact.Reset();
	ignored = true;
	player::Hook_InflictDamage(ped, attacker, 17, 10, 0, 0);
	camera::Hook_Process(cam);
	camera::DrawDamage();
	CHECK(calls == 0);
	ignored = false;
	armourHit = false;
	player::Damage(ped, 43, 10);
	camera::Hook_Process(cam);
	camera::DrawDamage();
	CHECK(calls > 0);
	calls = 0;
	*reinterpret_cast<float *>(ped + 0x44c) = 0;
	camera::Hook_Process(cam);
	camera::DrawDamage();
	CHECK(calls == 0);
	// Actual first-person camera update preserves accepted feedback as it moves
	// the eye, and a new pool generation clears it before another draw.
	*reinterpret_cast<float *>(ped + 0x44c) = 100;
	static uint8_t firstPersonScene[16]{};
	camera::gScene = firstPersonScene;
	camera::gUpdateRW = NoMatrix;
	camera::gUpdateRwFrame = camera::gUpdateRpHAnim = NoEntity;
	camera::gComponentPosition = Component;
	game::api.GetAssociation = [](uint8_t *, uint32_t) -> uint8_t * { return nullptr; };
	*reinterpret_cast<CVector *>(ped + 0x08) = {1, 0, 0};
	*reinterpret_cast<CVector *>(ped + 0x18) = {0, 1, 0};
	*reinterpret_cast<CVector *>(ped + 0x28) = {0, 0, 1};
	*reinterpret_cast<uint16_t *>(game::ActiveCam() + 0xc) = 4;
	*reinterpret_cast<float *>(game::ActiveCam() + 0xbc) = -1.57079633f;
	camera::gFirstPerson = true;
	fixtureHead = {0, 0, 1.7f};
	player::Hook_InflictDamage(ped, attacker, 17, 10, 0, 0);
	camera::Hook_Process(cam);
	calls = maxAlpha = 0;
	camera::DrawDamage();
	CHECK(camera::FirstPersonOnFoot() && calls > 0 && maxAlpha >= 60);
	CHECK(states[game::kStateZTest] == 0 && states[game::kStateZWrite] == 0 &&
	      states[game::kStateVertexAlpha] == 1);
	CHECK(states[game::kStateSrcBlend] == game::kBlendSrcAlpha &&
	      states[game::kStateDestBlend] == game::kBlendInvSrcAlpha);
	const int firstAlpha = maxAlpha;
	step = 5;
	camera::Hook_Process(cam);
	maxAlpha = 0;
	camera::DrawDamage();
	CHECK(maxAlpha > 0 && maxAlpha < firstAlpha);
	++generation;
	camera::Hook_Process(cam);
	calls = 0;
	camera::DrawDamage();
	CHECK(calls == 0);
	camera::gFirstPerson = false;
	// Real native car-eye adapter with an inverted roof resting on terrain.
	// Both the old eye and its origin+0.5 anchor are below the upward-facing
	// ground.
	static uint8_t scene[16]{};
	camera::gScene = scene;
	game::api.ProcessLineOfSight = Ground;
	game::api.ignoreEntity = &ignoredEntity;
	camera::gUpdateRW = NoMatrix;
	camera::gUpdateRwFrame = camera::gUpdateRpHAnim = NoEntity;
	camera::gComponentPosition = Component;
	*reinterpret_cast<CVector *>(vehicle + 0x08) = {1, 0, 0};
	*reinterpret_cast<CVector *>(vehicle + 0x18) = {0, -1, 0};
	*reinterpret_cast<CVector *>(vehicle + 0x28) = {0, 0, -1};
	*reinterpret_cast<CVector *>(vehicle + 0x38) = {0, 0, -.6f};
	fixtureHead = {0, 0, -1.2f};
	camera::gHeadReady = false;
	camera::InVehicle(ped, vehicle, .02f);
	CHECK(camera::Row(0x30).z >= .16f && camera::Row(0x30).z < .3f);
	CHECK(ignoredEntity == priorIgnored);
	CHECK(worldCasts <= 8);
	// A low bridge above an inverted car is not a floor. Trace past it to the
	// terrain underneath rather than teleporting the eye onto the bridge deck.
	overheadPlane = true;
	worldCasts = 0;
	camera::gHeadReady = false;
	camera::InVehicle(ped, vehicle, .02f);
	CHECK(camera::Row(0x30).z >= .16f && camera::Row(0x30).z < .3f);
	CHECK(ignoredEntity == priorIgnored && worldCasts <= 16);
	overheadPlane = false;
	// Upright seat must retain its natural height and window framing.
	*reinterpret_cast<CVector *>(vehicle + 0x28) = {0, 0, 1};
	*reinterpret_cast<CVector *>(vehicle + 0x38) = {0, 0, .6f};
	fixtureHead = {0, 0, 1.2f};
	camera::gHeadReady = false;
	camera::InVehicle(ped, vehicle, .02f);
	CHECK(std::fabs(camera::Row(0x30).z - 1.248f) < 1e-5f);
	CHECK(ignoredEntity == priorIgnored);
	// Knocked down in first person (#25, fully attached): the camera is his
	// ragdoll head's view; up again, it eases back to his look, no snap.
	{
		camera::gFirstPerson = true;
		*reinterpret_cast<float *>(ped + 0x44c) = 100;
		camera::gOnFoot = true;
		*reinterpret_cast<uint8_t *>(ped + 0x4c0) = 0;
		camera::OnFoot(ped, .02f);
		const CVector look = camera::Row(0x10);
		reactions::gStubDown = true;
		reactions::gStubEye = {3, 4, .4f};
		reactions::gStubForward = {0, 0, -1}; // his face in the road
		reactions::gStubUp = {0, 1, 0};
		reactions::gStubRight = {-1, 0, 0};
		camera::OnFoot(ped, .02f);
		CHECK(camera::Row(0x30).x == 3 && camera::Row(0x30).y == 4 && camera::Row(0x30).z == .4f);
		CHECK(camera::Row(0x10).z == -1 && camera::Row(0x20).y == 1);
		const auto angle = [](const CVector &a, const CVector &b) {
			return std::acos(std::clamp(a.x * b.x + a.y * b.y + a.z * b.z, -1.0f, 1.0f));
		};
		reactions::gStubDown = false;
		camera::OnFoot(ped, .02f);
		const CVector first = camera::Row(0x10);
		CHECK(angle(first, {0, 0, -1}) > 1e-3f && angle(first, look) > .5f); // on its way, not snapped
		for (int frame = 0; frame < 40; ++frame)
			camera::OnFoot(ped, .02f);
		CHECK(angle(camera::Row(0x10), look) < .01f);
		// Down, then out of first person on foot (third person, a car, a respawn):
		// back on foot later, the view is his look at once, no stale ease-back.
		reactions::gStubDown = true;
		camera::OnFoot(ped, .02f);
		reactions::gStubDown = false;
		gConfig.firstPerson = false;
		camera::Hook_Process(cam);
		gConfig.firstPerson = true;
		camera::OnFoot(ped, .02f);
		CHECK(angle(camera::Row(0x10), look) < .01f);
	}
	// #38: the view he's in goes with him into a vehicle. First person in: the seat
	// view; third person in: a third-person car view, even if his last drive
	// was in the seat view (the game keeps the car zoom).
	{
		int32_t &carZoom = *reinterpret_cast<int32_t *>(cam + camera::kCarZoom);
		uint8_t car[16];
		camera::gWasInVehicle = false;
		camera::gFirstPerson = true;
		carZoom = 3;
		camera::Vehicles(car); // in, first person
		CHECK(carZoom == 0);
		camera::Vehicles(nullptr); // out in the seat view: first person on foot
		CHECK(camera::gFirstPerson);
		camera::gFirstPerson = false; // third person on foot
		camera::Vehicles(car);
		CHECK(carZoom == 3); // the third-person view he last drove in
		carZoom = 1;
		camera::Vehicles(car);
		camera::Vehicles(nullptr);
		CHECK(!camera::gFirstPerson); // out of a third-person view: third person
		camera::gFirstPerson = true;
		camera::Vehicles(car);
		CHECK(carZoom == 0);
		camera::Vehicles(nullptr);
		camera::gFirstPerson = false;
		camera::Vehicles(car);
		CHECK(carZoom == 1); // not the seat view his last drive ended in
		// Mid-drive: the far view, then the seat view; out (first person), third
		// person on foot, back in: the far view he drove in.
		carZoom = 3;
		camera::Vehicles(car);
		carZoom = 0;
		camera::Vehicles(car);
		camera::Vehicles(nullptr);
		CHECK(camera::gFirstPerson);
		camera::gFirstPerson = false;
		camera::Vehicles(car);
		CHECK(carZoom == 3);
	}
	if (failures)
		return 1;
	std::puts("all accepted-damage camera-frame Im2D overlay tests passed");
	return 0;
}
