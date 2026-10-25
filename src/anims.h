#pragma once
// Animations asked for before their block is in memory. A vehicle's own
// animations (a bus's, a van's, a bike's) come in with its model and can go
// again; a ped getting off a bus whose "coach" block had gone crashed the game
// (CAnimManager::AddAnimation reads the animation it didn't find). Here the
// block is loaded there and then (and kept), and if it still isn't there a
// plain one plays instead (getting out of a car, or standing).
namespace anims {

bool Install();

} // namespace anims
