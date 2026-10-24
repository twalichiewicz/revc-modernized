// vcmm-preview: renders the minimap scene offline (software rasteriser) so the
// look can be iterated on without launching the game.
//
// usage: vcmm-preview <map.bin> <roads.csv> <out.png> <x> <y> <heading deg> <range> [targetX targetY]
//   roads.csv comes from VCHUD_DUMP_ROADS; heading 0 = camera facing north.
#include "../../src/config.h"
#include "../../src/scene.h"
#include "../bake/gamefiles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <queue>
#include <sstream>

namespace {

// The car path network as dumped from the game, for routing test routes.
struct RoadGraph {
	std::vector<Vec2> nodes;
	std::vector<std::pair<int, int>> edges;
};

struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba;
};

class SoftCanvas : public Canvas {
public:
	using Canvas::Tri;
	explicit SoftCanvas(Image &target) : fb_(target), depth_(size_t(target.w) * target.h, 1.0f) {}
	void SetTexture(void *texture) override { tex_ = static_cast<const Image *>(texture); }
	void SetDepthMode(DepthMode mode) override { mode_ = mode; }
	void Flush() override {}

	void Tri(const Vtx &a, const Vtx &b, const Vtx &c, CRGBA ca, CRGBA cb, CRGBA cc) override
	{
		const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
		if (fabsf(area) < 1e-6f)
			return;
		const int x0 = std::max(0, int(floorf(std::min({a.x, b.x, c.x}))));
		const int x1 = std::min(fb_.w - 1, int(ceilf(std::max({a.x, b.x, c.x}))));
		const int y0 = std::max(0, int(floorf(std::min({a.y, b.y, c.y}))));
		const int y1 = std::min(fb_.h - 1, int(ceilf(std::max({a.y, b.y, c.y}))));
		for (int y = y0; y <= y1; y++)
			for (int x = x0; x <= x1; x++) {
				const float px = x + 0.5f, py = y + 0.5f;
				const float w0 = ((b.x - px) * (c.y - py) - (c.x - px) * (b.y - py)) / area;
				const float w1 = ((c.x - px) * (a.y - py) - (a.x - px) * (c.y - py)) / area;
				const float w2 = 1 - w0 - w1;
				if (w0 < 0 || w1 < 0 || w2 < 0)
					continue;
				if (mode_ != DepthMode::kOff) {
					const float z = w0 * a.depth + w1 * b.depth + w2 * c.depth;
					float &stored = depth_[size_t(y) * fb_.w + x];
					if (mode_ == DepthMode::kTestWrite && z > stored)
						continue;
					stored = z;
				}
				float col[4] = {w0 * ca.r + w1 * cb.r + w2 * cc.r, w0 * ca.g + w1 * cb.g + w2 * cc.g,
				                w0 * ca.b + w1 * cb.b + w2 * cc.b, w0 * ca.a + w1 * cb.a + w2 * cc.a};
				if (tex_) {
					float t[4];
					Sample(w0 * a.u + w1 * b.u + w2 * c.u, w0 * a.v + w1 * b.v + w2 * c.v, t);
					for (int k = 0; k < 4; k++)
						col[k] = col[k] * t[k] / 255.0f;
				}
				uint8_t *dst = &fb_.rgba[(size_t(y) * fb_.w + x) * 4];
				const float al = col[3] / 255.0f;
				for (int k = 0; k < 3; k++)
					dst[k] = uint8_t(std::clamp(col[k] * al + dst[k] * (1 - al), 0.0f, 255.0f));
				dst[3] = 255;
			}
	}

private:
	void Sample(float u, float v, float out[4]) const
	{
		const float fx = std::clamp(u, 0.0f, 1.0f) * tex_->w - 0.5f, fy = std::clamp(v, 0.0f, 1.0f) * tex_->h - 0.5f;
		const int x0 = std::clamp(int(floorf(fx)), 0, tex_->w - 1), y0 = std::clamp(int(floorf(fy)), 0, tex_->h - 1);
		const int x1 = std::min(x0 + 1, tex_->w - 1), y1 = std::min(y0 + 1, tex_->h - 1);
		const float tx = std::clamp(fx - x0, 0.0f, 1.0f), ty = std::clamp(fy - y0, 0.0f, 1.0f);
		auto at = [&](int x, int y, int k) { return float(tex_->rgba[(size_t(y) * tex_->w + x) * 4 + k]); };
		for (int k = 0; k < 4; k++)
			out[k] = (at(x0, y0, k) * (1 - tx) + at(x1, y0, k) * tx) * (1 - ty) +
			         (at(x0, y1, k) * (1 - tx) + at(x1, y1, k) * tx) * ty;
	}

	Image &fb_;
	std::vector<float> depth_;
	DepthMode mode_ = DepthMode::kOff;
	const Image *tex_ = nullptr;
};

