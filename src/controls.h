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

#include "bindings.h"
#include "controlsmodel.h"

#include <vector>

namespace controls {

bool Install();
// The joysticks connected (for the Controls page's status), and the one the game reads (-1: none).
std::vector<controlsmodel::Joystick> Pads();
int GameJoystickId();
// The game reads this joystick from now on (the Controls page's USE THIS ONE).
void UseJoystick(int id);
// The pad's button is down now (the Controls page's live test).
bool Down(bindings::Pad pad);
// The button is down on this joystick, whichever the game reads (the Controls page's USE THIS ONE).
bool Down(int joystick, bindings::Pad pad);
// The kind of pad connected (PlayStation if any is, then Nintendo, else Xbox);
// with none connected, the last one's.
bindings::PadFamily Family();
// The right stick's look this frame (radians: yaw to the right, pitch up), at
// the on-foot camera's feel and sensitivity; false while it's at rest.
bool LookTurn(float &yaw, float &pitch);
// How far the triggers are pulled, 0 to 1, as last read (the frontend's too;
// it only sees them as buttons).
void Triggers(float &lt, float &rt);

} // namespace controls
