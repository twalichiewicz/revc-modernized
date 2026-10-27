#pragma once
// Where the viewmodel's gun is in the view, and the view it's seen through:
// framed as Counter-Strike frames its guns (low on the right, turned in
// towards the crosshair), its sights brought up under the crosshair while
// aiming, through a field of view of its own whatever the game's, and drawn
// pushed out from the eye (scaled about it, so it looks the same) where the
// game's near plane can't cut it. Unit-tested (tests/test_vmview.cpp).
#include "vmgun.h"

namespace vm {

// The Colt's, as the spike framed it (Framing is in vmgun.h; each gun carries its own).
constexpr Framing kColtFraming{{-0.150f, -0.195f, 0.160f}, 13.0f, -3.0f, 0.22f, 0.02f};

// The gun's frame in the view's (x right, y ahead, z up, the eye at the
// origin): the hip at `aim` 0, the sights on the crosshair's line at 1.
Frame GunInView(const Gun &gun, const Framing &framing, float aim);

// How much the view's x and z shrink (or grow) for the viewmodel to look as
// through its own vertical field of view (degrees), the game's being 2·atan(gameTanHalfV).
inline float FovSquash(float gameTanHalfV, float viewmodelFovV)
{
	return gameTanHalfV / std::tan(viewmodelFovV * 3.14159265f / 360.0f);
}

constexpr float kPushOut = 4.0f;

// A point in the viewmodel's view space, where it's drawn in the world (`view`: the eye's frame).
inline Vec3 ToWorld(const Frame &view, Vec3 p, float squash)
{
	return Apply(view, {p.x * squash * kPushOut, p.y * kPushOut, p.z * squash * kPushOut});
}
// A direction (a normal), likewise; unsquashed, so lighting sees the shape as it is.
inline Vec3 ToWorldDirection(const Frame &view, Vec3 d) { return Rotate(view, d); }

} // namespace vm
