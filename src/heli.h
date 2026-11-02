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
// Its parts (helizones.h has where they are): its searchlight is a lamp hung
// under its chin (drawn after CHeli::Render, lit as it is), turning to its
// spot on the ground, its lens lit at night with a faint beam down to the
// spot; the game's glow, 7 m out in front of it, is on the lens, and its red
// light, 9 m behind it, on its tail fin. One direct hit shoots the lamp out (its
// glass in pieces, dark for good, and at night it sees less far). Hits on the
// mast where the main rotor meets the roof count three times over; six on
// the tail rotor shoot it away and it spins down. A pilot sits behind the
// cockpit's glass (a policeman, CAR_sit): shot dead through it, it goes down.
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
// Its searchlight's been shot out (slot in CHeli::pHelis); and how much of
// its range a helicopter sees with it out, at night.
bool LightOut(int slot);
float SightAtNight();
// Confirmed actual rotor geometry contact from the shared aircraft precision
// path. The scripted CHeli retains its own crew/descent/impact controller.
bool FailRotor(uint8_t *vehicle, bool main);

} // namespace heli
