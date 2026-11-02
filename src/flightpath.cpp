#include "flightpath.h"

#include <algorithm>
#include <cmath>

namespace flightpath {
namespace {

// How far over the ground the climb/descent slope eases in (m).
constexpr float kEase = 600.0f;

float Ramp(float distance)
{
	if(distance>=kEase)return distance-kEase*.5f;
	const float t=std::max(0.0f,distance)/kEase;
	// Integral of smoothstep: height, slope and curvature join continuously.
	return kEase*t*t*t*(1-.5f*t);
}
float SmoothMin(float a,float b)
{
	constexpr float width=400;
	const float h=std::max(width-std::fabs(a-b),0.0f)/width;
	return std::min(a,b)-h*h*width*.25f;
}

} // namespace

void Raise(std::vector<Vec3> &nodes)
{
	const int n = int(nodes.size());
	if (n < 3)
		return;
	// The distance over the ground to each node (as CPlane::LoadPath has it).
	std::vector<float> d(size_t(n), 0.0f);
	for (int i = 1; i < n; i++)
		d[size_t(i)] = d[size_t(i - 1)] + std::hypot(nodes[size_t(i)].x - nodes[size_t(i - 1)].x, nodes[size_t(i)].y - nodes[size_t(i - 1)].y);
	// The flight: from the first node off the runway after it, to the last before it again.
	auto air = [&](int i) { return nodes[size_t(i)].z > kGround; };
	int first = -1, last = -1;
	for (int i = 1; i < n; i++)
		if (air(i) && !air(i - 1)) {
			first = i;
			break;
		}
	for (int i = n - 2; i >= 0; i--)
		if (air(i) && !air(i + 1)) {
			last = i;
			break;
		}
	if (first < 0 || last <= first)
		return;
	const float z0 = nodes[size_t(first)].z, z1 = nodes[size_t(last)].z, d0 = d[size_t(first)], d1 = d[size_t(last)];
	// Smooth the absolute envelope analytically. Averaging added heights by
	// waypoint count biases irregular native node spacing and can lift the
	// first/last airborne points into a sharp runway-adjacent pitch change.
	for (int i = first; i <= last; i++) {
		const float up=z0+Ramp(d[size_t(i)]-d0)*kClimb,down=z1+Ramp(d1-d[size_t(i)])*kDescent;
		const float want=SmoothMin(kPeak,SmoothMin(up,down));
		const float delta=want-nodes[size_t(i)].z;
		// A smooth positive part preserves native geometry where it is higher;
		// neither the original nor the new slope is exceeded at their join.
		constexpr float shoulder=80;
		if(delta>0)nodes[size_t(i)].z+=delta<shoulder?delta*delta/(2*shoulder):delta-shoulder*.5f;
	}
}

} // namespace flightpath
