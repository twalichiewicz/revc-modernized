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

} // namespace vm
