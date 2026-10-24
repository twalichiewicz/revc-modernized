#include "scene.h"

#include <algorithm>
#include <cmath>

namespace {

// A dark HUD map in the GTA V/VI vein.
constexpr CRGBA kBaseColours[4] = {
    {44, 74, 104, 255},   // water
    {64, 70, 78, 255},    // land
    {52, 84, 62, 255},    // park
    {138, 130, 104, 255}, // beach
};
constexpr CRGBA kTarmac{80, 84, 90, 255};
constexpr CRGBA kRoadCasing{38, 42, 48, 255};
constexpr CRGBA kRoad{128, 134, 142, 255};
constexpr CRGBA kRoof{200, 206, 214, 255};
constexpr CRGBA kWall{132, 138, 150, 255};
constexpr CRGBA kRouteOutline{14, 14, 18, 235};
constexpr CRGBA kBorder{0, 0, 0, 225};
constexpr CRGBA kShadow{0, 0, 0, 90};
constexpr CRGBA kWhite{255, 255, 255, 255};

constexpr float kRoadInset = 0.85f;  // share of the kerb-to-kerb width drawn as tarmac
constexpr float kCasingWidth = 1.2f; // world units either side of the road
constexpr float kRouteWidth = 5.5f;
constexpr float kRouteOutlineWidth = 1.6f;
constexpr float kBaseCell = 62.5f;       // base map subdivision, world units
constexpr float kMaxRoofHeight = 1.2f;   // ground units; keeps roofs in front of the camera
constexpr float kNearRoadRadius = 24.0f; // art roads this close to a real road are redrawn as vectors
constexpr float kMinRoadPx = 2.0f;       // keep thin lines visible when zoomed out
constexpr float kMinRoutePx = 3.5f;
constexpr float kClearDepth = 0.999f;

// Half width in ground units for a line `width` world units wide, but at least minPx on screen.
float HalfWidth(const Projector &proj, float range, float width, float minPx)
{
	return std::max(0.5f * width / range, 0.5f * minPx / proj.PixelsPerUnit());
}

struct View {
	const Projector &proj;
	const SceneInput &in;
	Vec2 lo, hi;            // world bounds of the visible area, with a margin
	float invNear, invSpan; // depth buffer mapping (1/z is linear on screen)

