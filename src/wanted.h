#pragma once
// The wanted overhaul's bridge to the game: it feeds heat::Model (what the
// police know) from the game every frame and keeps the game's own wanted
// level in step with it, so missions and the police read the stars as before.
//
// The game's level going up is a cop seeing a crime (CEventList::
// ReportCrimeForEvent registers one at once within 14 m of a cop) or a
// script setting stars: either way they know where he is. Going down, it's
// undone when it's the game's own wipe (a Pay 'n' Spray, a clothes change,
// one star running down) and followed otherwise (a script, the cheat, wasted
// or busted). Under the APB (hollow stars) the game's level is 0, so "lose
// the cops" passes and he can save; a save at a safehouse clears it. Police
// bribes are taken off the map.
#include "game.h"
#include "heat.h"

#include <cstdint>

namespace wanted {

bool Install();  // dlsym only; logs what's missing; false leaves the game's wanted system alone
void Update();   // each frame, first thing after CCamera::Process (before police::Update)
bool Active();   // installed, RealisticWanted on
const heat::Model &Model();
heat::Looks Looks(); // Tommy now
CVector Target();    // where the police think he is
bool OnHim();        // they know where he is now (Target() is him): wanted and seen, or only just lost
bool InPoliceCar();  // he's in one: he sees what they see (the search, the other units), and they him
// A full eye, a cop sees him while Wanted, or sees a crime of his (atLeast:
// the stars it's worth at the least, gun violence two).
void CopSees(const CVector &at, int atLeast = 0);
// A witness's call about a crime (at `when`, where he was) got through:
// what they saw of him then (null: they didn't, only where).
void Reported(const heat::Looks *seen, const CVector &at, uint32_t when, int stars);

enum class StarLook : uint8_t { Filled, Dim, Hollow };
int ShownStars();
StarLook Look();

} // namespace wanted
