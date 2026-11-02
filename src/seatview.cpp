#include "seatview.h"

#include <algorithm>
#include <cmath>

namespace seatview {
namespace {

constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kNear = 0.1f;  // m: nearer than this ahead isn't in the view (his own head's room)
constexpr float kJoin = 0.1f;  // degrees: pieces this close block as one (a mesh's shared edges)
constexpr int kWays = 5;       // looked along: straight ahead, and kAcross and half that either way
constexpr float kAround = 0.06f; // m about his eye the roof over it is looked for
constexpr float kStepUp = 0.01f; // m

Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct Span {
	float from, to; // degrees
};

// What blocks the view along one way ahead (the vertical plane through his
// eye that way): each triangle's cut across it, as the pitches it covers.
std::vector<Span> Blocked(const std::vector<Triangle> &body, Vec3 eye, float lift, float yaw)
{
	const Vec3 ahead{std::sin(yaw), std::cos(yaw), 0.0f}, side{std::cos(yaw), -std::sin(yaw), 0.0f};
	std::vector<Span> spans;
	for (const Triangle &t : body) {
		const Vec3 v[3] = {t.a, t.b, t.c};
		float s[3];
		for (int i = 0; i < 3; i++)
			s[i] = Dot(side, Sub(v[i], eye));
		float d[2], h[2];
		int n = 0;
		for (int i = 0; i < 3 && n < 2; i++) {
			const int j = (i + 1) % 3;
			if ((s[i] <= 0.0f) == (s[j] <= 0.0f))
				continue;
			const float k = s[i] / (s[i] - s[j]);
			const Vec3 p{v[i].x + (v[j].x - v[i].x) * k, v[i].y + (v[j].y - v[i].y) * k, v[i].z + (v[j].z - v[i].z) * k};
			d[n] = Dot(ahead, Sub(p, eye));
			h[n] = p.z - eye.z - lift;
			n++;
		}
		if (n < 2 || std::max(d[0], d[1]) <= kNear)
			continue;
		for (int i = 0; i < 2; i++) // cut where it comes nearer than kNear
			if (d[i] < kNear) {
				const int o = 1 - i;
				const float k = (kNear - d[o]) / (d[i] - d[o]);
				h[i] = h[o] + (h[i] - h[o]) * k;
				d[i] = kNear;
			}
		const float a = std::atan2(h[0], d[0]) / kDeg, b = std::atan2(h[1], d[1]) / kDeg;
		spans.push_back({std::min(a, b), std::max(a, b)});
	}
	std::sort(spans.begin(), spans.end(), [](const Span &x, const Span &y) { return x.from < y.from; });
	std::vector<Span> joined;
	for (const Span &s : spans)
		if (!joined.empty() && s.from <= joined.back().to + kJoin)
			joined.back().to = std::max(joined.back().to, s.to);
		else
			joined.push_back(s);
	return joined;
}

// The clear pitches about level along one way: the gap level is in, or (the
// hood over his eyes) the one just over it.
Window Gap(const std::vector<Span> &blocked)
{
	Window w{-90.0f, 90.0f};
	for (const Span &s : blocked) {
		if (s.from <= 0.0f && s.to >= 0.0f) { // level's blocked: the gap over it
			w.low = s.to;
			w.high = 90.0f;
			for (const Span &t : blocked)
				if (t.from > s.to)
					w.high = std::min(w.high, t.from);
			return w;
		}
		if (s.to < 0.0f)
			w.low = std::max(w.low, s.to);
		else
			w.high = std::min(w.high, s.from);
	}
	return w;
}

// m from his eye up to the roof over it (`most` if there's none so near).
float Room(const std::vector<Triangle> &body, Vec3 eye, float most)
{
	float room = most;
	const Vec3 up{0.0f, 0.0f, 1.0f};
	const float around[5][2] = {{0, 0}, {kAround, 0}, {-kAround, 0}, {0, kAround}, {0, -kAround}};
	for (const auto &o : around) {
		const Vec3 from{eye.x + o[0], eye.y + o[1], eye.z};
		for (const Triangle &t : body) { // Moller-Trumbore, straight up
			const Vec3 e1 = Sub(t.b, t.a), e2 = Sub(t.c, t.a), p = Cross(up, e2);
			const float det = Dot(e1, p);
			if (std::fabs(det) < 1e-9f)
				continue;
			const Vec3 s = Sub(from, t.a);
			const float u = Dot(s, p) / det;
			if (u < 0.0f || u > 1.0f)
				continue;
			const Vec3 q = Cross(s, e1);
			const float v = Dot(up, q) / det, dist = Dot(e2, q) / det;
			if (v >= 0.0f && u + v <= 1.0f && dist > 0.0f)
				room = std::min(room, dist);
		}
	}
	return room;
}

} // namespace

Window Ahead(const std::vector<Triangle> &body, Vec3 eye, float lift)
{
	Window w{-90.0f, 90.0f};
	for (int i = 0; i < kWays; i++) {
		const float yaw = kAcross * kDeg * (float(i) / float(kWays - 1) * 2.0f - 1.0f);
		const Window g = Gap(Blocked(body, eye, lift, yaw));
		w.low = std::max(w.low, g.low);
		w.high = std::min(w.high, g.high);
	}
	return w;
}

float Lift(const std::vector<Triangle> &body, Vec3 eye)
{
	if (body.empty() || Ahead(body, eye, 0.0f).low <= -kBelow)
		return 0.0f;
	const float top = std::clamp(Room(body, eye, kHighest + kHeadroom) - kHeadroom, 0.0f, kHighest);
	// Up a step at a time (a windscreen's frame or a roof's edge can come
	// across the road on the way up: only where he sees level, under the
	// roof's edge, counts), as little as puts the hood under him enough, or
	// where it's furthest under him short of that.
	float best = 0.0f, bestLow = 90.0f;
	for (float lift = 0.0f; lift <= top + 1e-4f; lift += kStepUp) {
		const Window w = Ahead(body, eye, lift);
		if (w.low >= 0.0f || w.high < kAbove)
			continue;
		if (w.low <= -kBelow)
			return lift;
		if (w.low < bestLow - 0.05f) {
			best = lift;
			bestLow = w.low;
		}
	}
	return best;
}

} // namespace seatview
