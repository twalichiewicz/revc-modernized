#include "vmkick.h"

#include <algorithm>
#include <cmath>

namespace vm {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kStepRate = 240.0f;                  // the springs' steps a second
constexpr float kBurst = 0.3f;                       // s without a shot: the next one starts a burst
constexpr float kClimbFull = 0.6f, kClimbHold = 0.15f; // s: the climb's timer when full; held after a shot before running down
constexpr float kClimbEase = 25.0f;                  // 1/s: the climb shown catching up
constexpr float kBuzzRate = 10.0f;                   // 1/s: the buzz coming and going

// A class's kick: at its peak, degrees up, m back and up, degrees either way
// over and round (at random); how much of it is there the frame it fires (the
// rest a spring's push), the spring (Hz; damping under 1 overshoots); a
// burst's first shot this much more; each shot's size this much either way;
// for an automatic, s each shot adds to the climb and the climb when full
// (degrees up, m back, degrees over to the left); and a buzz while it fires
// (degrees over, m about).
struct Profile {
	float pitch, back, up, roll, yaw;
	float hard, hz, damping;
	float first, jitter;
	float build, climbPitch, climbBack, climbRoll;
	float buzzRoll, buzzShift;
};
// Starting points from the research (§7, "Recommendations"): pistols 10-20
// degrees, a revolver more and near critically damped, a shotgun 2-4 cm
// straight back, automatics a few degrees with roll; tuned by play.
constexpr Profile kProfiles[] = {
    // pistol: flips up and drops back past where it was
    {14.0f, 0.012f, 0.008f, 3.0f, 1.5f, 0.5f, 2.8f, 0.45f, 1.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // revolver: higher, heavier, no wobble
    {22.0f, 0.020f, 0.012f, 4.0f, 2.0f, 0.6f, 2.2f, 0.9f, 1.0f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // shotgun: shoved straight back into his shoulder
    {6.0f, 0.035f, 0.006f, 3.0f, 1.5f, 0.6f, 2.4f, 0.55f, 1.0f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    // SMG in one hand: jumpy, climbing
    {4.0f, 0.008f, 0.003f, 2.5f, 1.2f, 0.5f, 5.0f, 0.6f, 1.5f, 0.3f, 0.08f, 4.0f, 0.010f, 2.0f, 0.0f, 0.0f},
    // SMG in two (the MP5)
    {2.5f, 0.008f, 0.002f, 2.0f, 0.8f, 0.5f, 5.0f, 0.65f, 1.5f, 0.3f, 0.08f, 2.5f, 0.012f, 2.0f, 0.0f, 0.0f},
    // rifle
    {4.0f, 0.012f, 0.003f, 2.0f, 1.0f, 0.5f, 4.5f, 0.65f, 1.5f, 0.25f, 0.1f, 3.0f, 0.015f, 3.0f, 0.0f, 0.0f},
    // M60: heavy, slow to settle, backing up as it fires
    {2.5f, 0.012f, 0.002f, 2.5f, 1.5f, 0.5f, 3.5f, 0.8f, 1.3f, 0.3f, 0.1f, 2.0f, 0.020f, 4.0f, 0.0f, 0.0f},
    // minigun: a jolt a shot, backing up, buzzing
    {0.4f, 0.004f, 0.001f, 1.2f, 0.6f, 0.3f, 8.0f, 0.6f, 1.0f, 0.5f, 0.05f, 0.5f, 0.015f, 1.0f, 1.2f, 0.0015f},
    // flamethrower: hardly a kick, a low shudder
    {0.3f, 0.002f, 0.0f, 0.3f, 0.2f, 0.3f, 6.0f, 0.7f, 1.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.4f, 0.0015f},
    // none (the rocket launcher fires from the game's scope)
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 4.0f, 0.7f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};

// How far a spring at rest pushed with a unit speed goes before it turns (its peak).
float PeakOfPush(float hz, float damping)
{
	const float w = 2.0f * kPi * hz, z = std::min(damping, 0.99f);
	const float wd = w * std::sqrt(1.0f - z * z), tp = std::atan2(wd, z * w) / wd;
	return std::exp(-z * w * tp) * std::sin(wd * tp) / wd;
}

} // namespace

KickClass KickClassFor(int weapon)
{
	switch (weapon) {
	case 17: return KickClass::kPistol;                      // Colt 45
	case 18: return KickClass::kRevolver;                    // Python
	case 19: case 20: case 21: return KickClass::kShotgun;   // shotgun, SPAS 12, stubby
	case 22: case 23: case 24: return KickClass::kSmg;       // Tec-9, Uzi, Ingram
	case 25: return KickClass::kHeavySmg;                    // MP5
	case 26: case 27: case 28: case 29: return KickClass::kRifle; // M4, Ruger, the sniper rifles
	case 31: return KickClass::kFlame;
	case 32: return KickClass::kLmg;                         // M60
	case 33: return KickClass::kMinigun;
	default: return KickClass::kNone;
	}
}

void Recoil::Shot(KickClass kind, const float random[3])
{
	const Profile &p = kProfiles[int(kind)];
	const float burst = since_ > kBurst ? p.first : 1.0f;
	const float size = burst * (1.0f + p.jitter * (2.0f * random[0] - 1.0f));
	const float roll = random[1] < 0.5f ? -p.roll : p.roll, yaw = p.yaw * (2.0f * random[2] - 1.0f);
	const float peaks[kChannels] = {p.pitch * size, p.back * size, p.up * size, roll * size, yaw};
	const float push = 1.0f / PeakOfPush(p.hz, p.damping);
	for (int c = 0; c < kChannels; c++) {
		x_[c] += p.hard * peaks[c];
		v_[c] += (1.0f - p.hard) * peaks[c] * push;
	}
	climb_ = std::min(kClimbFull, climb_ + p.build);
	since_ = 0.0f;
}

KickMove Recoil::Update(KickClass kind, float dt, bool firing, float spin)
{
	const Profile &p = kProfiles[int(kind)];
	if (dt > 0.0f) {
		const float w = 2.0f * kPi * p.hz, c = 2.0f * std::min(p.damping, 0.99f) * w;
		const int steps = std::max(1, int(std::ceil(dt * kStepRate)));
		const float h = dt / float(steps);
		for (int i = 0; i < steps; i++)
			for (int k = 0; k < kChannels; k++) {
				v_[k] += (-w * w * x_[k] - c * v_[k]) * h;
				x_[k] += v_[k] * h;
			}
		since_ += dt;
		if (since_ > kClimbHold)
			climb_ = std::max(0.0f, climb_ - dt);
		climbShown_ += (climb_ - climbShown_) * std::min(1.0f, kClimbEase * dt);
		const float buzzing = kind == KickClass::kMinigun ? (firing ? 1.0f : 0.25f * spin) : firing ? 1.0f : 0.0f;
		buzz_ += (buzzing - buzz_) * std::min(1.0f, kBuzzRate * dt);
		t_ = std::fmod(t_ + dt, 100.0f);
	}
	const float climb = climbShown_ / kClimbFull;
	KickMove m;
	m.offset = {0.0f, -x_[kBack] - p.climbBack * climb, x_[kUp]};
	m.pitch = x_[kPitch] + p.climbPitch * climb;
	m.roll = x_[kRoll] - p.climbRoll * climb;
	m.yaw = x_[kYaw];
	if (buzz_ > 1e-4f && (p.buzzRoll > 0.0f || p.buzzShift > 0.0f)) { // a buzz of a few unrelated rates (no pattern to see)
		const float b = buzz_, t = t_ * 2.0f * kPi;
		m.roll += p.buzzRoll * b * std::sin(t * 19.0f) * 0.7f + p.buzzRoll * b * std::sin(t * 23.0f + 1.3f) * 0.3f;
		m.pitch += 0.3f * p.buzzRoll * b * std::sin(t * 17.0f + 0.4f);
		m.offset = Add(m.offset, Mul(Vec3{std::sin(t * 27.0f), std::sin(t * 21.0f + 2.1f), std::sin(t * 31.0f + 0.7f)}, p.buzzShift * b));
	}
	return m;
}

} // namespace vm
