#pragma once
// First person's shooting, as Counter-Strike 1.6's (fpfire has the rules): in
// first person on foot RT is the mod's, not the game's, and a gun fires the
// frame it's pulled, through CWeapon::Fire (so the clip, the reload, the
// sound and flash, police hearing it and gunmen taking cover all stay the
// game's), never waiting for Tommy's attack animation; each shot punches the
// view (added to the camera with its shake, so shots go along it) and spreads
// as he keeps firing or moves. Scoped and thrown weapons keep the game's own
// firing; so do the keyboard and mouse. FirstPersonGuns = 0 gives today's.
#include <cstdint>

namespace fpshoot {

bool Install();
// From the pad's update (controls.cpp): RT; true if the mod fires with it
// (the game's fire button is then left up).
bool FromPad(bool rtDown);
// After CPlayerPed::ProcessPlayerWeapon: this frame's shots.
void AfterWeapon(uint8_t *ped, uint8_t *pad);
// Every frame (from the camera): the punch settling, the spread recovering, on game time.
void Tick(float dt);
// RT held with the mod firing; or firing, or just fired (sprinting drops to a run for it).
bool TriggerHeld();
bool Shooting();
// The view's punch this frame (degrees: up, right), for the camera.
void Punch(float &up, float &right);
// The shot's jolt this frame (degrees: left, up, over to the right): the force
// his body takes, a quick shake; the gun itself kicks little.
void Jolt(float &left, float &up, float &roll);
// The cone a shot leaving now spreads into (radians; 0 when it's exact), for CWeapon::Fire's hook.
float SpreadNow();
// Whether the mod is firing in first person, and the spread the next shot would have: the crosshair.
bool Firing();
float SpreadShown();

} // namespace fpshoot
