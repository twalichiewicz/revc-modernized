#pragma once
// The police as a force spread over the city, in place of the game's few cars
// made near the player for as long as the stars want them.
//
// Units: patrol cars on the roads all over, more where the zones have more
// traffic, and a few at each station. Wanted, the nearest are sent, and drive
// to him along the roads (off the map, where he can't see them); a unit
// becomes a police car where its way crosses the ring cars are made on, out
// of his sight, so they come in by road from wherever they were. More are sent
// the longer it goes on (more at once for more stars); one lost (its cops dead)
// is made up from the next nearest, further off, and one he's left far behind
// gives way to one nearer by road. One that can't come where it is (on top of
// him, in his view, something in its way) comes in on another road near it,
// out of his view. Against a gunman the first to arrive wait for another
// before going in.
//
// Perimeter: from two stars, roadblocks on the roads out of a ring about where
// he was last placed, more of them and heavier as it goes on: cars across part
// of the road, then all of it, then with a cop and a spike strip in front, then
// a second line behind (so many standing at once, more for more stars). He
// breaks out, and they set up again about him.
//
// Guns: once he's fired one (or aims one), cops draw their pistols, not the
// nightstick. And cars coming for him on foot pull up short, side-on, where
// he can't see them if they can, and the cops get out behind them. Past the
// ten the game lets after him at once, the cops near him on foot go for him
// too (from two stars); a roadblock's cops beside a car of it on fire leave
// their places.
//
// With the wanted overhaul they work from what they know (wanted::Target):
// sent to where he was last seen, the ring set up round it, and while they
// search, police cars drive about the search area from road to road instead
// of after him, till someone sees him again.
#include "game.h"

#include <cstdint>
#include <vector>

namespace police {

bool Install();
void Update();                    // once a frame, after the camera
void PlayerFired(int32_t weapon); // a shot of his (CWeapon::Fire), by weapon type
// While they search (the wanted overhaul): a road node in the area they're
// searching, a different one for each slot (cars and cops on foot take turns).
CVector SearchPoint(int slot);
// How far the nearest police unit about the city (on patrol, sent, at a
// station) is from `at`, if within `reach` (else more than it): who'd hear a shot.
float UnitWithin(const CVector &at, float reach);
// Where the units out in the city are (on patrol, sent, at a station: not
// those come as cars), for the map in a police car.
std::vector<CVector> UnitsAbout();

} // namespace police
