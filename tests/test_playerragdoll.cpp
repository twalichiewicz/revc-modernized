// Tommy's ragdoll on a native knockdown (#25), through the actual SetFall,
// control, collision, frame and render hooks: every CPed::SetFall on him
// (cars, melee, explosions, trips...) articulates him, carrying the game's
// own velocity, held in its fall until physical recovery, then its get-up;
// ordinary hits keep his controls; wasted keeps the ragdoll; the respawn
// releases it. Never in a cutscene, a vehicle or when his own gates say no.
#define main original_reaction_tests
#include "test_reactionhook.cpp"
#undef main

namespace {
int failures;
void Check(bool good, const char *message)
{
	if (!good) { std::printf("FAIL: %s\n", message); ++failures; }
}
uint8_t gTommy[0x8d0], gGunman[0x8d0];
int PoolRef(uint8_t *ped) { return ped == gTommy ? 40 << 8 : ped == gGunman ? 41 << 8 : -1; }
uint8_t *PoolPed(int handle) { return handle == 40 << 8 ? gTommy : handle == 41 << 8 ? gGunman : nullptr; }
uint8_t *Tommy() { return gTommy; }
int nativeFalls;
bool nativeDeclines; // CPed::SetFall's own refusals (dying, head under water...)
void NativeSetFall(uint8_t *ped, int, int, uint8_t)
{
	++nativeFalls;
	if (!nativeDeclines)
		game::SetPedState(ped, game::kPedFall);
}
void NativeControl(uint8_t *) {}
CVector &Root(uint8_t *ped) { return *reinterpret_cast<CVector *>(ped + 0x38); }
float &Life(uint8_t *ped) { return *reinterpret_cast<float *>(ped + 0x44c); }
const reactions::Person *Body() { return reactions::Find(gTommy); }

void Setup()
{
	gNow += 1000;
	reactions::gLastFrame = gNow;
	reactions::gPeople.clear();
	reactions::gContacts = {};
	std::memset(gTommy, 0, sizeof gTommy);
	std::memset(gGunman, 0, sizeof gGunman);
	for (uint8_t *ped : {gTommy, gGunman}) {
		game::EntityFlags(ped) = 3 | game::kEntityUsesCollision;
		game::SetPedState(ped, game::kPedIdle);
		*reinterpret_cast<uint8_t **>(ped + 0x58) = gContactClump;
		Life(ped) = 100;
	}
	Root(gTommy) = {-6, 0, 1.04f};
	gTommy[0x19a] = 2; // the script's CREATE_PLAYER marks him MISSION_CHAR (0x10008d8a4)
	*reinterpret_cast<CVector *>(gTommy + 0x08) = {1, 0, 0}; // facing +y
	*reinterpret_cast<CVector *>(gTommy + 0x18) = {0, 1, 0};
	*reinterpret_cast<CVector *>(gTommy + 0x28) = {0, 0, 1};
	Root(gGunman) = {-6, -8, 1.04f};
	game::api.FindPlayerPed = Tommy;
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
	reactions::orig_SetFall = NativeSetFall;
	reactions::orig_Control = NativeControl;
	reactions::orig_WorldCollision = NativeControl;
	playerreact::canRagdoll = true;
	playerreact::canStayDown = true;
	nativeDeclines = false;
	shotimpact::available = false;
	nativeFalls = 0;
	Check(reactcontact::Install(), "player ragdoll fixture binds the real native contact sweep");
}
void Tick()
{
	gNow += 16;
	reactions::Hook_Control(gTommy);
	reactions::Hook_WorldCollision(gTommy);
	reactions::Frame();
	reactions::BeforePreRender(gTommy);
	reactions::AfterPreRender(gTommy);
}

void CarKnocksHimDown()
{
	Setup();
	// KillPedWithCar's knockdown: the game's velocity already on him, then SetFall.
	game::EntitySpeed(gTommy) = {0, .08f, 0}; // 4 m/s
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	const auto *p = Body();
	Check(nativeFalls == 1 && game::PedState(gTommy) == game::kPedFall, "the game's own knockdown still runs");
	Check(p && p->physical.State() == reactphysics::Mode::Falling && !p->physical.Dead(),
	      "a knockdown articulates Tommy, off his feet and alive");
	Check(p && reactions::Length(reactions::Sub(p->physical.Velocity(reactphysics::Pelvis), {0, 4, 0})) < 1e-3f,
	      "his ragdoll carries exactly the game's velocity");
	Check(reactions::PlayerDown(), "the rest of the mod sees him down");
	// Native SetFall again (its own held fall, or another source): one body, no double impulse.
	const uint32_t revision = p ? p->physical.Revision() : 0;
	game::EntitySpeed(gTommy) = {};
	reactions::Hook_SetFall(gTommy, -1, 27, 1);
	Check(reactions::gPeople.size() == 1 && Body() == p && p->physical.Revision() == revision,
	      "a second SetFall on him (its own held fall, another source) neither tracks nor knocks him again");
	bool owned = true, gotUp = false;
	const int getUps = gGetUps;
	for (int frame = 0; frame < 900 && Body(); ++frame) {
		Tick();
		if (Body() && Body()->physical.State() == reactphysics::Mode::Falling)
			owned &= reactions::OwnsRoot(Body()) && std::fabs(Root(gTommy).z) < 2;
		gotUp |= gGetUps > getUps;
	}
	Check(owned, "the ragdoll owns his root while he falls");
	Check(gotUp, "physical recovery starts the game's get-up");
	Check(!Body() && !reactions::PlayerDown(), "once up, he is the game's again");
	Check(Life(gTommy) == 100, "the ragdoll adds no damage of its own");
}

// Up again (the game's get-up, finished as PedGetupCB does) and his ragdoll let
// go, so the fixture's shared rig is back as the next case expects it.
void LetGo()
{
	for (int frame = 0, getUps = gGetUps; frame < 2000 && Body(); ++frame) {
		Tick();
		if (gGetUps != getUps) {
			game::SetPedState(gTommy, game::kPedIdle);
			gTommy[0x18c] &= ~0x20;
			getUps = gGetUps;
		}
	}
	Check(!Body(), "fixture: up again, his ragdoll let go");
}

// The game finishes its get-up (CPed::PedGetupCB, 0x10019fe58: his state
// restored, then bGetUpAnimStarted cleared) while his ragdoll is still
// recovering, as under fire, when every hit holds its recovery back. That's the
// end of the knockdown: no second get-up from the lying pose (nor one a car
// beside him blocks, leaving him in PED_GETUP standing), and his root and
// controls are the game's at once.
void TheGameGetsHimUp()
{
	Setup();
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	const int before = gGetUps;
	for (int frame = 0; frame < 900 && Body() && gGetUps == before; ++frame)
		Tick();
	const int falls = gFalls;
	Check(gGetUps == before + 1 && Body() && Body()->physical.State() == reactphysics::Mode::Recovering,
	      "fixture: his ragdoll recovering, the game's get-up started once");
	// PedGetupCB: RestorePreviousState, then bGetUpAnimStarted cleared.
	game::SetPedState(gTommy, game::kPedIdle);
	gTommy[0x18c] &= ~0x20;
	Tick();
	Check(gGetUps == before + 1, "a finished get-up isn't started again");
	Check(!reactions::PlayerDown() && !reactions::OwnsRoot(Body()), "his controls and root are the game's at once");
	for (int frame = 0; frame < 300 && Body(); ++frame)
		Tick();
	Check(gGetUps == before + 1 && game::PedState(gTommy) == game::kPedIdle, "nor later, as his ragdoll lets go");
	Check(!Body(), "his ragdoll lets go");
	Check(gFalls == falls, "and the hand-back doesn't knock him down again");

	// Under fire: every shot holds a ragdoll's recovery back, but the game has him
	// up. Shots through the hand-back neither knock him down nor stop it.
	Setup();
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	const int started = gGetUps;
	for (int frame = 0; frame < 900 && Body() && gGetUps == started; ++frame)
		Tick();
	game::SetPedState(gTommy, game::kPedIdle);
	gTommy[0x18c] &= ~0x20;
	const int fallsUnderFire = gFalls;
	bool controls = true;
	for (int frame = 0; frame < 300 && Body(); ++frame) {
		if (frame % 6 == 0)
			reactions::Hit(gTommy, gGunman, 22, 1);
		Tick();
		controls &= !reactions::PlayerDown();
	}
	Check(controls && gFalls == fallsUnderFire && gGetUps == started + 1,
	      "under fire, his controls stay his: no knockdown, no second get-up");
	Check(!Body(), "and his ragdoll still lets go");

	// Knocked down again while his ragdoll fades out: a whole new knockdown.
	Setup();
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	const int first = gGetUps;
	for (int frame = 0; frame < 900 && Body() && gGetUps == first; ++frame)
		Tick();
	game::SetPedState(gTommy, game::kPedIdle);
	gTommy[0x18c] &= ~0x20;
	for (int frame = 0; frame < 10; ++frame)
		Tick();
	Check(Body() && Body()->physical.Weight() < 1, "fixture: his ragdoll part faded");
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	Check(Body() && Body()->physical.Weight() == 1 && reactions::PlayerDown(),
	      "a knockdown during the fade is a whole one, his ragdoll back at full weight");
	LetGo();

	// Knocked down again while getting up: a whole new fall, not straight back up.
	Setup();
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	const int rising = gGetUps;
	for (int frame = 0; frame < 900 && Body() && gGetUps == rising; ++frame)
		Tick();
	Check(Body() && Body()->physical.State() == reactphysics::Mode::Recovering, "fixture: getting up");
	game::EntitySpeed(gTommy) = {0, .08f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	Check(Body() && Body()->physical.State() == reactphysics::Mode::Falling && reactions::OwnsRoot(Body()),
	      "a knockdown while getting up is a whole new fall");
	Tick();
	Check(gGetUps == rising + 1, "and he isn't sent straight back up");
	LetGo();
}

// First person (#25, fully attached): the view rides his ragdoll's head,
// as it looked when he went down (his facing), and rolls with the tumble.
void HeadView()
{
	Setup();
	game::EntitySpeed(gTommy) = {0, .1f, 0};
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	Vec3 eye{}, right{}, forward{}, up{};
	Check(reactions::PlayerHeadView(eye, right, forward, up), "down, his head gives the first-person view");
	Check(forward.y > .99f && up.z > .99f && right.x > .99f, "at the knockdown the view looks the way he faced");
	float turned = 0, offHead = 0;
	for (int frame = 0; frame < 45 && reactions::PlayerDown(); ++frame) {
		Tick();
		if (!reactions::PlayerHeadView(eye, right, forward, up))
			continue;
		turned = std::max({turned, 1 - forward.y, 1 - up.z});
		const Vec3 head = Body()->physical.Positions()[reactphysics::Head];
		offHead = std::max(offHead, reactions::Length(reactions::Sub(eye, head)));
		const float orthogonal = std::fabs(right.x * forward.x + right.y * forward.y + right.z * forward.z) +
		                         std::fabs(up.x * forward.x + up.y * forward.y + up.z * forward.z);
		Check(orthogonal < 1e-3f, "the view stays a proper frame through the tumble");
	}
	Check(turned > .3f, "the view rolls and pitches with his head as he falls");
	Check(offHead < .3f, "the eye stays in his head");
	for (int frame = 0; frame < 900 && Body(); ++frame) Tick();
	Check(!reactions::PlayerHeadView(eye, right, forward, up), "back on his feet, the view is his own again");
}

void GentleKnockdowns()
{
	// A trip or bump: no speed of its own, so the push comes from the side he was hit.
	const struct { int anim; float forward; } cases[] = {{27, 1}, {15, 1}, {25, -1}, {13, -1}};
	for (const auto &c : cases) {
		Setup();
		reactions::Hook_SetFall(gTommy, 3000, c.anim, 0);
		const auto *p = Body();
		Check(p && p->physical.State() == reactphysics::Mode::Falling, "a gentle knockdown still takes him off his feet");
		Check(p && p->physical.Velocity(reactphysics::Pelvis).y * c.forward > .5f,
		      "a gentle knockdown falls away from the side he was hit");
	}
}

void OrdinaryHitsKeepControl()
{
	Setup();
	Life(gTommy) = 80;
	reactions::Hit(gTommy, gGunman, 22, 1);
	Check(!Body() && !reactions::PlayerDown(), "an ordinary hit on Tommy keeps his controls (no knockdown, no ragdoll)");
	// Excluded: his own gates (scripted camera, swimming, climbing...), a cutscene, a vehicle, the setting.
	for (int kind = 0; kind < 4; ++kind) {
		Setup();
		bool cutscene = true;
		switch (kind) {
		case 0: playerreact::canRagdoll = false; break;
		case 1: reactions::gCutscene = &cutscene; break;
		case 2: gTommy[0x4c0] = 1; break;
		case 3: gConfig.npcReactions = false; break;
		}
		reactions::Hook_SetFall(gTommy, 3000, 25, 0);
		Check(nativeFalls == 1 && !Body(), "excluded knockdowns stay the game's own");
		reactions::gCutscene = nullptr;
	}
}

// Native SetFall declining (dying, head under water...): no ragdoll without the
// game's own fall behind it.
void NativeDeclines()
{
	Setup();
	nativeDeclines = true;
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	Check(nativeFalls == 1 && !Body(), "a knockdown the game itself declines doesn't ragdoll him");
}

// Swimming takes over mid-fall (knocked off a pier): the ragdoll lets go, and
// his root is the swim's, not lifted.
void IntoTheWater()
{
	Setup();
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	for (int frame = 0; frame < 20; ++frame) Tick();
	playerreact::canStayDown = false;
	const CVector before = Root(gTommy);
	Tick();
	Check(!Body() && !reactions::PlayerDown(), "swimming lets the ragdoll go");
	Check(std::fabs(Root(gTommy).z - before.z) < .2f, "the swim keeps his root (no lift)");
}

// Released alive mid-fall for any other reason (a cutscene starts): the game's
// fall and get-up poses expect a standing-height root, so it gets one.
void CutsceneMidFall()
{
	Setup();
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	for (int frame = 0; frame < 60; ++frame) Tick();
	Check(Body() && Root(gTommy).z < .6f, "precondition: down, his root at his pelvis");
	bool cutscene = true;
	reactions::gCutscene = &cutscene;
	reactions::Frame();
	reactions::gCutscene = nullptr;
	Check(!Body() && std::fabs(Root(gTommy).z - 1.04f) < .01f,
	      "released alive mid-fall, his root is at standing height above the ground");
}

void ShotWhileDown()
{
	Setup();
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	for (int frame = 0; frame < 10; ++frame) Tick();
	const uint32_t revision = Body() ? Body()->physical.Revision() : 0;
	Life(gTommy) = 70;
	reactions::Hit(gTommy, gGunman, 22, 1);
	Check(Body() && Body()->physical.Revision() != revision, "a shot while he's down moves his ragdoll");
}

void WastedThenRespawned()
{
	Setup();
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	Life(gTommy) = 0;
	game::SetPedState(gTommy, game::kPedDie);
	for (int frame = 0; frame < 60; ++frame) Tick();
	game::EntityFlags(gTommy) &= ~game::kEntityUsesCollision; // native SetDead
	game::SetPedState(gTommy, game::kPedDead);
	for (int frame = 0; frame < 120; ++frame) Tick();
	Check(Body() && Body()->physical.Dead(), "wasted, he keeps his ragdoll");
	// The game's restart: alive at the hospital, collision back, standing.
	Life(gTommy) = 100;
	game::SetPedState(gTommy, game::kPedIdle);
	game::EntityFlags(gTommy) |= game::kEntityUsesCollision;
	Root(gTommy) = {400, 300, 11.04f};
	Tick();
	Check(!Body() && !reactions::PlayerDown(), "the respawn releases the ragdoll");
	Check(Root(gTommy).x == 400 && Root(gTommy).y == 300 && Root(gTommy).z == 11.04f,
	      "the respawned Tommy stays where the game put him");
	// The same seen first by the frame callback rather than his control.
	Setup();
	reactions::Hook_SetFall(gTommy, 3000, 25, 0);
	for (int frame = 0; frame < 30; ++frame) Tick();
	Root(gTommy) = {400, 300, 11.04f}; // a script's teleport
	reactions::Frame();
	Check(!Body() && Root(gTommy).z == 11.04f, "a teleport seen first by the frame callback leaves him where the game put him");
}
} // namespace

int main()
{
	CarKnocksHimDown();
	TheGameGetsHimUp();
	HeadView();
	GentleKnockdowns();
	OrdinaryHitsKeepControl();
	NativeDeclines();
	IntoTheWater();
	CutsceneMidFall();
	ShotWhileDown();
	WastedThenRespawned();
	std::printf("playerragdoll: %d failures\n", failures);
	return failures ? 1 : 0;
}
