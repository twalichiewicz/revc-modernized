#pragma once
// Environment-enabled acceptance in the ordinary saved-game world.
void VehicleBodySmokeTick(unsigned frames,const char *capture);
// VC_REGULAR_WING=body-curb: real kerb runs with per-frame motion telemetry.
void VehicleCurbSmokeTick(unsigned frames,const char *capture);
// body-bump: light strikes into kerbs, walls, a pole and a parked car, each
// run on a material car and an unowned stock twin, then a throttle probe.
void VehicleBumpSmokeTick(unsigned frames,const char *capture);
