#include "vmview.h"

#include <algorithm>
#include <cmath>

namespace vm {

Frame GunInView(const Gun &gun, const Framing &framing, float aim)
{
	constexpr float kDeg = 3.14159265f / 180.0f;
	const float yaw = framing.hipYaw * kDeg, pitch = framing.hipPitch * kDeg;
	const Vec3 hipEye = Mul(framing.hipEye, gun.scale);
	const Vec3 hipAhead{std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
	const Vec3 line = Normal(Sub(gun.sightFront, gun.sightRear));
	const Vec3 up = Normal(Sub(Vec3{0, 0, 1}, Mul(line, line.z)));
	const Vec3 aimEye = Add(Sub(gun.sightRear, Mul(line, framing.aimBack * gun.scale)), Mul(up, framing.aimAbove * gun.scale));
	const float t = std::clamp(aim, 0.0f, 1.0f);
	return Inverse(Look(Lerp(hipEye, aimEye, t), Normal(Lerp(hipAhead, line, t)), {0, 0, 1}));
}

void ClearArmNearPlane(const Arm &arm, std::vector<Vec3> &positions, float nearPlane, float tanHalfV)
{
	if (positions.size() != arm.vertices.size() || nearPlane <= 0)
		return;
	const float bottom = -nearPlane * tanHalfV - 0.002f;
	auto movable = [&](size_t i) {
		const auto &v = arm.vertices[i];
		float weight = 0;
		for (int k = 0; k < 4; ++k)
			if (v.bone[k] == kUpper || v.bone[k] == kFore)
				weight += v.weight[k];
		return weight > 0.5f;
	};
	for (const Triangle &triangle : arm.triangles)
		for (int edge = 0; edge < 3; ++edge) {
			const size_t i = triangle.v[edge], j = triangle.v[(edge + 1) % 3];
			if (i >= positions.size() || j >= positions.size())
				continue;
			Vec3 &a = positions[i], &b = positions[j];
			if ((a.y - nearPlane) * (b.y - nearPlane) >= 0)
				continue;
			const float t = (nearPlane - a.y) / (b.y - a.y), z = a.z + (b.z - a.z) * t;
			if (z <= bottom)
				continue;
			const float wa = movable(i) ? 1 - t : 0, wb = movable(j) ? t : 0, weight = wa * wa + wb * wb;
			if (weight < 1e-8f)
				continue;
			const float down = (z - bottom) / weight;
			a.z -= down * wa;
			b.z -= down * wb;
		}
}

} // namespace vm
