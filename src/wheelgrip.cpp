#include "wheelgrip.h"
#include <cmath>
namespace wheelgrip
{
using namespace vm;
Grip OnRim(Vec3 c, Vec3 axis, Vec3 up, Vec3 left, float radius, float tube, float angle)
{
	Grip g;
	g.centre = c;
	g.axis = Normal(axis);
	g.radius = radius;
	g.tube = tube;
	g.radial = Normal(Add(Mul(up, std::cos(angle)), Mul(left, std::sin(angle))));
	g.hand.x = Mul(g.axis, -1);
	g.hand.y = Mul(g.radial, -1);
	g.hand.z = Normal(Cross(g.hand.x, g.hand.y));
	g.hand.pos = Add(Add(c, Mul(g.radial, radius + tube + 0.015f)), Mul(g.axis, 0.090f));
	return g;
}
void Contacts(const Arm &arm, const Grip &g, Vec3 targets[kDigits], Vec3 bends[kDigits])
{
	const Frame carry = Compose(g.hand, Inverse(arm.bind[kHand]));
	for (int d = 0; d < kDigits; ++d) {
		const Vec3 base = Apply(carry, arm.bind[DigitBase(d)].pos);
		const Vec3 around = Sub(Sub(base, g.centre), Mul(g.axis, Dot(Sub(base, g.centre), g.axis)));
		const Vec3 radial = Normal(around);
		// Four fingers curl to the inner, far face; thumb opposes them on the
		// near face. Every anchor lies on the actual toroidal rim, not its centre.
		const float axial = d == kDigitThumb ? 0.8f : -0.70f;
		const float inward = -std::sqrt(1 - axial * axial);
		targets[d] = Add(Add(g.centre, Mul(radial, g.radius + inward * g.tube)), Mul(g.axis, axial * g.tube));
		bends[d] = Add(Mul(radial, 0.7f), Mul(g.axis, -0.7f));
	}
}
} // namespace wheelgrip
