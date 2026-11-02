// The rain's weather (#32), without the game: the ground's wetness follows the
// game's own (CWeather::WetRoads), eased; puddles build over minutes of heavy
// rain and drain over more; streaks thicken with the rain.
#include "rainmodel.h"
#include <cmath>
#include <cstdio>

using namespace rainmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static Weather Run(Model &m, float rain, float wet, float seconds)
{
	Weather w{};
	for (float t = 0; t < seconds - 1e-4f; t += 1.0f / 30)
		w = m.Update(rain, wet, 1.0f / 30);
	return w;
}

static void TestWetnessFollowsGame()
{
	Model m;
	CHECK(Run(m, 0, 0, 1).wetness == 0);
	const Weather w = Run(m, 1, 0.8f, 5); // 5 s: eased in by now
	CHECK(std::fabs(w.wetness - 0.8f) < 0.02f && w.rain == 1);
}

static void TestDriesSmoothly()
{
	Model m;
	Run(m, 1, 1, 10);
	float last = 1;
	bool smooth = true;
	for (int i = 0; i < 300; ++i) { // the game's wetness drops to 0 at once: ours eases
		const Weather w = m.Update(0, 0, 1.0f / 30);
		smooth &= last - w.wetness <= 0.05f;
		last = w.wetness;
	}
	CHECK(smooth && last < 0.05f);
}

static void TestPuddlesBuildAndDrain()
{
	Model m;
	CHECK(Run(m, 1, 1, 60).puddles > 0.2f && Run(m, 1, 1, 0).puddles < 0.5f); // a minute: building
	CHECK(Run(m, 1, 1, 130).puddles > 0.99f);                                // three minutes: full
	const float full = Run(m, 0, 0, 120).puddles;                            // two minutes dry
	CHECK(full > 0.5f && full < 0.75f);
	CHECK(Run(m, 0, 0, 200).puddles < 0.01f);                                // five minutes dry: gone
	Model light;
	CHECK(Run(light, 0.3f, 0.3f, 180).puddles < 0.4f); // light rain fills them less
}

static void TestStreakCounts()
{
	CHECK(StreakCount(0) == 0 && StreakCount(0.04f) == 0);
	CHECK(StreakCount(1) == kMaxStreaks && kMaxStreaks == 6000);
	CHECK(StreakCount(0.5f) > 0 && StreakCount(0.5f) < kMaxStreaks);
	CHECK(StreakCount(0.3f) < StreakCount(0.6f));
}

int main()
{
	TestWetnessFollowsGame();
	TestDriesSmoothly();
	TestPuddlesBuildAndDrain();
	TestStreakCounts();
	if (failures)
		return 1;
	std::puts("rainmodel: all rain weather tests passed");
}
