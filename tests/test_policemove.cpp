// Police foot-chase pace (#23): native CPed::Seek picks a run for every police
// objective and, in the seek states, only ever raises a ped's pace
// (0x1001a3954). So a sprint the mod sets must also be lowered by the mod.
#include "policemove.h"
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
	using namespace policemove;
	constexpr int32_t still = 1, walk = 2, run = 4, sprint = 5;
	// Chasing far behind at the game's run: sprint.
	CHECK(Decide(true, false, run, kSprintFrom + .1f) == Pace::Sprint);
	// Within reach, or between the thresholds: the game's pace stands.
	CHECK(Decide(true, false, run, kSprintFrom - .1f) == Pace::Native);
	CHECK(Decide(true, true, sprint, (kSprintFrom + kSprintUntil) / 2) == Pace::Native);
	// Back within reach while sprinting: the game's run again.
	CHECK(Decide(true, true, sprint, kSprintUntil - .1f) == Pace::Run);
	// Deliberate paces are never raised (combat's walks, standing to shoot).
	CHECK(Decide(true, false, walk, 50) == Pace::Native);
	CHECK(Decide(true, false, still, 50) == Pace::Native);
	// No longer chasing: a sprint we set is handed back for the game to re-pick
	// (still, which its raise-only rule replaces with its own pace next frame).
	CHECK(Decide(false, true, sprint, 50) == Pace::Repick);
	CHECK(Decide(false, true, sprint, 1) == Pace::Repick);
	// A sprint we didn't set, and anyone not chasing: untouched.
	CHECK(Decide(false, false, sprint, 50) == Pace::Native);
	CHECK(Decide(false, false, run, 50) == Pace::Native);
	CHECK(Decide(true, false, sprint, 1) == Pace::Native);
	CHECK(kSprintUntil < kSprintFrom);
	if (failures)
		return 1;
	std::puts("policemove: all police chase-pace tests passed");
}
