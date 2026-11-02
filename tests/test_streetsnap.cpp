// The baker's street outlines (#35): parsed from assets/streets.txt and snapped
// onto the game's car-path roads. A road takes the nearest outline running
// along it (within 12 m of its middle, within 30 degrees, either way round).
#include "../tools/bake/streets.h"
#include <cstdio>
#include <string>

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

using streets::Outline;
static const uint16_t kNone = mapdata::Map::kNoStreet;
static mapdata::Road Road(Vec2 a, Vec2 b) { return {a, b, 10, 10, 10}; }

static void TestParse()
{
	std::vector<Outline> out;
	std::string error;
	CHECK(streets::ParseStreets("# Ocean Beach\n\"Ocean Drive\" 0,0 100,0\n\n\"Carlo's Way\" 0,50 0,150.5\n", out,
	                            error));
	CHECK(out.size() == 2 && out[0].name == "Ocean Drive" && out[1].name == "Carlo's Way");
	CHECK(out[0].points.size() == 2 && out[1].points[1].y == 150.5f);
}

static void TestParseError()
{
	std::vector<Outline> out;
	std::string error;
	CHECK(!streets::ParseStreets("\"Ocean Drive\" 0,0 100,0\n# fine\nOcean Drive\" 0,0 1,1\n", out, error));
	CHECK(error.find("line 3") != std::string::npos);
	CHECK(!streets::ParseStreets("\"Lone\" 0,0\n", out, error)); // an outline needs two points
}

static void TestParallelRoadSnaps()
{
	const std::vector<Outline> outlines = {{"Ocean Drive", {{0, 0}, {100, 0}}}};
	const auto got = streets::Assign(outlines, {Road({10, 3}, {40, 3})});
	CHECK(got.size() == 1 && got[0] == 0);
}

static void TestCrossStreetNotSnapped()
{
	const std::vector<Outline> outlines = {{"Ocean Drive", {{0, 0}, {100, 0}}}};
	CHECK(streets::Assign(outlines, {Road({50, 2}, {50, 12})})[0] == kNone); // 7 m off, but across it
}

static void TestNearestOutlineWins()
{
	const std::vector<Outline> outlines = {{"Ocean Drive", {{0, 0}, {100, 0}}}, {"Collins Street", {{0, 10}, {100, 10}}}};
	CHECK(streets::Assign(outlines, {Road({10, 3}, {40, 3})})[0] == 0);
	CHECK(streets::Assign(outlines, {Road({10, 8}, {40, 8})})[0] == 1);
}

static void TestFarRoadUnnamed()
{
	const std::vector<Outline> outlines = {{"Ocean Drive", {{0, 0}, {100, 0}}}};
	CHECK(streets::Assign(outlines, {Road({10, 20}, {40, 20})})[0] == kNone);
}

static void TestDirectionIgnored()
{
	const std::vector<Outline> outlines = {{"Ocean Drive", {{100, 0}, {0, 0}}}};
	CHECK(streets::Assign(outlines, {Road({10, 3}, {40, 3})})[0] == 0);
}

// Coverage: road length named against all road length, for roads whose middle is in the box.
static void TestCoverageCountsLength()
{
	const std::vector<mapdata::Road> roads = {Road({0, 0}, {10, 0}), Road({0, 5}, {30, 5}), Road({500, 0}, {600, 0})};
	const std::vector<uint16_t> street = {0, kNone, 0};
	const streets::Coverage c = streets::Measure(roads, street, {-50, -50}, {100, 100});
	CHECK(c.named == 10 && c.total == 40); // the third road's outside the box
}

int main()
{
	TestCoverageCountsLength();
	TestParse();
	TestParseError();
	TestParallelRoadSnaps();
	TestCrossStreetNotSnapped();
	TestNearestOutlineWins();
	TestFarRoadUnnamed();
	TestDirectionIgnored();
	if (failures)
		return 1;
	std::puts("streetsnap: all street outline tests passed");
}
