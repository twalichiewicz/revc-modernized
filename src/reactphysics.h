#pragma once
#include "types.h"
#include <array>
#include <cstdint>

namespace reactphysics {
enum Joint {
	Pelvis,
	Chest,
	Head,
	ShoulderL,
	ElbowL,
	HandL,
	ShoulderR,
	ElbowR,
	HandR,
	HipL,
	KneeL,
	FootL,
	HipR,
	KneeR,
	FootR,
	Count
};
using Pose = std::array<Vec3, Count>;
struct Contact {
	Vec3 position{}, normal{};
	bool hit = false;
	// Unknown geometry or exhausted work is not a clearance certificate,
	// even when a stationary query returns its unchanged starting position.
	bool resolved = true;
};
using Sweep = Contact (*)(Vec3 from, Vec3 to, float radius, void *context);
enum class Mode { Balanced, Falling, Recovering, Settled };
enum class ContactControl { Physical, Native };

class Body {
  public:
	// Capture bone4 attachment and bone5 longitudinal skin axis when available.
	void Reset(const Pose &pose, Vec3 velocity = {}, const Vec3 *neck = nullptr, const Vec3 *headAxis = nullptr);
	Vec3 Neck() const;
	Vec3 HeadAxis() const;
	Vec3 HeadAxis(const Pose &pose) const;
	// Desired velocity change at the segment point (0=a, 1=b). Endpoint
	// inverse masses share the impulse; balance is evaluated once.
	void Impact(Joint a, Joint b, float along, Vec3 deltaVelocity, bool fatal = false);
	// Nondamaging native braking/recoil plus its pre-collision COM velocity.
	// Physical contacts can defeat support. Native locomotion contacts only
	// brace the pose; the accepted animation trajectory retains root ownership.
	void ContactResponse(Joint joint, Vec3 deltaVelocity, Vec3 normal, Vec3 incomingVelocity = {},
	                     ContactControl control = ContactControl::Physical);
	void Impulse(Joint joint, Vec3 deltaVelocity, bool fatal = false);
	void Momentum(Vec3 deltaVelocity, bool fatal = false);
	// A knockdown the game has already decided (CPed::SetFall): the push, and
	// off his feet at once however gentle it is (a trip falls too).
	void Knockdown(Vec3 deltaVelocity);
	// The game has got him up (its get-up finished): a body still recovering is
	// the game's again. It fades out, under fire too, and no longer falls by its
	// own balance or a hit: only the game's next knockdown (Knockdown) or death.
	void Recovered();
	void Advance(float dt, const Pose &target, Sweep sweep, void *context);
	const Pose &Positions() const { return position_; }
	const Pose &Reference() const { return rest_; }
	const Pose &PreviousPositions() const { return frameStart_; }
	// Shape-preserving contact repair. Only a common swept-clear fraction of
	// the requested translation is applied; positional repair adds no energy.
	float Translate(Vec3 delta, Sweep sweep, void *context);
	Vec3 CenterVelocity() const;
	void ResolveVelocity(Vec3 outwardNormal, float surfaceNormalSpeed);
	Vec3 Velocity(Joint joint) const { return velocity_[size_t(joint)]; }
	Mode State() const { return mode_; }
	bool Dead() const { return dead_; }
	bool Active() const { return active_; }
	float Balance() const { return balance_; }
	float Weight() const { return weight_; }
	int LastSteps() const { return lastSteps_; }
	uint32_t Revision() const { return revision_; }
	static float Radius(Joint joint);
	static constexpr int kMaxWorldSweeps = 512;
	static constexpr int kMaxStepSweeps = 768; // solve, support and sleeping validation

  private:
	void Step(const Pose &target, Sweep sweep, void *context, Vec3 targetVelocity);
	void Hit(Joint joint, float strength, bool fatal);
	Vec3 Neck(const Pose &pose) const;
	Contact Constrain(const Pose *previous, Sweep sweep, void *context, bool rigidAttachments = false);
	Contact SolveWorld(const Pose &previous, Sweep sweep, void *context, std::array<Contact, Count> &contacts);
	bool SupportedRest(Sweep sweep, void *context) const;
	Pose position_{}, velocity_{}, rest_{}, frameStart_{};
	std::array<float, Count> length_{};
	Mode mode_ = Mode::Balanced;
	float balance_ = 1, sinceHit_ = 0, still_ = 0, accumulator_ = 0, unsupported_ = 0, weight_ = 0;
	bool dead_ = false, active_ = false;
	bool nativeContact_ = false;
	bool handedBack_ = false; // Recovered: fading out, the game's to knock down
	bool nativeLocomotion_ = false;
	Vec3 nativeVelocity_{};
	int lastSteps_ = 0;
	Vec3 neckOffset_{}, headAxis_{}, contactDirection_{}, targetPelvis_{};
	float headLength_ = 0, contactEffort_ = 0;
	Joint contactJoint_ = Chest;
	Vec3 recoveryAnchor_{};
	Contact recoverySupport_{};
	uint32_t revision_ = 0;
};
} // namespace reactphysics
