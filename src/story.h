#pragma once
// Where Vice City's story stands once "Shakedown" puts the businesses up for
// sale: from then on it only carries on through them. Ken's call about the
// Print Works, which starts the finale, comes once seven of the nine business
// storylines are finished, the Print Works' and Cop Land's among them (the
// main.scm thread that starts 'FIN1'). The game never says so; here Ken
// texts what's left (on the phone), and the businesses it needs get a ring on
// the maps.
#include "game.h"

namespace story {

struct Business {
	const char *name;  // in a sentence: "the boatyard"
	const char *title; // on its own: "Boatyard"
	float x, y;        // its for-sale sign
	uint16_t sprite;   // its own radar icon
	int doneVar;       // the script global its storyline sets once it's finished
};
extern const Business kBusinesses[8];

// Its for-sale sign's pickup (the green house you walk into to buy), while it's for sale.
const uint8_t *SaleSign(const Business &b);
// The game's icon for it, once he's bought it.
const game::Blip *OwnedBlip(const Business &b);
// The business a for-sale pickup sells (null: a safehouse, or not a sign).
const Business *SoldAt(const uint8_t *pickup);
int32_t Price(const uint8_t *sign);
bool Done(const Business &b);

// The story is waiting on the businesses.
bool Waiting();
// ...and on this one (Waiting, the hints on, its storyline not finished, and it counts).
bool Needed(const Business &b);

bool Install();
// Every frame from CHud::Draw: Ken's texts, when there's news or it's been a while.
void Update();

} // namespace story
