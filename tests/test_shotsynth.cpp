// Unit tests for his own gun's synthesized body (thump, crack, mechanism): make test
#include "shotsynth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace shotsynth;

constexpr int kRate = 32000;
constexpr Kind kKinds[] = {Kind::kPistol, Kind::kSmg, Kind::kRifle, Kind::kShotgun, Kind::kHeavy, Kind::kSniper};

static float Rms(const std::vector<int16_t> &pcm, size_t from, size_t to)
{
	double sum = 0.0;
	for (size_t i = from; i < to && i < pcm.size(); i++)
		sum += double(pcm[i]) * pcm[i];
	return float(std::sqrt(sum / double(to - from)));
}

static void TestLengthAndPeak()
{
	for (Kind k : kKinds) {
		const std::vector<int16_t> pcm = Make(k, kRate, 1);
		CHECK(pcm.size() >= size_t(0.08f * kRate) && pcm.size() <= size_t(0.25f * kRate));
		int peak = 0;
		for (int16_t s : pcm)
			peak = std::max(peak, std::abs(int(s)));
		CHECK(peak <= 29205 && peak > 20000);
	}
}

// The thump is low: most of the first 40 ms's energy is under 150 Hz (a
// two-point average's low half against the rest).
static void TestThumpIsLow()
{
	for (Kind k : kKinds) {
		const std::vector<int16_t> pcm = Make(k, kRate, 1);
		const size_t n = size_t(0.04f * kRate);
		// A one-pole low-pass at 150 Hz splits it.
		const float a = 1.0f - std::exp(-2.0f * 3.14159265f * 150.0f / kRate);
		double low = 0.0, all = 0.0, y = 0.0;
		for (size_t i = 0; i < n; i++) {
			y += a * (pcm[i] - y);
			low += y * y;
			all += double(pcm[i]) * pcm[i];
		}
		CHECK(low > 0.5 * all);
	}
}

static void TestDecays()
{
	for (Kind k : kKinds) {
		const std::vector<int16_t> pcm = Make(k, kRate, 1);
		const float start = Rms(pcm, 0, size_t(0.02f * kRate)), end = Rms(pcm, pcm.size() - size_t(0.01f * kRate), pcm.size());
		CHECK(end < start * 0.01f); // 40 dB down
	}
}

// Each kind its own: a shotgun's and a heavy gun's longer than a pistol's; a different seed, a different shot.
static void TestKindsDiffer()
{
	CHECK(Make(Kind::kShotgun, kRate, 1).size() > Make(Kind::kPistol, kRate, 1).size());
	CHECK(Make(Kind::kHeavy, kRate, 1).size() > Make(Kind::kSmg, kRate, 1).size());
	CHECK(Make(Kind::kRifle, kRate, 1) != Make(Kind::kRifle, kRate, 2));
	CHECK(Make(Kind::kRifle, kRate, 3) == Make(Kind::kRifle, kRate, 3));
}

// The game's guns by kind; none for a weapon that isn't a gun.
static void TestKindOfWeapons()
{
	CHECK(KindOf(17) == Kind::kPistol && KindOf(18) == Kind::kPistol);
	CHECK(KindOf(19) == Kind::kShotgun && KindOf(21) == Kind::kShotgun);
	CHECK(KindOf(23) == Kind::kSmg && KindOf(25) == Kind::kSmg);
	CHECK(KindOf(24) == Kind::kNone); // the silenced Ingram: no thump under it
	CHECK(KindOf(26) == Kind::kRifle && KindOf(27) == Kind::kRifle);
	CHECK(KindOf(28) == Kind::kSniper && KindOf(29) == Kind::kSniper);
	CHECK(KindOf(32) == Kind::kHeavy && KindOf(33) == Kind::kHeavy);
	CHECK(KindOf(0) == Kind::kNone && KindOf(12) == Kind::kNone && KindOf(30) == Kind::kNone && KindOf(31) == Kind::kNone);
}

int main()
{
	TestLengthAndPeak();
	TestThumpIsLow();
	TestDecays();
	TestKindsDiffer();
	TestKindOfWeapons();
	if (gFailures) {
		printf("%d shotsynth test(s) failed\n", gFailures);
		return 1;
	}
	printf("all shotsynth tests passed\n");
	return 0;
}
