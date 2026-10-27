// Unit tests for the viewmodel's frames and CPU skinning: make test
#include "vmskin.h"

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

static bool Near(Vec3 a, Vec3 b, float eps = 1e-5f) { return Length(Sub(a, b)) < eps; }
static bool Near(const Frame &a, const Frame &b, float eps = 1e-5f)
{
	return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps) && Near(a.pos, b.pos, eps);
}

static Frame Scaled(const Frame &f, float s) { return {Mul(f.x, s), Mul(f.y, s), Mul(f.z, s), f.pos}; }

static void TestInverseUndoesCompose()
{
	Frame f = Scaled(TurnAbout(Normal({1, 2, 3}), 30.0f * 3.14159265f / 180.0f, {0, 0, 0}), 1.2f);
	f.pos = {4, 5, 6};
	CHECK(Near(Compose(f, Inverse(f)), Identity()));
	CHECK(Near(Compose(Inverse(f), f), Identity()));
}

static void TestRwLayoutRoundTrips()
{
	Frame f = TurnAbout(Normal({0, 1, 1}), 0.7f, {1, 2, 3});
	float m[16];
	ToRw(f, m);
	CHECK(Near(FromRw(m), f));
	CHECK(m[12] == f.pos.x && m[13] == f.pos.y && m[14] == f.pos.z);
	CHECK(m[0] == f.x.x && m[4] == f.y.x && m[8] == f.z.x);
}

static void TestSwingTakesDirection()
{
	const Vec3 a{1, 0.2f, -0.3f}, b{-0.4f, 1, 0.5f}, p{0.3f, 0.6f, 0.9f};
	const Frame s = Swing(a, b, p);
	CHECK(Near(Rotate(s, Normal(a)), Normal(b)));
	CHECK(Near(Apply(s, p), p));
	CHECK(Near(Swing(a, a, p), Identity()));
}

static SkinVertex Vertex(Vec3 p, Vec3 n, uint8_t b0, float w0, uint8_t b1 = 0, float w1 = 0.0f)
{
	SkinVertex v{};
	v.pos = p;
	v.normal = n;
	v.bone[0] = b0;
	v.weight[0] = w0;
	v.bone[1] = b1;
	v.weight[1] = w1;
	return v;
}

static void TestRestPoseIsIdentity()
{
	const std::vector<SkinVertex> in = {Vertex({1, 2, 3}, {0, 0, 1}, 0, 1.0f), Vertex({-1, 0, 2}, {1, 0, 0}, 1, 0.5f, 0, 0.5f)};
	const Frame m[2] = {Identity(), Identity()};
	std::vector<Vec3> pos, normal;
	Skin(in, m, pos, normal);
	CHECK(pos.size() == 2 && normal.size() == 2);
	CHECK(Near(pos[0], {1, 2, 3}) && Near(pos[1], {-1, 0, 2}));
	CHECK(Near(normal[0], {0, 0, 1}) && Near(normal[1], {1, 0, 0}));
}

static void TestWeightsBlend()
{
	const std::vector<SkinVertex> in = {Vertex({0, 0, 0}, {0, 0, 1}, 0, 0.5f, 1, 0.5f)};
	Frame m[2] = {Identity(), Identity()};
	m[0].pos = {0, 0, 1};
	m[1].pos = {0, 0, 3};
	std::vector<Vec3> pos, normal;
	Skin(in, m, pos, normal);
	CHECK(Near(pos[0], {0, 0, 2}));
	CHECK(Near(normal[0], {0, 0, 1}));
}

static void TestLookFrame()
{
	const Vec3 eye{5, 6, 7};
	const Frame f = Look(eye, {0, 1, 0}, {0, 0, 1});
	CHECK(Near(Apply(f, {1, 0, 0}), Add(eye, {1, 0, 0})));
	CHECK(Near(Apply(f, {0, 1, 0}), Add(eye, {0, 1, 0})));
	CHECK(Near(Apply(f, {0, 0, 1}), Add(eye, {0, 0, 1})));
	const Frame g = Look({0, 0, 0}, {1, 0, 0}, {0, 0, 1}); // looking along +x: right is -y
	CHECK(Near(Apply(g, {1, 0, 0}), {0, -1, 0}));
}

// A blend of two frames is each at its ends, and a proper frame between.
static void TestBlendEnds()
{
	Frame a = TurnAbout(Normal({0, 0, 1}), 0.3f, {0, 0, 0}), b = TurnAbout(Normal({1, 0, 0}), 1.2f, {0, 0, 0});
	a.pos = {1, 2, 3};
	b.pos = {-1, 0, 5};
	CHECK(Near(Blend(a, b, 0.0f), a, 1e-5f) && Near(Blend(a, b, 1.0f), b, 1e-5f));
	const Frame m = Blend(a, b, 0.5f);
	CHECK(std::fabs(Length(m.x) - 1.0f) < 1e-4f && std::fabs(Dot(m.x, m.y)) < 1e-4f && Near(Cross(m.x, m.y), m.z, 1e-4f));
	CHECK(Near(m.pos, {0, 1, 4}, 1e-5f));
}

int main()
{
	TestBlendEnds();
	TestInverseUndoesCompose();
	TestRwLayoutRoundTrips();
	TestSwingTakesDirection();
	TestRestPoseIsIdentity();
	TestWeightsBlend();
	TestLookFrame();
	if (gFailures) {
		printf("%d vmskin test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmskin tests passed\n");
	return 0;
}
