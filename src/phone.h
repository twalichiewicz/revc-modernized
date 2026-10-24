#pragma once
// Phone calls, GTA IV/V style. Vice City's story calls (the CELL script in
// main.scm) only ring while Tommy is on foot, are picked up with L1, and keep
// him standing still with the phone to his ear until they're over. Here they
// ring in vehicles too, a 1980s brick phone slides up in the bottom-right
// corner with the caller's name, D-pad up answers, and Tommy keeps walking or
// driving with the phone at his ear.
#include <cstdint>

namespace phone {

bool Install();
// The phone is ringing: D-pad up answers it (Answer, from the pad hook).
bool Ringing();
void Answer();
// After CPad::Update: holds L1, which the script waits for, until it picks up.
void HoldAnswer(uint8_t *pad);
// From CHud::Draw.
void Draw();

} // namespace phone
