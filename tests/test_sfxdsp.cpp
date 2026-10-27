// Unit tests for the gunshot samples' clean-up: make test
#include "sfxdsp.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

constexpr int kRate = 22050;
constexpr float kPi = 3.14159265f;

static std::vector<int16_t> Tones(float seconds, std::initializer_list<std::pair<float, float>> tones, float dc = 0.0f)
{
	std::vector<int16_t> pcm(size_t(seconds * kRate));
	for (size_t i = 0; i < pcm.size(); i++) {
		float v = dc;
		for (const auto &t : tones)
			v += t.second * std::sin(2.0f * kPi * t.first * float(i) / kRate);
		pcm[i] = int16_t(std::lround(v * 32767.0f));
	}
	return pcm;
}

// How much of a tone is in samples [from, to) (its amplitude, by correlation).
static float Amplitude(const std::vector<int16_t> &pcm, float hz, size_t from, size_t to)
{
	double s = 0.0, c = 0.0;
	for (size_t i = from; i < to; i++) {
		const double a = 2.0 * kPi * hz * double(i) / kRate;
		s += pcm[i] * std::sin(a);
		c += pcm[i] * std::cos(a);
	}
	return float(2.0 * std::sqrt(s * s + c * c) / double(to - from) / 32767.0);
}

static float Rms(const std::vector<int16_t> &pcm, size_t from, size_t to)
{
	double sum = 0.0;
	for (size_t i = from; i < to; i++)
		sum += double(pcm[i]) * pcm[i];
	return float(std::sqrt(sum / double(to - from)));
}

static float Db(float ratio) { return 20.0f * std::log10(ratio); }

// A sample's offset from the middle (several of the game's have 8%) goes.
static void TestDcRemoved()
{
	std::vector<int16_t> pcm = Tones(0.5f, {{1000.0f, 0.3f}}, 0.1f);
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	double mean = 0.0;
	for (size_t i = pcm.size() / 2; i < pcm.size(); i++)
		mean += pcm[i];
	mean /= double(pcm.size() / 2);
	CHECK(std::fabs(mean) < 0.005 * 32767.0);
}

// The low end comes up 4–6 dB against the top.
static void TestLowShelfLiftsLows()
{
	std::vector<int16_t> pcm = Tones(0.5f, {{80.0f, 0.2f}, {3000.0f, 0.2f}});
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	const size_t from = size_t(0.1f * kRate), to = size_t(0.45f * kRate);
	const float lift = Db(Amplitude(pcm, 80.0f, from, to) / Amplitude(pcm, 3000.0f, from, to));
	CHECK(lift > 4.0f && lift < 6.5f);
}

// The first few milliseconds (the shot's crack) stand out from what follows.
static void TestAttackLifted()
{
	std::vector<int16_t> pcm = Tones(0.3f, {{1000.0f, 0.3f}});
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	const float attack = Rms(pcm, 0, size_t(0.008f * kRate)), body = Rms(pcm, size_t(0.04f * kRate), size_t(0.06f * kRate));
	CHECK(Db(attack / body) > 2.0f);
}

// It ends a decibel under full scale: no clipping, nothing left quiet.
static void TestPeakUnderMinusOneDb()
{
	srand(7);
	std::vector<int16_t> pcm(kRate / 4);
	for (int16_t &s : pcm)
		s = int16_t(rand() % 65535 - 32767);
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	int peak = 0;
	for (int16_t s : pcm)
		peak = std::max(peak, std::abs(int(s)));
	CHECK(peak <= 29205 && peak >= 28500);
}

static void TestSilenceStaysSilent()
{
	std::vector<int16_t> pcm(1000, 0);
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	bool silent = true;
	for (int16_t s : pcm)
		silent &= s == 0;
	CHECK(silent);
	sfxdsp::Punch(nullptr, 0, kRate); // nothing to do
}

// A quiet sample (a tail stored low) stays as loud as the game had it: never
// brought up, only kept under a decibel below full scale.
static void TestNeverLouder()
{
	std::vector<int16_t> pcm = Tones(0.3f, {{200.0f, 0.25f}}); // peak -12 dBFS
	sfxdsp::Punch(pcm.data(), pcm.size(), kRate);
	int peak = 0;
	for (int16_t s : pcm)
		peak = std::max(peak, std::abs(int(s)));
	CHECK(peak <= int(0.25f * 32767.0f) + 2 && peak > int(0.24f * 32767.0f));
}

int main()
{
	TestNeverLouder();
	TestDcRemoved();
	TestLowShelfLiftsLows();
	TestAttackLifted();
	TestPeakUnderMinusOneDb();
	TestSilenceStaysSilent();
	if (gFailures) {
		printf("%d sfxdsp test(s) failed\n", gFailures);
		return 1;
	}
	printf("all sfxdsp tests passed\n");
	return 0;
}
