// Unit tests for the viewmodel's animations (draw, holster, fire, empty, reload, sprint): make test
#include "vmanim.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

constexpr int kColt = 17, kPython = 18, kUzi = 23;
constexpr float kStep = 1.0f / 60.0f;

static AnimInput Holding(int weapon, int clip = 7)
{
	AnimInput in{};
	in.weapon = weapon;
	in.covered = true;
	in.clip = clip;
	in.dt = kStep;
	return in;
}

// Runs `seconds` of frames; returns the last output.
static AnimOutput Run(Anim &anim, AnimInput in, float seconds)
{
	AnimOutput out{};
	for (float t = 0.0f; t < seconds - 1e-4f; t += in.dt)
		out = anim.Update(in);
	return out;
}

static void TestDrawRises()
{
	Anim anim;
	const AnimOutput first = anim.Update(Holding(kColt));
	CHECK(first.visible && first.weapon == kColt);
	CHECK(std::fabs(first.offset.z + 0.25f) < 0.02f && first.pitch < -30.0f);
	const AnimOutput half = Run(anim, Holding(kColt), 0.15f);
	CHECK(half.offset.z > -0.25f && half.offset.z < 0.0f);
	const AnimOutput done = Run(anim, Holding(kColt), 0.25f);
	CHECK(std::fabs(done.offset.z) < 1e-4f && std::fabs(done.pitch) < 1e-3f);
}

static void TestSwitchHolstersFirst()
{
	Anim anim;
	Run(anim, Holding(kColt), 1.0f);
	const AnimOutput early = Run(anim, Holding(kPython), 0.1f);
	CHECK(early.weapon == kColt && early.offset.z < -0.01f); // the Colt going down
	const AnimOutput late = Run(anim, Holding(kPython), 0.15f);
	CHECK(late.weapon == kPython && late.offset.z < -0.1f); // the Python coming up
	const AnimOutput up = Run(anim, Holding(kPython), 0.4f);
	CHECK(up.weapon == kPython && std::fabs(up.offset.z) < 1e-4f);
}

static void TestShotKicksAndSettles()
{
	Anim anim;
	Run(anim, Holding(kColt), 1.0f);
	AnimInput shot = Holding(kColt, 6);
	shot.shot = true;
	const AnimOutput fired = anim.Update(shot);
	CHECK(fired.flash > 0.99f);
	const float kick = -fired.offset.y;
	CHECK(kick > 0.006f && kick < 0.012f && fired.pitch > 1.0f && fired.pitch < 2.0f); // a small kick: his body takes most (the camera's jolt)
	float peak = fired.slideBack;
	AnimOutput out = fired;
	for (float t = kStep; t < 0.03f; t += kStep) {
		out = anim.Update(Holding(kColt, 6));
		peak = std::fmax(peak, out.slideBack);
	}
	CHECK(std::fabs(peak - 0.022f) < 0.002f);
	out = Run(anim, Holding(kColt, 6), 0.06f); // 0.09 s after the shot
	CHECK(out.slideBack < 1e-4f && out.flash < 1e-4f);
	out = Run(anim, Holding(kColt, 6), 0.16f); // 0.25 s
	CHECK(-out.offset.y < 0.05f * kick);
}

static void TestEmptyHoldsTheSlide()
{
	Anim anim;
	Run(anim, Holding(kColt, 1), 1.0f);
	AnimInput shot = Holding(kColt, 0);
	shot.shot = true;
	anim.Update(shot);
	const AnimOutput out = Run(anim, Holding(kColt, 0), 0.5f);
	CHECK(std::fabs(out.slideBack - 0.022f) < 1e-4f);
}

static void TestReloadPhases()
{
	Anim anim;
	Run(anim, Holding(kColt, 0), 1.0f);
	auto at = [&](float progress) {
		AnimInput in = Holding(kColt, 0);
		in.reloading = progress < 1.0f;
		in.reloadProgress = progress;
		return anim.Update(in);
	};
	const AnimOutput start = at(0.0f);
	CHECK(start.magazineShown && start.magazineDrop < 1e-4f);
	const AnimOutput tilted = at(0.15f);
	CHECK(tilted.roll > 20.0f);
	const AnimOutput dropping = at(0.2f);
	CHECK(dropping.magazineShown && dropping.magazineDrop > 0.0f && dropping.magazineDrop < 0.12f);
	CHECK(!at(0.4f).magazineShown);
	const AnimOutput seated = at(0.7f);
	CHECK(seated.magazineShown && seated.magazineDrop < 1e-4f && std::fabs(seated.slideBack - 0.022f) < 1e-4f);
	const AnimOutput release = at(0.8f);
	CHECK(release.slideBack > 0.0f && release.slideBack < 0.022f);
	AnimInput done = Holding(kColt, 7);
	const AnimOutput rest = anim.Update(done);
	CHECK(rest.slideBack < 1e-4f && std::fabs(rest.roll) < 1e-3f && rest.magazineShown && rest.magazineDrop < 1e-4f);
}

