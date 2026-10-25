#pragma once
// The gun in first person, as Counter-Strike or Call of Duty show it: held to
// the shoulder low on the right, pointing where you look; aiming (LT) brings
// it up to the middle, its sights just under where the shots go. Done to his
// own arms after the game has posed him (after CPed::PreRender): the right
// hand is put where the gun goes, holding it along the view (the gun is drawn
// in that hand), the elbows bent to reach (two-bone IK, each bone laid along
// its arm about the elbow's hinge, its axes as the skeleton has them); for
// two-handed guns the left hand goes where his aiming pose holds it. It bobs with his steps, kicks as it fires, dips while he
// reloads and drops a little while he sprints.
#include "game.h"

#include <cstdint>

namespace viewmodel {

bool Install();
// After the game's CPed::PreRender, in first person (camera.cpp): on foot, or
// a drive-by from the seat view (`aimed`: held as aiming).
void Apply(uint8_t *ped, bool aimed = false);
// One of his arms reaching `wrist` (world), the elbow bent towards `pole` (a
// direction), the hand turned on the forearm as the animation has it. For the
// steering wheel (steering.cpp); after CPed::PreRender.
void ReachArm(uint8_t *ped, bool left, const CVector &wrist, const CVector &pole);

} // namespace viewmodel
