#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace voiceclip {
struct Range {
	size_t first = 0, last = 0;
};
// Bounds of the first voiced phrase in mono PCM16; silence yields an empty range.
Range FirstPhrase(const std::vector<int16_t> &pcm, int rate);
} // namespace voiceclip
