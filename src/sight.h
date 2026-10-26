#pragma once
// Who can see Tommy: cops (on foot, or at the wheel of a police car) and the
// police helicopters, each with a line of sight to him and him in front of
// them (a helicopter sees all round, and much further). While he's wanted,
// any of them seeing him keeps the police on him (wanted::CopSees). Once
// they've lost him (searching, or the APB) each one looking at him has an
// eye that fills, fast from far off if he matches the description, only up
// close if not, and empties again once it can't see him; a full eye has
// him recognised. Nothing sees him in a NoPolice zone or while the police
// ignore him (a Pay 'n' Spray's door shut, a mission's cutscene).
#include "game.h"

#include <vector>

namespace sight {

struct Eye {
	int handle;      // the ped's pool handle, or -1 - its slot in CHeli::pHelis
	bool helicopter;
	CVector head;    // where to draw it
	float fill;      // 0..1
};

bool Install();
void Update(float dt);          // from wanted::Update, every frame
bool Seen();                    // a cop or helicopter sees him (as of the last look)
bool CanSee(const CVector &at); // a cop or police helicopter would see something there (a crime)
const std::vector<Eye> &Eyes(); // those filling (fill > 0), for the HUD

} // namespace sight