static void TestUncoveredGunFallsBack()
{
	Anim anim;
	Run(anim, Holding(kColt), 1.0f);
	AnimInput uzi = Holding(kUzi);
	uzi.covered = false;
	CHECK(!anim.Update(uzi).visible);
	// Back to the Colt: drawn from below again.
	const AnimOutput back = anim.Update(Holding(kColt));
	CHECK(back.visible && back.offset.z < -0.2f);
}

// Sprinting, each kind of gun is carried as a runner carries it (at a point
// in the stride where its swing is nought), staying in view: a pistol or SMG
// up by his ear, muzzle near straight up; a long gun up across his chest
// canted hard in; a heavy one at the hip, a little across.
// The carry is his arms' (they follow it); the gun's own moves are left alone.
static AnimInput Sprinting(int weapon, Carry, float phase)
{
	AnimInput in = Holding(weapon);
	in.sprinting = true;
	in.stepSpeed = 7.0f;
	in.stepPhase = phase;
	return in;
}

static void TestSprintCarriesByKind()
{
	struct Want {
		int weapon;
		Carry kind;
		Vec3 offset;
		float pitch, roll, yaw;
	};
	const Want wants[] = {{kColt, Carry::kOneHanded, {0.09f, 0.0f, 0.10f}, 80.0f, 15.0f, -10.0f},
	                      {23, Carry::kOneHanded, {0.09f, 0.0f, 0.10f}, 80.0f, 15.0f, -10.0f},
	                      {26, Carry::kTwoHanded, {-0.06f, -0.02f, 0.03f}, 5.0f, -40.0f, 35.0f},
	                      {33, Carry::kHeavy, {-0.03f, 0.0f, 0.0f}, -5.0f, -12.0f, 15.0f}};
	for (const Want &w : wants) {
		Anim anim;
		Run(anim, Holding(w.weapon), 1.0f);
		const AnimOutput out = Run(anim, Sprinting(w.weapon, w.kind, 0.0f), 1.0f);
		CHECK(Length(Sub(out.carry.offset, w.offset)) < 0.002f);
		CHECK(std::fabs(out.carry.pitch - w.pitch) < 0.5f && std::fabs(out.carry.roll - w.roll) < 0.5f &&
		      std::fabs(out.carry.yaw - w.yaw) < 0.5f);
		CHECK(std::fabs(out.pitch) < 0.01f && std::fabs(out.roll) < 0.01f && Length(out.offset) < 1e-4f);
	}
}

// Through a sprinting stride the carry swings: a pistol pumping with his arm,
// a long gun swaying side to side.
static void TestSprintSwingsWithTheStride()
{
	auto range = [](int weapon, Carry kind, auto value) {
		Anim anim;
		Run(anim, Sprinting(weapon, kind, 0.0f), 1.0f);
		float lo = 1e9f, hi = -1e9f;
		for (int i = 0; i < 40; i++) {
			const float v = value(anim.Update(Sprinting(weapon, kind, i / 20.0f)));
			lo = std::min(lo, v);
			hi = std::max(hi, v);
		}
		return hi - lo;
	};
	// A pistol or SMG held up by his ear only bobs with him (it doesn't swing out front).
	for (int w : {kColt, 23}) {
		CHECK(range(w, Carry::kOneHanded, [](const AnimOutput &o) { return o.carry.offset.y; }) < 0.03f);
		CHECK(range(w, Carry::kOneHanded, [](const AnimOutput &o) { return o.carry.offset.z; }) > 0.01f);
	}
	CHECK(range(26, Carry::kTwoHanded, [](const AnimOutput &o) { return o.carry.offset.x; }) > 0.035f);
	CHECK(range(26, Carry::kTwoHanded, [](const AnimOutput &o) { return o.carry.yaw; }) > 7.0f);
	CHECK(range(33, Carry::kHeavy, [](const AnimOutput &o) { return o.carry.offset.x; }) < 0.025f);
}

