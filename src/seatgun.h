#pragma once
// The gun he drives with. The game puts his SMG (or the pistol a drive-by
// took out) in his right hand in any vehicle, so on the wheel it was in front
// of the view in first person. Now, in a car, it lies on the console between
// the front seats while he drives (or on his own seat by his thigh, in a car
// with nothing between them: consolespot.h finds where for each model), and
// he takes it up to aim: holding LB his right hand goes down to it and brings
// it up into the drive-by, and letting go he puts it back down and his hand
// goes back to the wheel. It's in his hand before a shot leaves. On a bike or
// a boat it's put away (out of his hand) till he aims. The gun on the console
// is the game's own model of it (CSimpleModelInfo::CreateInstance), drawn
// after the car (carlights.cpp has CAutomobile::Render's hook), lit as the
// car is. Needs the free-aim drive-by (DriveByAim); ConsoleGun = 0 leaves the
// gun in his hand as the game has it.
#include <cstdint>

namespace seatgun {

bool Install();
// From CPed::PreRender (player.cpp), before the game's: the gun in his hand or not.
void BeforePose(uint8_t *ped);
// After steering::PoseDriver: his right hand reaching for it, or putting it back.
void Pose(uint8_t *ped);
// After the game has drawn a car: the gun on its console.
void DrawCar(uint8_t *car);
// The drive-by may fire: the gun's in his hand (or the game's to manage).
bool InHand();

} // namespace seatgun
