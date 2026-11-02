#pragma once
#include <cstdint>
namespace jetwash {
bool Install();
// After airportjets/planeflight, once per native game frame.
void Frame(uint32_t now);
}
