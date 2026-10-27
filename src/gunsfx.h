#pragma once
// Which of the game's sound samples are gunshots, and whether the sample
// table is the one this was written for (macVC 1.0.5217's sfx.SDT: each gun's
// stored rate, the gunshots inside bank 0). The ids are reVC's (AudioSamples.h;
// docs/research/2026-10-27-gunshot-audio.md §3): each gun's LEFT sample and
// its RIGHT partner (the next id), and the four tails. Unit-tested
// (tests/test_gunsfx.cpp).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gunsfx {

// cSampleManager::m_aSamples' entries (tSample).
struct Sample {
	uint32_t offset, size, rate, loopStart;
	int32_t loopEnd;
};
constexpr uint32_t kBankSamples = 524; // bank 0: entries 0..523

bool Gunshot(uint32_t id);
const std::vector<uint32_t> &GunshotIds();
// The rate a sample plays at, as the game asked for `freq` (its rate times the
// time scale it applies: bullet time slows every sound): the M4's (the Ruger's
// sample and tail played 1.66 times fast, tinny) at 1.15; the rest unchanged.
uint32_t Pitch(uint32_t id, uint32_t freq, float timeScale);

// `count` entries of the table: enough, the guns' rates as expected, every gunshot inside bank 0.
bool TableLooksRight(const Sample *table, size_t count);

} // namespace gunsfx
