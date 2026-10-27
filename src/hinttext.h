#pragma once
// Hint text: the game's ~k~~NAME~ tokens ("Press the ~k~~PED_FIREWEAPON~
// button") filled in with a button or key, and the English hints the mod
// rewrites because the controls they describe changed. No game calls here:
// unit-tested (tests/test_hinttext.cpp).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace hinttext {

// The game's text buffers (CHud::m_HelpMessage, CMessages::Display's), in units with the terminator.
constexpr size_t kCapacity = 256;

using Resolver = std::function<std::u16string(std::string_view name)>;

// Each ~k~~NAME~ in `text` becomes resolve(NAME) (empty: the token goes); other
// ~x~ tokens stay. In place, never past kCapacity units with the terminator.
void Expand(uint16_t *text, const Resolver &resolve);
// `text` as the font can draw it: CFont::RenderFontBuffer draws the character
// after a token without checking for the end, so a text ending in a token
// gets a space after it (it would step past its terminator into the next).
std::u16string Printable(std::u16string text);
// Past the token at `s` (its opening '~'): after its closing '~'; null if it has none.
const uint16_t *PastToken(const uint16_t *s);
// The mod's English text for a hint whose controls it changed, or null;
// `switches` as bindings::Switch (a rewrite applies while its switches are on).
const char *Rewrite(std::string_view key, uint16_t switches);

} // namespace hinttext
