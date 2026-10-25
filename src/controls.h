#pragma once
// Controller layer, GTA IV/V style. Runs in CPad::Update, between the game
// reading the gamepad and merging it into the pad state:
//   - the D-pad no longer moves the player: left/right switch weapons on foot;
//     in a vehicle a tap changes the radio station and a hold opens the radio
//     wheel; down zooms the minimap out; up starts a vehicle's side mission,
//   - on foot, holding LB opens the weapon wheel (wheel.cpp, GTA V), LT aims
//     and RT fires; the right stick looks smoothly (aimcam.cpp aims),
//   - in a vehicle, RT is the throttle and LT the brake, R3 looks behind, and
//     holding LB aims a drive-by, RB shooting (driveby.cpp),
//   - while a response prompt is up (interact.cpp), X / Square and B / Circle
//     answer it, as RDR2's greet and antagonize.
// The gamepad is read through GLFW directly, so this works whatever the
// game's own button mapping is.

namespace controls {

bool Install();
// A PlayStation pad is connected (for the button prompts' symbols).
bool PlayStationPad();

} // namespace controls
