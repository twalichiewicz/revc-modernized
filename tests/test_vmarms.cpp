// Unit tests for cutting Tommy's arms out of his mesh and articulating his fingers: make test
#include "vmarms.h"
#include "vmtestarm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace vm;

static float WeightOn(const SkinVertex &v, int bone)
{
	float w = 0.0f;
	for (int k = 0; k < 4; k++)
		if (v.bone[k] == bone)
			w += v.weight[k];
	return w;
}
static float WeightSum(const SkinVertex &v) { return v.weight[0] + v.weight[1] + v.weight[2] + v.weight[3]; }
static bool OnFingers(const SkinVertex &v)
{
	float w = 0.0f;
	for (int b = kIndex; b <= kPinkyTip; b++)
		w += WeightOn(v, b);
	return w > 0.5f;
}
static Vec3 Local(const Arm &arm, Vec3 skin) { return Apply(Inverse(arm.bind[kHand]), skin); }

// The finger pieces: triangles all on finger bones, grouped by shared vertices.
static std::vector<std::vector<int>> Fingers(const Arm &arm)
{
	std::vector<int> tris;
	for (size_t t = 0; t < arm.triangles.size(); t++) {
		const Triangle &tr = arm.triangles[t];
		if (OnFingers(arm.vertices[tr.v[0]]) && OnFingers(arm.vertices[tr.v[1]]) && OnFingers(arm.vertices[tr.v[2]]))
			tris.push_back(int(t));
	}
	// Union-find over vertices, welded by position (caps reuse the positions of the sides).
	std::map<std::tuple<long, long, long>, int> weld;
	auto key = [&](uint16_t i) {
		const Vec3 p = arm.vertices[i].pos;
		const auto k = std::make_tuple(std::lround(p.x * 1e5), std::lround(p.y * 1e5), std::lround(p.z * 1e5));
		auto it = weld.find(k);
		if (it != weld.end())
			return it->second;
		const int id = int(weld.size());
		weld[k] = id;
		return id;
	};
	std::vector<int> parent;
	auto find = [&](int a) {
		while (parent[a] != a)
			a = parent[a] = parent[parent[a]];
		return a;
	};
	std::vector<std::array<int, 3>> ids;
	for (int t : tris) {
		std::array<int, 3> id{};
		for (int c = 0; c < 3; c++) {
			id[c] = key(arm.triangles[t].v[c]);
			while (int(parent.size()) <= id[c])
				parent.push_back(int(parent.size()));
		}
		ids.push_back(id);
		parent[find(id[0])] = find(id[1]);
		parent[find(id[1])] = find(id[2]);
	}
	std::map<int, std::vector<int>> groups;
	for (size_t i = 0; i < tris.size(); i++)
		groups[find(ids[i][0])].push_back(tris[i]);
	std::vector<std::vector<int>> out;
	for (auto &g : groups)
		out.push_back(g.second);
	return out;
}

static float MeanLocalZ(const Arm &arm, const std::vector<int> &tris)
{
	float z = 0.0f;
	int n = 0;
	for (int t : tris)
		for (uint16_t v : arm.triangles[t].v) {
			z += Local(arm, arm.vertices[v].pos).z;
			n++;
		}
	return n ? z / float(n) : 0.0f;
}

static void TestKeepsOnlyHisArm()
{
	const Arms arms = Articulate(vmtest::Body());
	CHECK(arms.ok);
	for (const Arm *arm : {&arms.right, &arms.left})
		for (const SkinVertex &v : arm->vertices) {
			const bool inTorso = v.pos.x > -0.16f && v.pos.x < 0.16f && v.pos.y > 0.19f && v.pos.y < 0.61f;
			CHECK(!inTorso);
			CHECK(std::fabs(WeightSum(v) - 1.0f) < 1e-5f);
		}
	CHECK(arms.right.triangles.size() > 4 * 12);
}

