#include "rtgeometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rtgeometry {
namespace {

float EdgeSquared(const float *a, const float *b)
{
	const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

// Fit to trace: every coordinate finite and in the world, every edge a moving
// thing's. Its longest edge, squared, when it is.
bool Fit(const float *t, float &longest)
{
	for (int i = 0; i < 9; ++i)
		if (!std::isfinite(t[i]) || std::fabs(t[i]) > kWorld)
			return false;
	longest = std::max({EdgeSquared(t, t + 3), EdgeSquared(t + 3, t + 6), EdgeSquared(t + 6, t)});
	return longest <= kLongestEdge * kLongestEdge;
}

} // namespace

bool Clean(const float *tris, int count, std::vector<float> &out, Report &report)
{
	report = {};
	report.triangles = std::max(count, 0);
	float longest = 0;
	bool any = false;
	for (int i = 0; i < report.triangles; ++i) {
		const float *t = tris + size_t(i) * 9;
		float edge = 0;
		const bool fit = Fit(t, edge);
		const bool point = fit && edge == 0;
		if (!fit || point) {
			if (!report.dropped && !report.collapsed)
				out.assign(tris, t); // the ones before it
			if (point) {
				const bool origin = t[0] == 0 && t[1] == 0 && t[2] == 0;
				if (!origin && report.collapsed == report.atOrigin)
					std::memcpy(report.point, t, sizeof report.point);
				++report.collapsed;
				report.atOrigin += origin;
			} else if (!report.dropped++) {
				report.firstBad = i;
				std::memcpy(report.bad, t, sizeof report.bad);
			}
			continue;
		}
		if (report.dropped || report.collapsed)
			out.insert(out.end(), t, t + 9);
		longest = std::max(longest, edge);
		for (int v = 0; v < 9; ++v) {
			const int axis = v % 3;
			report.lo[axis] = any ? std::min(report.lo[axis], t[v]) : t[v];
			report.hi[axis] = any ? std::max(report.hi[axis], t[v]) : t[v];
			any |= axis == 2;
		}
	}
	report.longestEdge = std::sqrt(longest);
	return report.dropped + report.collapsed > 0;
}

int BadViewField(const float *head, const float *lights, int count)
{
	for (int i = 0; i < 22; ++i)
		if (!std::isfinite(head[i]))
			return i;
	for (int i = 0; i < std::max(count, 0) * 16; ++i)
		if (!std::isfinite(lights[i]))
			return 22 + i;
	return -1;
}

MaskStats Stats(const uint8_t *mask, int pixels)
{
	MaskStats s;
	if (!mask || pixels <= 0)
		return s;
	int64_t sum = 0;
	int any = 0, strong = 0;
	for (int i = 0; i < pixels; i++) {
		any += mask[i] > 0;
		strong += mask[i] >= 128;
		sum += mask[i];
	}
	s.shadowed = float(any) / float(pixels);
	s.strong = float(strong) / float(pixels);
	s.darkness = any ? float(sum) / float(any) / 255.0f : 0.0f;
	return s;
}

} // namespace rtgeometry
