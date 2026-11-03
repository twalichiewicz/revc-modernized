// Vehicles in the rain (#32): soft ground turned to mud under the wheels, and
// aircraft thrown about.
#include "vehiclerain.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace vehiclerain;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

constexpr uint8_t kTarmac = 1, kGrass = 2, kGravel = 3, kMud = 4, kSand = 18, kBeach = 33;

static void TestDryGroundUnchanged()
{
	for (uint8_t s : {kTarmac, kGrass, kGravel, kMud, kSand, kBeach}) {
		const Ground g = OnGround(s, 0);
		CHECK(g.grip == 1 && g.drive == 1 && g.drag == 0);
	}
	const Ground road = OnGround(kTarmac, 1); // the game's own wet roads already have less grip
	CHECK(road.grip == 1 && road.drive == 1 && road.drag == 0);
}

static void TestSoakedGroundBogsDown()
{
	for (uint8_t s : {kGrass, kMud, kSand, kBeach}) {
		const Ground half = OnGround(s, 0.5f), soaked = OnGround(s, 1);
		CHECK(soaked.grip < half.grip && half.grip < 1 && soaked.grip >= 0.3f);
		CHECK(soaked.drive < half.drive && half.drive < 1 && soaked.drive >= 0.3f);
		CHECK(soaked.drag > half.drag && half.drag > 0);
	}
	CHECK(OnGround(kGravel, 1).drag < OnGround(kGrass, 1).drag); // gravel drains: firmer
	CHECK(OnGround(kSand, 1).drag >= OnGround(kGrass, 1).drag);  // sand clogs the most
}

// A weak car (about 2.5 m/s² flat out) gets stuck in a soaked field; a strong
// one (6 m/s²) still crawls through.
static void TestWeakCarsStuck()
{
	const Ground field = OnGround(kGrass, 1), beach = OnGround(kBeach, 1);
	CHECK(2.5f * field.drive < field.drag && 2.5f * beach.drive < beach.drag);
	CHECK(6.0f * field.drive > field.drag && 6.0f * beach.drive > beach.drag);
	const Ground damp = OnGround(kGrass, 0.25f); // only just wet: the weak one still gets through
	CHECK(2.5f * damp.drive > damp.drag);
}

static void TestAircraftThrownAbout()
{
	const Air calm = InTheAir(0, 0, 10, 7);
	CHECK(calm.shove.x == 0 && calm.shove.y == 0 && calm.shove.z == 0 && calm.twist.x == 0 && calm.twist.z == 0);
	float most = 0, sink = 0, jump = 0;
	Air last = InTheAir(0.5f, 0.2f, 0, 7);
	for (int i = 1; i < 30 * 60; ++i) {
		const Air a = InTheAir(0.5f, 0.2f, i / 30.0f, 7);
		most = std::fmax(most, std::fabs(a.twist.x) + std::fabs(a.twist.y) + std::fabs(a.twist.z));
		sink += a.shove.z;
		jump = std::fmax(jump, std::fabs(a.twist.x - last.twist.x) + std::fabs(a.shove.z - last.shove.z));
		last = a;
	}
	CHECK(most > 0.3f && most < 4.0f);    // rad/s²: felt, not tumbling
	CHECK(sink / (30 * 60) < -0.2f);      // it loses lift in the rain, on average
	CHECK(jump < 0.5f);                   // bumps, not jerks from one frame to the next
	const Air gusty = InTheAir(0.5f, 1, 12.3f, 7), still = InTheAir(0.5f, 0, 12.3f, 7);
	CHECK(std::fabs(gusty.twist.x) + std::fabs(gusty.twist.y) > std::fabs(still.twist.x) + std::fabs(still.twist.y));
	const Air other = InTheAir(0.5f, 0.2f, 12.3f, 8); // each aircraft its own bumps
	CHECK(other.twist.x != InTheAir(0.5f, 0.2f, 12.3f, 7).twist.x);
}

int main()
{
	TestDryGroundUnchanged();
	TestSoakedGroundBogsDown();
	TestWeakCarsStuck();
	TestAircraftThrownAbout();
	if (failures)
		return 1;
	std::puts("vehiclerain: all vehicles-in-the-rain tests passed");
}
