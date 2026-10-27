#pragma once
// Keyboard keys and mouse buttons as this port codes them (its RsKeyCodes and
// the mouse buttons of CControllerConfigManager's MOUSE settings), and their
// names: in macVC.ini's [Bindings] shape ("kbd:Q,2ndKbd:F1", "mouse:MIDDLE")
// and as hints show them.
#include <cstdint>
#include <string>
#include <string_view>

namespace keys {

constexpr uint16_t kNone = 0;
constexpr uint16_t kSpecialBase = 1000; // RsKeyCodes ESC; NULL = 1056
constexpr uint16_t kMouseBase = 2000;   // + 1..7 = LEFT MIDDLE RIGHT WHLUP WHLDOWN X1 X2

struct Binding {
	uint16_t key[2] = {kNone, kNone}; // the key and its alternative; either may be a mouse button
	bool operator==(const Binding &o) const { return key[0] == o.key[0] && key[1] == o.key[1]; }
	bool Empty() const { return key[0] == kNone && key[1] == kNone; }
};

// "Q", "SPC", "LSHIFT", "MIDDLE"; "" for kNone.
std::string IniName(uint16_t code);
// kNone if unknown; `mouse`: a mouse button's name ("LEFT" is also an arrow key).
uint16_t FromIniName(std::string_view name, bool mouse);
// "kbd:Q,2ndKbd:F1"; a mouse button is written "mouse:MIDDLE".
std::string Format(const Binding &b);
// The macVC.ini [Bindings] shape; a third key and "joy:" are ignored.
Binding Parse(std::string_view value);
// A game action's settings (CControllerConfigManager's keyboard key, extra key
// and mouse button; 0 or rsNULL for none) as codes, all of them, in that order.
// Returns how many.
int GameCodes(int32_t keyboard, int32_t extra, int32_t mouse, uint16_t out[3]);
// As hints and the Controls page show it: "Q", "SPC", "LSHIFT", "LMB", "MS WHEEL UP".
std::string DisplayName(uint16_t code);

} // namespace keys
