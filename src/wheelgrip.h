#pragma once
#include "vmpose.h"
namespace wheelgrip
{
struct Grip {
	vm::Frame hand;
	vm::Vec3 centre, axis, radial;
	float radius = 0, tube = 0;
};
// Hand's x runs over the rim away from the driver, palm toward the rim.
Grip OnRim(vm::Vec3 centre, vm::Vec3 axis, vm::Vec3 up, vm::Vec3 left, float radius, float tube, float angle);
// Each finger gets its own rim contact, sized from this outfit's articulated hand.
void Contacts(const vm::Arm &, const Grip &, vm::Vec3 targets[vm::kDigits], vm::Vec3 bends[vm::kDigits]);
} // namespace wheelgrip
