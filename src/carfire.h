#pragma once
#include <cstdint>
namespace carfire {
bool Install();
// Unknown/unmanaged vehicles retain the caller's native-health fallback.
bool Burning(const uint8_t *vehicle,bool nativeFallback);
}
