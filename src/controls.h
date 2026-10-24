#pragma once
// Controller layer, GTA IV/V style. Runs in CPad::Update, between the game
// reading the gamepad and merging it into the pad state:
//   - the D-pad no longer moves the player: left/right switch weapons on foot;
//     in a vehicle a tap changes the radio station and a hold opens the radio
//     wheel; down zooms the minimap out; up starts a vehicle's side mission,
//   - on foot, holding LB opens the weapon wheel (wheel.cpp, GTA V), LT aims
//     and RT fires; the right stick looks smoothly (aimcam.cpp aims),
//   - in a vehicle, RT is the throttle and LT the brake, R3 looks behind, and
//     holding LB aims a drive-by, RB shooting (driveby.cpp).
// The gamepad is read through GLFW directly, so this works whatever the
// game's own button mapping is.

namespace controls {

bool Install();

} // namespace controls
