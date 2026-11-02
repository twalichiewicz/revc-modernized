#include "helizones.h"

#include <algorithm>
#include <cmath>

namespace helizones {
namespace {

// The body: the cabin and the tail boom, each a segment and a radius.
struct Capsule {
	Vec3 a, b;
	float r;
};
constexpr Capsule kCabin{{0.0f, 2.6f, 0.35f}, {0.0f, -1.6f, 0.35f}, 1.15f};
constexpr Capsule kBoom{{0.0f, -2.0f, 1.0f}, {0.0f, -7.4f, 1.3f}, 0.55f};
constexpr Capsule kPilotBody{{0.42f, 1.45f, -0.1f}, {0.42f, 1.5f, 0.75f}, kPilotReach};
// The body's capsules are rounder than it is: a part this far inside one
// (the lamp under the chin, the tail rotor beside the fin) still shows on
// the model, so it's hit first.
constexpr float kInside = 0.5f;
// The cockpit's glass, where a shot goes in to the pilot: forward of the doors, over the belly.
constexpr float kGlassFrom = 0.75f, kGlassLow = -0.52f, kGlassHigh = 1.0f;
// The lamp's mount: it turns all round, from straight down to this far below level (rad).
constexpr float kLampHighest = 0.15f;
// Its shape (m, colours): the housing's sides, the lens set into it, the mount it hangs from.
constexpr int kLampSides = 14;
constexpr float kLensInset = 0.012f, kLensRadius = 0.112f, kMountUp = 0.14f, kMountThick = 0.035f;
constexpr uint8_t kHousing[3] = {44, 46, 50}, kBezel[3] = {96, 98, 104}, kLensOn[3] = {255, 246, 222},
                  kLensOff[3] = {58, 66, 74}, kLensBroken[3] = {16, 16, 18};

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Normal(Vec3 a)
{
	const float l = std::sqrt(Dot(a, a));
	return l > 1e-6f ? Mul(a, 1.0f / l) : Vec3{0.0f, 0.0f, 1.0f};
}

Vec3 Nearest(Vec3 a, Vec3 b, Vec3 p)
{
	const Vec3 ab = Sub(b, a);
	const float t = std::clamp(Dot(Sub(p, a), ab) / std::fmax(Dot(ab, ab), 1e-6f), 0.0f, 1.0f);
	return Add(a, Mul(ab, t));
}

// How far along the ray it first meets the capsule: < 0 if it doesn't, or starts inside it.
float Meets(Vec3 ro, Vec3 rd, const Capsule &c)
{
	const Vec3 off = Sub(ro, Nearest(c.a, c.b, ro));
	if (Dot(off, off) <= c.r * c.r)
		return -1.0f;
	const Vec3 ba = Sub(c.b, c.a), oa = Sub(ro, c.a);
	const float baba = Dot(ba, ba), bard = Dot(ba, rd), baoa = Dot(ba, oa), rdoa = Dot(rd, oa), oaoa = Dot(oa, oa);
	const float qa = baba - bard * bard, qb = baba * rdoa - baoa * bard, qc = baba * oaoa - baoa * baoa - c.r * c.r * baba;
	const float h = qb * qb - qa * qc;
	if (h >= 0.0f && qa > 1e-6f) {
		const float t = (-qb - std::sqrt(h)) / qa, y = baoa + t * bard;
		if (y > 0.0f && y < baba)
			return t;
	}
	// Its rounded ends (and all a shot along its length can meet).
	float best = -1.0f;
	for (const Vec3 &end : {c.a, c.b}) {
		const Vec3 oc = Sub(ro, end);
		const float cb = Dot(rd, oc), cc = Dot(oc, oc) - c.r * c.r, e = cb * cb - cc;
		const float t = e > 0.0f ? -cb - std::sqrt(e) : -1.0f;
		if (t >= 0.0f && (best < 0.0f || t < best))
			best = t;
	}
	return best;
}

// ... an ellipsoid about `c` with these radii: the same, in its scaled space.
float MeetsEllipsoid(Vec3 ro, Vec3 rd, Vec3 c, Vec3 radii)
{
	const Vec3 o{(ro.x - c.x) / radii.x, (ro.y - c.y) / radii.y, (ro.z - c.z) / radii.z};
	const Vec3 d{rd.x / radii.x, rd.y / radii.y, rd.z / radii.z};
	const float a = Dot(d, d), b = Dot(o, d), cc = Dot(o, o) - 1.0f;
	if (cc <= 0.0f)
		return -1.0f;
	const float h = b * b - a * cc;
	return h >= 0.0f ? (-b - std::sqrt(h)) / a : -1.0f;
}

Vec3 Cross(Vec3 a, Vec3 b);

// Trace the very same triangles submitted for the visible housing, bezel,
// lens and mount. The former middle sphere omitted the front/rear barrel.
Hit LampContact(Vec3 from,Vec3 dir,float length,const Parts &parts)
{
	struct Cached { Lamp pose{};Mesh lit,glow;bool ready=false; };
	static thread_local Cached cached;
	const Lamp lamp{parts.lamp,parts.lampForward};
	auto same=[](Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;};
	if(!cached.ready||!same(cached.pose.middle,lamp.middle)||!same(cached.pose.forward,lamp.forward)){
		LampMesh(lamp,false,false,cached.lit,cached.glow);cached.pose=lamp;cached.ready=true;
	}
	Hit nearest;
	for(size_t i=0;i+2<cached.lit.i.size();i+=3){
		const Vec3 a=cached.lit.v[cached.lit.i[i]].p,b=cached.lit.v[cached.lit.i[i+1]].p,c=cached.lit.v[cached.lit.i[i+2]].p;
		const Vec3 ab=Sub(b,a),ac=Sub(c,a),p=Cross(dir,ac);
		const float determinant=Dot(ab,p);
		if(std::fabs(determinant)<1e-7f)continue;
		const Vec3 offset=Sub(from,a);const float u=Dot(offset,p)/determinant;
		if(u<0||u>1)continue;
		const Vec3 q=Cross(offset,ab);const float v=Dot(dir,q)/determinant;
		if(v<0||u+v>1)continue;
		const float t=Dot(ac,q)/determinant;
		if(t<0||t>length||(nearest.part!=Part::kNone&&t>=nearest.t))continue;
		Vec3 normal=Normal(Cross(ab,ac));if(Dot(dir,normal)>0)normal=Mul(normal,-1);
		nearest={Part::kLamp,t,Add(from,Mul(dir,t)),normal};
	}
	return nearest;
}

bool ThroughTheGlass(Vec3 p) { return p.y >= kGlassFrom && p.z >= kGlassLow && p.z <= kGlassHigh; }

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

void Vertex(Mesh &m, Vec3 p, Vec3 n, const uint8_t c[3]) { m.v.push_back({p, n, c[0], c[1], c[2]}); }
void Tri(Mesh &m, int a, int b, int c)
{
	m.i.push_back(uint16_t(a));
	m.i.push_back(uint16_t(b));
	m.i.push_back(uint16_t(c));
}

// A box from a to b, `half` thick each way across (`side` one of the ways).
void Bar(Mesh &m, Vec3 a, Vec3 b, Vec3 side, float half, const uint8_t c[3])
{
	const Vec3 along = Normal(Sub(b, a)), s = Normal(side), t = Normal(Cross(along, s));
	const Vec3 faces[4][2] = {{s, t}, {t, Mul(s, -1)}, {Mul(s, -1), Mul(t, -1)}, {Mul(t, -1), s}};
	for (const auto &f : faces) {
		const int base = int(m.v.size());
		const Vec3 n = f[0], e = f[1];
		for (Vec3 end : {a, b})
			for (float k : {-1.0f, 1.0f})
				Vertex(m, Add(end, Add(Mul(n, half), Mul(e, half * k))), n, c);
		Tri(m, base, base + 2, base + 3);
		Tri(m, base, base + 3, base + 1);
	}
}

} // namespace

Hit Trace(Vec3 from, Vec3 dir, float length, const Parts &parts)
{
	Hit hit;
	float body = -1.0f;
	Vec3 bodyOut{0, 0, 1};
	for (const Capsule *c : {&kCabin, &kBoom}) {
		const float t = Meets(from, dir, *c);
		if (t >= 0.0f && t <= length && (body < 0.0f || t < body)) {
			body = t;
			const Vec3 p = Add(from, Mul(dir, t));
			bodyOut = Normal(Sub(p, Nearest(c->a, c->b, p)));
		}
	}
	// The cabin's own capsule taken from inside (a gunner's shot) isn't the body: nor is anything in it.
	const Vec3 offCabin = Sub(from, Nearest(kCabin.a, kCabin.b, from));
	if (Dot(offCabin, offCabin) <= kCabin.r * kCabin.r)
		return hit;
	auto take = [&](Part part, float t, Vec3 out) {
		if (t < 0.0f || t > length || (body >= 0.0f && t > body + kInside) || (hit.part != Part::kNone && t >= hit.t))
			return;
		hit = {part, t, Add(from, Mul(dir, t)), out};
	};
	if (parts.haveLamp) {
		const Hit lamp = LampContact(from,dir,length,parts);
		if(lamp.part!=Part::kNone)take(Part::kLamp,lamp.t,lamp.normal);
	}
	{
		Capsule mast{kMastLow, kMastHigh, kMastRadius};
		const float t = Meets(from, dir, mast);
		take(Part::kMast, t, Normal(Sub(Add(from, Mul(dir, t)), Nearest(kMastLow, kMastHigh, Add(from, Mul(dir, t))))));
	}
	if (parts.haveTailRotor) {
		const float t = MeetsEllipsoid(from, dir, kTailRotor, {kTailRotorThick * 0.5f, kTailRotorRadius, kTailRotorRadius});
		take(Part::kTailRotor, t, {dir.x > 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f});
	}
	if (hit.part != Part::kNone)
		return hit;
	if (body < 0.0f)
		return hit;
	hit = {Part::kBody, body, Add(from, Mul(dir, body)), bodyOut};
	// In through the cockpit's glass, on to the pilot in his seat.
	if (parts.havePilot && ThroughTheGlass(hit.at)) {
		const float t = Meets(from, dir, kPilotBody);
		if (t >= 0.0f && t <= length) {
			const Vec3 p = Add(from, Mul(dir, t));
			hit = {Part::kPilot, t, p, Normal(Sub(p, Nearest(kPilotBody.a, kPilotBody.b, p)))};
		}
	}
	return hit;
}

Lamp Aim(Vec3 target)
{
	const Vec3 pivot = kLampPivot;
	Vec3 d = Normal(Sub(target, Vec3{pivot.x, pivot.y, pivot.z - kLampHang}));
	// No higher than a little below level (it's under the chin; it can't look up into the body).
	const float most = -std::sin(kLampHighest);
	if (d.z > most) {
		const float flat = std::sqrt(std::max(0.0f, 1.0f - most * most)), across = std::hypot(d.x, d.y);
		d = across > 1e-4f ? Vec3{d.x / across * flat, d.y / across * flat, most} : Vec3{0.0f, flat, most};
	}
	return {{pivot.x, pivot.y, pivot.z - kLampHang}, d};
}

Vec3 Lens(const Lamp &lamp)
{
	return Add(lamp.middle, Mul(lamp.forward, kLampLength * 0.5f - kLensInset));
}

void LampMesh(const Lamp &lamp, bool on, bool broken, Mesh &lit, Mesh &glow)
{
	lit.v.clear(), lit.i.clear(), glow.v.clear(), glow.i.clear();
	const Vec3 f = lamp.forward, mid = lamp.middle;
	const Vec3 side = Normal(std::fabs(f.z) > 0.95f ? Cross(f, Vec3{0, 1, 0}) : Cross(f, Vec3{0, 0, 1}));
	const Vec3 up = Cross(side, f);
	const float R = kLampRadius, L = kLampLength;
	const Vec3 back = Add(mid, Mul(f, -L * 0.5f)), front = Add(mid, Mul(f, L * 0.5f)), lensAt = Lens(lamp);
	auto ring = [&](int k) {
		const float a = 6.2831853f * float(k) / kLampSides;
		return Add(Mul(side, std::cos(a)), Mul(up, std::sin(a)));
	};
	for (int k = 0; k < kLampSides; k++) {
		const Vec3 r0 = ring(k), r1 = ring(k + 1);
		// The housing's side (narrowing to the back), its back, and the bezel round the lens.
		int base = int(lit.v.size());
		Vertex(lit, Add(back, Mul(r0, R * 0.85f)), r0, kHousing);
		Vertex(lit, Add(front, Mul(r0, R)), r0, kHousing);
		Vertex(lit, Add(front, Mul(r1, R)), r1, kHousing);
		Vertex(lit, Add(back, Mul(r1, R * 0.85f)), r1, kHousing);
		Tri(lit, base, base + 1, base + 2);
		Tri(lit, base, base + 2, base + 3);
		base = int(lit.v.size());
		Vertex(lit, back, Mul(f, -1), kHousing);
		Vertex(lit, Add(back, Mul(r1, R * 0.85f)), Mul(f, -1), kHousing);
		Vertex(lit, Add(back, Mul(r0, R * 0.85f)), Mul(f, -1), kHousing);
		Tri(lit, base, base + 1, base + 2);
		base = int(lit.v.size());
		Vertex(lit, Add(front, Mul(r0, R)), f, kBezel);
		Vertex(lit, Add(lensAt, Mul(r0, kLensRadius)), f, kBezel);
		Vertex(lit, Add(lensAt, Mul(r1, kLensRadius)), f, kBezel);
		Vertex(lit, Add(front, Mul(r1, R)), f, kBezel);
		Tri(lit, base, base + 2, base + 1);
		Tri(lit, base, base + 3, base + 2);
		// The lens: glowing while it's on, glass when it's off, black shot out.
		Mesh &lens = on && !broken ? glow : lit;
		const uint8_t *c = broken ? kLensBroken : on ? kLensOn : kLensOff;
		base = int(lens.v.size());
		Vertex(lens, lensAt, f, c);
		Vertex(lens, Add(lensAt, Mul(r1, kLensRadius)), f, c);
		Vertex(lens, Add(lensAt, Mul(r0, kLensRadius)), f, c);
		Tri(lens, base, base + 1, base + 2);
	}
	// The mount: a post down from the belly to a cross bar, its arms down to the lamp's sides.
	const Vec3 pivot = kLampPivot, flatSide = Normal(Vec3{side.x, side.y, 0.0f});
	Bar(lit, Add(pivot, Vec3{0, 0, kMountUp}), pivot, Vec3{1, 0, 0}, kMountThick * 0.5f, kHousing);
	const Vec3 armL = Add(pivot, Mul(flatSide, R + 0.02f)), armR = Add(pivot, Mul(flatSide, -R - 0.02f));
	Bar(lit, armL, armR, Vec3{0, 0, 1}, kMountThick * 0.4f, kHousing);
	Bar(lit, armL, Add(mid, Mul(flatSide, R + 0.02f)), f, kMountThick * 0.4f, kHousing);
	Bar(lit, armR, Add(mid, Mul(flatSide, -R - 0.02f)), f, kMountThick * 0.4f, kHousing);
}

} // namespace helizones
