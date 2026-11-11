// Who looks at whom (#56), without the game: bystanders glance at people near
// them, stare at a drawn weapon, ignore what's behind them; Tommy's head follows
// the camera and settles on the person it's on.
#include "gazemodel.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace gazemodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define NEAR(a, b, e) CHECK(std::fabs((a) - (b)) <= (e))

constexpr float kPi = 3.14159265f, kDeg = kPi / 180, kDt = 1.0f / 30;
// A fixed stream of rolls, so a run is the same every time.
struct Dice {
	uint32_t s = 12345;
	float Next() { s = s * 1664525u + 1013904223u; return float(s >> 8) / 16777216.0f; }
};
Seen Tommy(float x, float y, bool armed = false, bool fast = false) { return Seen{0, Point{x, y, 1.6f}, true, armed, fast}; }
Seen Bystander(int id, float x, float y) { return Seen{id, Point{x, y, 1.6f}, false, false, false}; }
// How many glances in `seconds`, and their durations, for one looker at the origin facing +y.
struct Tally {
	int glances = 0;
	uint32_t shortest = ~0u, longest = 0;
	int target = -1;
};
Tally Watch(const Seen *seen, int count, float seconds, uint32_t seed = 12345)
{
	Dice dice{seed};
	Memory memory;
	Tally tally;
	uint32_t now = 1000;
	for (float t = 0; t < seconds; t += kDt, now += uint32_t(kDt * 1000)) {
		const Glance g = Choose(Point{0, 0, 1.6f}, 0, seen, count, memory, now, kDt, dice.Next(), dice.Next());
		if (!g)
			continue;
		++tally.glances;
		tally.target = g.target;
		if (g.ms < tally.shortest)
			tally.shortest = g.ms;
		if (g.ms > tally.longest)
			tally.longest = g.ms;
	}
	return tally;
}

