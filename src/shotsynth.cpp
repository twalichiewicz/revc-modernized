#include "shotsynth.h"

#include <algorithm>
#include <cmath>

namespace shotsynth {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kPeak = 0.891f; // -1 dBFS

struct Voice {
	float length;          // s
	float from, to, glide; // the thump's Hz falling from, to, over (s)
	float decay;           // s, the thump's
	float crack, crackGain; // s, its length
	float click, clickGain; // s after the shot (0: none)
};
// pistol, SMG, rifle, shotgun, heavy, sniper
constexpr Voice kVoices[] = {
    {0.12f, 95.0f, 55.0f, 0.02f, 0.035f, 0.006f, 0.30f, 0.030f, 0.12f},
    {0.10f, 85.0f, 55.0f, 0.02f, 0.030f, 0.005f, 0.28f, 0.020f, 0.10f},
    {0.15f, 80.0f, 46.0f, 0.02f, 0.045f, 0.008f, 0.32f, 0.025f, 0.10f},
    {0.22f, 70.0f, 40.0f, 0.03f, 0.070f, 0.012f, 0.35f, 0.000f, 0.00f},
    {0.16f, 75.0f, 42.0f, 0.02f, 0.050f, 0.008f, 0.30f, 0.020f, 0.08f},
    {0.24f, 65.0f, 38.0f, 0.03f, 0.080f, 0.010f, 0.35f, 0.000f, 0.00f},
};

// A small seeded noise source (the same seed, the same shot).
struct Noise {
	uint32_t s;
	float Next()
	{
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / float(1u << 23) - 1.0f;
	}
};

} // namespace

Kind KindOf(int weapon)
{
	switch (weapon) {
	case 17: case 18: return Kind::kPistol;           // Colt, Python
	case 19: case 20: case 21: return Kind::kShotgun; // shotgun, SPAS, stubby
	case 22: case 23: case 25: return Kind::kSmg;     // Tec-9, Uzi, MP5 (the silenced Ingram: no thump under it)
	case 26: case 27: return Kind::kRifle;            // M4, Ruger
	case 28: case 29: return Kind::kSniper;           // sniper, laser sniper
	case 32: case 33: return Kind::kHeavy;            // M60, minigun
	default: return Kind::kNone;
	}
}

std::vector<int16_t> Make(Kind kind, int rate, uint32_t seed)
{
	if (kind == Kind::kNone || rate <= 0)
		return {};
	const Voice &v = kVoices[int(kind) - 1];
	const size_t n = size_t(v.length * float(rate));
	std::vector<float> out(n);
	Noise noise{seed * 2654435761u + 1u};
	float phase = 0.0f, lastNoise = 0.0f, ring = 0.0f, ringV = 0.0f;
	const float ringW = 2.0f * kPi * 3200.0f / float(rate); // the click's ring
	for (size_t i = 0; i < n; i++) {
		const float t = float(i) / float(rate);
		// The thump: a sine falling in pitch, dying away.
		const float hz = v.to + (v.from - v.to) * std::exp(-t / v.glide);
		phase += 2.0f * kPi * hz / float(rate);
		float s = std::sin(phase) * std::exp(-t / v.decay);
		// The crack: bright noise (its difference), over in milliseconds.
		const float white = noise.Next();
		s += v.crackGain * (white - lastNoise) * 0.5f * std::exp(-t / (v.crack / 3.0f));
		lastNoise = white;
		// The click: a struck ring, a moment after.
		if (v.click > 0.0f && t >= v.click) {
			const float kick = i == size_t(v.click * float(rate)) ? 1.0f : 0.0f;
			ringV += kick - ringW * ringW * ring - 0.02f * ringV;
			ring += ringV;
			s += v.clickGain * ring * 0.3f * std::exp(-(t - v.click) / 0.004f);
		}
		// Faded out over its last third.
		const float tail = std::clamp((v.length - t) / (v.length / 3.0f), 0.0f, 1.0f);
		out[i] = s * tail * tail;
	}
	float peak = 0.0f;
	for (float s : out)
		peak = std::max(peak, std::fabs(s));
	std::vector<int16_t> pcm(n);
	for (size_t i = 0; i < n; i++)
		pcm[i] = int16_t(std::lround(out[i] * (peak > 0.0f ? kPeak / peak : 0.0f) * 32767.0f));
	return pcm;
}

} // namespace shotsynth
