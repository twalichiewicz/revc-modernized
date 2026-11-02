#include "voiceclip.h"

#include <algorithm>
#include <cstdio>
#include <vector>

int main()
{
	// A greeting is one complete utterance, stopping in the silence before
	// the original scene's next line, never including its leading dead air.
	std::vector<int16_t> pcm(3000, 0); // 3 seconds at 1 kHz
	std::fill(pcm.begin() + 200, pcm.begin() + 650, 4000);
	std::fill(pcm.begin() + 1200, pcm.begin() + 1700, 6000);
	const voiceclip::Range hello = voiceclip::FirstPhrase(pcm, 1000);
	if (hello.first > 200 || hello.first < 100 || hello.last < 650 || hello.last > 1000) {
		printf("FAIL: first greeting bounds %zu..%zu\n", hello.first, hello.last);
		return 1;
	}
	const auto quiet = voiceclip::FirstPhrase(std::vector<int16_t>(2000), 1000);
	if (quiet.last != 0 || voiceclip::FirstPhrase(pcm, 0).last != 0) {
		printf("FAIL: silence or invalid sample rate accepted\n");
		return 1;
	}
	// Small gaps inside a spoken word must not chop it into a click.
	std::fill(pcm.begin() + 380, pcm.begin() + 410, 0);
	if (voiceclip::FirstPhrase(pcm, 1000).last < 650) {
		printf("FAIL: a brief intra-word gap truncated the greeting\n");
		return 1;
	}
	// The original phone dialogue has line hiss under the gaps. A fixed
	// amplitude gate treats that hiss as speech and includes the next line.
	for (int16_t &sample : pcm)
		if (sample == 0)
			sample = 300;
	const auto noisy = voiceclip::FirstPhrase(pcm, 1000);
	if (noisy.first > 200 || noisy.last < 650 || noisy.last > 1000) {
		printf("FAIL: phone hiss swallowed the phrase boundary %zu..%zu\n", noisy.first, noisy.last);
		return 1;
	}
	printf("voiceclip: all passed\n");
}
