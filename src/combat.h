#pragma once
// Enemies in a gunfight.
//
// No shooting through walls (CWeapon::Fire, from camera.cpp's hook): a
// ped's shot starts at its gun's muzzle, a forearm's length ahead of its
// hand, which against a wall or round a corner is through it, so the game's
// own test of the shot's line (which walls do stop) started on the far side.
// Now a shot whose muzzle is through something starts at the shooter's chest
// instead, and a ped that can't see what it's shooting at (its eyes to the
// target's middle: buildings and objects in the way, not glass or fences)
// holds its fire.
//
// Cover and moving (CPed::KillCharOnFootArmed, the armed ped's fight with
// the player on foot): the game's gunman runs at the player between shots
// until he's a third of his gun's range off, and stands there in the open.
// Now he shoots from where he is once the player's in range, and soon makes
// for the nearest cover: something solid and wide (a wall, a car, a crate)
// close in front of him, which he crouches behind, standing to shoot over it
// or stepping out round its corner, a few times. Shot at (a shot passing
// near, or someone near him hit), he runs for cover at once, or stays down
// in it a while longer, and keeps down while the player looks at him (aimed
// at a while where he is, he moves to other cover). Only a few shoot at once
// (more for more stars, as Half-Life's grunts take turns); the rest keep down
// or move up while those cover them, cover to cover, round the player's side,
// spreading out around him. Cover is also the far side of a car standing
// near (a cop out of his car gets behind it), and he goes round a corner to
// get to cover beyond it; he ducks down to reload. A car smoking is no cover
// to stay behind, and near one on fire (it'll blow up) he runs clear of the
// blast first, to cover if there's some. Coming from out of sight,
// he waits for others to come up (8 s at most), so they come on together.
// His aim is off when he first has the player in sight, and truer the longer
// he has him. One who can't see the player (a wall between: he holds his
// fire) goes round to where he can (a cop, with the wanted overhaul, to where
// the police think he is: he doesn't know better). One sat in a helicopter's
// door is left to the game. The game's car cover for cops is replaced
// by this; peds the mission keeps in place (roadblocks' cops), snipers and
// the minigun and flamethrower are left as they are. Every 20 s while there's
// fighting the log says what they did.
#include "game.h"

#include <cstdint>

namespace combat {

bool Install();
// Before CWeapon::Fire for anyone but the player: false holds the shot;
// `source` (the muzzle) may be moved back to the shooter's side of a wall.
bool MayFire(uint8_t *weapon, uint8_t *shooter, CVector *source);
// The player fired (from CWeapon::Fire): gunmen near the line from `from`
// along `dir` (a unit vector) are under fire.
void PlayerShot(const CVector &from, const CVector &dir);
// A ped was hurt (from CPed::InflictDamage): if by the player, gunmen near it
// are under fire.
void Hurt(uint8_t *ped, uint8_t *by, bool killed);

} // namespace combat
