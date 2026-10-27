#pragma once
// The buttons and keys the game's hints name ("Press the ~k~~PED_FIREWEAPON~
// button"), as the mod has the controls: the game fills those tokens in from
// its own pad setups, which the mod's layout replaced, so every help box,
// brief and the Briefs page goes through bindings.h instead
// (CMessages::InsertPlayerControlKeysInString). A few hints whose controls
// changed get new English text (hinttext.h, through CText::Get).
#include "bindings.h"

#include <string>

struct CSprite2d;
struct RwTexture;

namespace hints {

bool Install();
// From CPad::Update, every frame (the menus' too): the button icons for the pad connected.
void Frame();
// The player's situation now (his vehicle's, else on foot).
bindings::Situation Now();
// The ini switches as bindings::Switch bits.
uint16_t Switches();
// The hint the mod rewrote for this text key (CText::Get), or null.
const uint16_t *RewriteText(const char *key);
// What a hint shows for `action` now: an icon token on a pad ("~V~"), else the keys ("LMB or LCTRL").
std::u16string Glyph(bindings::Action action);
// From CSprite2d::Draw: the sprite to draw instead, for the icons the mod adds to the
// font's (D-pad, Start, Back: past CFont::ButtonSprite's end); null to draw nothing.
const CSprite2d *SpriteFor(const CSprite2d *sprite);
// The icon for a pad button, from the button set loaded for the pad in use (null if it has none).
RwTexture *Icon(bindings::Pad pad);
// Hints name keys, not pad buttons: the keyboard or mouse was used last (!CPad::IsAffectedByController).
bool KeyboardNow();

} // namespace hints
