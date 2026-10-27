#include "fpcross.h"

#include <algorithm>
#include <cmath>

namespace fpcross {

float MinGap(float h) { return 4.0f * h / 1080.0f; }
float Length(float h) { return 9.0f * h / 1080.0f; }
float Thickness(float h) { return 2.0f * h / 1080.0f; }
float Edge(float h) { return std::max(1.0f, std::round(h / 1080.0f)); }

float Gap(float spreadAngle, float tanHalfV, float h)
{
	return MinGap(h) + (tanHalfV > 0.0f ? std::tan(spreadAngle) / tanHalfV * h * 0.5f : 0.0f);
}

void Lines(float cx, float cy, float gap, float h, Rect out[4])
{
	const float t = std::max(1.0f, std::round(Thickness(h))), len = std::max(1.0f, std::round(Length(h)));
	const float x = std::round(cx - t / 2.0f), y = std::round(cy - t / 2.0f); // across the centre
	const float mx = x + t / 2.0f, my = y + t / 2.0f;                         // the centre as drawn
	const float above = std::floor(my - gap), below = std::ceil(my + gap);
	const float before = std::floor(mx - gap), after = std::ceil(mx + gap);
	out[0] = {x, above - len, t, len};
	out[1] = {x, below, t, len};
	out[2] = {before - len, y, len, t};
	out[3] = {after, y, len, t};
}

} // namespace fpcross
