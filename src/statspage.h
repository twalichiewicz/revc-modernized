#pragma once
// The pause menu's Stats page (#24), redrawn: a STATS | ACHIEVEMENTS pill row
// (the Controls page's), the game's own stat lines in two columns, and the
// achievements with each one's progress. CMenuManager::PrintStats is hooked off.
#include "statsmodel.h"
#include <cstdint>

namespace statspage {
bool Install(); // binds CStats::ConstructStatLine; false: the vanilla page stays
void SetAvailable(bool available);
bool Available();
void Enter(uint8_t *menu);
void Draw(uint8_t *menu);
bool UserInput(uint8_t *menu);
void ProcessInput(uint8_t *menu, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change);
// The unlock notice, top centre, every HUD frame.
void DrawNotice();
} // namespace statspage
