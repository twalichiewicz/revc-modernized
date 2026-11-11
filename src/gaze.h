#pragma once
// Head looks for Tommy and the people near him (#56). gazemodel.h decides who
// looks at whom; this hands that to the game's own head look (CPed::SetLookFlag,
// which turns head and torso within CPedIK's limits and eases back after).
// Nothing is looked at while a ped is busy (aiming, fighting, fleeing, driving,
// falling) or during cutscenes and replays; other looks the game or the mod
// set (aiming, combat) are never overridden.
#include <cstdint>

namespace gaze {
bool Install();
// Each frame, after the player's control and aim (player.cpp).
void Frame(uint8_t *player);
} // namespace gaze
