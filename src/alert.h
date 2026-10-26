#pragma once
// The sound of being spotted (as Hitman has it): while a cop's eye fills, a
// low swell that rises in pitch and grows louder with it; when one fills, a
// sting. Made here (a few synthesized notes, played through the game's own
// OpenAL), at the game's effects volume, and silent while it's paused.

namespace alert {

bool Install();
void Frame();               // each frame, paused or not (CPad::Update): the sound kept in step
void Spotting(float fill);  // the fullest eye now, 0..1 (sight.cpp, as it looks)
void Spotted();             // an eye's just filled

} // namespace alert
