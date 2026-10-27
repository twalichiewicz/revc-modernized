// Unit tests for where his hands go on the gun each frame: make test
#include "vmhands.h"

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

static bool Near(Vec3 a, Vec3 b, float eps = 1e-4f) { return Length(Sub(a, b)) < eps; }
static bool Near(const Frame &a, const Frame &b, float eps = 1e-4f)
{
	return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.pos, b.pos, eps);
}

// A gun held in either hand, with a magazine that drops straight down (part 1).
static Gun TestGun(bool twoHanded, bool magazine)
{
	Gun gun;
	gun.hold = TurnAbout(Normal({1, 0, 0}), 0.4f, {0.01f, 0.02f, 0.03f});
	gun.twoHanded = twoHanded;
	gun.holdLeft = TurnAbout(Normal({0, 1, 1}), -0.6f, {-0.02f, 0.05f, 0.0f});
	gun.leftForearm = {-0.6f, -1.0f, -0.45f};
	Part body{}, mag{};
	strcpy(body.name, "body");
	strcpy(mag.name, magazine ? "magazine" : "slide");
	mag.axis = {0, 0, -1};
	gun.parts = {body, mag};
	for (float x : {-0.01f, 0.01f})
		for (float z : {-0.1f, 0.0f})
			gun.vertices.push_back({{x, 0.03f, z}, {0, 0, 1}, 0, 0, 1});
	return gun;
}

static Frame Framed() { return TurnAbout(Normal({0, 0, 1}), 0.2f, {0.15f, 0.4f, -0.2f}); }
static Frame Rolled(const Frame &f) { return Compose(TurnAbout(Normal({0, 1, 0}), 0.45f, f.pos), f); }

// His right arm keeps to the gun as framed: the reload's tilt turns his hand, not his shoulder up into view.
static void TestRightArmKeepsToTheFramedGun()
{
	const Gun gun = TestGun(false, true);
	const Frame framed = Framed(), moved = Rolled(framed);
	const HandPlace right = RightPlace(gun, framed, moved);
	CHECK(right.shown);
	CHECK(Near(right.hand, HandFor(gun, moved)));
	Vec3 fore, upper;
	ArmDirections(HandFor(gun, framed), ArmAngles{}, false, fore, upper);
	CHECK(Near(right.angles.forearm, fore) && Near(right.angles.upper, upper));
}

// Not reloading, a two-handed gun's left hand is on its hold (moving with its part: a pump).
static void TestLeftHandOnItsHold()
{
	const Gun gun = TestGun(true, true);
	const Frame framed = Framed(), moved = Rolled(framed);
	std::vector<Frame> parts = {Identity(), Identity()};
	parts[1].pos = {0, -0.05f, 0};
	const HandPlace left = LeftPlace(gun, framed, moved, parts, 1, ReloadHand{0.0f, 0.0f, true});
	CHECK(left.shown && left.reaching && Near(left.anchors, Identity()));
	CHECK(Near(left.hand, Compose(Compose(moved, parts[1]), Inverse(gun.holdLeft))));
	CHECK(Near(left.angles.forearm, Normal(Rotate(framed, gun.leftForearm))));
}

// At the magazine, his left hand moves with it, holding its base.
static void TestLeftHandRidesTheMagazine()
{
	for (bool two : {false, true}) {
		const Gun gun = TestGun(two, true);
		const Frame framed = Framed(), moved = Rolled(framed);
		std::vector<Frame> parts = {Identity(), Identity()};
		parts[1].pos = {0, 0, -0.2f}; // dropped out
		Frame grip;
		CHECK(MagazineGrip(gun, grip));
		const HandPlace left = LeftPlace(gun, framed, moved, parts, 0, ReloadHand{1.0f, 0.0f, true});
		CHECK(left.shown && !left.reaching); // holding the magazine, not the anchors
		CHECK(Near(left.hand, Compose(Compose(moved, parts[1]), grip)));
	}
}

// Going away, it drops down and back out of view.
static void TestAwayDropsOutOfView()
{
	const Gun gun = TestGun(false, true);
	const Frame framed = Framed();
	const std::vector<Frame> parts = {Identity(), Identity()};
	const HandPlace there = LeftPlace(gun, framed, framed, parts, 0, ReloadHand{1.0f, 0.0f, true});
	const HandPlace gone = LeftPlace(gun, framed, framed, parts, 0, ReloadHand{1.0f, 1.0f, true});
	CHECK(gone.hand.pos.z < there.hand.pos.z - 0.3f && gone.hand.pos.y < there.hand.pos.y);
}

