#include "shapes.h"

#include <algorithm>
#include <cmath>

namespace shapes {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kMostMiter = 4.0f; // a corner pushed out at most this many times the width

CRGBA Clear(CRGBA c) { return {c.r, c.g, c.b, 0}; }
Vtx V(Vec2 p) { return {p.x, p.y, 0, 0}; }
float Cross(Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

float Area(const std::vector<Vec2> &p)
{
	float area = 0;
	for (size_t i = 0; i < p.size(); i++)
		area += Cross({0, 0}, p[i], p[(i + 1) % p.size()]);
	return area / 2;
}

// By ear clipping.
void Triangles(Canvas &c, std::vector<Vec2> p, CRGBA col)
{
	const float turn = Area(p) > 0 ? 1.0f : -1.0f;
	while (p.size() >= 3) {
		const size_t n = p.size();
		size_t ear = n;
		for (size_t i = 0; i < n && ear == n; i++) {
			const Vec2 a = p[(i + n - 1) % n], b = p[i], d = p[(i + 1) % n];
			if (Cross(a, b, d) * turn <= 0)
				continue; // reflex
			bool empty = true;
			for (size_t j = 0; j < n && empty; j++) {
				if (j == i || j == (i + n - 1) % n || j == (i + 1) % n)
					continue;
				const Vec2 q = p[j];
				empty = !(Cross(a, b, q) * turn > 0 && Cross(b, d, q) * turn > 0 && Cross(d, a, q) * turn > 0);
			}
			if (empty)
				ear = i;
		}
		if (ear == n)
			ear = 0; // (degenerate: clip anyway)
		c.Tri(V(p[(ear + n - 1) % n]), V(p[ear]), V(p[(ear + 1) % n]), col);
		p.erase(p.begin() + long(ear));
	}
}

// A strip between two outlines of as many points, `from` in its colour fading to nothing at `to`.
void Fade(Canvas &c, const std::vector<Vec2> &from, const std::vector<Vec2> &to, CRGBA col, bool closed)
{
	const size_t n = from.size();
	for (size_t i = 0; i + (closed ? 0 : 1) < n; i++) {
		const size_t j = (i + 1) % n;
		c.Tri(V(from[i]), V(from[j]), V(to[j]), col, col, Clear(col));
		c.Tri(V(from[i]), V(to[j]), V(to[i]), col, Clear(col), Clear(col));
	}
}

std::vector<Vec2> Circle(Vec2 at, float r, int segments, float from, float share)
{
	std::vector<Vec2> p;
	for (int i = 0; i <= segments; i++) {
		const float a = from + 2 * kPi * share * i / segments;
		p.push_back({at.x + r * std::cos(a), at.y + r * std::sin(a)});
	}
	return p;
}

int Segments(float r, float share) { return std::max(8, int(std::ceil(std::max(r, 2.0f) * 1.5f * share))); }

} // namespace

std::vector<Vec2> Grow(const std::vector<Vec2> &p, float by)
{
	const size_t n = p.size();
	const float out = Area(p) > 0 ? 1.0f : -1.0f; // (which side is outside, for this winding)
	std::vector<Vec2> grown(n);
	for (size_t i = 0; i < n; i++) {
		const Vec2 a = p[(i + n - 1) % n], b = p[i], d = p[(i + 1) % n];
		auto normal = [out](Vec2 from, Vec2 to) {
			const float dx = to.x - from.x, dy = to.y - from.y, len = std::max(std::hypot(dx, dy), 1e-5f);
			return Vec2{out * dy / len, -out * dx / len};
		};
		const Vec2 n1 = normal(a, b), n2 = normal(b, d);
		const float k = by / std::max(1.0f + n1.x * n2.x + n1.y * n2.y, 1.0f / (kMostMiter * kMostMiter));
		Vec2 m{(n1.x + n2.x) * k, (n1.y + n2.y) * k};
		const float len = std::hypot(m.x, m.y);
		if (len > by * kMostMiter)
			m = {m.x / len * by * kMostMiter, m.y / len * by * kMostMiter};
		grown[i] = {b.x + m.x, b.y + m.y};
	}
	return grown;
}

void Fill(Canvas &c, const std::vector<Vec2> &points, CRGBA col, float feather)
{
	if (points.size() < 3)
		return;
	Triangles(c, points, col);
	if (feather > 0)
		Fade(c, points, Grow(points, feather), col, true);
}

std::vector<Vec2> Star(Vec2 at, float r, float inner)
{
	std::vector<Vec2> p;
	for (int k = 0; k < 10; k++) {
		const float a = -kPi / 2 + k * kPi / 5, d = k % 2 ? r * inner : r;
		p.push_back({at.x + d * std::cos(a), at.y + d * std::sin(a)});
	}
	return p;
}

void Disc(Canvas &c, Vec2 at, float r, CRGBA col, float feather)
{
	std::vector<Vec2> p = Circle(at, r, Segments(r, 1.0f), 0.0f, 1.0f);
	p.pop_back(); // (the first again)
	for (size_t i = 0; i < p.size(); i++)
		c.Tri(V(at), V(p[i]), V(p[(i + 1) % p.size()]), col);
	if (feather > 0)
		Fade(c, p, Grow(p, feather), col, true);
}

void Ring(Canvas &c, Vec2 at, float r, float width, float share, CRGBA col, float feather)
{
	share = std::clamp(share, 0.0f, 1.0f);
	if (share <= 0.0f)
		return;
	const int n = Segments(r + width, share);
	const float from = -kPi / 2;
	const std::vector<Vec2> in = Circle(at, r - width / 2, n, from, share), out = Circle(at, r + width / 2, n, from, share);
	for (int i = 0; i < n; i++) {
		c.Tri(V(in[i]), V(out[i]), V(out[i + 1]), col);
		c.Tri(V(in[i]), V(out[i + 1]), V(in[i + 1]), col);
	}
	if (feather > 0) {
		const bool closed = share >= 1.0f;
		std::vector<Vec2> a = in, b = out, ia = Circle(at, std::max(r - width / 2 - feather, 0.0f), n, from, share),
		                  ob = Circle(at, r + width / 2 + feather, n, from, share);
		if (closed) {
			a.pop_back(), b.pop_back(), ia.pop_back(), ob.pop_back();
		}
		Fade(c, a, ia, col, closed);
		Fade(c, b, ob, col, closed);
	}
}

void Line(Canvas &c, Vec2 a, Vec2 b, float width, CRGBA col, float feather)
{
	const float dx = b.x - a.x, dy = b.y - a.y, len = std::max(std::hypot(dx, dy), 1e-3f);
	const float nx = -dy / len * width / 2, ny = dx / len * width / 2;
	Fill(c, {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}}, col, feather);
}

} // namespace shapes
