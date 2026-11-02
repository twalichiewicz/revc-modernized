#include "../tools/bake/roofgeometry.h"
#include <cassert>
#include <cstdio>
int main()
{
	using namespace roofgeometry;
	const Face base{{{0, 0, 10}, {12, 0, 10}, {0, 12, 10}}};
	const Face tower{{{2, 2, 30}, {7, 2, 30}, {2, 7, 30}}};
	assert(!Covered(std::vector<Face>{base, tower}, std::vector<bool>{true, true}, 0));
	const Face full{{{-1, -1, 30}, {30, -1, 30}, {-1, 30, 30}}};
	assert(Covered(std::vector<Face>{base, full}, std::vector<bool>{true, true}, 0));
	assert(!Covered(std::vector<Face>{base}, std::vector<bool>{true}, 0));
	const Face same{{{0, 0, 10}, {12, 0, 10}, {0, 12, 10}}};
	assert(!Covered(std::vector<Face>{base, same}, std::vector<bool>{true, true}, 0));
	puts("all roof geometry tests passed");
}