	Vec2 ToGround(Vec2 w) const
	{
		const float dx = w.x - in.origin.x, dy = w.y - in.origin.y;
		return {(in.cos * dx + in.sin * dy) / in.range, (in.cos * dy - in.sin * dx) / in.range};
	}
	Vec2 ToWorld(Vec2 g) const
	{
		const float rx = g.x * in.range, ry = g.y * in.range;
		return {in.origin.x + in.cos * rx - in.sin * ry, in.origin.y + in.sin * rx + in.cos * ry};
	}
	bool Overlaps(Vec2 a, Vec2 b) const { return b.x >= lo.x && a.x <= hi.x && b.y >= lo.y && a.y <= hi.y; }
	float Depth01(Vec2 g, float height) const
	{
		const float z = (invNear - 1.0f / std::max(proj.Depth(g, height), 1e-4f)) * invSpan;
		return 0.02f + 0.95f * std::clamp(z, 0.0f, 1.0f);
	}
};

View MakeView(const Projector &proj, const SceneInput &in)
{
	View v{proj, in, {}, {}, 1, 1};
	v.lo = v.hi = in.origin;
	float nearest = 1e9f, farthest = 0;
	for (int i = 0; i < 4; i++) {
		const Vec2 c = proj.Corners()[i];
		const Vec2 w = v.ToWorld(c);
		v.lo = {std::min(v.lo.x, w.x), std::min(v.lo.y, w.y)};
		v.hi = {std::max(v.hi.x, w.x), std::max(v.hi.y, w.y)};
		nearest = std::min(nearest, proj.Depth(c, kMaxRoofHeight));
		farthest = std::max(farthest, proj.Depth(c, 0));
	}
	const float margin = 0.25f * in.range;
	v.lo = {v.lo.x - margin, v.lo.y - margin};
	v.hi = {v.hi.x + margin, v.hi.y + margin};
	nearest = std::max(0.05f, nearest * 0.9f);
	farthest *= 1.05f;
	v.invNear = 1.0f / nearest;
	v.invSpan = 1.0f / (v.invNear - 1.0f / farthest);
	return v;
}

// Ground-plane polygon -> visible part -> screen. `uv` maps world XY to texture space.
template <typename UV>
void FillGround(Canvas &canvas, const View &view, const ClipVert *poly, int n, CRGBA col, UV uv)
{
	ClipVert clipped[16];
	const int m = view.proj.ClipPolygon(poly, n, clipped);
	Vtx v[16];
	for (int k = 0; k < m; k++) {
		Vec2 px;
		if (!view.proj.GroundToScreen(clipped[k].g, px))
			return;
		const Vec2 t = uv(clipped[k].world);
		v[k] = {px.x, px.y, t.x, t.y};
	}
	canvas.Fan(v, m, col);
}

const auto kNoUV = [](Vec2) { return Vec2{0, 0}; };

void ThickSegment(Canvas &canvas, const View &view, Vec2 aw, Vec2 bw, float halfWidth, CRGBA col)
{
	const Vec2 a = view.ToGround(aw), b = view.ToGround(bw);
	const float dx = b.x - a.x, dy = b.y - a.y, len = hypotf(dx, dy);
	if (len < 1e-6f)
		return;
	const float nx = -dy / len * halfWidth, ny = dx / len * halfWidth;
	const ClipVert quad[4] = {{{a.x + nx, a.y + ny}, {}}, {{b.x + nx, b.y + ny}, {}}, {{b.x - nx, b.y - ny}, {}},
	                          {{a.x - nx, a.y - ny}, {}}};
	FillGround(canvas, view, quad, 4, col, kNoUV);
}

void Disc(Canvas &canvas, const View &view, Vec2 cw, float radius, CRGBA col)
{
	const Vec2 c = view.ToGround(cw);
	ClipVert ring[8];
	for (int i = 0; i < 8; i++) {
		const float a = static_cast<float>(M_PI) * i / 4.0f;
		ring[i] = {{c.x + radius * cosf(a), c.y + radius * sinf(a)}, {}};
	}
	FillGround(canvas, view, ring, 8, col, kNoUV);
}

void DrawBase(Canvas &canvas, const View &view)
{
	if (!view.in.baseTexture)
		return;
	canvas.SetTexture(view.in.baseTexture);
	const int cells = int(mapdata::kWorldSize / kBaseCell);
	auto cellOf = [&](float w) { return std::clamp(int(floorf((w - mapdata::kWorldMin) / kBaseCell)), 0, cells - 1); };
	const auto uv = [](Vec2 w) {
		return Vec2{(w.x - mapdata::kWorldMin) / mapdata::kWorldSize,
		            (mapdata::kWorldMin + mapdata::kWorldSize - w.y) / mapdata::kWorldSize};
	};
	for (int cy = cellOf(view.lo.y); cy <= cellOf(view.hi.y); cy++)
		for (int cx = cellOf(view.lo.x); cx <= cellOf(view.hi.x); cx++) {
			const float x0 = mapdata::kWorldMin + cx * kBaseCell, y0 = mapdata::kWorldMin + cy * kBaseCell;
			const float x1 = x0 + kBaseCell, y1 = y0 + kBaseCell;
			const ClipVert quad[4] = {{view.ToGround({x0, y0}), {x0, y0}},
			                          {view.ToGround({x1, y0}), {x1, y0}},
			                          {view.ToGround({x1, y1}), {x1, y1}},
			                          {view.ToGround({x0, y1}), {x0, y1}}};
			FillGround(canvas, view, quad, 4, kWhite, uv);
		}
	canvas.Flush();
}

void DrawRoads(Canvas &canvas, const View &view)
{
	if (!view.in.roads)
		return;
	std::vector<const mapdata::Road *> visible;
	for (const mapdata::Road &r : *view.in.roads)
		if (view.Overlaps({std::min(r.a.x, r.b.x), std::min(r.a.y, r.b.y)}, {std::max(r.a.x, r.b.x), std::max(r.a.y, r.b.y)}))
			visible.push_back(&r);
	canvas.SetTexture(nullptr);
	for (int pass = 0; pass < 2; pass++) { // all casings first, so junctions merge cleanly
		const CRGBA col = pass == 0 ? kRoadCasing : kRoad;
		auto halfWidth = [&](float width) {
			return HalfWidth(view.proj, view.in.range, width * kRoadInset, kMinRoadPx) +
			       (pass == 0 ? kCasingWidth / view.in.range : 0.0f);
		};
		for (const mapdata::Road *r : visible) {
			ThickSegment(canvas, view, r->a, r->b, halfWidth(r->width), col);
			Disc(canvas, view, r->a, halfWidth(r->joinA), col);
			Disc(canvas, view, r->b, halfWidth(r->joinB), col);
		}
	}
	canvas.Flush();
}

// Sutherland-Hodgman against the minimap rectangle (screen space).
int ClipToRect(const Projector &proj, const Vtx *in, int n, Vtx *out)
{
	Vtx buf[2][12];
	std::copy(in, in + n, buf[0]);
	int cur = 0;
	const float bounds[4] = {proj.X(), proj.X() + proj.W(), proj.Y(), proj.Y() + proj.H()};
	for (int e = 0; e < 4 && n > 0; e++) {
		auto side = [&](const Vtx &v) {
			const float c = e < 2 ? v.x : v.y;
			return (e % 2 == 0) ? c - bounds[e] : bounds[e] - c;
		};
		int m = 0;
		for (int i = 0; i < n; i++) {
			const Vtx &a = buf[cur][i], &b = buf[cur][(i + 1) % n];
			const float sa = side(a), sb = side(b);
			if (sa >= 0)
				buf[cur ^ 1][m++] = a;
			if ((sa >= 0) != (sb >= 0) && m < 12) {
				const float t = sa / (sa - sb);
				buf[cur ^ 1][m++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.u + (b.u - a.u) * t,
				                     a.v + (b.v - a.v) * t, a.depth + (b.depth - a.depth) * t};
			}
		}
		n = m;
		cur ^= 1;
	}
	std::copy(buf[cur], buf[cur] + n, out);
	return n;
}

void FillScreen(Canvas &canvas, const Projector &proj, const Vtx *v, int n, CRGBA col)
{
	Vtx clipped[12];
	canvas.Fan(clipped, ClipToRect(proj, v, n, clipped), col);
}

CRGBA Shade(CRGBA c, float k)
{
	return {uint8_t(std::min(255.0f, c.r * k)), uint8_t(std::min(255.0f, c.g * k)), uint8_t(std::min(255.0f, c.b * k)),
	        c.a};
}

// The roof mesh at its real heights, and walls from its outline down to the
// street. The depth buffer sorts out what is in front.
void DrawBuilding(Canvas &canvas, const View &view, const mapdata::Building &b)
{
	const Projector &proj = view.proj;
	const float scale = view.in.buildingScale / view.in.range;
	const size_t n = b.verts.size();
	std::vector<Vec2> g(n);
	std::vector<Vtx> base(n), top(n);
	for (size_t i = 0; i < n; i++) {
		g[i] = view.ToGround({b.verts[i].x, b.verts[i].y});
		const float h = std::min(b.verts[i].z * scale, kMaxRoofHeight);
		Vec2 p0, p1;
		if (proj.Depth(g[i], h) < 0.05f || !proj.Project(g[i], 0, p0) || !proj.Project(g[i], h, p1))
			return; // too close to the camera to draw sensibly
		base[i] = {p0.x, p0.y, 0, 0, view.Depth01(g[i], 0)};
		top[i] = {p1.x, p1.y, 0, 0, view.Depth01(g[i], h)};
	}
	const Vec2 cam = proj.CameraGround();
	for (size_t e = 0; e + 1 < b.edges.size(); e += 2) {
		const uint16_t i = b.edges[e], j = b.edges[e + 1];
		const float nx = g[j].y - g[i].y, ny = g[i].x - g[j].x; // outward: the roof is on the edge's left
		if ((cam.x - g[i].x) * nx + (cam.y - g[i].y) * ny <= 0)
			continue; // faces away from the camera
		const float len = hypotf(nx, ny);
		const float light = len > 0 ? (-0.83f * nx - 0.55f * ny) / len : 0.0f; // from the upper left
		const Vtx quad[4] = {base[i], base[j], top[j], top[i]};
		FillScreen(canvas, proj, quad, 4, Shade(kWall, 0.62f + 0.3f * light));
	}
	for (size_t t = 0; t + 2 < b.tris.size(); t += 3) {
		const Vec3 &a = b.verts[b.tris[t]], &c = b.verts[b.tris[t + 1]], &d = b.verts[b.tris[t + 2]];
		// Flatter roofs read lighter.
		const float ux = c.x - a.x, uy = c.y - a.y, uz = (c.z - a.z) * view.in.buildingScale;
		const float vx = d.x - a.x, vy = d.y - a.y, vz = (d.z - a.z) * view.in.buildingScale;
		const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
		const float flat = fabsf(nz) / std::max(1e-6f, sqrtf(nx * nx + ny * ny + nz * nz));
		const Vtx tri[3] = {top[b.tris[t]], top[b.tris[t + 1]], top[b.tris[t + 2]]};
		FillScreen(canvas, proj, tri, 3, Shade(kRoof, 0.82f + 0.18f * flat));
	}
}

void DrawBuildings(Canvas &canvas, const View &view)
{
	if (!view.in.buildings)
		return;
	const float margin = kMaxRoofHeight * view.in.range;
	canvas.SetTexture(nullptr);
	canvas.SetDepthMode(DepthMode::kTestWrite);
	for (const mapdata::Building &b : *view.in.buildings)
		if (view.Overlaps({b.lo.x - margin, b.lo.y - margin}, {b.hi.x + margin, b.hi.y + margin}))
			DrawBuilding(canvas, view, b);
	canvas.Flush();
	canvas.SetDepthMode(DepthMode::kOff);
}

void DrawRoute(Canvas &canvas, const View &view)
{
	const Route *route = view.in.route;
	if (!route || route->points.size() < 2)
		return;
	canvas.SetTexture(nullptr);
	for (int pass = 0; pass < 2; pass++) {
		const float hw = HalfWidth(view.proj, view.in.range, kRouteWidth, kMinRoutePx) +
		                 (pass == 0 ? std::max(kRouteOutlineWidth / view.in.range, 1.0f / view.proj.PixelsPerUnit()) : 0.0f);
		const CRGBA col = pass == 0 ? kRouteOutline : route->colour;
		for (size_t i = 0; i + 1 < route->points.size(); i++)
			ThickSegment(canvas, view, route->points[i], route->points[i + 1], hw, col);
		for (const Vec2 &p : route->points)
			Disc(canvas, view, p, hw, col);
	}
	canvas.Flush();
}

} // namespace

