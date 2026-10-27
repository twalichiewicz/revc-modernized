#pragma once
// The sniper rifles' scope in first person, on the game's own sniper camera
// (scope.h has the rules; docs/research/2026-10-27-sniper-scope.md why).
// LT raises the gun to the eye (the viewmodel's aim), and only then does the
// game get its target button and go into its scope; RB steps the zoom, L3
// holds his breath (kept from the game's duck while scoped). The camera's
// eye is first person's, swayed, at the scope's zoom (CCam::Process_Sniper's
// hook); shots go along it, spread as he creeps. A shot drops the scope while
// the bolt cycles (the target button let go: his aimed gun shows), then it's
// back. The overlay is hud.cpp's.
#include "scope.h"

#include <cstdint>

struct CVector;

namespace sniper {

bool Install();
// From CPad::Update (controls.cpp): LT, RB's press and L3; whether the game may
// have its target button now (with any other gun, whenever LT is down).
bool GameAims(bool lt, bool rbPressed, bool l3);
// L3 kept from the game (its duck): held with the rifle up, till it's let go
// (through a shot's drop out of the game's scope too).
bool KeepsL3();
// Every frame (camera.cpp): the rules run, on game time.
void Frame(float dt);
// LT held with a sniper rifle in first person: the gun raised to the eye.
bool Wanted();
// The game's scope up, and the mod's: he creeps, the overlay shows.
bool Active();
const scope::Output &Now();
// The scope's last view, the first frame first person is back after it (within
// a few frames; camera.cpp carries it on: the follow camera would point
// elsewhere); once.
bool TakeLeftView(CVector &front);
// Around CWeapon::Fire for his shot in the scope: spread turned into the
// camera before, put back after (and the bolt, if it fired).
void BeforeShot();
void AfterShot(bool fired);

} // namespace sniper
