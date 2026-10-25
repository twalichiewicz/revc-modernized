#pragma once
// The port's ray-traced shadows (CRTShadows, Metal RT: MetalRayTracing=1 in
// macVC.ini) report what they do through re3_debug, which only reaches a
// terminal. Their lines go into our log too, a few at a time: how long the
// trace takes and how much it holds, when its scene of buildings is rebuilt,
// and when it's full (no more buildings cast shadows after that).
namespace rtshadows {

bool Install();

} // namespace rtshadows