void DrawScene(Canvas &canvas, const Projector &proj, const SceneInput &in)
{
	const View view = MakeView(proj, in);
	const float x = proj.X(), y = proj.Y(), w = proj.W(), h = proj.H();

	if (in.background) {
		canvas.SetTexture(nullptr);
		const float s = std::max(2.0f, h * 0.015f);
		canvas.Quad({x + s, y + s}, {x + w + s, y + s}, {x + w + s, y + h + s}, {x + s, y + h + s}, kShadow);
		// The (opaque) water also resets the depth buffer under the minimap.
		canvas.SetDepthMode(DepthMode::kWrite);
		const Vtx tl{x, y, 0, 0, kClearDepth}, tr{x + w, y, 0, 0, kClearDepth}, br{x + w, y + h, 0, 0, kClearDepth},
		    bl{x, y + h, 0, 0, kClearDepth};
		canvas.Tri(tl, tr, br, kBaseColours[mapdata::kWater]);
		canvas.Tri(tl, br, bl, kBaseColours[mapdata::kWater]);
		canvas.SetDepthMode(DepthMode::kOff);
	}

	DrawBase(canvas, view);
	DrawRoads(canvas, view);
	DrawBuildings(canvas, view);
	DrawRoute(canvas, view);

	// Depth cue: the far edge of the tilted map fades into haze. Then the frame.
	canvas.SetTexture(nullptr);
	canvas.VerticalGradient(x, y, w, h * 0.3f, {10, 16, 24, 120}, {10, 16, 24, 0});
	const float t = std::max(1.0f, h * 0.01f);
	canvas.Quad({x - t, y - t}, {x + w + t, y - t}, {x + w + t, y}, {x - t, y}, kBorder);
	canvas.Quad({x - t, y + h}, {x + w + t, y + h}, {x + w + t, y + h + t}, {x - t, y + h + t}, kBorder);
	canvas.Quad({x - t, y}, {x, y}, {x, y + h}, {x - t, y + h}, kBorder);
	canvas.Quad({x + w, y}, {x + w + t, y}, {x + w + t, y + h}, {x + w, y + h}, kBorder);
	canvas.Flush();
}

