#pragma once
// The viewmodel's maths: points and affine frames, kept as RenderWare keeps a
// matrix (its axes and origin are columns). Header-only; unit-tested
// (tests/test_vmskin.cpp).
#include <cmath>

namespace vm {

struct Vec3 {
	float x, y, z;
};

inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normal(Vec3 a)
{
	const float l = Length(a);
	return l > 1e-12f ? Mul(a, 1.0f / l) : Vec3{0, 0, 0};
}
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return Add(a, Mul(Sub(b, a), t)); }

// x, y and z are where the frame takes (1,0,0), (0,1,0) and (0,0,1); pos its origin.
struct Frame {
	Vec3 x, y, z, pos;
};

inline Frame Identity() { return {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}; }
inline Vec3 Rotate(const Frame &f, Vec3 d) { return Add(Add(Mul(f.x, d.x), Mul(f.y, d.y)), Mul(f.z, d.z)); }
inline Vec3 Apply(const Frame &f, Vec3 p) { return Add(Rotate(f, p), f.pos); }
// a · b: b first, then a.
inline Frame Compose(const Frame &a, const Frame &b) { return {Rotate(a, b.x), Rotate(a, b.y), Rotate(a, b.z), Apply(a, b.pos)}; }

// Any invertible affine frame (skin-to-bone matrices may carry scale).
inline Frame Inverse(const Frame &f)
{
	const Vec3 r0 = Cross(f.y, f.z), r1 = Cross(f.z, f.x), r2 = Cross(f.x, f.y);
	const float det = Dot(f.x, r0);
	const float k = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
	// The inverse's rows are r0, r1, r2 over the determinant.
	Frame out{{r0.x * k, r1.x * k, r2.x * k}, {r0.y * k, r1.y * k, r2.y * k}, {r0.z * k, r1.z * k, r2.z * k}, {0, 0, 0}};
	out.pos = Mul(Rotate(out, f.pos), -1.0f);
	return out;
}

// RenderWare's 16 floats: x, flags, y, pad, z, pad, pos, pad.
inline Frame FromRw(const float *m) { return {{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}, {m[12], m[13], m[14]}}; }
inline void ToRw(const Frame &f, float *m)
{
	const Vec3 *c[4] = {&f.x, &f.y, &f.z, &f.pos};
	for (int i = 0; i < 4; i++) {
		m[i * 4] = c[i]->x;
		m[i * 4 + 1] = c[i]->y;
		m[i * 4 + 2] = c[i]->z;
		m[i * 4 + 3] = i == 3 ? 1.0f : 0.0f;
	}
}

// A turn by `angle` (rad) about the line through `point` along `axis` (a unit vector).
inline Frame TurnAbout(Vec3 axis, float angle, Vec3 point)
{
	const float c = std::cos(angle), s = std::sin(angle), t = 1.0f - c;
	const Vec3 a = axis;
	Frame r{{t * a.x * a.x + c, t * a.x * a.y + s * a.z, t * a.x * a.z - s * a.y},
	        {t * a.x * a.y - s * a.z, t * a.y * a.y + c, t * a.y * a.z + s * a.x},
	        {t * a.x * a.z + s * a.y, t * a.y * a.z - s * a.x, t * a.z * a.z + c},
	        {0, 0, 0}};
	r.pos = Sub(point, Rotate(r, point));
	return r;
}

// The least turn taking direction `from` to direction `to`, about `point`.
inline Frame Swing(Vec3 from, Vec3 to, Vec3 point)
{
	const Vec3 f = Normal(from), t = Normal(to), axis = Cross(f, t);
	const float s = Length(axis), c = Dot(f, t);
	if (s < 1e-7f) {
		if (c > 0.0f)
			return Identity();
		const Vec3 other = std::fabs(f.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
		return TurnAbout(Normal(Cross(f, other)), 3.14159265f, point);
	}
	return TurnAbout(Mul(axis, 1.0f / s), std::atan2(s, c), point);
}

// Between two frames: positions and axes blended, made square again (for
// frames whose axes aren't nearly opposed: two holds of the same hand).
inline Frame Blend(const Frame &a, const Frame &b, float t)
{
	const Vec3 x = Normal(Lerp(a.x, b.x, t)), z = Normal(Cross(x, Lerp(a.y, b.y, t)));
	return {x, Cross(z, x), z, Lerp(a.pos, b.pos, t)};
}

// A view from `eye`: x right, y along `forward`, z up (towards `up`).
inline Frame Look(Vec3 eye, Vec3 forward, Vec3 up)
{
	const Vec3 y = Normal(forward), x = Normal(Cross(y, up)), z = Cross(x, y);
	return {x, y, z, eye};
}

} // namespace vm
