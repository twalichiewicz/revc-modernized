#pragma once
// First person in a car: his eye raised where the car's hood or dash comes up
// to it (seatview.h), so he sees the road over it. Worked out once a model,
// from its body as it's loaded and his eye as he sits in it; eased in.
#include "types.h"

#include <cstdint>

namespace seateye {

bool Install();
// How far (m) his eye goes up in `vehicle` this frame: his eye in its frame
// `eye` (x right, y ahead, z up), `driving` once he's in the seat (not
// getting in or out). 0 in anything but a car, or till it's known.
float Lift(const uint8_t *vehicle, Vec3 eye, bool driving, float dt);

} // namespace seateye
