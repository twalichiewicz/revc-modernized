// The police foot search (#22): when they lose him, cops on foot search the
// area's pedestrian paths (squares, alleys, beaches) as well as its streets,
// so leaving the roads doesn't leave him unsearched. Cars still get roads only.
#include "../src/police.cpp"
#include <cstdio>
namespace game
{
Api api{};
}
Config gConfig;
void Log(const char *, ...) {}
namespace wanted
{
// A fresh model searches 60 m (heat::SearchRadius(0)) about the origin.
const heat::Model &Model()
{
	static heat::Model model;
	return model;
}
} // namespace wanted

constexpr int kCarNodes = 4, kAll = 9;
alignas(16) static uint8_t paths[0x54ba0]{};
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
// CPathNode: position x8 as int16 at +4 (NodePos), flags at +0x10 (disabled bit 5, water bit 8).
static void Node(int i, float x, float y, uint32_t flags = 0)
{
	uint8_t *n = paths + size_t(i) * 0x14;
	*reinterpret_cast<int16_t *>(n + 4) = int16_t(x * 8);
	*reinterpret_cast<int16_t *>(n + 6) = int16_t(y * 8);
	*reinterpret_cast<uint32_t *>(n + 0x10) = flags;
}
static bool Has(const std::vector<int> &nodes, int i)
{
	for (int n : nodes)
		if (n == i)
			return true;
	return false;
}

int main()
{
	using namespace police;
	api.thePaths = paths;
	*reinterpret_cast<int32_t *>(paths + 0x54b78) = kAll;     // all nodes
	*reinterpret_cast<int32_t *>(paths + 0x54b7c) = kCarNodes; // car nodes first
	const CVector centre{100, 100, 0};
	Node(0, 110, 100);           // a street in the area
	Node(1, 400, 400);           // a street outside it
	Node(2, 105, 95, 1u << 8);   // water
	Node(3, 95, 105, 1u << 5);   // switched off
	Node(4, 120, 110);           // a pedestrian square in the area
	Node(5, 90, 80);             // an alley in the area
	Node(6, 500, 100);           // a footpath outside it
	Node(7, 101, 99, 1u << 8);   // a pier over water
	Node(8, 99, 101, 1u << 5);   // a disabled path
	const std::vector<int> roads = AreaNodes(centre, 50, false), foot = AreaNodes(centre, 50, true);
	CHECK(Has(roads, 0) && roads.size() == 1); // cars: open streets in the area only
	CHECK(Has(foot, 0) && Has(foot, 4) && Has(foot, 5) && foot.size() == 3);
	// Nothing open in the area: empty (the callers fall back to the nearest street).
	CHECK(AreaNodes({1000, 1000, 0}, 10, true).empty());
	// The two lists the search actually hands out, about the model's area:
	// cars keep the streets; cops on foot get the pedestrian paths as well.
	Node(0, 20, 0);
	Node(4, -30, 10);
	Node(5, 0, -40);
	Node(1, 400, 400);
	Node(6, 500, 100);
	const std::vector<int> &cars = SearchNodes();
	CHECK(Has(cars, 0) && cars.size() == 1);
	const std::vector<int> &feet = FootSearchNodes();
	CHECK(Has(feet, 0) && Has(feet, 4) && Has(feet, 5) && feet.size() == 3);
	if (failures)
		return 1;
	std::puts("policefootsearch: all police foot-search area tests passed");
}