static void TestFourSeparateFingers()
{
	const Arms arms = Articulate(vmtest::Body());
	const auto fingers = Fingers(arms.right);
	CHECK(fingers.size() == 4);
	// From the thumb's side (+z on the right hand): index, middle, ring, pinky.
	std::vector<std::pair<float, int>> byZ;
	for (size_t i = 0; i < fingers.size(); i++)
		byZ.push_back({-MeanLocalZ(arms.right, fingers[i]), int(i)});
	std::sort(byZ.begin(), byZ.end());
	for (int d = 0; d < 4 && d < int(byZ.size()); d++) {
		const auto &tris = fingers[byZ[d].second];
		const SkinVertex &v = arms.right.vertices[arms.right.triangles[tris[0]].v[0]];
		CHECK(WeightOn(v, kIndex + 2 * d) + WeightOn(v, kIndex + 2 * d + 1) > 0.99f);
	}
}

static void TestCutsAreCapped()
{
	const Arms arms = Articulate(vmtest::Body());
	for (const auto &tris : Fingers(arms.right)) {
		std::map<std::pair<std::tuple<long, long, long>, std::tuple<long, long, long>>, int> uses;
		auto key = [&](uint16_t i) {
			const Vec3 p = arms.right.vertices[i].pos;
			return std::make_tuple(std::lround(p.x * 1e5), std::lround(p.y * 1e5), std::lround(p.z * 1e5));
		};
		for (int t : tris)
			for (int c = 0; c < 3; c++) {
				auto a = key(arms.right.triangles[t].v[c]), b = key(arms.right.triangles[t].v[(c + 1) % 3]);
				if (b < a)
					std::swap(a, b);
				uses[{a, b}]++;
			}
		int open = 0;
		for (auto &e : uses)
			open += e.second != 2;
		CHECK(open == 0);
	}
}

static void TestFingerWeights()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	for (const auto &tris : Fingers(arm)) {
		std::set<uint16_t> verts;
		for (int t : tris)
			for (uint16_t v : arm.triangles[t].v)
				verts.insert(v);
		int base = -1;
		for (int b = kIndex; b <= kPinky; b += 2)
			if (WeightOn(arm.vertices[*verts.begin()], b) + WeightOn(arm.vertices[*verts.begin()], b + 1) > 0.99f)
				base = b;
		CHECK(base >= 0);
		if (base < 0)
			continue;
		// Only its own two bones; the vertex furthest round the hook (lowest local x on the tip's side) on the tip.
		uint16_t far = *verts.begin();
		for (uint16_t v : verts) {
			CHECK(std::fabs(WeightOn(arm.vertices[v], base) + WeightOn(arm.vertices[v], base + 1) - 1.0f) < 1e-5f);
			if (Local(arm, arm.vertices[v].pos).x < Local(arm, arm.vertices[far].pos).x)
				far = v;
		}
		CHECK(WeightOn(arm.vertices[far], base + 1) > 0.99f);
	}
}

static void TestThumbWeights()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	int tip = 0, root = 0;
	for (const SkinVertex &v : arm.vertices) {
		const Vec3 p = Local(arm, v.pos);
		const bool thumb = p.y > 0.034f && p.y < 0.076f && p.z > 0.019f && p.z < 0.051f;
		if (thumb && std::fabs(p.x - 0.09f) < 1e-4f) {
			CHECK(WeightOn(v, kThumbTip) > 0.99f);
			tip++;
		}
		if (thumb && std::fabs(p.x - 0.015f) < 1e-4f) {
			CHECK(WeightOn(v, kHand) > 0.99f);
			root++;
		}
	}
	CHECK(tip == 4 && root == 4);
}

static void TestOtherBonesFoldIntoUpperArm()
{
	const Arms arms = Articulate(vmtest::Body());
	int folded = 0;
	for (const SkinVertex &v : arms.right.vertices)
		if (WeightOn(v, kUpper) > 0.0f) {
			CHECK(WeightOn(v, kUpper) > 0.99999f);
			folded++;
		}
	CHECK(folded == 8);
}

static void TestBothHandsMirror()
{
	const Arms arms = Articulate(vmtest::Body());
	CHECK(arms.left.vertices.size() == arms.right.vertices.size());
	CHECK(arms.left.triangles.size() == arms.right.triangles.size());
	const auto fingers = Fingers(arms.left);
	CHECK(fingers.size() == 4);
	for (const auto &tris : fingers) {
		const SkinVertex &v = arms.left.vertices[arms.left.triangles[tris[0]].v[0]];
		if (WeightOn(v, kIndex) + WeightOn(v, kIndexTip) > 0.99f)
			CHECK(MeanLocalZ(arms.left, tris) < 0.0f);
	}
}

