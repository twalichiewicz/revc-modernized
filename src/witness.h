#pragma once
// Witnesses (the wanted overhaul): a crime no cop saw reaches the police only
// if someone calls it in. The game queues such crimes (CWanted::AddCrimeToQ)
// and files them itself half a second later (CWanted::UpdateCrimesQ); here
// they're taken off its queue and given to the civilians who saw them, by
// the game's own temperaments (CCivilianPed::ProcessControl's: fear no more
// than lawfulness, temper no more than fear). Most run for the nearest free
// payphone and call from it; rich ones (suits, business people) often have a
// brick phone and call from where they stand; with no phone in reach they
// run off, and tell a cop if they run into one. Stopped first (dead, knocked
// down), nothing is reported. A call that gets through counts the game's own
// points for the crime (CWanted::ReportCrimeNow, the police scanner with it)
// and tells the police where, and what he looks like if the witness saw him.
// A car alarm he sets off is called in (only where) by someone near.
#include "game.h"

#include <cstdint>
#include <vector>

namespace witness {

bool Install();
void Update(uint32_t now);      // from wanted::Update
// Everything cleared (a load, wasted or busted, a script, a save): the calls
// going on are dropped, those making them let go (not after a load).
void Reset(bool letGo);
float Calling();                // the furthest-on call's progress, 0..1 (0: none)
std::vector<CVector> Runners(); // witnesses on their way to a phone, for the minimap

} // namespace witness
