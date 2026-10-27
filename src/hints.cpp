#include "hints.h"
#include "actions.h"
#include "config.h"
#include "controls.h"
#include "game.h"
#include "hinttext.h"
#include "hooks.h"
#include "keys.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace game;
using bindings::Action;

namespace hints {
namespace {

constexpr uint16_t kKeysLength = 64; // GetWideStringOfCommandKeys' output, in units

bool gInstalled;
void (*orig_InsertKeys)(uint16_t *text);
bindings::PadFamily gIcons = bindings::PadFamily::kXbox; // CFont::Initialise loads the Xbox 360 set

// The icons the mod adds to the font's, as ~@x~ tokens: CFont::PS2Symbol past
// its 18 ButtonSprites, which hints::SpriteFor swaps for these when they're drawn.
constexpr int kFirstIcon = 18;
constexpr char kIconLetters[] = "udlrhvsb";
constexpr const char *kIconTextures[] = {"up", "down", "left", "right", "dlr", "dud", "start", "select"};
constexpr int kIcons = sizeof kIconTextures / sizeof kIconTextures[0];
static_assert(sizeof kIconLetters - 1 == kIcons, "a letter per icon");
RwTexture *gIconTextures[kIcons]; // each as a CSprite2d (its one field)
// Every button's icon by name, for the Controls page.
constexpr struct {
	bindings::Pad pad;
	const char *name;
} kPadIcons[] = {
    {bindings::Pad::kA, "cross"},      {bindings::Pad::kB, "circle"},     {bindings::Pad::kX, "square"},
    {bindings::Pad::kY, "triangle"},   {bindings::Pad::kLB, "l1"},        {bindings::Pad::kLT, "l2"},
    {bindings::Pad::kL3, "l3"},        {bindings::Pad::kRB, "r1"},        {bindings::Pad::kRT, "r2"},
    {bindings::Pad::kR3, "r3"},        {bindings::Pad::kUp, "up"},        {bindings::Pad::kDown, "down"},
    {bindings::Pad::kLeft, "left"},    {bindings::Pad::kRight, "right"},  {bindings::Pad::kLeftRight, "dlr"},
    {bindings::Pad::kStart, "start"},  {bindings::Pad::kBack, "select"},  {bindings::Pad::kLeftStick, "thumbl"},
    {bindings::Pad::kRightStick, "thumbr"},
};
RwTexture *gPadIcons[size_t(bindings::Pad::kCount)];
bool gIconsLooked;
constexpr ptrdiff_t kTokenCopy = 32; // a token parsed on its own, in units (the game's are 3 or 4)
uint16_t *(*orig_ParseToken)(uint16_t *s, CRGBA &col, bool &flash, bool &bold);
uint16_t *(*orig_ParseTokenPlain)(uint16_t *s);

// They're in every button TXD, which CFont::LoadButtons doesn't load sprites for.
void FindIconTextures()
{
	const int32_t slot = *api.fontButtonsSlot;
	RwTexDictionary *dict = slot >= 0 ? TxdDictionary(slot) : nullptr;
	for (int i = 0; i < kIcons; i++)
		gIconTextures[i] = dict ? api.TexDictionaryFindNamedTexture(dict, kIconTextures[i]) : nullptr;
	for (const auto &icon : kPadIcons)
		gPadIcons[size_t(icon.pad)] = dict ? api.TexDictionaryFindNamedTexture(dict, icon.name) : nullptr;
	gIconsLooked = slot >= 0;
}

// Our icon for the token at `s` ("~@u~"), or -1.
int OurIcon(const uint16_t *s)
{
	if (s[0] != '~' || s[1] != '@' || s[2] == 0 || s[2] >= 128 || s[3] != '~')
		return -1;
	const char *at = strchr(kIconLetters, char(s[2]));
	return at ? int(at - kIconLetters) : -1;
}

// The font's run of tokens at `s`. The game parses a whole run in one call and
// would skip ours, so a run with one of ours goes a token at a time: ours set
// the icon (unless this is the black shadow pass, which the game draws no
// icons in), the game's each go to `parse` on their own.
template <typename Parse>
uint16_t *Tokens(uint16_t *s, Parse parse)
{
	bool ours = false;
	for (const uint16_t *p = s; p && *p == '~'; p = hinttext::PastToken(p))
		ours = ours || OurIcon(p) >= 0;
	if (!ours)
		return parse(s);
	const uint8_t *colour = api.fontDetails; // CFont::Details starts with its colour
	while (*s == '~') {
		uint16_t *next = const_cast<uint16_t *>(hinttext::PastToken(s));
		if (!next) { // no closing '~': the rest of the text is none of the font's
			while (*s)
				s++;
			return s;
		}
		if (const int icon = OurIcon(s); icon >= 0) {
			if (colour[0] || colour[1] || colour[2])
				*api.fontPs2Symbol = kFirstIcon + icon;
		} else if (next - s < kTokenCopy) { // with its closing '~' and a terminator after it
			uint16_t one[kTokenCopy] = {};
			std::copy(s, next, one);
			parse(one);
		}
		s = next;
	}
	return s;
}

uint16_t *Hook_ParseToken(uint16_t *s, CRGBA &col, bool &flash, bool &bold)
{
	return Tokens(s, [&](uint16_t *t) { return orig_ParseToken(t, col, flash, bold); });
}

uint16_t *Hook_ParseTokenPlain(uint16_t *s) { return Tokens(s, orig_ParseTokenPlain); }

const char *IconFile(bindings::PadFamily family)
{
	switch (family) {
	case bindings::PadFamily::kPlayStation: return "MODELS/PS3BTNS.TXD";
	case bindings::PadFamily::kNintendo: return "MODELS/NSWBTNS.TXD";
	default: return "MODELS/X360BTNS.TXD";
	}
}

// The game's icons are the Xbox 360's whatever the pad; the files for the others are there too.
void MatchIcons()
{
	const bindings::PadFamily family = controls::Family();
	if (family == gIcons || !api.FontLoadButtons || !api.fontButtonsSlot || *api.fontButtonsSlot < 0)
		return;
	gIcons = family;
	api.FontLoadButtons(IconFile(family));
	FindIconTextures();
	Log("hints: button icons %s", IconFile(family));
}

std::u16string Widen(const char *ascii)
{
	std::u16string s;
	for (; *ascii; ascii++)
		s += char16_t(uint8_t(*ascii));
	return s;
}

// The game's own name for one of its actions: its pad setup's token, or its keys.
std::u16string GameKeys(Action a)
{
	uint16_t buf[kKeysLength] = {};
	api.GetWideStringOfCommandKeys(api.controlsManager, uint16_t(a), buf, kKeysLength);
	std::u16string s;
	for (int i = 0; i < kKeysLength && buf[i]; i++)
		s += char16_t(buf[i]);
	return s;
}

// A mod action's keys, as hints show them ("Q", "Q or F1").
std::u16string ModKeys(Action a)
{
	const keys::Binding b = actions::Keys(a);
	std::string s = keys::DisplayName(b.key[0]);
	if (b.key[1] != keys::kNone)
		s += (s.empty() ? "" : " or ") + keys::DisplayName(b.key[1]);
	return Widen(s.c_str());
}

std::u16string Resolve(Action a)
{
	if (!KeyboardNow()) {
		bool game;
		const char *token = bindings::HintToken(Switches(), bindings::ForHint(Now(), a), a, game);
		return game && a < Action::kGameCount ? GameKeys(a) : Widen(token);
	}
	if (a < Action::kGameCount)
		return GameKeys(a);
	Action game;
	return bindings::KeyAlias(a, game) ? GameKeys(game) : ModKeys(a);
}

std::u16string ResolveName(std::string_view name)
{
	Action a;
	return bindings::FromName(name, a) ? Resolve(a) : std::u16string();
}

// The help box is filled in once, when it's set (the briefs every frame): its
// text as set, and as filled in, to fill it in again when what it names changes.
constexpr uintptr_t kSetHelpMessageSite = 0x1fe8a8; // CHud::SetHelpMessage's call, as a return address
uint16_t gHelpRaw[hinttext::kCapacity], gHelpShown[hinttext::kCapacity];
bool gHelpSaved;
uint64_t gHelpState; // what the help box's buttons depend on, last frame

void Copy(uint16_t *to, const uint16_t *from)
{
	size_t i = 0;
	for (; i + 1 < hinttext::kCapacity && from[i]; i++)
		to[i] = from[i];
	to[i] = 0;
}

bool Same(const uint16_t *a, const uint16_t *b)
{
	for (size_t i = 0; i < hinttext::kCapacity; i++) {
		if (a[i] != b[i])
			return false;
		if (!a[i])
			return true;
	}
	return true;
}

// Every hint's ~k~~NAME~ tokens, in place (the game's runs only through its own pad setups and actions).
void Hook_InsertKeys(uint16_t *text)
{
	if (!text)
		return;
	const uintptr_t site =
	    reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - reinterpret_cast<uintptr_t>(hooks::ImageBase());
	const bool help = site == kSetHelpMessageSite;
	if (help)
		Copy(gHelpRaw, text);
	hinttext::Expand(text, ResolveName);
	if (help) {
		Copy(gHelpShown, text);
		gHelpSaved = true;
	}
}

// Pad or keyboard, another kind of pad, a key rebound, in or out of a vehicle:
// the help box up names its buttons again, in place, so it doesn't start over
// (CHud::Draw starts a message when its text differs from the last one's).
void RefreshHelp()
{
	if (!gInstalled || !gHelpSaved || !api.helpMessage || !api.lastHelpMessage || !api.helpMessageToPrint)
		return;
	const uint64_t state = uint64_t(KeyboardNow()) | uint64_t(controls::Family()) << 1 | uint64_t(Now()) << 8 |
	                       uint64_t(actions::Version()) << 16;
	if (state == gHelpState)
		return;
	if (!api.helpMessage[0] || !Same(api.helpMessage, gHelpShown)) { // the box shows something else now
		gHelpState = state;
		return;
	}
	if (!Same(api.lastHelpMessage, api.helpMessage)) // CHud::Draw hasn't started it yet: next frame
		return;
	gHelpState = state;
	uint16_t text[hinttext::kCapacity];
	Copy(text, gHelpRaw);
	hinttext::Expand(text, ResolveName);
	for (uint16_t *box : {api.helpMessage, api.lastHelpMessage, api.helpMessageToPrint, gHelpShown})
		Copy(box, text);
}

} // namespace

bool Install()
{
	gInstalled = api.controlsManager && api.GetWideStringOfCommandKeys &&
	             hooks::Install("_ZN9CMessages31InsertPlayerControlKeysInStringEPt", Hook_InsertKeys, orig_InsertKeys);
	if (!gInstalled)
		Log("hints: hook missing (re-run install.sh); hints name the game's own pad buttons");
	const bool icons =
	    api.fontPs2Symbol && api.fontButtonSprites && api.fontButtonsSlot && api.fontDetails &&
	    hooks::Install("_ZN5CFont10ParseTokenEPtR5CRGBARbS3_", Hook_ParseToken, orig_ParseToken) &&
	    hooks::Install("_ZN5CFont10ParseTokenEPt", Hook_ParseTokenPlain, orig_ParseTokenPlain);
	if (!icons)
		Log("hints: font hooks missing (re-run install.sh); no D-pad, Start or Back icons in hints");
	return gInstalled;
}

void Frame()
{
	if (!gIconsLooked && api.fontButtonsSlot)
		FindIconTextures();
	MatchIcons();
	RefreshHelp();
}

const CSprite2d *SpriteFor(const CSprite2d *sprite)
{
	const uint8_t *p = reinterpret_cast<const uint8_t *>(sprite), *base = api.fontButtonSprites;
	if (!base || p < base + 8 * kFirstIcon || p >= base + 8 * (kFirstIcon + kIcons))
		return sprite;
	RwTexture *const &texture = gIconTextures[(p - base) / 8 - kFirstIcon];
	return texture ? reinterpret_cast<const CSprite2d *>(&texture) : nullptr;
}

bindings::Situation Now()
{
	const uint8_t *ped = api.FindPlayerPed();
	if (!ped || !PedInVehicle(ped) || !PedVehicle(ped))
		return bindings::Situation::kOnFoot;
	const uint8_t *vehicle = PedVehicle(ped);
	return bindings::ForVehicle(EntityModel(vehicle), IsAircraft(vehicle));
}

uint16_t Switches()
{
	uint16_t s = 0;
	if (gConfig.modernTriggers)
		s |= bindings::kTriggers;
	if (gConfig.modernButtons)
		s |= bindings::kButtons;
	if (gConfig.dpadActions)
		s |= bindings::kDpad;
	if (gConfig.wheels)
		s |= bindings::kWheels;
	if (gConfig.driveByAim)
		s |= bindings::kDriveBy;
	if (gConfig.bulletTime)
		s |= bindings::kBulletTime;
	if (gConfig.interactions)
		s |= bindings::kInteractions;
	if (gConfig.phone)
		s |= bindings::kPhone;
	return s;
}

const uint16_t *RewriteText(const char *key)
{
	if (!gInstalled || !key)
		return nullptr;
	const char *text = hinttext::Rewrite(key, Switches());
	if (!text)
		return nullptr;
	static std::map<std::string, std::vector<uint16_t>> wide; // one copy per key, as CText::Get's last
	std::vector<uint16_t> &w = wide[key];
	if (w.empty()) {
		for (const char *c = text; *c; c++)
			w.push_back(uint8_t(*c));
		w.push_back(0);
	}
	return w.data();
}

std::u16string Glyph(Action action) { return Resolve(action); }

RwTexture *Icon(bindings::Pad pad) { return pad < bindings::Pad::kCount ? gPadIcons[size_t(pad)] : nullptr; }

bool KeyboardNow() { return !*api.padAffectedByController; }

} // namespace hints
