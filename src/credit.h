#pragma once
// Businesses on credit. At a business's for-sale sign with less than its
// price but at least a quarter of it, the buy button buys it for the cash he
// has and he owes the rest. What he owes comes out of his businesses' income
// (the money piling up at each one he runs) before he can collect any, and
// while he owes there's no more credit. It's kept in a script global main.scm
// never uses, so it saves and loads with the game, per save.
#include <cstdint>

namespace credit {

// Its hooks, whatever the script space holds yet; they act only while
// story::Active() (a game running with the main.scm its variable is for).
bool Install();
int32_t Owed();
// Every frame from CHud::Draw: income pays off what he owes.
void Update();

} // namespace credit