// A one-handed gun's left hand is drawn only while it reloads; a gun without a
// magazine keeps the hand where it is (two-handed) or away (one-handed).
static void TestWithoutAMagazine()
{
	const Frame framed = Framed();
	const std::vector<Frame> parts = {Identity(), Identity()};
	CHECK(!LeftPlace(TestGun(false, true), framed, framed, parts, 0, ReloadHand{0.0f, 0.0f, false}).shown);
	CHECK(!LeftPlace(TestGun(false, false), framed, framed, parts, 0, ReloadHand{1.0f, 0.0f, true}).shown);
	const Gun two = TestGun(true, false);
	const HandPlace kept = LeftPlace(two, framed, framed, parts, 0, ReloadHand{1.0f, 0.0f, true});
	CHECK(kept.shown && Near(kept.hand, Compose(framed, Inverse(two.holdLeft))));
	CHECK(kept.reaching && Near(kept.anchors, Identity())); // its fingers stay on the gun through the reload
}

// The gun as the animation moves it: turned about his grip, then moved; still, it's as framed.
static void TestGunMoved()
{
	const Gun gun = TestGun(false, true);
	const Frame framed = Framed();
	AnimOutput still{};
	CHECK(Near(GunMoved(gun, framed, still), framed));
	AnimOutput tilted{};
	tilted.roll = 25.0f;
	tilted.offset = {0, 0, -0.03f};
	const Frame moved = GunMoved(gun, framed, tilted);
	CHECK(Near(HandFor(gun, moved).pos, Add(HandFor(gun, framed).pos, tilted.offset)));
	CHECK(Near(moved.x, Rotate(TurnAbout({0, 1, 0}, 25.0f * 3.14159265f / 180.0f, {0, 0, 0}), framed.x))); // rolled about the view's ahead
}

// The sprint carry turns the gun about his grip (his muzzle to the left by
// its yaw); his arms then keep to the carried gun (RightPlace's `framed`).
static void TestGunCarried()
{
	const Gun gun = TestGun(true, true);
	const Frame framed = Framed();
	CHECK(Near(GunCarried(gun, framed, Move{}), framed));
	const Move across{{-0.05f, 0.0f, -0.06f}, 0.0f, 0.0f, 30.0f};
	const Frame carried = GunCarried(gun, framed, across);
	CHECK(Near(HandFor(gun, carried).pos, Add(HandFor(gun, framed).pos, across.offset)));
	CHECK(Near(carried.y, Rotate(TurnAbout({0, 0, 1}, 30.0f * 3.14159265f / 180.0f, {0, 0, 0}), framed.y)));
	CHECK(Rotate(carried, {0, 1, 0}).x < Rotate(framed, {0, 1, 0}).x); // the muzzle's gone left
	AnimOutput yawed{};
	yawed.yaw = 30.0f;
	CHECK(Near(GunMoved(gun, framed, yawed), GunCarried(gun, framed, Move{{0, 0, 0}, 0.0f, 0.0f, 30.0f})));
}

// On its way to the magazine, the hand's fingers keep their grip's shape
// (their anchors travel with it) till it lets go, halfway.
static void TestFingersTravelWithTheHand()
{
	const Gun gun = TestGun(true, true);
	const Frame framed = Framed();
	const std::vector<Frame> parts = {Identity(), Identity()};
	const HandPlace hold = LeftPlace(gun, framed, framed, parts, 0, ReloadHand{0.0f, 0.0f, true});
	const HandPlace going = LeftPlace(gun, framed, framed, parts, 0, ReloadHand{0.3f, 0.0f, true});
	CHECK(going.reaching && !Near(going.hand, hold.hand));
	CHECK(Near(Compose(going.anchors, hold.hand), going.hand));
	CHECK(!LeftPlace(gun, framed, framed, parts, 0, ReloadHand{0.6f, 0.0f, true}).reaching);
	CHECK(RightPlace(gun, framed, framed).reaching && Near(RightPlace(gun, framed, framed).anchors, Identity()));
}

int main()
{
	TestFingersTravelWithTheHand();
	TestGunCarried();
	TestGunMoved();
	TestRightArmKeepsToTheFramedGun();
	TestLeftHandOnItsHold();
	TestLeftHandRidesTheMagazine();
	TestAwayDropsOutOfView();
	TestWithoutAMagazine();
	if (gFailures) {
		printf("%d vmhands test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmhands tests passed\n");
	return 0;
}
