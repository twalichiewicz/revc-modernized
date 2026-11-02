#include "reticlemodel.h"
#include "fpcross.h"
#include "inifile.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
	using namespace reticlemodel;
	Settings s;
	const Shape base = Build(s, 960, 540, 1080, 0, 0.6f);
	assert(base.count == 4);
	fpcross::Rect old[4];
	fpcross::Lines(960, 540, 4, 1080, old);
	for (int i = 0; i < 4; i++)
		assert(base.rects[i].x == old[i].x && base.rects[i].y == old[i].y && base.rects[i].w == old[i].w &&
		       base.rects[i].h == old[i].h);
	const Shape spread = Build(s, 960, 540, 1080, 0.02f, 0.6f);
	assert(spread.rects[0].y < base.rects[0].y);
	s.dynamic = false;
	assert(Build(s, 960, 540, 1080, 0.02f, 0.6f).rects[0].y == base.rects[0].y);
	s.size = 2;
	s.thickness = 1.5f;
	s.dot = true;
	const Shape large = Build(s, 960, 540, 1080, 0, 0.6f);
	assert(large.count == 5 && large.rects[0].h == 18 && large.rects[0].w == 3);
	s.style = 1;
	assert(Build(s, 960, 540, 1080, 0, 0.6f).count == 1);
	s.style = 2;
	s.dot = false;
	assert(Build(s, 960, 540, 1080, 0, 0.6f).count == 3);
	s = {99, -9, NAN, INFINITY, true, true};
	s = Sanitize(s);
	assert(s.style == 2 && s.color == 0 && s.size == 1 && s.thickness == 1);
	s = {};
	Change(s, Row::kSize, -1);
	assert(s.size < 1);
	Change(s, Row::kDefaults, 1);
	assert(s.size == 1);
	Change(s, Row::kColor, -1);
	assert(s.color == kColors - 1);
	std::string ini = "[Crosshair]\nSize=2 ; comment\n[Other]\nKeep=1\n";
	for (const auto &v : Values(s))
		ini = inifile::WithValue(ini, "Crosshair", v.key, v.value);
	assert(ini.find("Size=1.00 ; comment") != std::string::npos && ini.find("Keep=1") != std::string::npos);
	assert(ini.find("DynamicSpread=1") != std::string::npos);
	puts("all reticle model tests passed");
}
