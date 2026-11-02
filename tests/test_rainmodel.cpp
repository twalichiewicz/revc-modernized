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
	Model m; // the game's rain (CATSANDDOGS gives 0.5)
	const float minute = Run(m, 0.5f, 1, 60).puddles;
	CHECK(minute > 0.4f && minute < 0.6f);            // a minute: half full
	CHECK(Run(m, 0.5f, 1, 65).puddles > 0.99f);       // two minutes: full
	const float drying = Run(m, 0, 0, 120).puddles;   // two minutes dry
	CHECK(drying > 0.5f && drying < 0.75f);
	CHECK(Run(m, 0, 0, 200).puddles < 0.01f);         // five minutes dry: gone
	Model light;
	const float drizzle = Run(light, 0.2f, 0.3f, 300).puddles;
	CHECK(drizzle > 0.3f && drizzle < 0.41f);         // a drizzle fills them only so far
}

static const Vec3 kCamera{1000, -500, 30};
static bool Open(Vec3) { return false; }

static void TestStreaksSlantWithWind()
{
	const std::vector<Streak> s = Streaks(kCamera, {7, 3}, 500, 12.5f, Open);
	CHECK(s.size() == 500);
	bool slant = true;
	for (const Streak &x : s) {
		const float dx = x.bottom.x - x.top.x, dy = x.bottom.y - x.top.y, dz = x.bottom.z - x.top.z;
		slant &= dz < 0 && dx > 0 && dy > 0 && std::fabs(dx / -dz - 7.0f / 9) < 0.01f && std::fabs(dy / -dz - 3.0f / 9) < 0.01f;
	}
	CHECK(slant);
	bool straight = true;
	for (const Streak &x : Streaks(kCamera, {0, 0}, 100, 3, Open))
		straight &= x.bottom.x == x.top.x && x.bottom.y == x.top.y && x.bottom.z < x.top.z;
	CHECK(straight);
}

static void TestCoveredLeftOut()
{
	const auto east = [](Vec3 p) { return p.x > kCamera.x; }; // a roof over everything east of him
	const std::vector<Streak> s = Streaks(kCamera, {7, 3}, 2000, 4, east);
	bool west = true;
	for (const Streak &x : s)
		west &= x.top.x <= kCamera.x && x.bottom.x <= kCamera.x;
	CHECK(west && s.size() > 700 && s.size() < 1300);
}

static void TestWrapsAroundCamera()
{
	const std::vector<Streak> a = Streaks(kCamera, {7, 3}, 1000, 100, Open);
	bool inside = true;
	for (const Streak &x : a)
		inside &= std::fabs(x.top.x - kCamera.x) <= kStreakBox / 2 && std::fabs(x.top.y - kCamera.y) <= kStreakBox / 2 &&
		          x.top.z >= kCamera.z - kStreakBelow && x.top.z <= kCamera.z - kStreakBelow + kStreakHeight;
	CHECK(inside);
	// He walks half a metre: the rain stays where it is in the world, but for the few that wrap.
	const std::vector<Streak> b = Streaks({kCamera.x + 0.5f, kCamera.y, kCamera.z}, {7, 3}, 1000, 100, Open);
	int same = 0;
	for (size_t i = 0; i < a.size() && i < b.size(); i++)
		same += a[i].top.x == b[i].top.x && a[i].top.y == b[i].top.y && a[i].top.z == b[i].top.z;
	CHECK(b.size() == 1000 && same > 950 && same < 1000);
	// And it falls: later, lower (or wrapped back to the top).
	const std::vector<Streak> c = Streaks(kCamera, {0, 0}, 1000, 100.1f, Open);
	int fell = 0;
	for (size_t i = 0; i < a.size(); i++)
		fell += c[i].top.z < a[i].top.z;
	CHECK(fell > 900);
}

static void TestInteriorNoStreaks()
{
	CHECK(StreakCount(1, true) == 0 && StreakCount(1, false) == kMaxStreaks);
	CHECK(Streaks(kCamera, {7, 3}, StreakCount(1, true), 5, Open).empty());
}

static void TestNotInsideTheCabin()
{
	// A car turned 90 degrees (its forward along -x), its cabin 0.7 wide, 1.2 long, 0.5 tall either way.
	const Box cabin{{kCamera.x, kCamera.y, kCamera.z}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}, {0.7f, 1.2f, 0.5f}};
	CHECK(Inside(cabin, {kCamera.x - 1.1f, kCamera.y + 0.6f, kCamera.z + 0.4f}));
	CHECK(!Inside(cabin, {kCamera.x + 0.6f, kCamera.y + 1.1f, kCamera.z})); // along its width: out of it
	CHECK(!Inside(cabin, {kCamera.x, kCamera.y, kCamera.z + 0.6f}));        // over its roof
	const auto inCabin = [&](Vec3 p) { return Inside(cabin, p); };
	bool out = true;
	for (const Streak &s : Streaks(kCamera, {7, 3}, kMaxStreaks, 9, inCabin))
		out &= !Inside(cabin, s.top) && !Inside(cabin, s.bottom);
	CHECK(out);
}

static void TestLitByNearLights()
{
	const Vec3 sky{0.2f, 0.2f, 0.25f};
	const std::vector<Lamp> lamps{{{10, 0, 5}, {1, 0.5f, 0.2f}, 8}};
	const Vec3 far = Lit({100, 0, 5}, sky, lamps), near = Lit({11, 0, 5}, sky, lamps);
	CHECK(far.x == sky.x && far.y == sky.y && far.z == sky.z);
	CHECK(near.x > sky.x + 0.5f && near.y > sky.y + 0.2f && near.z < near.y);
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
	TestStreaksSlantWithWind();
	TestCoveredLeftOut();
	TestWrapsAroundCamera();
	TestInteriorNoStreaks();
	TestLitByNearLights();
	TestNotInsideTheCabin();
	if (failures)
		return 1;
	std::puts("rainmodel: all rain weather tests passed");
}
