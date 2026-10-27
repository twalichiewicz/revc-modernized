// Unit tests for which of the game's samples are gunshots, and the bank check: make test
#include "gunsfx.h"

#include <cstdio>
#include <vector>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

using namespace gunsfx;

// A table as macVC 1.0.5217's sfx.SDT has it where it matters: the guns' stored rates, the bank packed.
static std::vector<Sample> GameTable()
{
	std::vector<Sample> t(kBankSamples);
	uint32_t at = 0;
	for (uint32_t i = 0; i < kBankSamples; i++) {
		t[i] = {at, 2000, 22050, 0, -1};
		at += 2000;
	}
	const std::pair<uint32_t, uint32_t> rates[] = {{50, 22050}, {88, 17600}, {54, 37000}, {90, 30200}, {71, 17000}, {74, 26000},
	                                               {68, 32000}, {66, 22050}, {85, 28000}, {57, 24000}, {59, 23000}};
	for (const auto &r : rates)
		t[r.first].rate = t[r.first + 1].rate = r.second;
	return t;
}

static void TestTheGameTable()
{
	std::vector<Sample> t = GameTable();
	CHECK(TableLooksRight(t.data(), t.size()));
	t[74].rate = 44100; // another build's bank
	CHECK(!TableLooksRight(t.data(), t.size()));
	t = GameTable();
	t[88].size = 0x7fffffff; // past the bank's end
	CHECK(!TableLooksRight(t.data(), t.size()));
	CHECK(!TableLooksRight(t.data(), 100)); // too few
}

// Every gun's shot (both halves of its pair) and the four tails; nothing else.
static void TestGunshotIds()
{
	for (uint32_t id : {50u, 51u, 88u, 89u, 54u, 55u, 90u, 91u, 71u, 72u, 74u, 75u, 68u, 69u, 66u, 67u, 85u, 86u, 57u, 58u, 59u, 60u, 56u, 73u, 76u, 87u})
		CHECK(Gunshot(id));
	for (uint32_t id : {0u, 49u, 52u, 61u, 100u, 523u})
		CHECK(!Gunshot(id));
	CHECK(GunshotIds().size() == 26);
}

// The M4 plays the Ruger's sample (and tail) 1.66 times fast: it's played at
// 1.15 instead; the Ruger itself, and every other sample, as the game has them.
static void TestM4Pitch()
{
	CHECK(Pitch(74, 43150, 1.0f) > 29500 && Pitch(74, 43150, 1.0f) < 30300); // the M4's shot, both halves
	CHECK(Pitch(75, 44150, 1.0f) > 30200 && Pitch(75, 44150, 1.0f) < 31000);
	CHECK(Pitch(76, 15600, 1.0f) > 10600 && Pitch(76, 15600, 1.0f) < 11000); // its tail
	CHECK(Pitch(74, 26000, 1.0f) == 26000 && Pitch(74, 26800, 1.0f) == 26800); // the Ruger's
	CHECK(Pitch(76, 9400, 1.0f) == 9400);
	CHECK(Pitch(50, 43150, 1.0f) == 43150 && Pitch(71, 34000, 1.0f) == 34000); // others (the Ingram's double-speed TEC-9 too)
}

// In bullet time the game slows every sound's rate by its time scale: the M4
// is still told from the Ruger, and keeps its own pitch, slowed with the rest.
static void TestM4PitchInBulletTime()
{
	const float ts = 0.3f;
	const uint32_t m4 = Pitch(74, uint32_t(43150 * ts), ts);
	CHECK(m4 > uint32_t(29500 * ts) && m4 < uint32_t(30300 * ts));
	CHECK(Pitch(74, uint32_t(26000 * ts), ts) == uint32_t(26000 * ts));
	const uint32_t tail = Pitch(76, uint32_t(15600 * ts), ts);
	CHECK(tail > uint32_t(10600 * ts) && tail < uint32_t(11000 * ts));
	CHECK(Pitch(76, uint32_t(9400 * ts), ts) == uint32_t(9400 * ts));
	CHECK(Pitch(74, 43150, 0.0f) == Pitch(74, 43150, 1.0f)); // (no scale known: taken as none)
}

int main()
{
	TestM4Pitch();
	TestM4PitchInBulletTime();
	TestTheGameTable();
	TestGunshotIds();
	if (gFailures) {
		printf("%d gunsfx test(s) failed\n", gFailures);
		return 1;
	}
	printf("all gunsfx tests passed\n");
	return 0;
}
