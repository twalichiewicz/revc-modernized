#pragma once
#include <cstdint>
namespace flightmodel {
// Shared native FlyingControl entry: fixed-wing owner is consulted first.
bool Install();
bool RotorFailed(uint8_t *vehicle);
void Frame(uint32_t now);
}
