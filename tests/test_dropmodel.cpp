// Drops on the view in the rain (#32), without the game: where they show
// (a lens on foot and on bikes, the windscreen in cars), how they land, merge,
// trickle, and blow away with speed.
#include "dropmodel.h"
#include <cmath>
#include <cstdio>

using namespace dropmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static void Run(Drops &d, float rain, bool exposed, float speed, DropMode mode, float seconds)
{
	for (float t = 0; t < seconds - 1e-4f; t += 1.0f / 30)
		d.Update(rain, exposed, speed, mode, 1.0f / 30);
}

static void TestModeFor()
{
	CHECK(ModeFor(false, false, kCar, false, false) == DropMode::None);   // third person
	CHECK(ModeFor(true, false, kCar, false, false) == DropMode::Lens);    // on foot in first person
	CHECK(ModeFor(false, true, kCar, false, false) == DropMode::Windscreen);
	CHECK(ModeFor(false, true, kHeli, false, false) == DropMode::Windscreen);
	CHECK(ModeFor(false, true, kPlane, false, false) == DropMode::Windscreen);
	CHECK(ModeFor(false, true, kBike, false, false) == DropMode::Lens);
	CHECK(ModeFor(false, true, kBoat, false, false) == DropMode::Lens);
	CHECK(ModeFor(false, true, kCar, true, false) == DropMode::Lens);     // the top down
	CHECK(ModeFor(true, false, kCar, false, true) == DropMode::None);     // indoors
	CHECK(ModeFor(false, true, kCar, false, true) == DropMode::None);
}

static void TestDropsLandWithRain()
{
	Drops wet, dry;
	Run(wet, 1, true, 0, DropMode::Lens, 2);
	Run(dry, 0, true, 0, DropMode::Lens, 2);
	CHECK(wet.All().size() > 20 && dry.All().empty());
	bool onScreen = true, big = true, shapes = false;
	for (const Drop &d : wet.All()) {
		onScreen &= d.pos.x >= 0 && d.pos.x <= 1 && d.pos.y >= 0 && d.pos.y <= 1 && d.radius > 0;
		big &= d.radius >= 0.009f; // drops on a lens, not specks: 9 px and up at 1080
		shapes |= d.seed != wet.All()[0].seed;
	}
	CHECK(onScreen && big && shapes);
	Drops light;
	Run(light, 0.3f, true, 0, DropMode::Lens, 2);
	CHECK(light.All().size() < wet.All().size());
}

static void TestNoDropsWhenCovered()
{
	Drops d;
	Run(d, 1, false, 0, DropMode::Lens, 2);
	CHECK(d.All().empty());
	Run(d, 1, true, 0, DropMode::Lens, 2); // out from under it they land, and under it again they dry
	const size_t had = d.All().size();
	Run(d, 1, false, 0, DropMode::Lens, 15);
	CHECK(had > 0 && d.All().empty());
}

static void TestMergeAndTrickle()
{
	Drops d;
	d.Update(0, true, 0, DropMode::Lens, 0);
	d.Land({0.5f, 0.5f}, 0.026f);
	d.Land({0.52f, 0.5f}, 0.026f); // touching: one, as much water as both, the bigger one's shape
	CHECK(d.All().size() == 1);
	if (d.All().size() != 1)
		return;
	CHECK(std::fabs(d.All()[0].radius - 0.026f * std::cbrt(2.0f)) < 1e-4f);
	Run(d, 0, true, 0, DropMode::Lens, 1); // heavy enough to run down, leaving a trail
	CHECK(d.All().size() == 1 && d.All()[0].pos.y > 0.52f && d.All()[0].trail > 0.01f);
	Drops small;
	small.Update(0, true, 0, DropMode::Lens, 0);
	small.Land({0.5f, 0.5f}, 0.012f); // a small one stays where it is
	Run(small, 0, true, 0, DropMode::Lens, 1);
	CHECK(small.All().size() == 1 && small.All()[0].pos.y == 0.5f);
}

static void TestSpeedPushesUp()
{
	Drops d;
	d.Update(0, true, 30, DropMode::Windscreen, 0);
	d.Land({0.3f, 0.5f}, 0.012f);
	Run(d, 0, true, 30, DropMode::Windscreen, 0.5f);
	CHECK(d.All().size() == 1 && d.All()[0].pos.y < 0.47f && d.All()[0].pos.x < 0.3f); // up, and aside
	Drops parked;
	parked.Update(0, true, 0, DropMode::Windscreen, 0);
	parked.Land({0.3f, 0.5f}, 0.012f);
	Run(parked, 0, true, 0, DropMode::Windscreen, 0.5f);
	CHECK(parked.All().size() == 1 && parked.All()[0].pos.y == 0.5f);
}

// On a windscreen they're where the glass is from his seat, fixed to the car:
// a window of directions (its x right, y ahead, z up), so they stay on the
// glass as he looks round.
static void TestOnTheWindscreen()
{
	const Vec3 middle = WindscreenDirection({0.5f, 0.5f}), topLeft = WindscreenDirection({0, 0}),
	           bottomRight = WindscreenDirection({1, 1});
	CHECK(std::fabs(middle.x) < 1e-5f && middle.y > 0.99f && std::fabs(middle.z) < 0.1f); // about straight ahead
	CHECK(topLeft.x < -0.6f && topLeft.z > 0.3f && topLeft.y > 0);                          // up and to the left
	CHECK(bottomRight.x > 0.6f && bottomRight.z < -0.2f && bottomRight.y > 0);               // down and to the right
	bool unit = true;
	for (float u : {0.0f, 0.3f, 1.0f})
		for (float v : {0.0f, 0.7f, 1.0f}) {
			const Vec3 d = WindscreenDirection({u, v});
			unit &= std::fabs(d.x * d.x + d.y * d.y + d.z * d.z - 1) < 1e-4f;
		}
	CHECK(unit);
	CHECK(std::fabs(WindscreenAngle(0.02f) - 0.02f * kWindscreenHigh) < 1e-6f); // a drop's size as an angle
}

static void TestModeSwitchClears()
{
	Drops d;
	Run(d, 1, true, 0, DropMode::Lens, 2);
	CHECK(!d.All().empty());
	d.Update(0, true, 0, DropMode::Windscreen, 1.0f / 30); // (no rain this frame: none land on the new glass yet)
	CHECK(d.All().empty());
}

static void TestInteriorNoDrops()
{
	Drops d;
	Run(d, 1, true, 0, ModeFor(true, false, kCar, false, true), 2);
	CHECK(d.All().empty());
}

int main()
{
	TestModeFor();
	TestDropsLandWithRain();
	TestNoDropsWhenCovered();
	TestMergeAndTrickle();
	TestSpeedPushesUp();
	TestOnTheWindscreen();
	TestModeSwitchClears();
	TestInteriorNoDrops();
	if (failures)
		return 1;
	std::puts("dropmodel: all drop tests passed");
}
