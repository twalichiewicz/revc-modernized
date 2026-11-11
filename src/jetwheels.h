#pragma once
#include "game.h"
#include <array>

namespace jetwheels {
struct Gear {CVector center;float radius,width;bool nose;};
// Original Learjet636's three separate92-face tire components, measured in
// model space. Axles run along localX; only the nose assembly steers aboutZ.
inline constexpr std::array<Gear,3> kGear{{
 {{.003197f,6.161244f,-1.867551f},.327233f,.346674f,true},
 {{-1.080758f,-.980299f,-1.867551f},.327233f,.346674f,false},
 {{1.071306f,-.980299f,-1.867551f},.327233f,.346674f,false}
}};
inline constexpr float kTravel=.10f,kTireBottom=-2.194784f;
// Original deaddodo.dff: separate nose wheel and two tires in wheel_rear.
inline constexpr std::array<Gear,3> kDodoGear{{
 {{0,2.649681f,-1.243550f},.258f,.176f,true},
 {{-1.790860f,-.432198f,-1.243550f},.258f,.176f,false},
 {{1.790860f,-.432198f,-1.243550f},.258f,.176f,false}
}};
inline constexpr float kDodoTireBottom=-1.501550f;
struct WheelPose {
 float spin=0;         // actual accumulated rotation about localX, radians
 float compression=0;  // wheel center displacement along localZ, metres
 float steer=0;        // actual nose rotation about localZ, radians
 bool grounded=false;
};
bool Install();
bool Grounded(const uint8_t *vehicle);
// Copies current-generation physical wheel state; false clears the output.
bool Snapshot(const uint8_t *vehicle,std::array<WheelPose,3> &out);
}
