#include "populationmodel.h"
#include <cmath>
#include <cstdio>
using namespace populationmodel;
int main()
{
	int failures = 0;
	World city;
	std::vector<Node> nodes = {{{0, 0, 0}, 9, {1}}, {{10, 0, 0}, 1, {0}}, {{1000, 0, 0}, 0, {}}};
	city.Init(nodes, 1000, 80, 731);
	if (city.Agents().size() != 1080) {
		puts("FAIL: initial city population budget");
		failures++;
	}
	int dense = 0;
	for (const Agent &a : city.Agents()) {
		if (a.node == 2 || a.node < 0)
			failures++;
		if (!a.cop && a.node == 0)
			dense++;
	}
	if (dense < 820 || dense > 960) {
		puts("FAIL: density-weighted distribution");
		failures++;
	}
	city.Tick(1);
	for (const Agent &a : city.Agents()) {
		const Vec3 p = city.Position(a);
		if (!std::isfinite(p.x) || p.x < 0 || p.x > 10)
			failures++;
	}
	if (city.Agents().size() != 1080)
		failures++;
	World empty;
	empty.Init({{{0, 0, 0}, 0, {}}}, 50, 10, 1);
	if (!empty.Agents().empty()) {
		puts("FAIL: no population without suitable paths");
		failures++;
	}
	Station station;
	if (!station.Queue(1, Kind::Officer, 1000) || !station.Queue(2, Kind::Car, 1000))
		failures++;
	station.Queue(1, Kind::Officer, 1000);
	if (station.Pending() != 2 || station.Ready(1, 1000) || !station.Ready(1, 22000) || station.Ready(2, 22000)) {
		puts("FAIL: replenishment must queue serially and deduplicate");
		failures++;
	}
	if (!station.Ready(2, 70000))
		failures++;
	station.Collect(1);
	if (station.Ready(1, 100000) || station.Pending() != 1)
		failures++;
	for (int i = 3; i < 20; i++)
		station.Queue(i, Kind::Officer, 1000);
	if (station.Pending() > 8) {
		puts("FAIL: unbounded station capacity");
		failures++;
	}
	if (failures) {
		printf("populationmodel: %d failures\n", failures);
		return 1;
	}
	puts("populationmodel: all passed");
}
