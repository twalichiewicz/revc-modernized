#pragma once
#include "gamefiles.h"
#include <cmath>
#include <vector>
namespace roofgeometry {
struct Face {
	Vec3f v[3];
};
inline float Cross2D(Vec3f o, Vec3f a, Vec3f b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
inline bool HeightAt(const Face &f, float x, float y, float &z)
{
	const float area = Cross2D(f.v[0], f.v[1], f.v[2]);
	if (std::fabs(area) < 1e-6f)
		return false;
	const Vec3f p{x, y, 0};
	const float a = Cross2D(f.v[1], f.v[2], p) / area, b = Cross2D(f.v[2], f.v[0], p) / area, c = 1 - a - b;
	if (a < -1e-4f || b < -1e-4f || c < -1e-4f)
		return false;
	z = a * f.v[0].z + b * f.v[1].z + c * f.v[2].z;
	return true;
}
inline bool Covered(const std::vector<Face> &faces, const std::vector<bool> &flat, size_t k)
{
	const Face &f = faces[k];
	// A centroid test deletes an entire lower roof when a small tower overlaps
	// only its middle. A convex triangle contains the whole lower triangle only
	// when it contains all three corners. Keep uncertain overlap; depth testing
	// hides it correctly in the map, preserving every exposed roof terrace.
	for (size_t j = 0; j < faces.size(); j++) {
		if (j == k || !flat[j])
			continue;
		bool all = true;
		for (const Vec3f &v : f.v) {
			float z;
			all = all && HeightAt(faces[j], v.x, v.y, z) && z > v.z + 0.5f;
		}
		if (all)
			return true;
	}
	return false;
}
} // namespace roofgeometry
