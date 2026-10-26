#pragma once
// The police helicopter (CHeli: the one that comes after the player at three
// stars, not the flyable Police Maverick).
//
// Shots hit it. The game gives it no body (its collision is a ped's), so a
// shot's trace went on through it without the sparks and clang a car gives,
// and it counted any shot passing within 5 m of its middle, through walls
// too, at 4 points of 701 (176 hits). Now a shot through a ball about its
// body stops there (CWeapon::DoBulletImpact: its trace, sparks, smoke and the
// sound of a hit car), only those count, not through a wall, and each counts
// four times over (44 hits; 25 for the lighter kind). It smokes as it's hurt,
// as cars do: steam, then grey smoke, then black smoke and fire, and it burns
// as it spins down, no longer shooting.
//
// Gunners (the wanted overhaul, HeliGunners): no belly gun and no SWAT
// abseiling (CHeli::ProcessControl). Two SWAT sit in its open doors with
// rifles (CPed::AttachPedToEntity, as Tommy is in Phnom Penh '86), each
// shooting out of its own side; shot, one falls out, and with both down it
// carries on as the police's eyes in the sky. From 4 stars a second comes
// (the game sends one at 3 to 6, CWanted::NumOfHelisRequired). While nobody
// sees him they circle where he was last seen instead of flying at him.
#include <cstdint>

namespace heli {

bool Install();
void Update(); // each frame, after the world (camera.cpp): the gunners, the second helicopter
// Its gunners still alive, for slot (CHeli::pHelis 0 or 1); false if it has none aboard.
bool Gunners(int slot, int &alive);

} // namespace heli
