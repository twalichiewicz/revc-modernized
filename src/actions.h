#pragma once
// The keyboard and mouse side of the mod's own actions (the weapon and radio
// wheels, greet / insult, bullet time, drive-by aiming, the cinematic camera,
// the big map...), which were the pad's alone: their keys (the ini's MOD_
// lines, else bindings.h's defaults that don't clash with the game's), and
// whether they're held, read each frame through the game's own key state.
#include "bindings.h"
#include "keys.h"

#include <array>
#include <cstdint>

namespace actions {

// The keys of one of the mod's actions; for an alias (the call's answer, a
// drive-by's fire), its game action's.
keys::Binding Keys(bindings::Action a);
// The game's keys for one of its actions (its keyboard key, the extra one, or the mouse button).
keys::Binding GameKeys(bindings::Action a);
// A game action's three settings as codes, by type: [0] its keyboard key, [1] the second key, [2] its mouse button (0: none).
std::array<uint16_t, 3> GameSlots(bindings::Action a);
// The ini's keys are read, if they weren't yet: the Controls page can open before the first gameplay frame.
void LoadKeys();
// One of the mod's actions gets these keys from now on (saved to the ini's [Keys]).
void SetKeys(bindings::Action a, const keys::Binding &b);
// The game's keys were changed (the Controls page): hints on screen are filled in again.
void GameKeysChanged();
// Goes up whenever a binding changes, so hints on screen are filled in again.
uint32_t Version();
// From CPad::Update each frame, outside the menus: the keys' state.
void Update();
bool KeyHeld(bindings::Action a);
bool KeyPressed(bindings::Action a); // went down this frame
bool KeyReleased(bindings::Action a);

} // namespace actions
