#pragma once
// Traffic and people without the old pop-in and sameness.
//
// Pop-in: the game makes cars 100-120 m away where you can see them (fading
// them in) and people 40-50 m away, and draws cars only to about 100 m, all
// scaled by CCamera::GenerationDistMultiplier, which follows the field of
// view but not the draw distance. With a long draw distance you watch them
// appear. Scaling that multiplier (SpawnDistance) moves where they're made,
// where they're taken away and how far cars are drawn out together, past
// where you notice; the caps on cars and people grow with it, so it isn't
// emptier near you.
//
// Sameness: the game keeps 12 car models loaded and swaps in a new one only
// every 350 frames, and picks among the loaded ones by the zone's class, so
// a class with one model loaded fills the street with it. More models stay
// loaded, they turn over every 1.5 s or so, and a model that's already on
// the road several times is passed over for another loaded one.
#include <cstdint>

namespace traffic {

bool Install();
// After CCamera::Process, which sets the multiplier each frame.
void AfterCamera();

} // namespace traffic
