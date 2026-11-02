// map.bin version 5 (#35): street names, the street each road belongs to, and
// each road's height at its two ends. Older files (3, 4) still load, with no
// streets.
#include "mapdata.h"
#include <cstdio>
#include <cstring>
#include <string>

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

static const std::string kDir = "/private/tmp/claude-501/mapstreets-test";

static mapdata::Map TwoRoads()
{
	mapdata::Map m;
	m.size = 1;
	m.classes = {0};
	m.roads = {{{0, 0}, {50, 0}, 10, 10, 10}, {{50, 0}, {50, 40}, 8, 8, 8}};
	return m;
}

static void TestRoundTrip()
{
	mapdata::Map m = TwoRoads();
	m.streets = {"Ocean Drive", "5th Street"};
	m.roadStreet = {0, mapdata::Map::kNoStreet};
	m.roadZ = {10.0f, 10.5f, 6.0f, 7.0f};
	CHECK(m.Save(kDir + "/v5.bin"));
	mapdata::Map back;
	CHECK(back.Load(kDir + "/v5.bin"));
	CHECK(back.roads.size() == 2 && back.streets.size() == 2 && back.streets[0] == "Ocean Drive" &&
	      back.streets[1] == "5th Street");
	CHECK(back.roadStreet.size() == 2 && back.roadStreet[0] == 0 && back.roadStreet[1] == mapdata::Map::kNoStreet);
	CHECK(back.roadZ.size() == 4 && back.roadZ[1] == 10.5f && back.roadZ[3] == 7.0f);
}

// A version 4 file (as baked before street names): magic, size, classes, no
// buildings, roads.
static void TestLoadsVersion4()
{
	mapdata::Map m = TwoRoads();
	FILE *f = std::fopen((kDir + "/v4.bin").c_str(), "wb");
	const char magic[8] = {'V', 'C', 'M', 'M', 'M', 'A', 'P', '4'};
	const uint32_t size = 1, buildings = 0, roads = 2;
	std::fwrite(magic, 8, 1, f);
	std::fwrite(&size, 4, 1, f);
	std::fwrite(m.classes.data(), 1, 1, f);
	std::fwrite(&buildings, 4, 1, f);
	std::fwrite(&roads, 4, 1, f);
	std::fwrite(m.roads.data(), sizeof(mapdata::Road), 2, f);
	std::fclose(f);
	mapdata::Map back;
	CHECK(back.Load(kDir + "/v4.bin"));
	CHECK(back.roads.size() == 2 && back.streets.empty());
	CHECK(back.roadStreet.size() == 2 && back.roadStreet[0] == mapdata::Map::kNoStreet &&
	      back.roadStreet[1] == mapdata::Map::kNoStreet);
	CHECK(back.roadZ.size() == 4 && back.roadZ[0] == 0);
}

// A street index past the names, or a name too long to have been written: refused.
static void TestRejectsBadStreetIndex()
{
	mapdata::Map m = TwoRoads();
	m.streets = {"Ocean Drive"};
	m.roadStreet = {0, 5};
	m.roadZ = {0, 0, 0, 0};
	CHECK(m.Save(kDir + "/bad.bin"));
	mapdata::Map back;
	CHECK(!back.Load(kDir + "/bad.bin"));
}

int main()
{
	std::system(("mkdir -p " + kDir).c_str());
	TestRoundTrip();
	TestLoadsVersion4();
	TestRejectsBadStreetIndex();
	if (failures)
		return 1;
	std::puts("mapstreets: all map.bin street tests passed");
}
