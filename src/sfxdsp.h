#pragma once
// The game's gunshot samples, cleaned up and given weight in place (once,
// as the bank loads: gunbank.cpp): their offset from the middle taken out
// (several sit 8% off), the low end lifted 5 dB under 150 Hz, the first
// milliseconds (the crack) lifted, a little saturation for density, and the
// peak where it was (never louder), a decibel under full scale at most. The length and rate never change (the
// game pitches a sample against its stored rate). Unit-tested
// (tests/test_sfxdsp.cpp).
#include <cstddef>
#include <cstdint>

namespace sfxdsp {

void Punch(int16_t *pcm, size_t n, int rate);

} // namespace sfxdsp
