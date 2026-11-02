#include "reactcontact.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <dlfcn.h>
#include <limits>

namespace reactcontact {
namespace {
World gWorld;
NativeBindings gNative;
constexpr unsigned kProjectionPasses = 8, kSlidePasses = 3, kMaxDepth = 18, kMaxFiltered = 16;
constexpr float kPushEpsilon = .0002f;
Vec3 Add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float Length(Vec3 a) { return std::sqrt(Dot(a, a)); }
bool Finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

struct PointScope {
	NativePoint &point;
	NativePoint saved;
	explicit PointScope(NativePoint &p) : point(p), saved(p)
	{
		const float nan = std::numeric_limits<float>::quiet_NaN();
		point = {};
		point.point = point.normal = {nan, nan, nan};
		point.depth = nan;
	}
	~PointScope() { point = saved; }
};
struct Query {
	bool occupied = false, contact = false;
	Vec3 normal{};
	float depth = 0;
};
// A clear candidate is filtered only for the current Sphere query. A
// reentrant Sweep may test another location where that entity really collides.
// Temporarily expose all outer candidates for that nested sweep, then restore
// the exact outer filter state when it returns.
struct FilterPause;
struct NativeFilters;
NativeFilters *gFilters = nullptr;
struct NativeFilters {
	struct Entry { uint8_t *entity; uint32_t flags, pausedFlags = 0; };
	std::array<Entry, kMaxFiltered> entries{};
	unsigned count = 0;
	NativeFilters *previous = gFilters;
	const FilterPause *pausedBy = nullptr;
	NativeFilters() { gFilters = this; }
	~NativeFilters()
	{
		while (count) {
			const auto &e = entries[--count];
			std::memcpy(e.entity + 0x60, &e.flags, sizeof e.flags);
		}
		gFilters = previous;
	}
};
struct FilterPause {
	NativeFilters *first = gFilters;
	FilterPause()
	{
		for (auto *scope = first; scope; scope = scope->previous) {
			if (scope->pausedBy) continue;
			scope->pausedBy = this;
			for (unsigned i = 0; i < scope->count; ++i) {
				auto &e = scope->entries[i];
				std::memcpy(&e.pausedFlags, e.entity + 0x60, sizeof e.pausedFlags);
				std::memcpy(e.entity + 0x60, &e.flags, sizeof e.flags);
			}
		}
	}
	~FilterPause()
	{
		for (auto *scope = first; scope; scope = scope->previous) {
			if (scope->pausedBy != this) continue;
			for (unsigned i = 0; i < scope->count; ++i) {
				const auto &e = scope->entries[i];
				std::memcpy(e.entity + 0x60, &e.pausedFlags, sizeof e.pausedFlags);
			}
			scope->pausedBy = nullptr;
		}
	}
};
class Solver {
  public:
	Solver(const World &world, float radius, void *ped, Stats &stats)
	    : world_(world), radius_(radius), ped_(static_cast<uint8_t *>(ped)), stats_(stats)
	{
	}
	bool Budget()
	{
		if (stats_.queries < kMaxQueries) {
			++stats_.queries;
			return true;
		}
		stats_.exhausted = true;
		return false;
	}
	Query Contact(const NativePoint &p, float radius)
	{
		const float length = Length(p.normal);
		const bool valid = Finite(p.point) && Finite(p.normal) && std::isfinite(length) && length > .1f &&
		                   std::isfinite(p.depth) && p.depth >= 0 && p.depth <= 2 * radius + .02f;
		return {true, valid, valid ? Mul(p.normal, 1 / length) : Vec3{}, valid ? p.depth : 0};
	}
	// TestSphereAgainstSectorList (0x100131660..1688) uses this exact model
	// and matrix. Its 0x100131690..16c4 automobile fallback has no point.
	// A second native narrowphase result of zero proves only THAT car clear.
	int Refine(uint8_t *entity, Vec3 center, float radius, NativePoint &result)
	{
		if (!world_.native || !world_.native->models || !world_.native->modelInfos || entity == ped_)
			return -1;
		const uint32_t flags = *reinterpret_cast<const uint32_t *>(entity + 0x60);
		const int model = *reinterpret_cast<const int16_t *>(entity + 0x6a);
		if ((flags & 7) != 2 || !(flags & 0x100) || model < 0 || model >= 6500)
			return -1;
		auto *info = world_.native->modelInfos[model];
		auto *col = info ? *reinterpret_cast<uint8_t **>(info + 0x20) : nullptr;
		if (!col)
			return -1;
		const Vec3 boundCenter = *reinterpret_cast<const Vec3 *>(col);
		const float boundRadius = *reinterpret_cast<const float *>(col + 0xc);
		const Vec3 lower = *reinterpret_cast<const Vec3 *>(col + 0x10), upper = *reinterpret_cast<const Vec3 *>(col + 0x1c);
		if (!Finite(boundCenter) || !std::isfinite(boundRadius) || boundRadius < 0 || !Finite(lower) || !Finite(upper) ||
		    lower.x > upper.x || lower.y > upper.y || lower.z > upper.z)
			return -1;
		const int spheres = *reinterpret_cast<const int16_t *>(col + 0x28);
		const int boxes = *reinterpret_cast<const int16_t *>(col + 0x2a);
		const int triangles = *reinterpret_cast<const int16_t *>(col + 0x2c);
		if (spheres < 0 || boxes < 0 || triangles < 0 || !(spheres || boxes || triangles) ||
		    (spheres && !*reinterpret_cast<uint8_t **>(col + 0x38)) ||
		    (boxes && !*reinterpret_cast<uint8_t **>(col + 0x48)) ||
		    (triangles && (!*reinterpret_cast<uint8_t **>(col + 0x50) || !*reinterpret_cast<uint8_t **>(col + 0x58))))
			return -1;
		for (unsigned i = 0; i < 4; ++i)
			if (!Finite(*reinterpret_cast<const Vec3 *>(entity + 8 + 16 * i)))
				return -1;
		std::array<uint8_t, 0x40> transform;
		std::array<uint8_t, 0x60> geometry;
		std::memcpy(transform.data(), entity + 8, transform.size());
		std::memcpy(geometry.data(), col, geometry.size());
		// Mirror the native world query's stack sphere and COL (0x100131540).
		alignas(16) std::array<uint8_t, 0x50> matrix{};
		alignas(16) std::array<uint8_t, 0x68> modelA{};
		alignas(16) std::array<uint8_t, 20> sphere{};
		*reinterpret_cast<float *>(matrix.data()) = 1;
		*reinterpret_cast<float *>(matrix.data() + 0x14) = 1;
		*reinterpret_cast<float *>(matrix.data() + 0x28) = 1;
		*reinterpret_cast<Vec3 *>(matrix.data() + 0x30) = center;
		*reinterpret_cast<float *>(sphere.data() + 0xc) = radius;
		*reinterpret_cast<float *>(modelA.data() + 0xc) = radius;
		*reinterpret_cast<Vec3 *>(modelA.data() + 0x10) = {-radius, -radius, -radius};
		*reinterpret_cast<Vec3 *>(modelA.data() + 0x1c) = {radius, radius, radius};
		*reinterpret_cast<int16_t *>(modelA.data() + 0x28) = 1;
		*reinterpret_cast<uint8_t **>(modelA.data() + 0x38) = sphere.data();
		if (!Budget())
			return -1;
		NativePoint point;
		PointScope fresh(point);
		const int count = world_.native->models(matrix.data(), modelA.data(), entity + 8, col,
		                                       reinterpret_cast<uint8_t *>(&point), nullptr, nullptr);
		const NativePoint output = point;
		// Recheck identities after native code before treating zero as clear.
		if (*reinterpret_cast<const uint32_t *>(entity + 0x60) != flags ||
		    *reinterpret_cast<const int16_t *>(entity + 0x6a) != model || world_.native->modelInfos[model] != info ||
		    *reinterpret_cast<uint8_t **>(info + 0x20) != col ||
		    std::memcmp(transform.data(), entity + 8, transform.size()) ||
		    std::memcmp(geometry.data(), col, geometry.size()))
			return -1;
		if (count == 1)
			result = output;
		return count == 0 || count == 1 ? count : -1;
	}
	Query Sphere(Vec3 center, float radius)
	{
		NativeFilters filters;
		for (;;) {
			if (!Budget())
				return {true, false, {}, 0};
			PointScope scope(*world_.point);
			uint8_t *entity = world_.sphere({center.x, center.y, center.z}, radius, ped_, true, true, false, true, false, false);
			if (!entity)
				return {};
			if (entity == ped_) {
				stats_.unresolved = true;
				return {true, false, {}, 0};
			}
			const Query q = Contact(*world_.point, radius);
			if (q.contact)
				return q;
			NativePoint refinedPoint;
			const int refined = Refine(entity, center, radius, refinedPoint);
			if (refined == 1) {
				const Query refinedContact = Contact(refinedPoint, radius);
				if (!refinedContact.contact) stats_.unresolved = true;
				return refinedContact;
			}
			if (refined != 0 || filters.count == kMaxFiltered) {
				if (!stats_.exhausted) stats_.unresolved = true;
				return {true, false, {}, 0};
			}
			for (unsigned i = 0; i < filters.count; ++i)
				if (filters.entries[i].entity == entity) {
					stats_.unresolved = true;
					return {true, false, {}, 0};
				}
			const uint32_t flags = *reinterpret_cast<const uint32_t *>(entity + 0x60);
			filters.entries[filters.count++] = {entity, flags};
			*reinterpret_cast<uint32_t *>(entity + 0x60) = flags & ~uint32_t(0x100);
		}
	}
	bool Project(Vec3 &position, Vec3 &normal, bool &hit)
	{
		struct Plane { Vec3 n; float offset; };
		std::array<Plane, kProjectionPasses> planes{};
		unsigned count = 0;
		const Vec3 origin = position;
		const float skin = std::clamp(radius_ * .05f, .003f, .01f);
		for (unsigned pass = 0; pass < kProjectionPasses; pass++) {
			const Query q = Sphere(position, radius_ + skin);
			if (!q.occupied)
				return true;
			hit = true;
			if (!q.contact)
				break;
			normal = q.normal;
			planes[count++] = {q.normal, Dot(q.normal, Sub(position, origin)) + q.depth + kPushEpsilon};
			// Minimum Euclidean correction under all observed halfspaces. In
			// 3D an optimum has at most three independent active constraints.
			// Enumerate those small sets instead of alternating normal pushes
			// (which converge arbitrarily slowly in an acute corner).
			Vec3 best{};
			float bestLength = std::numeric_limits<float>::infinity();
			auto candidate = [&](unsigned i, unsigned j, unsigned k, unsigned size) {
				const unsigned indices[3]{i, j, k};
				double a[3][4]{};
				for (unsigned row = 0; row < size; ++row) {
					for (unsigned col = 0; col < size; ++col)
						a[row][col] = Dot(planes[indices[row]].n, planes[indices[col]].n);
					a[row][size] = planes[indices[row]].offset;
				}
				for (unsigned col = 0; col < size; ++col) {
					unsigned pivot = col;
					for (unsigned row = col + 1; row < size; ++row)
						if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
					if (std::fabs(a[pivot][col]) < 1e-7) return;
					for (unsigned c = col; c <= size; ++c) std::swap(a[col][c], a[pivot][c]);
					const double scale = a[col][col];
					for (unsigned c = col; c <= size; ++c) a[col][c] /= scale;
					for (unsigned row = 0; row < size; ++row) if (row != col) {
						const double factor = a[row][col];
						for (unsigned c = col; c <= size; ++c) a[row][c] -= factor * a[col][c];
					}
				}
				Vec3 delta{};
				for (unsigned row = 0; row < size; ++row) {
					if (a[row][size] < -1e-6) return;
					delta = Add(delta, Mul(planes[indices[row]].n, float(a[row][size])));
				}
				for (unsigned p = 0; p < count; ++p)
					if (Dot(planes[p].n, delta) < planes[p].offset - 1e-6f) return;
				const float length = Dot(delta, delta);
				if (Finite(delta) && length < bestLength) { bestLength = length; best = delta; }
			};
			for (unsigned i = 0; i < count; ++i) {
				candidate(i, 0, 0, 1);
				for (unsigned j = i + 1; j < count; ++j) {
					candidate(i, j, 0, 2);
					for (unsigned k = j + 1; k < count; ++k) candidate(i, j, k, 3);
				}
			}
			if (!std::isfinite(bestLength)) break;
			position = Add(origin, best);
		}
		if (!stats_.exhausted) stats_.unresolved = true;
		return false;
	}
	// Every accepted interval has a geometric certificate: a sphere at the
	// segment midpoint, radius r+halfLength, encloses the ENTIRE swept joint
	// sphere. Native TestSphereAgainstWorld must report this enclosure clear
	// before any part of the interval is accepted. Thus a thin obstacle between clear
	// endpoints, or an edge missed by the center ray, cannot authorize a pass.
	bool Advance(Vec3 a, Vec3 b, unsigned depth, Vec3 &position, Vec3 &normal)
	{
		const float length = Length(Sub(b, a));
		const Vec3 middle = Mul(Add(a, b), .5f);
		const Query q = Sphere(middle, radius_ + length * .5f);
		if (!q.occupied) {
			position = b;
			return true;
		}
		if (q.contact)
			normal = q.normal;
		position = a;
		if (stats_.exhausted || depth >= kMaxDepth || length <= std::max(.0005f, radius_ * .01f))
			return false;
		if (!Advance(a, middle, depth + 1, position, normal))
			return false;
		return Advance(middle, b, depth + 1, position, normal);
	}

