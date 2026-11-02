#pragma once
// Contact posing for Tommy, independent of the ordinary NPC body controller.
// Contact velocities are world metres/second. The native controller remains authoritative.
#include "types.h"
#include "vmmath.h"
#include <array>
#include <cstdint>

namespace playerreact {
struct Response {
	float weight = 0;
	Vec3 point{}, normal{};
};
class Guard {
  public:
	bool Contact(Vec3 point, Vec3 outwardNormal, Vec3 relativeVelocity);
	void Advance(float dt);
	void Reset();
	Response Current() const;
  private:
	Response response_{};
	float remaining_ = 0;
};
constexpr int kBones = 22;
using Matrix = std::array<float, 16>;
using Matrices = std::array<Matrix, kBones>;
struct ArmReach {
	vm::Vec3 elbow{}, wrist{};
	bool valid=false;
};
// A measured two-bone reach in the shoulder's forward half-space. Keeps a
// small elbow bend and never asks the upper arm to extend behind the torso.
ArmReach ProtectiveReach(vm::Vec3 shoulder, float upperLength, float foreLength,
                         vm::Vec3 target, vm::Vec3 forward, vm::Vec3 pole);
// Rigid posing of a captured native skeleton; no root/leg/controller edits.
Matrices Pose(const Matrices &animated, Response response, Vec3 forward, bool aiming);
// In view coordinates (x right, y forward, z up): shared by gun and hands.
vm::Frame ViewTransform(Response response, Vec3 right, Vec3 forward, bool aiming);
vm::Frame GuardHand(Response response, Vec3 right, Vec3 forward, bool left);

void Contact(uint8_t *ped, Vec3 worldPoint, Vec3 outwardNormal, Vec3 relativeVelocity);
// A native contact knockdown may take over only ordinary, freely controlled
// locomotion. Revalidates the player/pool generation and scripted camera owner.
bool CanKnockdown(uint8_t *ped);
bool CanResistContact(uint8_t *ped);
// A native knockdown may ragdoll him (#25): freely controlled on foot, his own
// pose owners (swim, climb, dive, crouch) and any scripted camera clear.
bool CanRagdoll(uint8_t *ped);
// While his ragdoll owns him: nothing else has taken him over (swimming can
// start mid-fall, knocked off a pier).
bool CanStayDown(uint8_t *ped);
void BeforeControl(uint8_t *ped);
void AfterControl(uint8_t *ped);
// Before: AFTER camera restoration, before native PreRender. After: before camera hides bones.
void BeforePreRender(uint8_t *ped);
void AfterPreRender(uint8_t *ped);
Response Current(uint8_t *ped);
vm::Frame ViewTransform(uint8_t *ped);
vm::Frame GuardHand(uint8_t *ped, bool left);
} // namespace playerreact
