#pragma once
#include <cstdint>
namespace crashdamage {
bool Install();
// Actual accepted native contact impulse / body mass, expressed in m/s.
// Queued until the native world pass completes; no hull-health conversion.
void Contact(uint8_t *vehicle,float deltaSpeed);
}
