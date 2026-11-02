#include "voiceclip.h"
#include <algorithm>
#include <cmath>

namespace voiceclip {
Range FirstPhrase(const std::vector<int16_t> &pcm, int rate)
{
	if (rate <= 0 || pcm.empty())
		return {};
	const size_t window = std::max(1, rate / 100), lead = size_t(rate / 20);
	const size_t silence = size_t(rate / 4), most = std::min(pcm.size(), size_t(rate) * 3);
	std::vector<double> energy;
	double peak = 0;
	for (size_t at = 0; at < most; at += window) {
		const size_t end = std::min(at + window, most);
		double sum = 0;
		for (size_t i = at; i < end; i++)
			sum += double(pcm[i]) * pcm[i];
		const double rms = std::sqrt(sum / double(end - at));
		energy.push_back(rms);
		peak = std::max(peak, rms);
	}
	const double threshold = std::max(220.0, peak * 0.15);
	size_t start = pcm.size(), lastVoice = 0;
	for (size_t at = 0; at < most; at += window) {
		const size_t end = std::min(at + window, most);
		const bool voiced = energy[at / window] > threshold;
		if (voiced) {
			if (start == pcm.size())
				start = at > lead ? at - lead : 0;
			lastVoice = end;
		} else if (start != pcm.size() && at - lastVoice >= silence) {
			return {start, std::min(pcm.size(), lastVoice + size_t(rate / 10))};
		}
	}
	return start == pcm.size() ? Range{} : Range{start, std::min(most, lastVoice + size_t(rate / 10))};
}
} // namespace voiceclip
