#include "projection.h"

#include <algorithm>
#include <cmath>

// A virtual camera looks down at the ground with pitch `tilt` from vertical,
// focused on a point `focus_` ahead of the player at distance `dist_`. With
// t = g.y - focus_ the ground point lands at
//     n.x = k * g.x * D / (D + t sin),   n.y = k * t cos * D / (D + t sin)
// in normalised rect coordinates. `k` is chosen so the player sits at
// n = (0, -anchor) with a scale of `zoom` there.

void Projector::Setup(const ViewParams &params, float x, float y, float w, float h)
{
	x_ = x;
	y_ = y;
	w_ = w;
	h_ = h;
	const float aspect = w / h;
	const float zoom = std::max(params.zoom, 0.05f);
	const float anchor = std::clamp(params.anchor, -0.9f, 0.9f);
	dist_ = std::max(params.camDist, 0.5f);

	// Lower the tilt until the top edge of the rect is safely below the horizon.
	for (float tilt = std::clamp(params.tiltDeg, 0.0f, 75.0f);; tilt -= 1.0f) {
		const float rad = tilt * static_cast<float>(M_PI) / 180.0f;
		sin_ = sinf(rad);
		cos_ = cosf(rad);
		focus_ = anchor / (zoom * cos_);
		const float camDepth = dist_ - focus_ * sin_;
		k_ = zoom * camDepth / dist_;
		const bool horizonClear = sin_ < 1e-4f || k_ * dist_ * cos_ / sin_ > 1.05f;
		if (tilt <= 0.0f || (camDepth > 0.1f * dist_ && horizonClear))
			break;
	}

	corners_[0] = NormToGround({-aspect, -1.0f});
	corners_[1] = NormToGround({aspect, -1.0f});
	corners_[2] = NormToGround({aspect, 1.0f});
	corners_[3] = NormToGround({-aspect, 1.0f});
}

// Raising a point by h moves it along the camera's up vector (0, cos, sin) and
// toward the camera along its view axis (0, sin, -cos).
bool Projector::Project(Vec2 g, float height, Vec2 &px) const
{
	const float t = g.y - focus_;
	const float den = Depth(g, height);
	if (den <= 1e-4f)
		return false;
	const float nx = k_ * g.x * dist_ / den;
	const float ny = k_ * (t * cos_ + height * sin_) * dist_ / den;
	px.x = x_ + 0.5f * w_ + nx * 0.5f * h_;
	px.y = y_ + 0.5f * h_ - ny * 0.5f * h_;
	return true;
}

float Projector::Depth(Vec2 g, float height) const { return dist_ + (g.y - focus_) * sin_ - height * cos_; }

Vec2 Projector::NormToGround(Vec2 n) const
{
	const float t = n.y * dist_ / (k_ * cos_ * dist_ - n.y * sin_);
	return {n.x * (dist_ + t * sin_) / (k_ * dist_), focus_ + t};
}

float Projector::EdgeSide(int edge, Vec2 p) const
{
	const Vec2 a = corners_[edge];
	const Vec2 b = corners_[(edge + 1) & 3];
	return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

bool Projector::Inside(Vec2 g) const
{
	for (int e = 0; e < 4; e++)
		if (EdgeSide(e, g) < -1e-5f)
			return false;
	return true;
}

Vec2 Projector::ClampToEdge(Vec2 g) const
{
	float s = 1.0f;
	for (int e = 0; e < 4; e++) {
		const float h0 = EdgeSide(e, {0, 0});
		const float hg = EdgeSide(e, g);
		if (hg < 0.0f)
			s = std::min(s, h0 / (h0 - hg));
	}
	return {g.x * s, g.y * s};
}

float Projector::MaxVisibleDistance() const
{
	float d = 0.0f;
	for (const Vec2 &c : corners_)
		d = std::max(d, hypotf(c.x, c.y));
	return d;
}

int Projector::ClipPolygon(const ClipVert *in, int n, ClipVert *out) const
{
	constexpr int kMax = 16;
	ClipVert buf[2][kMax];
	if (n < 3 || n > kMax - 4)
		return 0;
	std::copy(in, in + n, buf[0]);
	int cur = 0;
	for (int e = 0; e < 4 && n > 0; e++) {
		const ClipVert *src = buf[cur];
		ClipVert *dst = buf[cur ^ 1];
		int m = 0;
		for (int i = 0; i < n; i++) {
			const ClipVert &a = src[i];
			const ClipVert &b = src[(i + 1) % n];
			const float ha = EdgeSide(e, a.g);
			const float hb = EdgeSide(e, b.g);
			if (ha >= 0.0f)
				dst[m++] = a;
			if ((ha >= 0.0f) != (hb >= 0.0f)) {
				const float t = ha / (ha - hb);
				dst[m++] = {{a.g.x + (b.g.x - a.g.x) * t, a.g.y + (b.g.y - a.g.y) * t},
				            {a.world.x + (b.world.x - a.world.x) * t, a.world.y + (b.world.y - a.world.y) * t}};
			}
		}
		n = m;
		cur ^= 1;
	}
	if (n < 3)
		return 0;
	std::copy(buf[cur], buf[cur] + n, out);
	return n;
}

bool Projector::ClipSegment(Vec2 &a, Vec2 &b) const
{
	float t0 = 0.0f, t1 = 1.0f;
	for (int e = 0; e < 4; e++) {
		const float ha = EdgeSide(e, a);
		const float hb = EdgeSide(e, b);
		if (ha < 0.0f && hb < 0.0f)
			return false;
		if (ha < 0.0f)
			t0 = std::max(t0, ha / (ha - hb));
		else if (hb < 0.0f)
			t1 = std::min(t1, ha / (ha - hb));
	}
	if (t0 > t1)
		return false;
	const Vec2 d{b.x - a.x, b.y - a.y};
	b = {a.x + d.x * t1, a.y + d.y * t1};
	a = {a.x + d.x * t0, a.y + d.y * t0};
	return true;
}
