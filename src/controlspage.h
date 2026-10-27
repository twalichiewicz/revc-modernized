#pragma once
// Options' Controls page, in place of the game's Controller Setup pages: the
// game's page 26 keeps its frontend (the camo, the title, Back) and the mod
// draws the rest. The Controller tab shows the pad's status, a picture of it
// with a legend for each situation (bindings.h) and a live button test, and
// the pad's settings; the Keyboard & Mouse tab sets every action's keys and
// the mouse's settings. What it shows is controlsmodel.h's.
#include <cstdint>

namespace controlspage {

constexpr int32_t kPage = 26; // the game's CONTROLLER SETUP

// menu.cpp: its hooks are in (else Options keeps the game's Controller Setup).
void SetAvailable(bool available);
// menu.cpp, the first frame the page is shown: its own list trimmed to Back, the controls Standard.
void Enter(uint8_t *menu);
// From DrawBackground, after the frontend and the tabs.
void Draw(uint8_t *menu);
// From UserInput, before the frontend's: true if the page took this frame's input (the frontend's is skipped).
bool UserInput(uint8_t *menu);
// From ProcessUserInput: the frontend's up / down / left / right / select / back, for the page; what it keeps is zeroed.
void ProcessInput(uint8_t *menu, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change);
// CText::Get: the page's names ("FEO_CON" Controls, "FET_CTL" CONTROLS), or null.
const uint16_t *Text(const char *key);

} // namespace controlspage
