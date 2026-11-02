#pragma once
#include <cstdint>
namespace jetboardingpose {
// Around the original CPed::PreRender, after other procedural arm owners.
// The owned reach borrows two arm keys; crouch walking and sitting borrow
// the posed rig and root translation until the native render rebuild.
bool BeforePedRender(uint8_t *ped);
void AfterPedRender(uint8_t *ped);
}
