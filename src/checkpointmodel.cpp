#include "checkpointmodel.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace checkpointmodel {
namespace {
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool Matches(int32_t handle, Vec3 pos, const Blip *blips, size_t count)
{
	if (handle == -1 || !Finite(pos))
		return false;
	const uint32_t h = uint32_t(handle), index = h & 0xffff;
	if (index >= count)
		return false;
	const Blip &b = blips[index];
	return b.inUse && b.generation == (h >> 16) && Finite(b.pos) && std::fabs(b.pos.x - pos.x) < 0.25f &&
	       std::fabs(b.pos.y - pos.y) < 0.25f && std::fabs(b.pos.z - pos.z) < 1.5f;
}
// Each face, both windings (the engine's cull mode is shared, and isn't changed
// here): the top as a fan from the tip (it covers the concave outline), the
// underside likewise, each side a quad.
constexpr std::array<uint16_t, kArrowIndexCount> Faces()
{
	std::array<uint16_t, kArrowIndexCount> out{};
	int n = 0;
	auto tri = [&](int a, int b, int c) {
		out[size_t(n)] = uint16_t(a), out[size_t(n + 1)] = uint16_t(b), out[size_t(n + 2)] = uint16_t(c);
		const int back = kArrowIndexCount / 2;
		out[size_t(back + n)] = uint16_t(a), out[size_t(back + n + 1)] = uint16_t(c), out[size_t(back + n + 2)] = uint16_t(b);
		n += 3;
	};
	for (int i = 1; i < 6; i++) {
		tri(0, i, i + 1);
		tri(7, i + 8, i + 7);
	}
	for (int i = 0; i < 7; i++) {
		const int j = (i + 1) % 7;
		tri(i, i + 7, j + 7);
		tri(i, j + 7, j);
	}
	return out;
}
} // namespace
const std::array<uint16_t, kArrowIndexCount> kArrowIndices = Faces();
Arrow Resolve(const Input &in, const Blip *blips, size_t count)
{
	Arrow out;
	if (!in.racing || !blips || !Matches(in.currentHandle, in.current, blips, count) ||
	    !Matches(in.nextHandle, in.next, blips, count))
		return out;
	const float dx = in.next.x - in.current.x, dy = in.next.y - in.current.y, len = std::hypot(dx, dy);
	if (len < 1 || !std::isfinite(len))
		return out;
	out.valid = true;
	out.direction = {dx / len, dy / len};
	const float size = std::clamp(in.radius * 0.55f, 1.0f, 3.0f), x = out.direction.x, y = out.direction.y;
	const float dz = in.next.z - in.current.z, spatialLength = std::hypot(len, dz);
	const Vec3 forward{dx / spatialLength, dy / spatialLength, dz / spatialLength};
	// A broad arrow within the existing corona, including the next flight checkpoint's elevation.
	static constexpr Vec2 local[] = {{0, 1},         {-0.72f, 0.05f}, {-0.28f, 0.05f}, {-0.28f, -0.8f},
	                                 {0.28f, -0.8f}, {0.28f, 0.05f},  {0.72f, 0.05f}};
	// Up in the checkpoint (#28), where a driver looks, and solid: a thickness to its sides.
	const float height = std::max(1.5f, size * 0.8f), thickness = size * 0.22f;
	for (int i = 0; i < 7; i++) {
		out.vertices[i] = {in.current.x + (forward.x * local[i].y + y * local[i].x) * size,
		                   in.current.y + (forward.y * local[i].y - x * local[i].x) * size,
		                   in.current.z + height + thickness / 2 + forward.z * local[i].y * size};
		out.vertices[i + 7] = out.vertices[i];
		out.vertices[i + 7].z -= thickness;
	}
	return out;
}
} // namespace checkpointmodel