// Running (not sprinting) the gun bobs enough to see, and not at all aimed;
// sprinting aimed, it isn't carried.
static void TestBobVisibleRunning()
{
	for (float aimed : {0.0f, 1.0f}) {
		Anim anim;
		Run(anim, Holding(26), 1.0f);
		float lo = 1e9f, hi = -1e9f, turned = 0.0f;
		for (int i = 0; i < 40; i++) {
			AnimInput in = Holding(26);
			in.stepSpeed = 4.5f;
			in.stepPhase = i / 20.0f;
			in.aimed = aimed;
			const AnimOutput out = anim.Update(in);
			lo = std::min(lo, out.offset.x);
			hi = std::max(hi, out.offset.x);
			turned = std::max({turned, std::fabs(out.pitch), std::fabs(out.roll), std::fabs(out.yaw)});
		}
		if (aimed == 0.0f)
			CHECK(hi - lo > 0.02f && turned > 1.0f);
		else
			CHECK(hi - lo < 1e-5f && turned < 1e-3f);
	}
	Anim anim;
	AnimInput in = Sprinting(26, Carry::kTwoHanded, 0.5f);
	in.aimed = 1.0f;
	const AnimOutput out = Run(anim, in, 1.0f);
	CHECK(Length(out.carry.offset) < 1e-3f && std::fabs(out.carry.yaw) < 0.05f && Length(out.offset) < 1e-4f);
}

// Stopping a sprint (to fire, say), the carry eases out over a few frames, never at once.
static void TestCarryEasesOut()
{
	Anim anim;
	const AnimOutput full = Run(anim, Sprinting(26, Carry::kTwoHanded, 0.0f), 1.0f);
	AnimInput stop = Holding(26);
	const AnimOutput next = anim.Update(stop);
	CHECK(next.carry.yaw > 0.6f * full.carry.yaw && next.carry.yaw < full.carry.yaw);
	CHECK(std::fabs(Run(anim, stop, 1.0f).carry.yaw) < 0.01f * full.carry.yaw);
}

// Switching guns sprinting, the gun on screen keeps its own carry as it's
// holstered (easing out), and the new one comes up into its own.
static void TestCarryFollowsTheGunOnScreen()
{
	Anim anim;
	Run(anim, Sprinting(kColt, Carry::kOneHanded, 0.0f), 1.0f);
	const AnimOutput holstering = anim.Update(Sprinting(26, Carry::kTwoHanded, 0.0f));
	CHECK(holstering.weapon == kColt && holstering.carry.pitch > 40.0f); // the Colt's (easing out), muzzle up by his ear
	const AnimOutput drawn = Run(anim, Sprinting(26, Carry::kTwoHanded, 0.0f), 1.5f);
	CHECK(drawn.weapon == 26 && std::fabs(drawn.carry.roll + 40.0f) < 0.5f);
}

// Aiming out of a sprint, the carry is gone by the time the sights are up.
static void TestCarryGoneAimed()
{
	Anim anim;
	Run(anim, Sprinting(26, Carry::kTwoHanded, 0.0f), 1.0f);
	AnimOutput out{};
	for (int i = 1; i <= 10; i++) { // the sights up in a sixth of a second
		AnimInput in = Sprinting(26, Carry::kTwoHanded, 0.0f);
		in.aimed = std::min(1.0f, i / 10.0f);
		out = anim.Update(in);
	}
	CHECK(std::fabs(out.carry.yaw) < 0.01f && std::fabs(out.carry.roll) < 0.01f && Length(out.carry.offset) < 1e-4f);
}

// A reload eases the carry out (the hands are busy), even sprinting.
static void TestCarryOutForAReload()
{
	Anim anim;
	const AnimOutput full = Run(anim, Sprinting(26, Carry::kTwoHanded, 0.0f), 1.0f);
	AnimInput in = Sprinting(26, Carry::kTwoHanded, 0.0f);
	in.reloading = true;
	in.reloadProgress = 0.1f;
	const AnimOutput reloading = Run(anim, in, 0.2f);
	CHECK(std::fabs(reloading.carry.yaw) < 0.05f * full.carry.yaw);
}

// Out of a sprint it's quick (a fifth of a second), so a shot leaves from a gun that's up.
static void TestCarryOutQuickly()
{
	Anim anim;
	const AnimOutput full = Run(anim, Sprinting(26, Carry::kTwoHanded, 0.0f), 1.0f);
	CHECK(std::fabs(Run(anim, Holding(26), 0.2f).carry.yaw) < 0.05f * full.carry.yaw);
}

