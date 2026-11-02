#pragma once
#include "gameplaymodel.h"
#include <cstdint>
namespace gameplaypage {
constexpr int32_t kPage = gameplaymodel::kPage;
void SetAvailable(bool available);
// First frontend draw: reserve the checked native page and insert Options item.
void Prepare(uint8_t *menu);
void Enter(uint8_t *menu);
void Draw(uint8_t *menu);
bool UserInput(uint8_t *menu);
void ProcessInput(uint8_t *menu, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change);
const uint16_t *Text(const char *key);
} // namespace gameplaypage
