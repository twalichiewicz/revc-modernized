#pragma once
// Controller layer, GTA IV/V style. Runs in CPad::Update, between the game
// reading the gamepad and merging it into the pad state:
//   - the D-pad no longer moves the player: left/right switch weapons on foot
//     and change the radio station in a vehicle (GTA IV),
//   - holding LB opens the weapon or radio wheel (wheel.cpp, GTA V); a tap
//     still does what LB did (centre the camera, next station).
// The gamepad is read through GLFW directly, so this works whatever the
// game's own button mapping is.

namespace controls {

bool Install();

} // namespace controls
