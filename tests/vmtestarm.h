#pragma once
// A synthetic skinned body for the viewmodel's tests: both arms laid out as
// Tommy's are in his hand bones' own frames (x along the hand, +y the palm
// side, z across, the knuckles at x 0.088; the thumb at +z on the right hand
// and -z on the left), boxes for the upper arm, forearm, palm and thumb, a
// hooked finger slab like player.dff's (a closed strip from the knuckles down
// and back under), and a torso box that isn't his arm.
#include "vmarms.h"

#include <cmath>
#include <initializer_list>

namespace vmtest {

using namespace vm;

constexpr int kSpine = 3, kRightClavicle = 21, kLeftClavicle = 31;
constexpr int kSlabSteps = 6, kSlabAcross = 8; // along the hook, across the hand (cuts fall between)
constexpr float kSlabZ = 0.045f;

struct Builder {
	SkinnedMesh mesh;
	Frame toSkin = Identity(); // a hand's local frame into skin space, while building it

	int Bone(int tag)
	{
		for (size_t i = 0; i < mesh.tags.size(); i++)
			if (mesh.tags[i] == tag)
				return int(i);
		return -1;
	}
	void AddBone(int tag, const Frame &bindInSkin)
	{
		mesh.tags.push_back(tag);
		mesh.bind.push_back(bindInSkin);
	}
	uint16_t Vertex(Vec3 local, Vec3 normal, int tag, float u = 0.5f, float v = 0.5f, int tag2 = -1, float w2 = 0.0f)
	{
		SkinVertex s{};
		s.pos = Apply(toSkin, local);
		s.normal = Normal(Rotate(toSkin, normal));
		s.u = u;
		s.v = v;
		s.bone[0] = uint8_t(Bone(tag));
		s.weight[0] = 1.0f - w2;
		if (tag2 >= 0) {
			s.bone[1] = uint8_t(Bone(tag2));
			s.weight[1] = w2;
		}
		mesh.vertices.push_back(s);
		return uint16_t(mesh.vertices.size() - 1);
	}
	void Tri(uint16_t a, uint16_t b, uint16_t c) { mesh.triangles.push_back({{a, b, c}, 0}); }
	void Quad(uint16_t a, uint16_t b, uint16_t c, uint16_t d)
	{
		Tri(a, b, c);
		Tri(a, c, d);
	}
	// An axis-aligned box on one bone (in the hand's local frame), its faces outward.
	void Box(Vec3 lo, Vec3 hi, int tag)
	{
		uint16_t v[8];
		for (int i = 0; i < 8; i++) {
			const Vec3 p{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
			const Vec3 mid = Mul(Add(lo, hi), 0.5f);
			v[i] = Vertex(p, Normal(Sub(p, mid)), tag);
		}
		Quad(v[0], v[2], v[3], v[1]); // -z
		Quad(v[4], v[5], v[7], v[6]); // +z
		Quad(v[0], v[1], v[5], v[4]); // -y
		Quad(v[2], v[6], v[7], v[3]); // +y
		Quad(v[0], v[4], v[6], v[2]); // -x
		Quad(v[1], v[3], v[7], v[5]); // +x
	}
};

// The hook's centreline (x, y) in the hand's frame, from the knuckles' row down and back under.
inline Vec3 HookPoint(int i, float z)
{
	const float t = float(i) / float(kSlabSteps - 1);
	// knuckle side (0.105, 0.040) -> corner (0.110, 0.070) -> tip (0.058, 0.076)
	const float x = t < 0.5f ? 0.105f + 0.010f * (t / 0.5f) : 0.110f - 0.104f * (t - 0.5f);
	const float y = t < 0.5f ? 0.040f + 0.060f * t : 0.070f + 0.012f * (t - 0.5f);
	return {x, y, z};
}

// One arm. `thumbSide` is +1 for the right hand (thumb at +z), -1 for the left.
inline void AddArm(Builder &b, const Frame &handBind, int base, float thumbSide, bool withSlab = true)
{
	const int upper = base, fore = base + 1, hand = base + 2, fingers = base + 3, clavicle = base - 1;
	b.AddBone(clavicle, Compose(handBind, Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-0.60f, 0, 0}}));
	b.AddBone(upper, Compose(handBind, Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-0.55f, 0, 0}}));
	b.AddBone(fore, Compose(handBind, Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-0.26f, 0, 0}}));
	b.AddBone(hand, handBind);
	b.AddBone(fingers, Compose(handBind, Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0.088f, 0, 0}}));
	b.toSkin = handBind;
	b.Box({-0.55f, -0.04f, -0.04f}, {-0.27f, 0.04f, 0.04f}, upper);
	// One of the upper arm's corners hung partly on the clavicle.
	SkinVertex &corner = b.mesh.vertices[b.mesh.vertices.size() - 8];
	corner.bone[1] = uint8_t(b.Bone(clavicle));
	corner.weight[0] = 0.7f;
	corner.weight[1] = 0.3f;
	b.Box({-0.26f, -0.035f, -0.035f}, {-0.01f, 0.035f, 0.035f}, fore);
	b.Box({0.0f, -0.017f, -0.045f}, {0.088f, 0.03f, 0.045f}, hand);
	const float tz0 = thumbSide > 0 ? 0.02f : -0.05f, tz1 = thumbSide > 0 ? 0.05f : -0.02f;
	b.Box({0.015f, 0.04f, tz0}, {0.09f, 0.075f, tz1}, hand); // the thumb
	if (!withSlab)
		return;
	// The slab: a closed strip, its two layers 6 mm apart along the hook's normal.
	uint16_t top[kSlabSteps][kSlabAcross], bottom[kSlabSteps][kSlabAcross];
	for (int i = 0; i < kSlabSteps; i++) {
		const Vec3 a = HookPoint(i > 0 ? i - 1 : 0, 0), c = HookPoint(i < kSlabSteps - 1 ? i + 1 : i, 0);
		const Vec3 along = Normal(Sub(c, a)), out = Normal(Cross(along, {0, 0, 1}));
		for (int k = 0; k < kSlabAcross; k++) {
			const float z = -kSlabZ + 2.0f * kSlabZ * float(k) / float(kSlabAcross - 1);
			const Vec3 p = HookPoint(i, z);
			const float u = 0.5f + 0.05f * float(i), v = 0.6f + 0.02f * float(k); // not where a cap of plain skin would be
			top[i][k] = b.Vertex(Add(p, Mul(out, 0.003f)), out, fingers, u, v);
			bottom[i][k] = b.Vertex(Sub(p, Mul(out, 0.003f)), Mul(out, -1), fingers, u, v);
		}
	}
	for (int i = 0; i + 1 < kSlabSteps; i++)
		for (int k = 0; k + 1 < kSlabAcross; k++) {
			b.Quad(top[i][k], top[i + 1][k], top[i + 1][k + 1], top[i][k + 1]);
			b.Quad(bottom[i][k], bottom[i][k + 1], bottom[i + 1][k + 1], bottom[i + 1][k]);
		}
	for (int i = 0; i + 1 < kSlabSteps; i++) { // the sides across the hand's edges
		b.Quad(top[i][0], bottom[i][0], bottom[i + 1][0], top[i + 1][0]);
		const int e = kSlabAcross - 1;
		b.Quad(top[i][e], top[i + 1][e], bottom[i + 1][e], bottom[i][e]);
	}
	for (int k = 0; k + 1 < kSlabAcross; k++) { // the ends: at the knuckles, and the fingertips
		b.Quad(top[0][k], top[0][k + 1], bottom[0][k + 1], bottom[0][k]);
		const int e = kSlabSteps - 1;
		b.Quad(top[e][k], bottom[e][k], bottom[e][k + 1], top[e][k + 1]);
	}
	// Joined to the palm at the knuckles (as his are, by triangles over both bones).
	const uint16_t palm0 = b.Vertex({0.088f, 0.03f, -kSlabZ}, {1, 0, 0}, hand), palm1 = b.Vertex({0.088f, 0.03f, kSlabZ}, {1, 0, 0}, hand);
	b.Tri(palm0, bottom[0][0], bottom[0][kSlabAcross - 1]);
	b.Tri(palm0, bottom[0][kSlabAcross - 1], palm1);
}

// The right hand's bind frame as player.dff has it (x along -x, y down, z across); the left mirrored.
inline Frame RightHand() { return {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {-0.76f, 0.537f, -0.074f}}; }
inline Frame LeftHand() { return {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}, {0.76f, 0.537f, -0.074f}}; }

// The whole body: a torso and both arms, optionally all of it turned (as player6.dff's bind axes are).
inline SkinnedMesh Body(const Frame &turn = Identity(), bool rightSlab = true)
{
	Builder b;
	b.AddBone(kSpine, Compose(turn, Frame{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0.4f, 0}}));
	b.toSkin = turn;
	b.Box({-0.15f, 0.2f, -0.1f}, {0.15f, 0.6f, 0.1f}, kSpine);
	AddArm(b, Compose(turn, RightHand()), 22, 1.0f, rightSlab);
	AddArm(b, Compose(turn, LeftHand()), 32, -1.0f);
	return b.mesh;
}

} // namespace vmtest