int main()
{
	// The game's headings.
	NEAR(HeadingTo(Point{0, 0, 0}, Point{0, 5, 0}), 0, 1e-5f);
	NEAR(HeadingTo(Point{0, 0, 0}, Point{-5, 0, 0}), kPi / 2, 1e-5f);
	NEAR(HeadingTo(Point{0, 0, 0}, Point{5, 0, 0}), -kPi / 2, 1e-5f);
	NEAR(Wrap(3 * kPi / 2), -kPi / 2, 1e-5f);
	NEAR(Wrap(-3 * kPi / 2), kPi / 2, 1e-5f);

	// Nobody near: nothing to look at.
	CHECK(Watch(nullptr, 0, 30).glances == 0);

	// Tommy two metres in front: noticed within seconds, glances of 1.2-2.8 s,
	// with a pause between them (no more than one every ~5 s on average).
	{
		const Seen s[] = {Tommy(0, 2)};
		const Tally t = Watch(s, 1, 60);
		CHECK(t.glances >= 3 && t.glances <= 12);
		CHECK(t.target == 0);
		CHECK(t.shortest >= 1200 && t.longest <= 2800);
	}
	// Behind at six metres he isn't seen; behind at two metres he's sensed.
	{
		const Seen far[] = {Tommy(0, -6)}, near[] = {Tommy(0, -2)};
		CHECK(Rate(Point{0, 0, 1.6f}, 0, far[0]) == 0);
		CHECK(Rate(Point{0, 0, 1.6f}, 0, near[0]) > 0);
		CHECK(Watch(far, 1, 60).glances == 0);
	}
	// Out of range: never, however he's dressed.
	{
		const Seen s[] = {Tommy(0, 13, true, true)};
		CHECK(Rate(Point{0, 0, 1.6f}, 0, s[0]) == 0);
	}
	// Closer is more often; a drawn weapon is several times more, and a stare (3-6 s).
	{
		const Point eye{0, 0, 1.6f};
		CHECK(Rate(eye, 0, Tommy(0, 2)) > Rate(eye, 0, Tommy(0, 8)));
		CHECK(Rate(eye, 0, Tommy(0, 8, true)) >= 3 * Rate(eye, 0, Tommy(0, 8)));
		CHECK(Rate(eye, 0, Tommy(0, 8, false, true)) > Rate(eye, 0, Tommy(0, 8)));
		const Seen armed[] = {Tommy(0, 8, true)};
		const Tally t = Watch(armed, 1, 90);
		CHECK(t.glances >= 3);
		CHECK(t.shortest >= 3000 && t.longest <= 6000);
	}
	// Tommy is looked at before another bystander as close; a bystander alone
	// is glanced at, but only within six metres and less often than Tommy.
	{
		const Point eye{0, 0, 1.6f};
		const Seen both[] = {Bystander(7, 0.5f, 3), Tommy(-0.5f, 3)};
		CHECK(Watch(both, 2, 30).target == 0);
		CHECK(Rate(eye, 0, Bystander(7, 0, 3)) > 0 && Rate(eye, 0, Bystander(7, 0, 3)) < Rate(eye, 0, Tommy(0, 3)));
		CHECK(Rate(eye, 0, Bystander(7, 0, 7)) == 0);
	}
	// While a glance is under way, or during the pause after it, nothing new.
	{
		const Seen s[] = {Tommy(0, 2)};
		Memory memory;
		Glance first;
		uint32_t now = 1000;
		for (int i = 0; i < 600 && !first; i++, now += 33)
			first = Choose(Point{0, 0, 1.6f}, 0, s, 1, memory, now, kDt, 0.0f, 0.5f);
		CHECK(bool(first));
		CHECK(memory.target == 0 && memory.until >= now + first.ms - 100 && memory.next > memory.until);
		CHECK(!Choose(Point{0, 0, 1.6f}, 0, s, 1, memory, now + 33, kDt, 0.0f, 0.5f));
		CHECK(!Choose(Point{0, 0, 1.6f}, 0, s, 1, memory, memory.next - 1, kDt, 0.0f, 0.5f));
		CHECK(bool(Choose(Point{0, 0, 1.6f}, 0, s, 1, memory, memory.next, kDt, 0.0f, 0.5f)));
	}
	// Bad input looks at nobody.
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const Seen s[] = {Seen{0, Point{nan, 2, 1.6f}, true, false, false}};
		CHECK(Watch(s, 1, 10).glances == 0);
		CHECK(Rate(Point{0, 0, 1.6f}, nan, Tommy(0, 2)) == 0);
	}

	// Tommy: his head follows the camera within 100 degrees of where he faces.
	{
		const Point eye{0, 0, 1.6f};
		Follow f = Track(eye, 0, 60 * kDeg, nullptr, 0, -1);
		CHECK(f.look && f.target < 0);
		NEAR(f.heading, 60 * kDeg, 1e-5f);
		CHECK(!Track(eye, 0, 150 * kDeg, nullptr, 0, -1).look); // camera behind him: he faces forward
		// Someone 5 m out, 10 degrees off the camera's line: he looks at them.
		const float a = 40 * kDeg, off = 10 * kDeg, r = 5;
		const Seen s[] = {Bystander(3, -std::sin(a + off) * r, std::cos(a + off) * r)};
		f = Track(eye, 0, a, s, 1, -1);
		CHECK(f.look && f.target == 3);
		// Kept at 25 degrees off once he's on them, dropped at 35.
		const Seen drift[] = {Bystander(3, -std::sin(a + 25 * kDeg) * r, std::cos(a + 25 * kDeg) * r)};
		CHECK(Track(eye, 0, a, drift, 1, 3).target == 3);
		CHECK(Track(eye, 0, a, drift, 1, -1).target == -1);
		const Seen gone[] = {Bystander(3, -std::sin(a + 35 * kDeg) * r, std::cos(a + 35 * kDeg) * r)};
		CHECK(Track(eye, 0, a, gone, 1, 3).target == -1);
		// Too far along the line: just the camera's heading.
		const Seen far[] = {Bystander(3, -std::sin(a) * 10, std::cos(a) * 10)};
		f = Track(eye, 0, a, far, 1, -1);
		CHECK(f.look && f.target == -1);
		// Someone on the camera's line but behind his shoulder is not turned to.
		const Seen behind[] = {Bystander(3, -std::sin(150 * kDeg) * 3, std::cos(150 * kDeg) * 3)};
		CHECK(!Track(eye, 0, 150 * kDeg, behind, 1, -1).look);
		CHECK(!Track(eye, std::numeric_limits<float>::quiet_NaN(), a, nullptr, 0, -1).look);
	}

	if (failures)
		return 1;
	std::printf("gaze model: all passed\n");
	return 0;
}
