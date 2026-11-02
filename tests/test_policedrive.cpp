// Police cars arriving where Tommy is on foot (#21): a smooth approach speed
// limit that brakes them toward the pull-up line instead of a full-speed
// handbrake swing, in the car AI's own unit (m/s x 1.2: steering compares the
// cruise byte with game speed x 60, 0x10005c5dc, and game speed x 50 is m/s).
#include "policedrive.h"
#include <cmath>
#include <cstdio>

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
	using namespace policedrive;
	// At and inside the pull-up line: no faster than the arrival speed.
	CHECK(std::fabs(ArrivalSpeed(kArriveAt) - kArriveSpeed) < 1e-5f);
	CHECK(std::fabs(ArrivalSpeed(5) - kArriveSpeed) < 1e-5f);
	// A stopping curve: braking at kBrake from any distance reaches the line at
	// the arrival speed (v^2 = v0^2 + 2ad), and rises monotonically with distance.
	for (float d = kArriveAt; d < 300; d += 7) {
		const float v = ArrivalSpeed(d), farther = ArrivalSpeed(d + 7);
		CHECK(farther > v);
		CHECK(std::fabs(v * v - kArriveSpeed * kArriveSpeed - 2 * kBrake * (d - kArriveAt)) < 1e-2f);
	}
	// PullUp's hidden range is reached slow enough to stop without a spin.
	CHECK(ArrivalSpeed(45) < 16);
	// Far away it never limits a chase at full speed (34 at two stars, about 28 m/s).
	CHECK(ArrivalSpeed(120) > 30);
	// The car AI's cruise byte: m/s x 1.2, rounded, within a byte.
	CHECK(CruiseFor(28.333f) == 34 && CruiseFor(0) == 0 && CruiseFor(-3) == 0 && CruiseFor(1000) == 255);
	CHECK(CruiseFor(ArrivalSpeed(kArriveAt)) == 10);
	// Only lowered, never raised: the game's own slower speed stands.
	CHECK(Limit(60, 30) == CruiseFor(ArrivalSpeed(30)) && Limit(5, 30) == 5 && Limit(60, 500) == 60);
	if (failures)
		return 1;
	std::puts("policedrive: all police arrival-speed tests passed");
}
