#include "gunsfx.h"

#include <algorithm>

namespace gunsfx {
namespace {

// Each gun's LEFT sample and its stored rate (the research's table, measured).
struct Gun {
	uint32_t left, rate;
};
constexpr Gun kGuns[] = {{50, 22050}, {88, 17600}, {54, 37000}, {90, 30200}, {71, 17000}, {74, 26000},
                         {68, 32000}, {66, 22050}, {85, 28000}, {57, 24000}, {59, 23000}};
constexpr uint32_t kTails[] = {56, 73, 76, 87}; // UZI_END, TEC_TAIL, RUGER_TAIL, SPAS12_TAIL

} // namespace

const std::vector<uint32_t> &GunshotIds()
{
	static const std::vector<uint32_t> ids = [] {
		std::vector<uint32_t> v;
		for (const Gun &g : kGuns) {
			v.push_back(g.left);
			v.push_back(g.left + 1);
		}
		v.insert(v.end(), std::begin(kTails), std::end(kTails));
		return v;
	}();
	return ids;
}

bool Gunshot(uint32_t id)
{
	const std::vector<uint32_t> &ids = GunshotIds();
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

uint32_t Pitch(uint32_t id, uint32_t freq, float timeScale)
{
	// The game plays the M4 at 43150 ±1000 (the Ruger's at 26000 ±3%), its tail at 15600 (the Ruger's 9400):
	// told apart halfway, at the rate before the time scale.
	constexpr float kM4 = 1.15f / 1.66f;
	const float base = timeScale > 0.0f ? float(freq) / timeScale : float(freq);
	const bool shot = (id == 74 || id == 75) && base > 34000.0f, tail = id == 76 && base > 12500.0f;
	return shot || tail ? uint32_t(float(freq) * kM4) : freq;
}

bool TableLooksRight(const Sample *table, size_t count)
{
	if (!table || count < kBankSamples)
		return false;
	const uint64_t bankEnd = uint64_t(table[kBankSamples - 1].offset) + table[kBankSamples - 1].size;
	for (const Gun &g : kGuns)
		if (table[g.left].rate != g.rate || table[g.left + 1].rate != g.rate)
			return false;
	for (uint32_t id : GunshotIds())
		if (table[id].size == 0 || uint64_t(table[id].offset) + table[id].size > bankEnd || table[id].offset < table[0].offset)
			return false;
	return true;
}

} // namespace gunsfx