bool LoadRoads(const char *path, RoadGraph &roads, std::vector<std::vector<int>> &adj)
{
	std::ifstream f(path);
	std::string line;
	std::vector<std::vector<int>> links;
	std::vector<bool> water;
	while (std::getline(f, line)) {
		std::replace(line.begin(), line.end(), ',', ' ');
		std::istringstream ss(line);
		std::string tag;
		int id, width;
		float x, y, z;
		unsigned flags;
		if (!(ss >> tag >> id >> x >> y >> z >> width >> flags))
			continue;
		roads.nodes.push_back({x, y});
		water.push_back(flags & 0x100);
		links.emplace_back();
		for (int j; ss >> j;)
			links.back().push_back(j);
	}
	adj.assign(roads.nodes.size(), {});
	for (size_t i = 0; i < links.size(); i++)
		for (int j : links[i])
			if (j >= 0 && size_t(j) < links.size() && !water[i] && !water[size_t(j)]) {
				adj[i].push_back(j);
				if (int(i) < j)
					roads.edges.push_back({int(i), j});
			}
	return !roads.nodes.empty();
}

int Nearest(const RoadGraph &roads, const std::vector<std::vector<int>> &adj, Vec2 p)
{
	int best = -1;
	float bestD = 1e30f;
	for (size_t i = 0; i < roads.nodes.size(); i++) {
		const float d = fabsf(roads.nodes[i].x - p.x) + fabsf(roads.nodes[i].y - p.y);
		if (!adj[i].empty() && d < bestD) {
			bestD = d;
			best = int(i);
		}
	}
	return best;
}

std::vector<Vec2> ShortestPath(const RoadGraph &roads, const std::vector<std::vector<int>> &adj, int from, int to)
{
	std::vector<float> dist(roads.nodes.size(), 1e30f);
	std::vector<int> prev(roads.nodes.size(), -1);
	std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<>> pq;
	dist[from] = 0;
	pq.push({0, from});
	while (!pq.empty()) {
		auto [d, u] = pq.top();
		pq.pop();
		if (u == to)
			break;
		if (d > dist[u])
			continue;
		for (int v : adj[u]) {
			const float nd = d + hypotf(roads.nodes[u].x - roads.nodes[v].x, roads.nodes[u].y - roads.nodes[v].y);
			if (nd < dist[v]) {
				dist[v] = nd;
				prev[v] = u;
				pq.push({nd, v});
			}
		}
	}
	std::vector<Vec2> path;
	for (int n = to; n >= 0; n = prev[n])
		path.push_back(roads.nodes[n]);
	std::reverse(path.begin(), path.end());
	return path;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 8) {
		fprintf(stderr, "usage: %s <map.bin> <roads.csv> <out.png> <x> <y> <heading> <range> [tx ty]\n", argv[0]);
		return 2;
	}
	mapdata::Map map;
	RoadGraph roads;
	std::vector<std::vector<int>> adj;
	if (!map.Load(argv[1]) || !LoadRoads(argv[2], roads, adj)) {
		fprintf(stderr, "cannot load inputs\n");
		return 1;
	}
	const Vec2 origin{float(atof(argv[4])), float(atof(argv[5]))};
	const float heading = float(atof(argv[6])) * float(M_PI) / 180.0f;

	Image base{map.size, map.size, BuildBaseImage(map)};
	Route route{{}, {164, 76, 242, 255}};
	if (argc >= 10) {
		const Vec2 target{float(atof(argv[8])), float(atof(argv[9]))};
		route.points = ShortestPath(roads, adj, Nearest(roads, adj, origin), Nearest(roads, adj, target));
	}

	// Same rectangle the game logged: 357x231 at 1920x1080-ish HUD scale.
	// VCMM_PREVIEW_SCALE enlarges the picture for close inspection.
	const float zoom = getenv("VCMM_PREVIEW_SCALE") ? float(atof(getenv("VCMM_PREVIEW_SCALE"))) : 1.0f;
	const int fbw = int(460 * zoom), fbh = int(320 * zoom);
	Image fb{fbw, fbh, std::vector<uint8_t>(size_t(fbw) * fbh * 4, 0)};
	for (size_t i = 0; i < fb.rgba.size(); i += 4) {
		fb.rgba[i] = 92;
		fb.rgba[i + 1] = 72;
		fb.rgba[i + 2] = 60;
	}
	const Config cfg;
	Projector proj;
	proj.Setup(cfg.view, 50 * zoom, 45 * zoom, 357 * zoom, 231 * zoom);
	SceneInput in;
	in.cos = cosf(-heading);
	in.sin = sinf(-heading);
	in.range = float(atof(argv[7]));
	in.origin = origin;
	in.baseTexture = &base;
	in.roads = &map.roads;
	in.buildings = &map.buildings;
	in.route = route.points.empty() ? nullptr : &route;
	in.buildingScale = cfg.buildingScale;
	SoftCanvas canvas(fb);
	DrawScene(canvas, proj, in);

	// Stand-in for the game's player arrow, then our north marker.
	Vec2 p;
	proj.GroundToScreen({0, 0}, p);
	canvas.SetTexture(nullptr);
	canvas.Tri({p.x, p.y - 9, 0, 0}, {p.x + 6, p.y + 6, 0, 0}, {p.x - 6, p.y + 6, 0, 0}, {255, 255, 255, 255});
	DrawNorthMarker(canvas, proj, in);
	WritePng(argv[3], fb.w, fb.h, fb.rgba);
	printf("route nodes: %zu\n", route.points.size());
	return 0;
}