// The bob is about where the gun's held: over a stride it averages out, not low and pointing down.
static void TestBobAveragesOut()
{
	float pitch = 0.0f, z = 0.0f;
	for (int i = 0; i < 200; i++) {
		const GunSway s = GunBob(i / 100.0f, 4.5f);
		pitch += s.pitch / 200.0f;
		z += s.offset.z / 200.0f;
	}
	CHECK(std::fabs(pitch) < 0.05f && std::fabs(z) < 0.0005f);
}

// Which guns are carried how (as Vice City holds them).
static void TestCarryForEachGun()
{
	for (int w : {17, 18, 22, 23, 24})
		CHECK(CarryFor(w) == Carry::kOneHanded);
	for (int w : {19, 20, 21, 25, 26, 27, 28, 29})
		CHECK(CarryFor(w) == Carry::kTwoHanded);
	for (int w : {30, 31, 32, 33})
		CHECK(CarryFor(w) == Carry::kHeavy);
}

static AnimOutput Fire(Anim &anim, int weapon)
{
	AnimInput shot = Holding(weapon, 5);
	shot.shot = true;
	return anim.Update(shot);
}

static void TestPumpCyclesAfterAShot()
{
	Anim anim;
	Run(anim, Holding(19), 1.0f); // the shotgun
	Fire(anim, 19);
	float most = 0.0f;
	AnimOutput out{};
	for (float t = kStep; t < 0.15f; t += kStep) {
		out = anim.Update(Holding(19, 5));
		most = std::fmax(most, out.pumpBack);
	}
	CHECK(std::fabs(most - 0.07f) < 0.005f);
	out = Run(anim, Holding(19, 5), 0.2f); // 0.35 s after the shot
	CHECK(out.pumpBack < 1e-4f);
}

static void TestCylinderTurnsASixth()
{
	Anim anim;
	Run(anim, Holding(18), 1.0f); // the Python
	const float before = Run(anim, Holding(18), 0.1f).cylinderTurn;
	Fire(anim, 18);
	const float after = Run(anim, Holding(18, 5), 0.3f).cylinderTurn;
	CHECK(std::fabs(std::fmod(after - before + 360.0f, 360.0f) - 60.0f) < 0.5f);
}

static void TestBarrelsSpinWhileFiring()
{
	Anim anim;
	Run(anim, Holding(33), 1.0f); // the minigun
	const float still = Run(anim, Holding(33), 0.2f).barrelsTurn;
	CHECK(std::fabs(Run(anim, Holding(33), 0.2f).barrelsTurn - still) < 1e-3f);
	AnimInput firing = Holding(33);
	firing.triggerHeld = true;
	const float a = Run(anim, firing, 0.5f).barrelsTurn, b = Run(anim, firing, 0.1f).barrelsTurn;
	CHECK(std::fabs(b - a) > 20.0f); // spinning
}

// A part the animation doesn't know stays where it's modelled; one it does moves as it should.
static void TestUnknownPartStaysPut()
{
	AnimOutput out{};
	out.slideBack = 0.02f;
	out.cylinderTurn = 90.0f;
	Part odd{};
	strcpy(odd.name, "sling");
	odd.axis = {0, -1, 0};
	const Frame still = PartMove(odd, out, 1.0f);
	CHECK(Length(Sub(Apply(still, {0.1f, 0.2f, 0.3f}), {0.1f, 0.2f, 0.3f})) < 1e-6f);
	Part slide{};
	strcpy(slide.name, "slide");
	slide.axis = {0, -1, 0};
	CHECK(Length(Sub(Apply(PartMove(slide, out, 2.0f), {0, 0, 0}), {0, -0.04f, 0})) < 1e-6f);
	Part cylinder{};
	strcpy(cylinder.name, "cylinder");
	cylinder.axis = {0, 1, 0};
	cylinder.pivot = {0, 0, 0.1f};
	const Vec3 turned = Apply(PartMove(cylinder, out, 1.0f), {0, 0, 0.2f}); // a point over the pivot
	CHECK(Length(Sub(turned, {0.1f, 0, 0.1f})) < 1e-5f || Length(Sub(turned, {-0.1f, 0, 0.1f})) < 1e-5f);
}

// It can fire once it's up: not while it's drawn, holstered, or another's coming.
static void TestReadyOnlyWhenUp()
{
	Anim anim;
	CHECK(!anim.Update(Holding(kColt)).ready); // just drawn
	CHECK(!Run(anim, Holding(kColt), 0.2f).ready);
	CHECK(Run(anim, Holding(kColt), 0.3f).ready);
	CHECK(!Run(anim, Holding(kPython), 0.1f).ready); // switching
	CHECK(Run(anim, Holding(kPython), 0.6f).ready);
}

