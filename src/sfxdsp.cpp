#include "sfxdsp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sfxdsp {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDcCut = 20.0f;                         // Hz
constexpr float kShelfAt = 150.0f, kShelfGain = 5.0f;   // Hz, dB
constexpr float kAttackFor = 0.008f, kAttackGone = 0.025f, kAttackGain = 4.0f; // s, s, dB
constexpr float kDrive = 1.4f;                          // the saturation's
constexpr float kPeak = 0.891f;                         // -1 dBFS

// A low shelf (the Audio EQ Cookbook's, shelf slope 1).
struct Biquad {
	float b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
	float Run(float x)
	{
		const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return y;
	}
};

Biquad LowShelf(float hz, float db, int rate)
{
	const float a = std::pow(10.0f, db / 40.0f), w = 2.0f * kPi * hz / float(rate);
	const float cw = std::cos(w), alpha = std::sin(w) / 2.0f * std::sqrt(2.0f), sa = 2.0f * std::sqrt(a) * alpha;
	const float a0 = (a + 1) + (a - 1) * cw + sa;
	Biquad q{};
	q.b0 = a * ((a + 1) - (a - 1) * cw + sa) / a0;
	q.b1 = 2 * a * ((a - 1) - (a + 1) * cw) / a0;
	q.b2 = a * ((a + 1) - (a - 1) * cw - sa) / a0;
	q.a1 = -2 * ((a - 1) + (a + 1) * cw) / a0;
	q.a2 = ((a + 1) + (a - 1) * cw - sa) / a0;
	return q;
}

float Smooth(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void ScalePeakTo(std::vector<float> &v, float to)
{
	float peak = 0.0f;
	for (float s : v)
		peak = std::max(peak, std::fabs(s));
	if (peak > 0.0f)
		for (float &s : v)
			s *= to / peak;
}

} // namespace

void Punch(int16_t *pcm, size_t n, int rate)
{
	if (!pcm || n == 0 || rate <= 0)
		return;
	std::vector<float> v(n);
	float inPeak = 0.0f;
	for (size_t i = 0; i < n; i++)
		inPeak = std::max(inPeak, std::fabs(float(pcm[i]) / 32768.0f));
	// The offset out (a one-pole high-pass), the low end up, the crack lifted.
	const float r = std::exp(-2.0f * kPi * kDcCut / float(rate));
	float lastIn = 0.0f, lastOut = 0.0f;
	Biquad shelf = LowShelf(kShelfAt, kShelfGain, rate);
	const float lift = std::pow(10.0f, kAttackGain / 20.0f);
	for (size_t i = 0; i < n; i++) {
		const float x = float(pcm[i]) / 32768.0f;
		const float y = x - lastIn + r * lastOut;
		lastIn = x;
		lastOut = y;
		const float t = float(i) / float(rate);
		const float attack = 1.0f + (lift - 1.0f) * (1.0f - Smooth((t - kAttackFor) / (kAttackGone - kAttackFor)));
		v[i] = shelf.Run(y) * attack;
	}
	// A little saturation at full level, then a decibel under it.
	ScalePeakTo(v, 1.0f);
	const float norm = std::tanh(kDrive);
	for (float &s : v)
		s = std::tanh(kDrive * s) / norm;
	ScalePeakTo(v, std::min(kPeak, inPeak)); // never louder than the game had it
	for (size_t i = 0; i < n; i++)
		pcm[i] = int16_t(std::lround(std::clamp(v[i], -1.0f, 1.0f) * 32767.0f));
}

} // namespace sfxdsp
