#pragma once
// Steering wheels that turn. Vice City's cars have the wheel modelled into
// the body (chassis_hi), a pentagon of a ring with a bar across, so nothing
// turns it. For each car model the wheel is found once (in front of the
// driver's seat: a flat piece 0.2-0.6 m across facing him, and whatever
// shares its middle), its triangles collapsed out of the body (the geometry
// is the model's, so for every car of it), and a round three-spoked wheel of
// the same size, in the same place and of the same colour drawn instead,
// turned by the car's steering, with a blinker stalk on the column behind it.
// Drawn after CAutomobile::Render (carlights.cpp has the hook) for cars close
// enough to show their insides, lit as the game lights them.
//
// Tommy's hands go on it (after CPed::PreRender, driving): at ten to two,
// turning with it. Sounding the horn (the left stick's click) his right hand
// presses its middle; when the blinkers come on (carlights.cpp) his left hand
// flicks the stalk, down for a left turn, up for a right, and it stays there
// until the signal ends.
#include <cstdint>

namespace steering {

bool Install();
// After the game has drawn a car.
void DrawCar(uint8_t *car);
// After the game has posed a ped (the player driving).
void PoseDriver(uint8_t *ped);

} // namespace steering
