// Unit tests for the viewmodel's framing: where the gun is in the view, and its own field of view: make test
#include "vmview.h"

#include <cmath>
#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

static Gun Colt()
{
	Gun g;
	g.scale = 1.09f;
	g.sightRear = {0, 0.026f * g.scale, 0.134f * g.scale};
	g.sightFront = {0, 0.2035f * g.scale, 0.136f * g.scale};
	return g;
}

static bool Near(Vec3 a, Vec3 b, float eps) { return Length(Sub(a, b)) < eps; }

static void TestHipEyeSitsWhereFramed()
{
	const Gun colt = Colt();
	const Frame view = GunInView(colt, kColtFraming, 0.0f);
	CHECK(Near(Apply(Inverse(view), {0, 0, 0}), Mul(kColtFraming.hipEye, colt.scale), 1e-5f));
	// Looking 13 degrees right of the barrel: the barrel points 13 degrees left in the view.
	const Vec3 barrel = Rotate(view, {0, 1, 0});
	CHECK(std::fabs(std::atan2(barrel.x, barrel.y) * 180.0f / 3.14159265f + 13.0f) < 0.2f);
}

static void TestAimPutsSightsOnTheLine()
{
	const Gun colt = Colt();
	const Frame view = GunInView(colt, kColtFraming, 1.0f);
	const Vec3 rear = Apply(view, colt.sightRear), front = Apply(view, colt.sightFront);
	CHECK(std::fabs(rear.x) < 1e-4f && std::fabs(front.x) < 1e-4f);
	CHECK(std::fabs(rear.z + kColtFraming.aimAbove * colt.scale) < 1e-4f);
	CHECK(std::fabs(front.z + kColtFraming.aimAbove * colt.scale) < 1e-4f);
	CHECK(rear.y > 0.0f && front.y > rear.y);
	// Half way, half way between.
	const Vec3 mid = Apply(Inverse(GunInView(colt, kColtFraming, 0.5f)), {0, 0, 0});
	const Vec3 hip = Apply(Inverse(GunInView(colt, kColtFraming, 0.0f)), {0, 0, 0});
	const Vec3 aim = Apply(Inverse(view), {0, 0, 0});
	CHECK(Near(mid, Lerp(hip, aim, 0.5f), 1e-5f));
}

static void TestSquashMatchesFov()
{
	const float d = 3.14159265f / 180.0f;
	const float squash = FovSquash(std::tan(55.4f / 2.0f * d), 62.0f);
	const Vec3 p = ToWorld(Identity(), {0, 1, std::tan(31.0f * d)}, squash);
	CHECK(std::fabs(p.z / p.y - std::tan(27.7f * d)) < 1e-4f);
	const Vec3 q = ToWorld(Identity(), {std::tan(31.0f * d), 1, 0}, squash);
	CHECK(std::fabs(q.x / q.y - std::tan(27.7f * d)) < 1e-4f);
}

static void TestPushOutKeepsTheImage()
{
	const Frame view = Look({10, 20, 30}, Normal({0.3f, 1, -0.2f}), {0, 0, 1});
	const Vec3 p{0.12f, 0.35f, -0.08f};
	const Vec3 near = Apply(view, p), far = ToWorld(view, p, 1.0f);
	CHECK(Near(Normal(Sub(near, view.pos)), Normal(Sub(far, view.pos)), 1e-5f));
	CHECK(std::fabs(Length(Sub(far, view.pos)) - kPushOut * Length(p)) < 1e-4f);
}

int main()
{
	TestHipEyeSitsWhereFramed();
	TestAimPutsSightsOnTheLine();
	TestSquashMatchesFov();
	TestPushOutKeepsTheImage();
	if (gFailures) {
		printf("%d vmview test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmview tests passed\n");
	return 0;
}
