#pragma once
// Achievements (#24): reads the game's own values for achievementsmodel.h,
// watches the five mission conditions the game keeps no record of, awards and
// remembers unlocks (~/Library/Application Support/macVC/vcmodernhud/achievements),
// and shows a notice for each. The Stats page reads Current()/Snapshot().
#include "achievementsmodel.h"
#include <cstdint>

namespace achievements {

bool Install();
// Installed, and on (Achievements=1): the Stats page shows them.
bool Ready();
// Every frame from CHud::Draw: a few times a second, reads and awards.
void Update();
// From CPed::InflictDamage (player.cpp), after the game's own damage.
void Damaged(uint8_t *ped, uint8_t *by, int weapon, bool fatal);

const Record &Current();
Facts Snapshot();

// The unlock notice showing now, oldest unlock first: one achievement, or
// `count` of them at once (a save loaded that already met them).
struct Shown {
	const Def *def = nullptr;
	int count = 1;
	uint32_t age = 0, shownFor = 0; // ms
};
bool Notice(Shown &out);

} // namespace achievements
