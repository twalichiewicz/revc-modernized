#pragma once
// Car radios heard from outside, muffled. Vice City only plays the radio with
// Tommy in the car. Here, on foot, the nearest car with its radio on keeps
// playing its station: his own after he gets out (for a few minutes), or one
// stopped nearby with someone at the wheel. It's quieter with distance, panned
// towards the car and low-passed, as through the bodywork. The music manager
// does the playing (station, track position, retuning): while it decides,
// cAudioManager::FindVehicleOfPlayer answers with that car.

namespace carradio {

bool Install();

} // namespace carradio
