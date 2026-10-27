#pragma once
// First person's guns: Tommy's own arm, cut from whichever outfit's model he
// has on and articulated (vmarms), holding a new low-poly gun (vmgun) with
// his fingers fitted to it (vmfit, vmpose), animated (vmanim), and drawn in
// a pass of its own after the world (CRenderer::RenderFirstPersonVehicle):
// through its own field of view, never into a wall (the depth is cleared
// first), lit as the game lights him. His real arms are shrunk away
// meanwhile; wherever it isn't used (a gun without a new model yet, fists,
// climbing, swimming, the dive) his real arms are posed as before
// (viewmodel.cpp). FirstPersonGuns = 0 gives first person as before.
#include "game.h"

#include <cstdint>

namespace fpgun {

bool Install();
// From camera::AfterPreRender, in first person on foot: works out this
// frame's viewmodel and hides his real arms; false if today's viewmodel
// should pose them instead.
bool Prepare(uint8_t *ped);
// Last frame: the new viewmodel was drawn; it's up and may fire; where its muzzle was (world).
bool Active();
bool Ready();
CVector Muzzle();

} // namespace fpgun