static void TestPermutedBindAxes()
{
	const Frame turn = Compose(TurnAbout({1, 0, 0}, 1.5707963f, {0, 0, 0}), TurnAbout({0, 1, 0}, 1.5707963f, {0, 0, 0}));
	const Arms a = Articulate(vmtest::Body()), b = Articulate(vmtest::Body(turn));
	CHECK(a.ok && b.ok);
	CHECK(a.right.vertices.size() == b.right.vertices.size());
	for (size_t i = 0; i < a.right.vertices.size() && i < b.right.vertices.size(); i++)
		for (int bone = 0; bone < kArmBones; bone++)
			CHECK(std::fabs(WeightOn(a.right.vertices[i], bone) - WeightOn(b.right.vertices[i], bone)) < 1e-4f);
	for (int d = 0; d < kDigits; d++)
		CHECK(Length(Sub(Apply(turn, a.right.tip[d]), b.right.tip[d])) < 1e-4f);
}

static void TestNoSlabIsRefused()
{
	const Arms arms = Articulate(vmtest::Body(Identity(), false));
	CHECK(!arms.ok);
	CHECK(arms.why.find("right") != std::string::npos);
	CHECK(arms.why.find("finger") != std::string::npos);
}

// A hooked finger's cut is a U: its caps must fill the U's own outline, not
// the hook's hollow (a fan from the outline's middle would stretch a skin
// membrane across it).
static float FromHook(Vec3 p)
{
	float best = 1e9f;
	for (int i = 0; i + 1 < vmtest::kSlabSteps; i++) {
		const Vec3 a = vmtest::HookPoint(i, 0), b = vmtest::HookPoint(i + 1, 0), q{p.x, p.y, 0};
		const Vec3 ab = Sub(b, a);
		const float t = std::clamp(Dot(Sub(q, a), ab) / Dot(ab, ab), 0.0f, 1.0f);
		best = std::min(best, Length(Sub(q, Add(a, Mul(ab, t)))));
	}
	return best;
}

static void TestCapsStayInsideTheFinger()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	int caps = 0;
	for (const Triangle &t : arm.triangles) {
		bool cap = true;
		Vec3 centre{0, 0, 0};
		for (uint16_t v : t.v) {
			const Vec3 n = Rotate(Inverse(arm.bind[kHand]), arm.vertices[v].normal);
			cap = cap && std::fabs(std::fabs(n.z) - 1.0f) < 1e-4f;
			centre = Add(centre, Mul(Local(arm, arm.vertices[v].pos), 1.0f / 3.0f));
		}
		if (!cap)
			continue;
		caps++;
		CHECK(FromHook(centre) < 0.0035f);
	}
	CHECK(caps > 0);
}

// Each outfit's texture is its own: a cap wears the skin of the finger it
// closes (its cut edge's own UVs), not a fixed spot that's skin on one.
static void TestCapsWearTheFingersSkin()
{
	const Arms arms = Articulate(vmtest::Body());
	const Arm &arm = arms.right;
	for (const Triangle &t : arm.triangles) {
		bool cap = true;
		for (uint16_t v : t.v)
			cap = cap && std::fabs(std::fabs(Rotate(Inverse(arm.bind[kHand]), arm.vertices[v].normal).z) - 1.0f) < 1e-4f;
		if (!cap)
			continue;
		for (uint16_t v : t.v) // the fixture's slab UVs: u 0.5..0.75, v 0.6..0.74
			CHECK(arm.vertices[v].u >= 0.499f && arm.vertices[v].u <= 0.751f && arm.vertices[v].v >= 0.599f &&
			      arm.vertices[v].v <= 0.741f);
	}
}

int main()
{
	TestKeepsOnlyHisArm();
	TestFourSeparateFingers();
	TestCutsAreCapped();
	TestFingerWeights();
	TestThumbWeights();
	TestOtherBonesFoldIntoUpperArm();
	TestBothHandsMirror();
	TestPermutedBindAxes();
	TestNoSlabIsRefused();
	TestCapsStayInsideTheFinger();
	TestCapsWearTheFingersSkin();
	if (gFailures) {
		printf("%d vmarms test(s) failed\n", gFailures);
		return 1;
	}
	printf("all vmarms tests passed\n");
	return 0;
}