// A two-handed gun's left hand leaves the handguard for the magazine, rides it
// out and away and the new one back in, then goes back to the handguard.
static void TestReloadHandTwoHanded()
{
	const ReloadHand still = HandForReload(false, 0.0f, true);
	CHECK(still.shown && still.atMagazine == 0.0f && still.away == 0.0f);
	CHECK(HandForReload(true, 0.05f, true).atMagazine == 0.0f);
	CHECK(HandForReload(true, 0.3f, true).atMagazine == 1.0f);
	CHECK(HandForReload(true, 0.55f, true).atMagazine == 1.0f);
	CHECK(HandForReload(true, 0.9f, true).atMagazine == 0.0f);
	CHECK(HandForReload(true, 0.4f, true).away == 0.0f && HandForReload(true, 0.4f, true).shown);
}

// A one-handed gun's left hand only comes for the new magazine, from out of view, and goes.
static void TestReloadHandOneHanded()
{
	CHECK(!HandForReload(false, 0.0f, false).shown);
	CHECK(!HandForReload(true, 0.2f, false).shown);
	const ReloadHand in = HandForReload(true, 0.55f, false);
	CHECK(in.shown && in.atMagazine == 1.0f && in.away == 0.0f);
	CHECK(HandForReload(true, 0.75f, false).away > 0.0f);
	CHECK(!HandForReload(true, 0.9f, false).shown);
}

// The magazine travels: out and away out of sight, the new one up from there and in.
static void TestMagazineTravels()
{
	Anim anim;
	Run(anim, Holding(kColt, 0), 1.0f);
	auto at = [&](float progress) {
		AnimInput in = Holding(kColt, 0);
		in.reloading = true;
		in.reloadProgress = progress;
		return anim.Update(in);
	};
	CHECK(at(0.35f).magazineShown && at(0.35f).magazineDrop > 0.12f); // carried away
	CHECK(!at(0.4f).magazineShown);                                   // swapped, out of sight
	const AnimOutput back = at(0.45f);
	CHECK(back.magazineShown && back.magazineDrop > 0.12f);           // the new one coming up
	CHECK(at(0.7f).magazineDrop < 1e-4f);
}

// The gun's figure-eight with his steps: none standing, larger running.
static float GunMost(float speed)
{
	float most = 0.0f;
	for (int i = 0; i < 200; i++) {
		const GunSway s = GunBob(i / 100.0f, speed);
		most = std::max({most, std::fabs(s.offset.x), std::fabs(s.offset.z), std::fabs(s.pitch) * 0.01f, std::fabs(s.roll) * 0.01f});
	}
	return most;
}

static void TestGunBobStillIsZero() { CHECK(GunMost(0.0f) == 0.0f); }

static void TestGunBobGrowsWithSpeed()
{
	const float walk = GunMost(1.8f), run = GunMost(4.5f), sprint = GunMost(7.0f);
	CHECK(walk > 0.0f && run > 2.0f * walk && sprint > 1.5f * run && sprint < 0.06f);
	// Side to side once a stride, down at each step: a figure-eight.
	CHECK(GunBob(0.5f, 4.5f).offset.x > 0.0f && GunBob(1.5f, 4.5f).offset.x < 0.0f);
	CHECK(GunBob(0.5f, 4.5f).offset.z < GunBob(0.0f, 4.5f).offset.z && GunBob(1.5f, 4.5f).offset.z < GunBob(1.0f, 4.5f).offset.z);
}

int main()
{
	TestGunBobStillIsZero();
	TestGunBobGrowsWithSpeed();
	TestReloadHandTwoHanded();
	TestReloadHandOneHanded();
	TestMagazineTravels();
	TestReadyOnlyWhenUp();
	TestPumpCyclesAfterAShot();
	TestCylinderTurnsASixth();
	TestBarrelsSpinWhileFiring();
	TestUnknownPartStaysPut();
	TestDrawRises();
	TestSwitchHolstersFirst();
	TestShotKicksAndSettles();
	TestEmptyHoldsTheSlide();
	TestReloadPhases();
	TestUncoveredGunFallsBack();
	TestSprintCarriesByKind();
	TestSprintSwingsWithTheStride();
	TestBobVisibleRunning();
	TestCarryEasesOut();
	TestCarryForEachGun();
	TestCarryFollowsTheGunOnScreen();
	TestCarryGoneAimed();
	TestCarryOutForAReload();
	TestCarryOutQuickly();
	TestBobAveragesOut();
	if (gFailures) {
		printf("%d vmanim test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmanim tests passed\n");
	return 0;
}
