// Tommy's feet on uneven ground, without the game: flat ground changes nothing,
// a kerb edge raises the upper foot, stepping off lowers his hips, stairs, drops
// out of reach are ignored, and it all eases in and fades out.
#include "footplantmodel.h"
#include <cmath>
#include <cstdio>
#include <limits>

using namespace footplantmodel;
static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)
#define NEAR(a, b, e) CHECK(std::fabs((a) - (b)) <= (e))

constexpr float kDt = 1.0f / 30;
// Settled output for both feet over the given ground (relative to a floor at 0).
static Output Settle(float groundL, float groundR, bool hitL = true, bool hitR = true, bool active = true)
{
	State s;
	Foot feet[2] = {{.1f, groundL, hitL}, {.1f, groundR, hitR}};
	Output o;
	for (int i = 0; i < 90; i++)
		o = Step(s, feet, 0, active, kDt);
	return o;
}

int main()
{
	{ // Flat ground: nothing changes.
		const Output o = Settle(0, 0);
		NEAR(o.drop, 0, 1e-5f);
		NEAR(o.raise[0], 0, 1e-5f);
		NEAR(o.raise[1], 0, 1e-5f);
	}
	{ // Left foot on a 15 cm kerb, right on the road he stands on: the left comes up.
		const Output o = Settle(.15f, 0);
		NEAR(o.drop, 0, 1e-3f);
		NEAR(o.raise[0], .15f, 1e-3f);
		NEAR(o.raise[1], 0, 1e-3f);
	}
	{ // Stood on the kerb, his left foot over the road 15 cm down: hips down, right foot back up to the kerb.
		const Output o = Settle(-.15f, 0);
		NEAR(o.drop, -.15f, 1e-3f);
		NEAR(o.raise[0], 0, 1e-3f);
		NEAR(o.raise[1], .15f, 1e-3f);
	}
	{ // Stairs going up under him: each foot to its own step.
		const Output o = Settle(.2f, .4f);
		NEAR(o.drop, 0, 1e-3f);
		NEAR(o.raise[0], .2f, 1e-3f);
		NEAR(o.raise[1], .4f, 1e-3f);
	}
	{ // Limits: hips at most 35 cm down, a foot at most 45 cm up.
		const Output o = Settle(-.5f, .2f);
		NEAR(o.drop, -kMaxDrop, 1e-3f);
		CHECK(o.raise[1] <= kMaxRaise + 1e-4f);
	}
	{ // A drop or a wall out of reach, or no ground found, is ignored.
		Output o = Settle(-1.5f, 0);
		NEAR(o.drop, 0, 1e-5f);
		NEAR(o.raise[0], 0, 1e-5f);
		o = Settle(.15f, 0, false, true);
		NEAR(o.raise[0], 0, 1e-5f);
	}
	{ // Eases in: a sudden kerb is followed within a quarter second, never overshot.
		State s;
		Foot flat[2] = {{.1f, 0, true}, {.1f, 0, true}}, kerb[2] = {{.1f, .15f, true}, {.1f, 0, true}};
		for (int i = 0; i < 30; i++)
			Step(s, flat, 0, true, kDt);
		Output first = Step(s, kerb, 0, true, kDt), o = first;
		float most = o.raise[0];
		for (int i = 1; i < 8; i++) {
			o = Step(s, kerb, 0, true, kDt);
			most = std::fmax(most, o.raise[0]);
		}
		CHECK(first.raise[0] < .1f);      // not a snap
		CHECK(o.raise[0] > .95f * .15f);  // there within 0.27 s
		CHECK(most <= .15f + 1e-5f);      // no overshoot
	}
	{ // Inactive (jumping, swimming, in a car): fades out within half a second.
		State s;
		Foot kerb[2] = {{.1f, .15f, true}, {.1f, 0, true}};
		for (int i = 0; i < 30; i++)
			Step(s, kerb, 0, true, kDt);
		Output o;
		for (int i = 0; i < 15; i++)
			o = Step(s, kerb, 0, false, kDt);
		CHECK(o.raise[0] < .01f);
	}
	{ // Bad numbers change nothing.
		State s;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		Foot feet[2] = {{.1f, nan, true}, {nan, 0, true}};
		Output o;
		for (int i = 0; i < 30; i++)
			o = Step(s, feet, 0, true, kDt);
		CHECK(std::isfinite(o.drop) && std::isfinite(o.raise[0]) && std::isfinite(o.raise[1]));
		NEAR(o.drop, 0, 1e-5f);
		o = Step(s, feet, nan, true, nan);
		CHECK(std::isfinite(o.drop) && std::isfinite(o.raise[0]));
	}
	{ // The leg: lengths kept, knee bent forward as animated, target reached.
		auto len = [](V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
		const V3 hip{0, 0, 1}, knee{0, .05f, .55f}, foot{0, 0, .1f}; // knee slightly forward (+y)
		V3 k, f;
		CHECK(Leg(hip, knee, foot, V3{0, 0, .25f}, k, f)); // the foot 15 cm up (a kerb)
		NEAR(len(hip, k), len(hip, knee), 1e-4f);
		NEAR(len(k, f), len(knee, foot), 1e-4f);
		NEAR(f.z, .25f, 1e-4f);
		NEAR(f.y, 0, 1e-4f);
		CHECK(k.y > .05f); // bends further forward, never backward
		// Beyond reach: the leg straightens toward it and stops.
		CHECK(Leg(hip, knee, foot, V3{0, 0, -1}, k, f));
		NEAR(len(hip, f), len(hip, knee) + len(knee, foot), 3e-3f);
		CHECK(f.z < .1f);
		// Bad input: nothing changes.
		const float nan = std::numeric_limits<float>::quiet_NaN();
		CHECK(!Leg(hip, knee, foot, V3{nan, 0, 0}, k, f));
		CHECK(!Leg(hip, hip, foot, V3{0, 0, .2f}, k, f)); // zero-length thigh
	}
	if (failures)
		return 1;
	std::printf("foot plant model: all passed\n");
	return 0;
}