  private:
	const World &world_;
	float radius_;
	uint8_t *ped_;
	Stats &stats_;
};
} // namespace

reactphysics::Contact Sweep(const World &world, Vec3 from, Vec3 to, float radius, void *ped, Stats *output)
{
	FilterPause pauseOuterFilters;
	Stats stats;
	struct CopyStats {
		Stats *to;
		Stats &from;
		~CopyStats()
		{
			if (to)
				*to = from;
		}
	} copy{output, stats};
	auto finish = [&](reactphysics::Contact contact) {
		contact.resolved = !stats.unresolved && !stats.exhausted;
		return contact;
	};
	// Unsafe/missing bindings and implausible inputs fail closed. The owner
	// checks Install before enabling reactions; no center-ray fallback exists.
	if (!world.sphere || !world.point || !Finite(from) || !Finite(to) || !std::isfinite(radius) || radius < .01f ||
	    radius > 1 || Length(Sub(to, from)) > 4.001f) {
		stats.unresolved = true;
		return finish({from, {}, true});
	}
	Solver solver(world, radius, ped, stats);
	reactphysics::Contact result{from, {}, false};
	if (!solver.Project(result.position, result.normal, result.hit))
		return finish({from, result.normal, true});
	Vec3 remaining = Sub(to, result.position);
	if (result.hit) {
		const float into = Dot(remaining, result.normal);
		if (into < 0)
			remaining = Sub(remaining, Mul(result.normal, into));
	}
	for (unsigned slide = 0; slide < kSlidePasses; slide++) {
		if (Length(remaining) < 1e-6f)
			return finish(result);
		const Vec3 goal = Add(result.position, remaining);
		if (solver.Advance(result.position, goal, 0, result.position, result.normal))
			return finish(result);
		result.hit = true;
		if (stats.exhausted || Length(result.normal) < .5f)
			return finish(result);
		const Vec3 certified = result.position;
		if (!solver.Project(result.position, result.normal, result.hit)) {
			result.position = certified;
			return finish(result);
		}
		remaining = Sub(goal, result.position);
		const float into = Dot(remaining, result.normal);
		if (into < 0)
			remaining = Sub(remaining, Mul(result.normal, into));
	}
	return finish(result);
}

bool Install()
{
	gWorld.sphere = reinterpret_cast<SphereQuery>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN6CWorld22TestSphereAgainstWorldE7CVectorfP7CEntitybbbbbb"));
	gWorld.point = static_cast<NativePoint *>(dlsym(RTLD_MAIN_ONLY, "gaTempSphereColPoints"));
	gNative.models = reinterpret_cast<ModelsQuery>(dlsym(RTLD_MAIN_ONLY,
	    "_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf"));
	gNative.modelInfos = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
	gWorld.native = gNative.models && gNative.modelInfos ? &gNative : nullptr;
	return gWorld.sphere && gWorld.point;
}
reactphysics::Contact Sweep(Vec3 from, Vec3 to, float radius, void *ped)
{
	return Sweep(gWorld, from, to, radius, ped);
}
} // namespace reactcontact
