#pragma once
// Car radios heard from outside, muffled. Vice City only plays the radio with
// Tommy in the car. Here, on foot, the nearest car with its radio on keeps
// playing its station: his own after he gets out (for a few minutes), or one
// stopped nearby with someone at the wheel. It's quieter with distance, panned
// towards the car and low-passed, as through the bodywork. The music manager
// does the playing (station, track position, retuning): while it decides,
// cAudioManager::FindVehicleOfPlayer answers with that car.
//
// Also (whatever the setting): a radio stuck repeating a moment of its song
// (the port's streaming, sometimes after the pause menu) is noticed, its place
// in the song not moving on, and started again where the station is by now.

namespace carradio {

bool Install();

} // namespace carradio
