// Which street he's on (#35): the nearest named road within its half-width
// plus 6 m; driving, only roads running his way (either way round); never one
// more than 4 m above or below him (a bridge over the street). A new street
// takes over after 1 s or 15 m on it; no street after 2 s.
#include "streetmodel.h"
#include <cstdio>

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

using namespace streetmodel;
struct Fixture {
	mapdata::Map map;
	void Add(Vec2 a, Vec2 b, uint16_t street, float za = 0, float zb = 0)
	{
		map.roads.push_back({a, b, 10, 10, 10});
		map.roadStreet.push_back(street);
		map.roadZ.push_back(za);
		map.roadZ.push_back(zb);
	}
};

static void TestOnFootNearest()
{
	Fixture f;
	f.map.streets = {"Ocean Drive", "Collins Street"};
	f.Add({0, 0}, {100, 0}, 0);
	f.Add({0, 30}, {100, 30}, 1);
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {50, 4, 1}, {1, 0}, false) == 0);
	CHECK(StreetAt(index, f.map, {50, 27, 1}, {1, 0}, false) == 1);
}

static void TestOffRoadNone()
{
	Fixture f;
	f.map.streets = {"Ocean Drive", "Collins Street"};
	f.Add({0, 0}, {100, 0}, 0);
	f.Add({0, 60}, {100, 60}, 1);
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {50, 25, 1}, {1, 0}, false) == -1); // 25 m and 35 m off: a park
}

static void TestDrivingUsesHeading()
{
	Fixture f;
	f.map.streets = {"Ocean Drive", "5th Street"};
	f.Add({0, 0}, {100, 0}, 0);  // east-west
	f.Add({50, 0}, {50, 100}, 1); // north from the junction
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {50, 2, 1}, {1, 0}, true) == 0);
	CHECK(StreetAt(index, f.map, {50, 2, 1}, {0, 1}, true) == 1);
}

static void TestReverseHeadingMatches()
{
	Fixture f;
	f.map.streets = {"Ocean Drive"};
	f.Add({0, 0}, {100, 0}, 0);
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {20, 2, 1}, {-1, 0}, true) == 0);
}

static void TestOverpassPicksByHeight()
{
	Fixture f;
	f.map.streets = {"Ocean Drive", "Starfish Causeway"};
	f.Add({0, 0}, {100, 0}, 0, 0, 0);       // the street
	f.Add({50, -50}, {50, 50}, 1, 8, 8);    // the bridge over it
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {50, 0, 1}, {1, 0}, false) == 0);
	CHECK(StreetAt(index, f.map, {50, 0, 9}, {0, 1}, false) == 1);
}

static void TestUnnamedRoadIgnored()
{
	Fixture f;
	f.map.streets = {"Ocean Drive"};
	f.Add({0, 0}, {100, 0}, mapdata::Map::kNoStreet);
	const Index index(f.map);
	CHECK(StreetAt(index, f.map, {50, 1, 1}, {1, 0}, false) == -1);
}

static void TestCrossroadsKeepsCurrent()
{
	Tracker t;
	for (int i = 0; i < 40; ++i)
		t.Update(0, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == 0);
	for (int i = 0; i < 90; ++i) // 3 s flipping between the two
		t.Update(i % 2 ? 0 : 1, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == 0);
}

static void TestSwitchAfterSecond()
{
	Tracker t;
	for (int i = 0; i < 40; ++i)
		t.Update(0, {0, 0, 0}, 1.0f / 30);
	for (int i = 0; i < 25; ++i)
		t.Update(1, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == 0); // 0.83 s
	for (int i = 0; i < 10; ++i)
		t.Update(1, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == 1);
	for (int i = 0; i < 50; ++i) // off the roads: 1.7 s isn't enough
		t.Update(-1, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == 1);
	for (int i = 0; i < 20; ++i)
		t.Update(-1, {0, 0, 0}, 1.0f / 30);
	CHECK(t.Current() == -1);
}

static void TestSwitchAfter15m()
{
	Tracker t;
	for (int i = 0; i < 40; ++i)
		t.Update(0, {0, 0, 0}, 1.0f / 30);
	t.Update(1, {0, 0, 0}, 0.1f);
	t.Update(1, {16, 0, 0}, 0.1f); // driving: 16 m in 0.2 s
	CHECK(t.Current() == 1);
}

// The banner: shows (rising over 0.25 s) on a new street, a new area or getting
// into a vehicle; holds 4 s, fades over 0.6 s; held while the expanded map is
// up; back on the street it last showed within 3 s, it isn't shown again.
static void Run(Banner &b, int street, int area, float seconds, bool expanded = false)
{
	for (float t = 0; t < seconds - 1e-4f; t += 1.0f / 60)
		b.Update(street, area, false, expanded, 1.0f / 60);
}

static void TestShowsOnNewStreet()
{
	Banner b;
	Run(b, 0, 1, 5);
	CHECK(b.Alpha() == 0);
	b.Update(1, 1, false, false, 1.0f / 60);
	Run(b, 1, 1, 0.3f);
	CHECK(b.Alpha() == 1);
}

static void TestFadesAfterHold()
{
	Banner b;
	Run(b, 0, 1, 5);
	b.Update(1, 1, false, false, 0);
	Run(b, 1, 1, 0.1f);
	CHECK(b.Alpha() > 0 && b.Alpha() < 1); // rising
	Run(b, 1, 1, 3.8f);                    // 3.9 s
	CHECK(b.Alpha() == 1);
	Run(b, 1, 1, 0.8f);                    // 4.7 s
	CHECK(b.Alpha() == 0);
}

static void TestShowsOnAreaChange()
{
	Banner b;
	Run(b, -1, 1, 5);
	b.Update(-1, 2, false, false, 0); // off-road, into another area: the area line alone
	Run(b, -1, 2, 1);
	CHECK(b.Alpha() == 1);
}

static void TestShowsOnVehicleEntry()
{
	Banner b;
	Run(b, 0, 1, 5);
	b.Update(0, 1, true, false, 0);
	Run(b, 0, 1, 1);
	CHECK(b.Alpha() == 1);
}

static void TestExpandedHolds()
{
	Banner b;
	Run(b, 0, 1, 5);
	CHECK(b.Alpha() == 0);
	Run(b, 0, 1, 10, true);
	CHECK(b.Alpha() == 1);
}

static void TestBriefGapNoRebanner()
{
	Banner b;
	Run(b, 0, 1, 5);
	Run(b, -1, 1, 1); // a car park entrance
	Run(b, 0, 1, 1);
	CHECK(b.Alpha() == 0);
	Run(b, -1, 1, 4); // longer off it
	Run(b, 0, 1, 0.5f);
	CHECK(b.Alpha() == 1);
}

int main()
{
	TestShowsOnNewStreet();
	TestFadesAfterHold();
	TestShowsOnAreaChange();
	TestShowsOnVehicleEntry();
	TestExpandedHolds();
	TestBriefGapNoRebanner();
	TestOnFootNearest();
	TestOffRoadNone();
	TestDrivingUsesHeading();
	TestReverseHeadingMatches();
	TestOverpassPicksByHeight();
	TestUnnamedRoadIgnored();
	TestCrossroadsKeepsCurrent();
	TestSwitchAfterSecond();
	TestSwitchAfter15m();
	if (failures)
		return 1;
	std::puts("streetmodel: all street tracker tests passed");
}
