#include "canvas.h"

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

void Canvas::VerticalGradient(float x, float y, float w, float h, CRGBA top, CRGBA bottom)
{
	const Vtx tl{x, y, 0, 0}, tr{x + w, y, 0, 0}, br{x + w, y + h, 0, 0}, bl{x, y + h, 0, 0};
	Tri(tl, tr, br, top, top, bottom);
	Tri(tl, br, bl, top, bottom, bottom);
}
