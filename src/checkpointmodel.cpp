#include "checkpointmodel.h"
#include <algorithm>
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
} // namespace
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
	for (int i = 0; i < 7; i++)
		out.vertices[i] = {in.current.x + (forward.x * local[i].y + y * local[i].x) * size,
		                   in.current.y + (forward.y * local[i].y - x * local[i].x) * size,
		                   in.current.z + 0.35f + forward.z * local[i].y * size};
	return out;
}
} // namespace checkpointmodel
