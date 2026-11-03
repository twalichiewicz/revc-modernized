#include "vehiclerain.h"

#include <algorithm>
#include <cmath>

namespace vehiclerain {
namespace {

float Heavy(float rain) // 0 dry .. 1 at the game's rainy weather (0.5) and over
{
	const float x = std::clamp((rain - 0.1f) / 0.4f, 0.0f, 1.0f);
	return x * x * (3 - 2 * x);
}

float Phase(uint32_t seed, int i)
{
	uint32_t x = seed * 0x9e3779b9u + uint32_t(i) * 0x85ebca6bu;
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	return float(x & 0xffff) / 65536.0f * 6.2831853f;
}

} // namespace

Ground OnGround(uint8_t surface, float soak)
{
	const float k = std::clamp(soak, 0.0f, 1.0f);
	switch (surface) {
	case 2: // grass
	case 4: // mud
		return {1 - 0.55f * k, 1 - 0.45f * k, 3.2f * k};
	case 18: // sand
	case 33: // the beach
		return {1 - 0.35f * k, 1 - 0.4f * k, 3.3f * k};
	case 3: // gravel: drains, firmer
		return {1 - 0.25f * k, 1 - 0.15f * k, 1.0f * k};
	default:
		return {1, 1, 0};
	}
}

Air InTheAir(float rain, float gust, float seconds, uint32_t seed)
{
	const float k = Heavy(rain);
	if (k <= 0)
		return {};
	const float amp = k * (0.5f + 0.5f * std::clamp(gust, 0.0f, 1.0f));
	const double t = seconds;
	auto wave = [&](int i, double slow, double fast) { // two swells of its own, -1..1
		return float(0.6 * std::sin(t * 6.283185307 / slow + Phase(seed, i)) +
		             0.4 * std::sin(t * 6.283185307 / fast + Phase(seed, i + 7)));
	};
	Air a;
	a.twist = {amp * 1.2f * wave(0, 2.3, 0.9), amp * 1.2f * wave(1, 2.9, 1.1), amp * 0.5f * wave(2, 3.7, 1.3)};
	a.shove = {amp * 0.8f * wave(3, 3.1, 1.2), amp * 0.8f * wave(4, 2.6, 1.0), -0.6f * k + amp * 1.5f * wave(5, 2.1, 0.8)};
	return a;
}

} // namespace vehiclerain
