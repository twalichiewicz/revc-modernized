#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace airframedebris {
struct Piece {void *geometry=nullptr;std::array<float,16> transform{};};
bool Install();bool Emit(uint8_t *vehicle,const Piece *,size_t count);
}