void DrawNorthMarker(Canvas &canvas, const Projector &proj, const SceneInput &in)
{
	// World north (0, 1) in ground space, pushed out to the visible edge.
	const Vec2 edge = proj.ClampToEdge({in.sin * 100.0f, in.cos * 100.0f});
	Vec2 p;
	if (!proj.GroundToScreen(edge, p))
		return;
	const float r = std::max(6.0f, proj.H() * 0.052f);
	p.x = std::clamp(p.x, proj.X() + r + 2, proj.X() + proj.W() - r - 2);
	p.y = std::clamp(p.y, proj.Y() + r + 2, proj.Y() + proj.H() - r - 2);

	canvas.SetTexture(nullptr);
	auto disc = [&](float radius, CRGBA col) {
		constexpr int kSeg = 20;
		for (int i = 0; i < kSeg; i++) {
			const float a0 = 2.0f * float(M_PI) * i / kSeg, a1 = 2.0f * float(M_PI) * (i + 1) / kSeg;
			canvas.Tri({p.x, p.y, 0, 0}, {p.x + radius * cosf(a0), p.y + radius * sinf(a0), 0, 0},
			           {p.x + radius * cosf(a1), p.y + radius * sinf(a1), 0, 0}, col);
		}
	};
	disc(r + std::max(1.0f, r * 0.12f), {20, 22, 26, 230});
	disc(r, {245, 246, 248, 255});
	// The letter: two uprights and a diagonal.
	const float hw = r * 0.11f, sx = r * 0.36f, sy = r * 0.5f;
	const CRGBA ink{24, 26, 30, 255};
	auto stroke = [&](Vec2 a, Vec2 b) {
		const float dx = b.x - a.x, dy = b.y - a.y, len = hypotf(dx, dy);
		const float nx = -dy / len * hw, ny = dx / len * hw;
		canvas.Quad({a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, ink);
	};
	stroke({p.x - sx, p.y + sy}, {p.x - sx, p.y - sy});
	stroke({p.x - sx, p.y - sy}, {p.x + sx, p.y + sy});
	stroke({p.x + sx, p.y + sy}, {p.x + sx, p.y - sy});
	canvas.Flush();
}

std::vector<uint8_t> BuildBaseImage(const mapdata::Map &map)
{
	const int size = map.size;
	const float pxPerUnit = size / mapdata::kWorldSize;
	std::vector<uint8_t> nearRoad(size_t(size) * size, 0);
	for (const mapdata::Road &road : map.roads) {
		const float r = std::max(kNearRoadRadius, road.width * 0.5f + 10.0f) * pxPerUnit;
		const float ax = (road.a.x - mapdata::kWorldMin) * pxPerUnit;
		const float ay = (mapdata::kWorldMin + mapdata::kWorldSize - road.a.y) * pxPerUnit;
		const float bx = (road.b.x - mapdata::kWorldMin) * pxPerUnit;
		const float by = (mapdata::kWorldMin + mapdata::kWorldSize - road.b.y) * pxPerUnit;
		const float dx = bx - ax, dy = by - ay, len2 = std::max(dx * dx + dy * dy, 1e-6f);
		for (int y = std::max(0, int(std::min(ay, by) - r)); y <= std::min(size - 1, int(std::max(ay, by) + r)); y++)
			for (int x = std::max(0, int(std::min(ax, bx) - r)); x <= std::min(size - 1, int(std::max(ax, bx) + r)); x++) {
				const float t = std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f);
				const float ex = ax + dx * t - x, ey = ay + dy * t - y;
				if (ex * ex + ey * ey <= r * r)
					nearRoad[size_t(y) * size + x] = 1;
			}
	}
	std::vector<uint8_t> rgba(size_t(size) * size * 4);
	for (size_t i = 0; i < map.classes.size(); i++) {
		const uint8_t c = map.classes[i];
		// Runways and car parks stay; misdrawn copies of real roads and bridges go.
		const bool tarmac = (c & mapdata::kArtRoad) && (c & mapdata::kClassMask) != mapdata::kWater && !nearRoad[i];
		const CRGBA col = tarmac ? kTarmac : kBaseColours[c & 3];
		rgba[i * 4] = col.r;
		rgba[i * 4 + 1] = col.g;
		rgba[i * 4 + 2] = col.b;
		rgba[i * 4 + 3] = 255;
	}
	return rgba;
}
