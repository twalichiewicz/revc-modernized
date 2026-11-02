#pragma once
// How a gun kicks in his hands as it fires (prior art and numbers:
// docs/research/2026-10-27-viewmodel-fire.md). Each class of gun its own: a
// pistol flips up 14 degrees, a revolver more and heavier, a shotgun shoves
// straight back, automatics jolt a little each shot and climb as they keep
// firing (Doom 3's muzzle rise: each shot adds to a timer that runs down), a
// minigun buzzes. Part of each kick is there the frame it fires, the rest a
// spring's push, so it snaps and then settles with a little overshoot (as
// Counter-Strike's shoot animations do; Apex's springs); each shot's size,
// roll and yaw a little different, a burst's first shot bigger. The gun
// turns about his forearm, so his arm goes with it. Game time.
// Unit-tested (tests/test_vmkick.cpp).
#include "vmmath.h"

namespace vm {

enum class KickClass { kPistol, kRevolver, kShotgun, kSmg, kHeavySmg, kRifle, kLmg, kMinigun, kFlame, kNone };
KickClass KickClassFor(int weapon);

// The kick this frame: m in the view's frame (x right, y ahead, z up), and
// degrees: up, over to the right, its muzzle to the left.
struct KickMove {
	Vec3 offset;
	float pitch, roll, yaw;
};

class Recoil {
public:
	// A shot: `random` three numbers 0..1 (its size, its roll's side, its yaw).
	void Shot(KickClass kind, const float random[3]);
	// `firing`: the trigger held (a minigun's or flamethrower's buzz); `spin`:
	// a minigun's barrels, 0..1 of their top speed.
	KickMove Update(KickClass kind, float dt, bool firing, float spin);

private:
	enum { kPitch, kBack, kUp, kRoll, kYaw, kChannels };
	float x_[kChannels] = {}, v_[kChannels] = {};
	float climb_ = 0.0f, climbShown_ = 0.0f; // s on the climb's timer; as shown (eased)
	float since_ = 1e9f;                     // s since the last shot
	float buzz_ = 0.0f, t_ = 0.0f;
};

} // namespace vm
