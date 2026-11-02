// map.bin version 6 (#32): the paved ground that gets wet, by 64 m cell, and
// how high the cover reaches over every 2 m, both baked from the collision.
#include "mapdata.h"
#include "wetground.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

using namespace wetground;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

constexpr uint8_t kTarmac = 1, kGrass = 2, kPavement = 5, kCloth = 8, kLampPost = 13, kWall = 0;

// A flat square at height z (two triangles, wound as the collision winds a
// floor: clockwise seen from above), or facing down (a ceiling).
static void Square(std::vector<Face> &faces, float x0, float y0, float x1, float y1, float z, uint8_t surface,
                   bool down = false)
{
	if (down) {
		faces.push_back({{x0, y0, z}, {x1, y1, z}, {x0, y1, z}, surface});
		faces.push_back({{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, surface});
		return;
	}
	faces.push_back({{x0, y0, z}, {x0, y1, z}, {x1, y1, z}, surface});
	faces.push_back({{x0, y0, z}, {x1, y1, z}, {x1, y0, z}, surface});
}

static mapdata::Map Covered(const std::vector<Face> &faces)
{
	mapdata::Map m;
	m.covers = CoverTiles(faces);
	m.IndexCovers();
	return m;
}

static size_t Tris(const std::vector<mapdata::WetCell> &cells)
{
	size_t n = 0;
	for (const mapdata::WetCell &c : cells)
		n += c.tris.size() / 3;
	return n;
}

static void TestKeepsPavedUpward()
{
	std::vector<Face> faces;
	Square(faces, 0, 0, 10, 10, 5, kTarmac);   // the road: kept
	Square(faces, 20, 0, 30, 10, 5, kGrass);   // grass: dropped
	faces.push_back({{40, 0, 5}, {40, 10, 5}, {40, 10, 9}, kTarmac}); // a wall: dropped
	Square(faces, 60, 0, 70, 10, 9, kWall);    // a deck's top...
	Square(faces, 60, 0, 70, 10, 8, kTarmac, true); // ...and its tarmac ceiling under it: dropped
	Square(faces, 80, 0, 90, 10, 5, kPavement); // pavement: kept
	const std::vector<mapdata::WetCell> wet = WetGround(faces, Covered(faces));
	CHECK(Tris(wet) == 4);
	CHECK(Paved(kTarmac) && Paved(kPavement) && !Paved(kGrass) && !Paved(kWall));
}

static void TestUnderRoofDropped()
{
	std::vector<Face> faces;
	Square(faces, 0, 0, 20, 20, 0, kTarmac);   // under the roof
	Square(faces, 0, 0, 20, 20, 6, kWall);     // the roof
	Square(faces, 30, 0, 50, 20, 0, kTarmac);  // in the open
	Square(faces, 34, 4, 46, 16, 1, kWall);    // under a bench, or by a low wall: still wet
	const std::vector<mapdata::WetCell> wet = WetGround(faces, Covered(faces));
	CHECK(Tris(wet) == 2);
	bool open = true;
	for (const mapdata::WetCell &c : wet)
		for (const Vec3 &v : c.tris)
			open &= v.x >= 30;
	CHECK(open);
}

static void TestCellsBounded()
{
	std::vector<Face> faces;
	for (int i = 0; i < 4000; ++i) { // one 64 m cell ([-16, 48) both ways), crowded: the biggest kWetTrisPerCell stay
		const float x = -10.0f + float(i % 50), y = -10.0f + float(i / 50) * 0.6f, s = i < 3000 ? 0.8f : 0.2f;
		faces.push_back({{x, y, 0}, {x, y + s, 0}, {x + s, y, 0}, kTarmac});
	}
	const std::vector<mapdata::WetCell> wet = WetGround(faces, Covered(faces));
	CHECK(wet.size() == 1 && Tris(wet) == size_t(kWetTrisPerCell));
	bool big = true;
	for (size_t t = 0; t + 2 < wet[0].tris.size(); t += 3)
		big &= wet[0].tris[t + 1].y - wet[0].tris[t].y > 0.5f;
	CHECK(big);
}

static void TestCoverBelowRoof()
{
	std::vector<Face> faces;
	Square(faces, 0, 0, 40, 40, 0, kTarmac);   // the street
	Square(faces, 10, 10, 20, 20, 12, kWall);  // a roof
	Square(faces, 30, 10, 34, 20, 4, kCloth);  // an awning
	Square(faces, 24, 24, 26, 26, 6, kLampPost); // a lamp's head: not cover
	const mapdata::Map m = Covered(faces);
	CHECK(CoveredAt(m, {15, 15, 1}) && CoveredAt(m, {15, 15, 11}));
	CHECK(!CoveredAt(m, {15, 15, 13}));        // on the roof
	CHECK(!CoveredAt(m, {5, 5, 1}));           // the open street
	CHECK(CoveredAt(m, {32, 15, 2}));          // under the awning
	CHECK(!CoveredAt(m, {25, 25, 2}));         // under the lamp
	CHECK(!CoveredAt(m, {500, 500, 0}));       // the sea: nothing there
	CHECK(!CoveredAt(m, {-1e6f, 3e6f, 0}));    // off the map
	CHECK(!CoveredAt(mapdata::Map{}, {15, 15, 1})); // an old map: never covered
}

static std::string Dir()
{
	const char *tmp = std::getenv("TMPDIR");
	std::string t = std::string(tmp ? tmp : "/tmp") + "/wetground-XXXXXX";
	return mkdtemp(t.data()) ? t : std::string(tmp ? tmp : "/tmp");
}

static void TestRoundTrip()
{
	std::vector<Face> faces;
	Square(faces, 0, 0, 10, 10, 3, kTarmac);
	Square(faces, 100, 100, 110, 110, 20, kWall);
	mapdata::Map m = Covered(faces);
	m.size = 1;
	m.classes = {1};
	m.wet = WetGround(faces, m);
	const std::string path = Dir() + "/v6.bin";
	CHECK(m.Save(path));
	mapdata::Map back;
	CHECK(back.Load(path));
	CHECK(back.wet.size() == 1 && back.wet[0].tris.size() == 6 && back.wet[0].cx == m.wet[0].cx &&
	      back.wet[0].tris[2].z == 3);
	CHECK(back.covers.size() == m.covers.size() && CoveredAt(back, {105, 105, 10}) && !CoveredAt(back, {5, 5, 3.5f}));
}

static void TestLoadsVersion5()
{
	mapdata::Map m;
	m.size = 1;
	m.classes = {1};
	m.roads = {{{0, 0}, {50, 0}, 10, 10, 10}};
	const std::string path = Dir() + "/v5.bin";
	CHECK(m.Save(path));
	// A version 5 file: the same, without the rain's two (empty) counts, and the old magic.
	FILE *f = std::fopen(path.c_str(), "r+b");
	CHECK(f != nullptr);
	if (!f)
		return;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 7, SEEK_SET);
	std::fputc('5', f);
	std::fclose(f);
	CHECK(truncate(path.c_str(), size - 8) == 0);
	mapdata::Map back;
	CHECK(back.Load(path));
	CHECK(back.roads.size() == 1 && back.wet.empty() && back.covers.empty() && !CoveredAt(back, {0, 0, 0}));
}

int main()
{
	TestKeepsPavedUpward();
	TestUnderRoofDropped();
	TestCellsBounded();
	TestCoverBelowRoof();
	TestRoundTrip();
	TestLoadsVersion5();
	if (failures)
		return 1;
	std::puts("wetground: all wet ground and cover tests passed");
}
