#pragma once
// Vehicles in the rain (#32), in the game (vehiclerain.h has the rules): cars'
// wheels on soaked grass, mud, gravel or sand lose grip and drive and are held
// back (CVehicle::ProcessWheel); his aircraft, clear of the ground, is thrown
// about by turbulence, pushed by the gusts and loses a little lift (after
// CVehicle::FlyingControl). With RainVehicles=0, neither.
#include <cstdint>

namespace rainvehicles {

bool Install();

// (for the tests)
void Hook_ProcessWheel(uint8_t *vehicle, float *fwd, float *right, float *contactSpeed, float *contactPoint, int32_t wheelsOnGround,
                       float thrust, float brake, float adhesion, int8_t wheel, float *wheelSpeed, void *state, uint16_t status);
void Hook_FlyingControl(uint8_t *vehicle, int32_t model);

} // namespace rainvehicles
