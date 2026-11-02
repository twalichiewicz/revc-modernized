#pragma once
// The port's ray-traced shadows (CRTShadows, Metal RT: MetalRayTracing=1 in
// macVC.ini) report what they do through re3_debug, which only reaches a
// terminal. Their lines go into our log too, a few at a time: how long the
// trace takes and how much it holds, when its scene of buildings is rebuilt,
// and when it's full (no more buildings cast shadows after that).
// Each frame's moving geometry (rw::gl3::mtlrtSetDynamicTriangles) is checked
// first: triangles Metal can't trace sanely are left out, and logged (rtgeometry.h).
namespace rtshadows {

bool Install();
// The last trace's moving geometry, for the watchdog's freeze report.
void LogLastGeometry();

} // namespace rtshadows
