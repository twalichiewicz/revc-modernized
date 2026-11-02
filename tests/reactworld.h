#pragma once
#include "../src/reactcontact.h"
#include "../src/reactrig.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
namespace reactfixture {
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
Vec3 Unit(Vec3 a) { return Mul(a, 1 / Length(a)); }
struct Triangle {
	Vec3 a, b, c;
};
// Closest point to a finite triangle (face, edge and vertex regions). The
// fixture supplies geometry, never a preselected normal/contact to the adapter.
Vec3 Closest(Vec3 p, const Triangle &t)
{
	Vec3 ab = Sub(t.b, t.a), ac = Sub(t.c, t.a), ap = Sub(p, t.a);
	float d1 = Dot(ab, ap), d2 = Dot(ac, ap);
	if (d1 <= 0 && d2 <= 0)
		return t.a;
	Vec3 bp = Sub(p, t.b);
	float d3 = Dot(ab, bp), d4 = Dot(ac, bp);
	if (d3 >= 0 && d4 <= d3)
		return t.b;
	float vc = d1 * d4 - d3 * d2;
	if (vc <= 0 && d1 >= 0 && d3 <= 0)
		return Add(t.a, Mul(ab, d1 / (d1 - d3)));
	Vec3 cp = Sub(p, t.c);
	float d5 = Dot(ab, cp), d6 = Dot(ac, cp);
	if (d6 >= 0 && d5 <= d6)
		return t.c;
	float vb = d5 * d2 - d1 * d6;
	if (vb <= 0 && d2 >= 0 && d6 <= 0)
		return Add(t.a, Mul(ac, d2 / (d2 - d6)));
	float va = d3 * d6 - d5 * d4;
	if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
		return Add(t.b, Mul(Sub(t.c, t.b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
	float inv = 1 / (va + vb + vc);
	return Add(t.a, Add(Mul(ab, vb * inv), Mul(ac, vc * inv)));
}
struct Object {
	std::vector<Triangle> triangles;
	bool vehicle = false, ghostCar = false;
};
inline std::vector<Object> objects;
inline reactcontact::NativePoint nativePoint;
inline uint8_t *lastIgnore;
inline unsigned calls;
// Exact TestSphereAgainstWorld signature and output convention: returns the
// first colliding entity, writes one raw CColPoint, and has the native car
// bounding-volume fallback that returns an entity without writing a point.
uint8_t *NativeSphere(CVector c, float radius, uint8_t *ignore, bool buildings, bool vehicles, bool peds, bool props,
                      bool dummies, bool ignoreSomeObjects)
{
	assert(buildings && vehicles && !peds && props && !dummies && !ignoreSomeObjects);
	++calls;
	lastIgnore = ignore;
	const Vec3 center{c.x, c.y, c.z};
	for (Object &o : objects) {
		auto *entity = reinterpret_cast<uint8_t *>(&o);
		if (entity == ignore)
			continue;
		if (o.ghostCar)
			return entity;
		float nearest = radius;
		Vec3 contact{};
		bool hit = false;
		for (const Triangle &t : o.triangles) {
			Vec3 point = Closest(center, t);
			float distance = Length(Sub(center, point));
			if (distance < nearest) {
				nearest = distance;
				contact = point;
				hit = true;
			}
		}
		if (hit) {
			nativePoint.point = contact;
			nativePoint.normal = nearest > 1e-7f ? Mul(Sub(center, contact), 1 / nearest) : Vec3{};
			nativePoint.depth = radius - nearest;
			nativePoint.surfaceB = 4;
			return entity;
		}
	}
	return nullptr;
}
inline reactcontact::World world{NativeSphere, &nativePoint};
void Reset()
{
	objects.clear();
	nativePoint = {};
	calls = 0;
	lastIgnore = nullptr;
}
void Plane(Vec3 normal, float offset = 0, bool vehicle = false)
{
	Vec3 n = Unit(normal), u = Unit(Cross(n, std::fabs(n.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0})), v = Cross(n, u),
	     o = Mul(n, offset);
	Vec3 a = Add(o, Add(Mul(u, -20), Mul(v, -20))), b = Add(o, Add(Mul(u, 20), Mul(v, -20))),
	     c = Add(o, Add(Mul(u, 20), Mul(v, 20))), d = Add(o, Add(Mul(u, -20), Mul(v, 20)));
	objects.push_back({{{a, b, c}, {a, c, d}}, vehicle, false});
}
// Native standing capture from npc-fatal-structural/standing.json. Keep the
// actual bone4 offset and skin axes: the head origin is at the neck attachment,
// so a sphere at that origin alone cannot protect the rendered skull volume.
reactrig::Matrices CapturedRig()
{
 reactrig::Matrices rig{};
 for (auto &m : rig) m = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 rig[1] = {-0.079631f,-0.016556f,0.996687f,0.000000f,0.406424f,0.912443f,0.047628f,0.000000f,-0.910208f,0.408870f,-0.065930f,0.000000f,-0.007935f,-0.020386f,1.016361f,1.000000f};
 rig[3] = {0.032539f,0.116248f,0.992687f,0.000000f,0.271461f,0.954849f,-0.120715f,0.000000f,-0.961900f,0.273404f,-0.000487f,0.000000f,-0.023682f,-0.055267f,1.319704f,1.000000f};
 rig[5] = {0.070594f,0.029798f,0.997060f,0.000000f,0.240878f,0.969464f,-0.046028f,0.000000f,-0.967985f,0.243418f,0.061260f,0.000000f,0.000275f,0.029755f,1.721015f,1.000000f};
 rig[7] = {-0.224064f,-0.021423f,-0.974339f,0.000000f,-0.523851f,-0.840402f,0.138945f,0.000000f,-0.821813f,0.541541f,0.177081f,0.000000f,-0.210571f,0.028625f,1.556948f,1.000000f};
 rig[8] = {0.054140f,0.383618f,-0.921904f,0.000000f,-0.567180f,-0.748045f,-0.344581f,0.000000f,-0.821813f,0.541541f,0.177081f,0.000000f,-0.276672f,0.022308f,1.269473f,1.000000f};
 rig[9] = {-0.171797f,0.224209f,-0.959280f,0.000000f,0.253019f,-0.931049f,-0.262924f,0.000000f,-0.952086f,-0.287886f,0.103221f,0.000000f,-0.261169f,0.132111f,1.005601f,1.000000f};
 rig[12] = {0.255843f,-0.192669f,-0.947324f,0.000000f,0.516356f,-0.801205f,0.302403f,0.000000f,-0.817265f,-0.566524f,-0.105497f,0.000000f,0.179382f,-0.077545f,1.552001f,1.000000f};
 rig[13] = {0.042066f,0.123931f,-0.991399f,0.000000f,0.574726f,-0.814673f,-0.077453f,0.000000f,-0.817265f,-0.566524f,-0.105497f,0.000000f,0.254883f,-0.134399f,1.272496f,1.000000f};
 rig[14] = {-0.050506f,0.036304f,-0.998064f,0.000000f,-0.923729f,-0.381636f,0.032862f,0.000000f,-0.379704f,0.923600f,0.052810f,0.000000f,0.266937f,-0.098938f,0.988733f,1.000000f};
 rig[16] = {-0.139134f,0.073809f,-0.987520f,0.000000f,-0.135541f,0.986414f,0.092823f,0.000000f,0.980955f,0.146764f,-0.127239f,0.000000f,-0.116058f,0.006714f,1.013731f,1.000000f};
 rig[17] = {-0.068741f,-0.350365f,-0.934088f,0.000000f,-0.181671f,0.925043f,-0.333603f,0.000000f,0.980955f,0.146764f,-0.127239f,0.000000f,-0.179657f,0.040466f,0.562261f,1.000000f};
 rig[18] = {-0.030223f,-0.021888f,-0.999304f,0.000000f,-0.056396f,0.998205f,-0.020158f,0.000000f,0.997952f,0.055747f,-0.031403f,0.000000f,-0.207947f,-0.103760f,0.177729f,1.000000f};
 rig[19] = {0.067458f,0.020739f,-0.997507f,0.000000f,0.521687f,0.851491f,0.052983f,0.000000f,0.850466f,-0.523960f,0.046621f,0.000000f,0.083527f,-0.039185f,1.010022f,1.000000f};
 rig[20] = {-0.036841f,-0.147737f,-0.988340f,0.000000f,0.524739f,0.838832f,-0.144949f,0.000000f,0.850466f,-0.523960f,0.046621f,0.000000f,0.114380f,-0.029694f,0.553987f,1.000000f};
 rig[21] = {0.031525f,-0.059503f,-0.997730f,0.000000f,0.651240f,0.758472f,-0.024657f,0.000000f,0.758217f,-0.648984f,0.062662f,0.000000f,0.099213f,-0.090515f,0.147131f,1.000000f};
 rig[4] = {0.106103f,0.374937f,0.920959f,0.000000f,0.251976f,0.885818f,-0.389660f,0.000000f,-0.961900f,0.273404f,-0.000488f,0.000000f,-0.014862f,-0.023773f,1.589546f,1.000000f};
 return rig;
}
reactphysics::Contact NativeSweep(Vec3 from, Vec3 to, float radius, void *)
{
 return reactcontact::Sweep(world, from, to, radius, nullptr);
}
}
