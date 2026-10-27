#include "keys.h"

namespace keys {
namespace {

// The port's names for its special keys, from ESC (kSpecialBase) to NULL, and
// for the mouse buttons, from 1 (as macVC.ini writes them).
constexpr const char *kSpecial[] = {
    "ESC",    "F1",      "F2",      "F3",      "F4",     "F5",     "F6",       "F7",       "F8",      "F9",
    "F10",    "F11",     "F12",     "INS",     "DEL",    "HOME",   "END",      "PGUP",     "PGDN",    "UP",
    "DOWN",   "LEFT",    "RIGHT",   "DIVIDE",  "TIMES",  "PLUS",   "MINUS",    "PADDEL",   "PADEND",  "PADDOWN",
    "PADPGDN", "PADLEFT", "PAD5",   "NUMLOCK", "PADRIGHT", "PADHOME", "PADUP", "PADPGUP",  "PADINS",  "PADENTER",
    "SCROLL", "PAUSE",   "BACKSP",  "TAB",     "CAPSLK", "ENTER",  "LSHIFT",   "RSHIFT",   "SHIFT",   "LCTRL",
    "RCTRL",  "LALT",    "RALT",    "LWIN",    "RWIN",   "APPS",   "NULL",
};
constexpr int kNumSpecial = sizeof kSpecial / sizeof kSpecial[0];
constexpr const char *kMouse[] = {"LEFT", "MIDDLE", "RIGHT", "WHLUP", "WHLDOWN", "X1", "X2"};
constexpr const char *kMouseShown[] = {"LMB", "MMB", "RMB", "MS WHEEL UP", "MS WHEEL DN", "MXB1", "MXB2"};
constexpr int kNumMouse = sizeof kMouse / sizeof kMouse[0];

bool IsMouse(uint16_t code) { return code > kMouseBase && code <= kMouseBase + kNumMouse; }

} // namespace

std::string IniName(uint16_t code)
{
	if (code == kNone)
		return "";
	if (code == ' ')
		return "SPC";
	if (code > ' ' && code < 127)
		return std::string(1, char(code));
	if (code >= kSpecialBase && code < kSpecialBase + kNumSpecial)
		return kSpecial[code - kSpecialBase];
	if (IsMouse(code))
		return kMouse[code - kMouseBase - 1];
	return "";
}

uint16_t FromIniName(std::string_view name, bool mouse)
{
	if (mouse) {
		for (int i = 0; i < kNumMouse; i++)
			if (name == kMouse[i])
				return uint16_t(kMouseBase + 1 + i);
		return kNone;
	}
	if (name == "SPC")
		return ' ';
	if (name.size() == 1 && name[0] > ' ' && name[0] < 127)
		return uint16_t(name[0]);
	for (int i = 0; i < kNumSpecial; i++)
		if (name == kSpecial[i])
			return uint16_t(kSpecialBase + i);
	return kNone;
}

std::string Format(const Binding &b)
{
	std::string out;
	for (int slot = 0; slot < 2; slot++) {
		if (b.key[slot] == kNone)
			continue;
		if (!out.empty())
			out += ',';
		out += IsMouse(b.key[slot]) ? "mouse:" : slot == 0 ? "kbd:" : "2ndKbd:";
		out += IniName(b.key[slot]);
	}
	return out;
}

Binding Parse(std::string_view value)
{
	Binding b;
	auto put = [&b](int slot, uint16_t code) {
		if (code == kNone)
			return;
		if (slot < 0) // a mouse button: the first free slot
			slot = b.key[0] == kNone ? 0 : 1;
		if (b.key[slot] == kNone)
			b.key[slot] = code;
	};
	while (!value.empty()) {
		const size_t comma = value.find(',');
		const std::string_view part = value.substr(0, comma);
		value = comma == std::string_view::npos ? std::string_view() : value.substr(comma + 1);
		const size_t colon = part.find(':');
		if (colon == std::string_view::npos)
			continue;
		const std::string_view kind = part.substr(0, colon), name = part.substr(colon + 1);
		if (kind == "kbd")
			put(0, FromIniName(name, false));
		else if (kind == "2ndKbd")
			put(1, FromIniName(name, false));
		else if (kind == "mouse")
			put(-1, FromIniName(name, true));
	}
	return b;
}

int GameCodes(int32_t keyboard, int32_t extra, int32_t mouse, uint16_t out[3])
{
	int n = 0;
	for (int32_t key : {keyboard, extra})
		if (key > 0 && key < kSpecialBase + kNumSpecial - 1) // up to APPS: NULL is no key
			out[n++] = uint16_t(key);
	if (mouse >= 1 && mouse <= kNumMouse)
		out[n++] = uint16_t(kMouseBase + mouse);
	return n;
}

std::string DisplayName(uint16_t code)
{
	if (IsMouse(code))
		return kMouseShown[code - kMouseBase - 1];
	return IniName(code);
}

} // namespace keys
