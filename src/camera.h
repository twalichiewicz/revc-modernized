#pragma once
// After the game's camera has worked out the frame (CCamera::Process):
//
// First person (GTA V style): the Select button's cycle on foot (near, mid,
// far) goes on to first person, and in a vehicle the bumper view is from his
// eyes instead. The view keeps the game's own camera's direction (so walking,
// steering, looking about and aiming work as they do); only the eye moves,
// to Tommy's head, steadied as first-person games do (it follows his body;
// his head's bob and lean about it ease out; a gentle step bob of its own),
// and his head is hidden. While aiming, he looks from his eye at what the
// shots' line through the crosshair hits, so they go where the middle of the
// screen is. He holds a gun as a first-person shooter shows it
// (viewmodel.cpp). It goes with him into vehicles, as the seat view (the
// game's bumper view, from his eyes, his vehicle drawn about him), and out
// again. In a cutscene or a scope the game's own cameras take over, and first
// person comes back after.
//
// Camera shake (GTA IV style): a slow handheld drift on foot, more as he
// runs; in a vehicle a sway and a hum with speed, and a jolt over bumps and
// in crashes (sudden changes of the vehicle's speed), all very slight. Less
// while aiming and in first person.
//
// Both rotate or move the camera's matrix and hand it to RenderWare again,
// as Process does.
#include <cstdint>

struct CVector;

namespace camera {

bool Install();
// From the CPed::PreRender hook, after the game's: in first person his head
// is shrunk away (its skinning matrix), so the eye isn't inside it.
void AfterPreRender(uint8_t *ped);
// For the gun in first person (viewmodel.cpp): on foot in first person this
// frame; how far into aiming (0..1); his steps (phase 0..2 a stride, amount 0..1).
bool FirstPersonOnFoot();
// First person chosen on foot (the game's own scope can be up: sniper.cpp).
bool FirstPersonChosen();
// Where first person's eye is in his head (the scope's eye).
CVector ScopeEye(uint8_t *ped);
// The view's forward, as the camera's matrix has it now.
CVector ViewForward();
// In a vehicle's first-person seat view this frame.
bool InSeat();
float AimBlend();
float StepPhase();
float StepAmount();
// His speed on the ground (m/s, eased; 0 in the air): first person's sway with his steps.
float StepSpeed();

} // namespace camera
