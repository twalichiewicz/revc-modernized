#include "canvas.h"

#include <algorithm>
#include <cmath>

void Canvas::Fan(const Vtx *v, int n, CRGBA col)
{
	for (int i = 1; i + 1 < n; i++)
		Tri(v[0], v[i], v[i + 1], col);
}

void Canvas::Quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, CRGBA col)
{
	const Vtx v[4] = {{a.x, a.y, 0, 0}, {b.x, b.y, 0, 0}, {c.x, c.y, 0, 0}, {d.x, d.y, 0, 0}};
	Fan(v, 4, col);
}

void Canvas::Pill(float x, float y, float w, float h, CRGBA col)
{
	constexpr int kSeg = 12; // per half circle
	const float r = h * 0.5f, cy = y + r;
	const float left = x + r, right = x + std::max(w, h) - r;
	Vtx v[2 * (kSeg + 1)];
	int n = 0;
	for (int i = 0; i <= kSeg; i++) { // right cap, top to bottom
		const float a = float(M_PI) * (-0.5f + float(i) / kSeg);
		v[n++] = {right + r * cosf(a), cy + r * sinf(a), 0, 0};
	}
	for (int i = 0; i <= kSeg; i++) { // left cap, bottom to top
		const float a = float(M_PI) * (0.5f + float(i) / kSeg);
		v[n++] = {left + r * cosf(a), cy + r * sinf(a), 0, 0};
	}
	Fan(v, n, col); // convex, so a fan from any vertex works
}

namespace {

constexpr int kCornerSeg = 6;
constexpr int kRoundRectVerts = 4 * (kCornerSeg + 1);

// RoundRect's edge, grown by `grow` about the same corner centres, clockwise
// from the top-right corner.
void RoundRectEdge(float x, float y, float w, float h, float r, float grow, Vtx *v)
{
	r = std::min(r, std::min(w, h) * 0.5f);
	const Vec2 centres[4] = {{x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}, {x + r, y + r}};
	int n = 0;
	for (int c = 0; c < 4; c++)
		for (int i = 0; i <= kCornerSeg; i++) {
			const float a = float(M_PI) * 0.5f * (float(c) - 1.0f + float(i) / kCornerSeg);
			v[n++] = {centres[c].x + (r + grow) * cosf(a), centres[c].y + (r + grow) * sinf(a), 0, 0};
		}
}

} // namespace

void Canvas::RoundRect(float x, float y, float w, float h, float r, CRGBA col)
{
	Vtx v[kRoundRectVerts];
	RoundRectEdge(x, y, w, h, r, 0.0f, v);
	Fan(v, kRoundRectVerts, col);
}

void Canvas::RoundRectOutline(float x, float y, float w, float h, float r, float t, CRGBA col)
{
	Vtx in[kRoundRectVerts], out[kRoundRectVerts];
	RoundRectEdge(x, y, w, h, r, 0.0f, in);
	RoundRectEdge(x, y, w, h, r, t, out);
	for (int i = 0; i < kRoundRectVerts; i++) {
		const int j = (i + 1) % kRoundRectVerts;
		Tri(in[i], out[i], out[j], col);
		Tri(in[i], out[j], in[j], col);
	}
}

void Canvas::VerticalGradient(float x, float y, float w, float h, CRGBA top, CRGBA bottom)
{
	const Vtx tl{x, y, 0, 0}, tr{x + w, y, 0, 0}, br{x + w, y + h, 0, 0}, bl{x, y + h, 0, 0};
	Tri(tl, tr, br, top, top, bottom);
	Tri(tl, br, bl, top, bottom, bottom);
}
