#include "vmarms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>

namespace vm {
namespace {

constexpr int kRightTags[4] = {22, 23, 24, 25}, kLeftTags[4] = {32, 33, 34, 35}; // upper arm, forearm, hand, fingers
constexpr float kNarrow = 0.86f;      // of each finger's width kept, about its own middle
constexpr float kThumbBelow = 0.032f; // m to the palm's side: the hand's vertices past it are his thumb...
constexpr float kThumbSide = 0.01f;   // ...on the thumb's side of the hand...
constexpr float kThumbFront = 0.0f;   // ...ahead of the wrist
constexpr float kRamp = 0.004f;       // m: how gradually a vertex goes over to the thumb
constexpr int kMinSlab = 12, kMinThumb = 8;
constexpr float kOnPlane = 1e-6f, kOnCut = 1e-5f;

// A vertex while it's worked on: in the hand's frame, weighted per arm bone
// and (till they're shared out) on his finger bone.
struct Work {
	Vec3 pos, normal;
	float u, v;
	float w[kArmBones];
	float finger;
	bool slab;      // strongest on the finger bone, or made by cutting it
	int band = -1;  // which finger, by z (0 lowest)
	int from = -1;  // a cap's vertex: the side vertex it copies
	float g = 0.0f; // along the finger from the knuckles (m)
};
struct Tri {
	int v[3];
	uint16_t material;
	bool slab;
	bool cap = false;
	int band = -1;
};

float Smooth(float a, float b, float x)
{
	const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

Work Mix(const Work &a, const Work &b, float t)
{
	Work m = a;
	m.pos = Lerp(a.pos, b.pos, t);
	m.normal = Normal(Lerp(a.normal, b.normal, t));
	m.u = a.u + (b.u - a.u) * t;
	m.v = a.v + (b.v - a.v) * t;
	for (int k = 0; k < kArmBones; k++)
		m.w[k] = a.w[k] + (b.w[k] - a.w[k]) * t;
	m.finger = a.finger + (b.finger - a.finger) * t;
	m.slab = true;
	m.band = -1;
	return m;
}

// Cuts the slab's triangles across the plane z = zc, sharing each new vertex
// between the triangles either side of its edge.
void Bisect(std::vector<Work> &verts, std::vector<Tri> &tris, float zc)
{
	std::map<std::pair<int, int>, int> made;
	auto cross = [&](int a, int b) {
		const auto key = std::minmax(a, b);
		auto it = made.find(key);
		if (it != made.end())
			return it->second;
		const float da = verts[a].pos.z - zc, db = verts[b].pos.z - zc;
		Work m = Mix(verts[a], verts[b], da / (da - db));
		m.pos.z = zc;
		verts.push_back(m);
		made[key] = int(verts.size()) - 1;
		return int(verts.size()) - 1;
	};
	std::vector<Tri> out;
	out.reserve(tris.size() * 2);
	for (const Tri &t : tris) {
		int s[3];
		bool above = false, below = false;
		for (int c = 0; c < 3; c++) {
			const float d = verts[t.v[c]].pos.z - zc;
			s[c] = d > kOnPlane ? 1 : d < -kOnPlane ? -1 : 0;
			above |= s[c] > 0;
			below |= s[c] < 0;
		}
		if (!t.slab || !above || !below) {
			out.push_back(t);
			continue;
		}
		// Each side's polygon, in the triangle's own winding.
		std::vector<int> up, down;
		for (int c = 0; c < 3; c++) {
			const int a = t.v[c], b = t.v[(c + 1) % 3], sa = s[c], sb = s[(c + 1) % 3];
			if (sa >= 0)
				up.push_back(a);
			if (sa <= 0)
				down.push_back(a);
			if (sa * sb < 0) {
				const int m = cross(a, b);
				up.push_back(m);
				down.push_back(m);
			}
		}
		for (const auto *poly : {&up, &down})
			for (size_t k = 1; k + 1 < poly->size(); k++)
				out.push_back({{(*poly)[0], (*poly)[k], (*poly)[k + 1]}, t.material, true});
	}
	tris.swap(out);
}

// The direction points spread most along (power iteration on their covariance).
Vec3 MainAxis(const std::vector<Vec3> &pts, Vec3 c)
{
	float cov[3][3] = {};
	for (const Vec3 &p : pts) {
		const float d[3] = {p.x - c.x, p.y - c.y, p.z - c.z};
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				cov[i][j] += d[i] * d[j];
	}
	Vec3 v{1.0f, 0.1f, 0.1f};
	for (int it = 0; it < 64; it++)
		v = Normal({cov[0][0] * v.x + cov[0][1] * v.y + cov[0][2] * v.z, cov[1][0] * v.x + cov[1][1] * v.y + cov[1][2] * v.z,
		            cov[2][0] * v.x + cov[2][1] * v.y + cov[2][2] * v.z});
	return v;
}

// A bone from `from` to `to`: x along it, z as near the hand's z as it can be.
Frame Along(Vec3 from, Vec3 to)
{
	const Vec3 x = Normal(Sub(to, from));
	Vec3 z = Sub(Vec3{0, 0, 1}, Mul(x, x.z));
	z = Length(z) > 1e-4f ? Normal(z) : Normal(Cross(x, {0, 1, 0}));
	return {x, Cross(z, x), z, from};
}

// Where `p` stands off the line a-b (the part of p - a across it).
Vec3 OffLine(Vec3 a, Vec3 b, Vec3 p)
{
	const Vec3 d = Normal(Sub(b, a)), ap = Sub(p, a);
	return Sub(ap, Mul(d, Dot(ap, d)));
}

class Builder {
public:
	Builder(const SkinnedMesh &mesh, const int *tags, const char *name) : mesh_(mesh), tags_(tags), name_(name) {}

	bool Build(Arm &out, std::string &why)
	{
		return Find(why) && Take() && Slab(why) && Cut() && Caps() && Narrow() && Thumb(why) && Fingers(why) && Out(out, why);
	}

private:
	const SkinnedMesh &mesh_;
	const int *tags_;
	const char *name_;
	int bone_[4] = {-1, -1, -1, -1};
	Frame hand_{}, toHand_{};
	std::vector<Work> verts_;
	std::vector<Tri> tris_;
	float z0_ = 0, z1_ = 0, cut_[4] = {};
	float side_ = 1.0f; // the thumb's side of the hand in z
	Vec3 knuckle_{};
	Frame local_[kArmBones]; // the new bones, in the hand's frame
	Vec3 tip_[kDigits], pole_[kDigits];

	bool Refuse(std::string &why, const std::string &what)
	{
		why = std::string(name_) + " " + what;
		return false;
	}

	bool Find(std::string &why)
	{
		for (int i = 0; i < 4; i++) {
			for (size_t b = 0; b < mesh_.tags.size(); b++)
				if (mesh_.tags[b] == tags_[i])
					bone_[i] = int(b);
			if (bone_[i] < 0)
				return Refuse(why, "arm: the model has no bone " + std::to_string(tags_[i]));
		}
		hand_ = mesh_.bind[bone_[2]];
		toHand_ = Inverse(hand_);
		knuckle_ = Apply(toHand_, mesh_.bind[bone_[3]].pos);
		return true;
	}

	int Strongest(const SkinVertex &v) const
	{
		int best = 0;
		for (int k = 1; k < 4; k++)
			if (v.weight[k] > v.weight[best])
				best = k;
		return v.bone[best];
	}

	bool InArm(int bone) const { return bone == bone_[0] || bone == bone_[1] || bone == bone_[2] || bone == bone_[3]; }

	// His arm's triangles (every corner strongest on one of its bones), in the hand's frame.
	bool Take()
	{
		std::vector<int> remap(mesh_.vertices.size(), -1);
		for (const Triangle &t : mesh_.triangles) {
			if (!InArm(Strongest(mesh_.vertices[t.v[0]])) || !InArm(Strongest(mesh_.vertices[t.v[1]])) ||
			    !InArm(Strongest(mesh_.vertices[t.v[2]])))
				continue;
			Tri out{{0, 0, 0}, t.material, true};
			for (int c = 0; c < 3; c++) {
				int &r = remap[t.v[c]];
				if (r < 0) {
					verts_.push_back(Convert(mesh_.vertices[t.v[c]]));
					r = int(verts_.size()) - 1;
				}
				out.v[c] = r;
				out.slab = out.slab && verts_[r].slab;
			}
			tris_.push_back(out);
		}
		return true;
	}

	Work Convert(const SkinVertex &s) const
	{
		Work w{};
		w.pos = Apply(toHand_, s.pos);
		w.normal = Normal(Rotate(toHand_, s.normal));
		w.u = s.u;
		w.v = s.v;
		for (int k = 0; k < 4; k++) {
			const int b = s.bone[k];
			const float wt = s.weight[k];
			if (wt <= 0.0f)
				continue;
			if (b == bone_[1])
				w.w[kFore] += wt;
			else if (b == bone_[2])
				w.w[kHand] += wt;
			else if (b == bone_[3])
				w.finger += wt;
			else
				w.w[kUpper] += wt; // the upper arm's own, and any other bone's (his clavicle's, say)
		}
		w.slab = Strongest(s) == bone_[3];
		return w;
	}

	bool Slab(std::string &why)
	{
		int n = 0;
		z0_ = std::numeric_limits<float>::max();
		z1_ = -z0_;
		for (const Work &w : verts_)
			if (w.slab) {
				n++;
				z0_ = std::min(z0_, w.pos.z);
				z1_ = std::max(z1_, w.pos.z);
			}
		if (n < kMinSlab)
			return Refuse(why, "hand: its finger slab has " + std::to_string(n) + " vertices");
		if (z1_ - z0_ < 0.01f)
			return Refuse(why, "hand: its finger slab is too narrow to cut into fingers");
		for (int k = 0; k <= 3; k++)
			cut_[k] = z0_ + (z1_ - z0_) * float(k) / 4.0f;
		return true;
	}

	int BandOf(float z) const { return std::clamp(int((z - z0_) / (z1_ - z0_) * 4.0f), 0, 3); }
	float Centre(int band) const { return z0_ + (z1_ - z0_) * (float(band) + 0.5f) / 4.0f; }

	// Cut at the quarters; each piece's vertices its own (the cuts' copied).
	bool Cut()
	{
		for (int k = 1; k <= 3; k++)
			Bisect(verts_, tris_, cut_[k]);
		std::map<std::pair<int, int>, int> copies;
		for (Tri &t : tris_) {
			if (!t.slab)
				continue;
			t.band = BandOf((verts_[t.v[0]].pos.z + verts_[t.v[1]].pos.z + verts_[t.v[2]].pos.z) / 3.0f);
			for (int &v : t.v) {
				if (verts_[v].band < 0) {
					verts_[v].band = t.band;
				} else if (verts_[v].band != t.band) {
					auto it = copies.find({v, t.band});
					if (it == copies.end()) {
						Work copy = verts_[v];
						copy.band = t.band;
						verts_.push_back(copy);
						it = copies.emplace(std::make_pair(v, t.band), int(verts_.size()) - 1).first;
					}
					v = it->second;
				}
			}
		}
		for (Work &w : verts_) // slab vertices only the knuckles' triangles use
			if (w.slab && w.band < 0)
				w.band = BandOf(w.pos.z);
		return true;
	}

	// A flat loop on a cut (its vertices in order) in triangles, ear by ear in
	// its plane (x, y), so a hooked finger's U-shaped cut is filled and its
	// hollow isn't.
	std::vector<std::array<int, 3>> EarClip(std::vector<int> poly) const
	{
		std::vector<std::array<int, 3>> out;
		auto at = [&](int v) { return verts_[v].pos; };
		auto cross = [&](int a, int b, int c) {
			const Vec3 p = at(a), q = at(b), r = at(c);
			return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
		};
		float area = 0.0f;
		for (size_t i = 0; i < poly.size(); i++)
			area += cross(poly[0], poly[i], poly[(i + 1) % poly.size()]);
		const float turn = area >= 0.0f ? 1.0f : -1.0f;
		while (poly.size() > 3) {
			bool clipped = false;
			for (size_t i = 0; i < poly.size() && !clipped; i++) {
				const size_t n = poly.size();
				const int a = poly[(i + n - 1) % n], b = poly[i], c = poly[(i + 1) % n];
				if (cross(a, b, c) * turn <= 1e-12f)
					continue; // reflex (or flat): not an ear
				bool empty = true;
				for (int v : poly)
					if (v != a && v != b && v != c && cross(a, b, v) * turn > 0.0f && cross(b, c, v) * turn > 0.0f &&
					    cross(c, a, v) * turn > 0.0f)
						empty = false;
				if (!empty)
					continue;
				out.push_back({a, b, c});
				poly.erase(poly.begin() + long(i));
				clipped = true;
			}
			if (!clipped)
				break; // degenerate: what's left, as a fan
		}
		for (size_t i = 1; i + 1 < poly.size(); i++)
			out.push_back({poly[0], poly[i], poly[i + 1]});
		return out;
	}

	// Each piece's open edges on a cut, closed with the finger's own skin.
	bool Caps()
	{
		const size_t sides = tris_.size();
		for (int band = 0; band < 4; band++)
			for (int k = band; k <= band + 1; k++) {
				if (k < 1 || k > 3)
					continue;
				const float zc = cut_[k];
				auto onCut = [&](int v) { return std::fabs(verts_[v].pos.z - zc) < kOnCut; };
				std::map<std::pair<int, int>, int> uses;
				std::vector<std::pair<std::pair<int, int>, uint16_t>> edges; // directed, with the material
				for (size_t i = 0; i < sides; i++) {
					const Tri &t = tris_[i];
					if (!t.slab || t.band != band)
						continue;
					for (int c = 0; c < 3; c++) {
						const int a = t.v[c], b = t.v[(c + 1) % 3];
						if (!onCut(a) || !onCut(b))
							continue;
						uses[std::minmax(a, b)]++;
						edges.push_back({{a, b}, t.material});
					}
				}
				// The open ones, grouped into loops by shared vertices.
				std::map<int, int> parent;
				std::function<int(int)> find = [&](int a) { return parent[a] == a ? a : parent[a] = find(parent[a]); };
				std::vector<std::pair<std::pair<int, int>, uint16_t>> open;
				for (auto &e : edges)
					if (uses[std::minmax(e.first.first, e.first.second)] == 1) {
						open.push_back(e);
						for (int v : {e.first.first, e.first.second})
							if (!parent.count(v))
								parent[v] = v;
						parent[find(e.first.first)] = find(e.first.second);
					}
				std::map<int, std::vector<size_t>> loops;
				for (size_t i = 0; i < open.size(); i++)
					loops[find(open[i].first.first)].push_back(i);
				const Vec3 normal{0, 0, k == band + 1 ? 1.0f : -1.0f}; // out of the piece
				for (auto &loop : loops) {
					if (loop.second.size() < 3)
						continue;
					// The cap goes round the loop the other way to the sides' edges.
					std::map<int, int> next;
					std::map<int, uint16_t> material;
					for (size_t i : loop.second) {
						next[open[i].first.second] = open[i].first.first;
						material[open[i].first.second] = open[i].second;
					}
					std::vector<int> ring{next.begin()->first};
					while (ring.size() <= next.size()) {
						auto it = next.find(ring.back());
						if (it == next.end() || it->second == ring.front())
							break;
						ring.push_back(it->second);
					}
					std::vector<int> poly;
					for (int v : ring) {
						Work c = verts_[v]; // its UVs kept: the cap wears the finger's own skin
						c.normal = normal;
						c.from = v;
						verts_.push_back(c);
						poly.push_back(int(verts_.size()) - 1);
					}
					for (const auto &t : EarClip(poly))
						tris_.push_back({{t[0], t[1], t[2]}, material[ring[0]], true, true, band});
				}
			}
		return true;
	}

	// The fingers parted: each narrowed about its own middle.
	bool Narrow()
	{
		for (Work &w : verts_)
			if (w.slab && w.band >= 0) {
				const float c = Centre(w.band);
				w.pos.z = c + (w.pos.z - c) * kNarrow;
			}
		return true;
	}

	// His thumb: its bones, and the hand's weight moved onto them.
	bool Thumb(std::string &why)
	{
		std::vector<Vec3> pts, hand;
		float zSum = 0.0f;
		for (const Work &w : verts_)
			if (!w.slab && w.w[kHand] > 0.5f) {
				hand.push_back(w.pos);
				if (w.pos.y > kThumbBelow && w.pos.x > kThumbFront && std::fabs(w.pos.z) > kThumbSide)
					zSum += w.pos.z;
			}
		side_ = zSum >= 0.0f ? 1.0f : -1.0f;
		for (const Work &w : verts_)
			if (!w.slab && w.w[kHand] > 0.5f && w.pos.y > kThumbBelow && w.pos.x > kThumbFront && w.pos.z * side_ > kThumbSide)
				pts.push_back(w.pos);
		if (int(pts.size()) < kMinThumb)
			return Refuse(why, "hand: its thumb has " + std::to_string(pts.size()) + " vertices");
		Vec3 c{0, 0, 0}, palm{0, 0, 0};
		for (const Vec3 &p : pts)
			c = Add(c, p);
		c = Mul(c, 1.0f / float(pts.size()));
		for (const Vec3 &p : hand)
			palm = Add(palm, p);
		palm = Mul(palm, 1.0f / float(hand.size()));
		Vec3 axis = MainAxis(pts, c);
		if (axis.x < 0.0f)
			axis = Mul(axis, -1.0f); // towards the fingers
		float lo = std::numeric_limits<float>::max(), hi = -lo;
		for (const Vec3 &p : pts) {
			lo = std::min(lo, Dot(Sub(p, c), axis));
			hi = std::max(hi, Dot(Sub(p, c), axis));
		}
		const Vec3 a = Add(c, Mul(axis, lo + (hi - lo) * 0.2f)), m = Add(c, Mul(axis, lo + (hi - lo) * 0.6f)),
		           e = Add(c, Mul(axis, hi));
		local_[kThumb] = Along(a, m);
		local_[kThumbTip] = Along(m, e);
		tip_[kDigitThumb] = e;
		const Vec3 out = Sub(Sub(c, palm), Mul(axis, Dot(Sub(c, palm), axis))); // away from the palm
		pole_[kDigitThumb] = Length(out) > 1e-5f ? Normal(out) : Vec3{0, -1, 0};
		for (Work &w : verts_) {
			if (w.slab || w.w[kHand] <= 0.0f)
				continue;
			const float t = (Dot(Sub(w.pos, c), axis) - lo) / (hi - lo);
			const float below = Smooth(kThumbBelow - kRamp, kThumbBelow + kRamp, w.pos.y);
			const float beside = Smooth(kThumbSide - kRamp, kThumbSide + kRamp, w.pos.z * side_);
			const float ahead = Smooth(kThumbFront - kRamp, kThumbFront + kRamp, w.pos.x);
			const float on = Smooth(0.1f, 0.3f, t) * below * beside * ahead, tip = Smooth(0.5f, 0.65f, t) * on;
			const float wh = w.w[kHand];
			w.w[kHand] = wh * (1.0f - on);
			w.w[kThumb] += wh * (on - tip);
			w.w[kThumbTip] += wh * tip;
		}
		return true;
	}

	// Each finger's joints from its own shape: how far each of its vertices is
	// along it from the knuckles (over its edges), the tip past 0.9 of the way,
	// the bend half way.
	bool Fingers(std::string &why)
	{
		for (int band = 0; band < 4; band++) {
			const int digit = side_ > 0.0f ? 3 - band : band; // the index on the thumb's side
			std::vector<std::vector<std::pair<int, float>>> next(verts_.size());
			std::vector<char> attached(verts_.size(), 0), in(verts_.size(), 0);
			for (const Tri &t : tris_) {
				const bool mine = t.slab && !t.cap && t.band == band;
				for (int c = 0; c < 3; c++) {
					const int a = t.v[c], b = t.v[(c + 1) % 3];
					if (mine) {
						const float l = Length(Sub(verts_[a].pos, verts_[b].pos));
						next[a].push_back({b, l});
						next[b].push_back({a, l});
						in[a] = in[b] = 1;
					} else if (!t.slab && verts_[a].slab && verts_[a].band == band) {
						attached[a] = 1; // joined to the palm
					}
				}
			}
			const Vec3 knuckle{knuckle_.x, knuckle_.y, Centre(band)};
			std::vector<float> g(verts_.size(), std::numeric_limits<float>::infinity());
			using Item = std::pair<float, int>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
			bool any = false;
			for (size_t v = 0; v < verts_.size(); v++)
				if (in[v] && attached[v]) {
					g[v] = 0.0f;
					queue.push({0.0f, int(v)});
					any = true;
				}
			if (!any) { // not joined: from the vertices nearest the knuckle
				float nearest = std::numeric_limits<float>::max();
				for (size_t v = 0; v < verts_.size(); v++)
					if (in[v])
						nearest = std::min(nearest, Length(Sub(verts_[v].pos, knuckle)));
				for (size_t v = 0; v < verts_.size(); v++)
					if (in[v] && Length(Sub(verts_[v].pos, knuckle)) <= nearest + 0.003f) {
						g[v] = 0.0f;
						queue.push({0.0f, int(v)});
					}
			}
			while (!queue.empty()) {
				const auto [d, v] = queue.top();
				queue.pop();
				if (d > g[v])
					continue;
				for (const auto &[u, l] : next[v])
					if (d + l < g[u]) {
						g[u] = d + l;
						queue.push({g[u], u});
					}
			}
			float most = 0.0f;
			for (size_t v = 0; v < verts_.size(); v++)
				if (in[v] && std::isfinite(g[v]))
					most = std::max(most, g[v]);
			if (most < 0.005f)
				return Refuse(why, "hand: a finger is too short to bend (" + std::to_string(most) + " m)");
			Vec3 tip{0, 0, 0}, bend{0, 0, 0};
			int tips = 0, bends = 0;
			for (size_t v = 0; v < verts_.size(); v++) {
				if (!in[v] || !std::isfinite(g[v]))
					continue;
				const float f = g[v] / most;
				if (f >= 0.9f) {
					tip = Add(tip, verts_[v].pos);
					tips++;
				}
				if (f >= 0.45f && f <= 0.55f) {
					bend = Add(bend, verts_[v].pos);
					bends++;
				}
			}
			if (!bends) { // none half way: the two nearest it
				std::vector<std::pair<float, int>> by;
				for (size_t v = 0; v < verts_.size(); v++)
					if (in[v] && std::isfinite(g[v]))
						by.push_back({std::fabs(g[v] / most - 0.5f), int(v)});
				std::sort(by.begin(), by.end());
				for (size_t i = 0; i < 2 && i < by.size(); i++, bends++)
					bend = Add(bend, verts_[by[i].second].pos);
			}
			tip = Mul(tip, 1.0f / float(tips));
			bend = Mul(bend, 1.0f / float(bends));
			const int base = DigitBase(digit);
			local_[base] = Along(knuckle, bend);
			local_[base + 1] = Along(bend, tip);
			tip_[digit] = tip;
			const Vec3 out = OffLine(knuckle, tip, bend);
			pole_[digit] = Length(out) > 1e-5f ? Normal(out) : Vec3{1, 0, 0};
			// Its weight shared between the two: the tip's past half way.
			for (size_t v = 0; v < verts_.size(); v++) {
				Work &w = verts_[v];
				if (w.finger <= 0.0f || (w.slab ? w.band : BandOf(w.pos.z)) != band)
					continue;
				float along = in[v] && std::isfinite(g[v]) ? g[v] / most : 0.0f;
				if (w.from >= 0 && std::isfinite(g[w.from]))
					along = g[w.from] / most;
				const float t = Smooth(0.4f, 0.6f, along);
				w.w[base] += w.finger * (1.0f - t);
				w.w[base + 1] += w.finger * t;
				w.finger = 0.0f;
			}
		}
		return true;
	}

	bool Out(Arm &arm, std::string &why)
	{
		if (verts_.size() > 65535)
			return Refuse(why, "arm: too many vertices");
		arm.vertices.clear();
		arm.triangles.clear();
		for (const Work &w : verts_) {
			SkinVertex s{};
			s.pos = Apply(hand_, w.pos);
			s.normal = Normal(Rotate(hand_, w.normal));
			s.u = w.u;
			s.v = w.v;
			// Its four strongest bones, their weights summing to one.
			int order[kArmBones];
			for (int b = 0; b < kArmBones; b++)
				order[b] = b;
			std::stable_sort(order, order + kArmBones, [&](int a, int b) { return w.w[a] > w.w[b]; });
			float sum = 0.0f;
			for (int k = 0; k < 4; k++)
				sum += w.w[order[k]];
			for (int k = 0; k < 4; k++) {
				s.bone[k] = uint8_t(order[k]);
				s.weight[k] = sum > 0.0f ? w.w[order[k]] / sum : (k == 0 ? 1.0f : 0.0f);
			}
			arm.vertices.push_back(s);
		}
		for (const Tri &t : tris_)
			arm.triangles.push_back({{uint16_t(t.v[0]), uint16_t(t.v[1]), uint16_t(t.v[2])}, t.material});
		arm.bind[kUpper] = mesh_.bind[bone_[0]];
		arm.bind[kFore] = mesh_.bind[bone_[1]];
		arm.bind[kHand] = hand_;
		for (int b = kIndex; b < kArmBones; b++)
			arm.bind[b] = Compose(hand_, local_[b]);
		for (int d = 0; d < kDigits; d++) {
			arm.tip[d] = Apply(hand_, tip_[d]);
			arm.pole[d] = Normal(Rotate(hand_, pole_[d]));
		}
		return true;
	}
};

} // namespace

Arms Articulate(const SkinnedMesh &mesh)
{
	Arms arms;
	arms.ok = Builder(mesh, kRightTags, "right").Build(arms.right, arms.why) &&
	          Builder(mesh, kLeftTags, "left").Build(arms.left, arms.why);
	return arms;
}

} // namespace vm
