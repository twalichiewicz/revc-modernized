#pragma once
// Pulled out of his car only when it's barely moving (grip.h has the rules).
// Whoever's after Tommy's door (a cop arresting him, anyone jacking him) can
// open it only while the car crawls (CVehicle::CanPedEnterCar, for one
// seeking it: CPed::SeekCar); faster, one who reaches it catches hold of the
// handle and hangs on (CPed::AttachPedToEntity at the door, tugging at it:
// CAR_doorlocked), dragged along, till his grip goes and he lets go and falls
// (CPed::SetFall, sliding off with the car's speed) or the car slows and he
// opens it. One already opening it as the car pulls away stops (CPed::
// QuitEnteringCar) and hangs on instead. Cars only; on a bike there's
// nothing to hold, so he just can't. CopsGrabDoors = 0 leaves it as the game
// had it.
namespace doorgrab {

bool Install();
void Update(); // each frame, after the world (camera.cpp)

} // namespace doorgrab
