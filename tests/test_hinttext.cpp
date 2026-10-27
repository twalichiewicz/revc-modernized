// Unit tests for filling in the buttons a hint names, and the hints the mod rewrites: make test
#include "bindings.h"
#include "hinttext.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

// `ascii` into a game buffer of hinttext::kCapacity units, cut to fit as the game's copies are.
static void Wide(uint16_t *out, const char *ascii)
{
	size_t n = 0;
	for (; ascii[n] && n + 1 < hinttext::kCapacity; n++)
		out[n] = uint8_t(ascii[n]);
	out[n] = 0;
}

static std::string Ascii(const uint16_t *text)
{
	std::string s;
	for (; *text; text++)
		s += char(*text);
	return s;
}

static std::u16string Fire(std::string_view n) { return n == "PED_FIREWEAPON" ? u"~V~" : u""; }

static void TestExpandReplacesActionToken()
{
	uint16_t t[hinttext::kCapacity];
	Wide(t, "Press the ~h~~k~~PED_FIREWEAPON~~w~ to fire.");
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == "Press the ~h~~V~~w~ to fire.");
}

static void TestExpandDropsUnknownAndKeepsTwo()
{
	uint16_t t[hinttext::kCapacity];
	Wide(t, "~k~~PED_FIREWEAPON~ or ~k~~NOPE~ then ~k~~PED_FIREWEAPON~");
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == "~V~ or  then ~V~  "); // two spaces after the last: TestNoTextEndsWithAToken
}

// The token fits, but what it expands to doesn't: cut at the buffer's end, never past it.
static void TestExpandTruncatesAtCapacity()
{
	uint16_t t[hinttext::kCapacity + 8];
	std::fill(std::begin(t), std::end(t), uint16_t('#'));
	std::string longText(230, 'a');
	longText += "~k~~PED_FIREWEAPON~";
	Wide(t, longText.c_str());
	hinttext::Expand(t, [](std::string_view) { return std::u16string(40, u'K'); });
	CHECK(t[hinttext::kCapacity - 1] == 0); // terminated within the buffer
	CHECK(t[hinttext::kCapacity] == '#');   // nothing written past it
	CHECK(t[230] == 'K');                    // the expansion went in, as far as it fits
}

// Cut short, the text mustn't end in a token without its closing '~' (the game's parser would run on).
static void TestExpandLeavesNoOpenToken()
{
	uint16_t t[hinttext::kCapacity];
	std::string text(248, 'a');
	text += "~k~~PED_FIREWEAPON~";
	Wide(t, text.c_str());
	hinttext::Expand(t, [](std::string_view) { return std::u16string(u"~V~~V~~V~"); });
	int tildes = 0;
	for (const uint16_t *c = t; *c; c++)
		tildes += *c == '~';
	CHECK(tildes % 2 == 0);
}

static void TestPastToken()
{
	const uint16_t closed[] = {'~', 'h', '~', 'x', 0}, open[] = {'~', 'h', 0};
	CHECK(hinttext::PastToken(closed) == closed + 3);
	CHECK(hinttext::PastToken(open) == nullptr);
}

// CFont::RenderFontBuffer draws whatever follows a token without checking for
// the end, so a text ending in one steps past its terminator: never hand it
// one. And CFont::PrintString drops a single space at the end (writing its
// terminator over it), so it takes two: one stays.
static void TestNoTextEndsWithAToken()
{
	CHECK(hinttext::Printable(u"~O~") == u"~O~  ");
	CHECK(hinttext::Printable(u"Press ~V~") == u"Press ~V~  ");
	CHECK(hinttext::Printable(u"Press ~V~ ") == u"Press ~V~  ");
	CHECK(hinttext::Printable(u"Q") == u"Q");
	CHECK(hinttext::Printable(u"") == u"");
	uint16_t t[hinttext::kCapacity];
	Wide(t, "Press the ~h~~k~~PED_FIREWEAPON~");
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == "Press the ~h~~V~  ");
	// Full to the last unit with a token at its end: no room for the spaces, so the token goes.
	std::string full(252, 'a');
	full += "~k~~PED_FIREWEAPON~";
	Wide(t, full.c_str());
	hinttext::Expand(t, Fire);
	CHECK(Ascii(t) == std::string(252, 'a'));
}

static void TestRewrites()
{
	CHECK(hinttext::Rewrite("HELP31", bindings::kAll) != nullptr);
	CHECK(std::string(hinttext::Rewrite("HELP31", bindings::kAll)).find("~k~~MOD_DRIVEBY_AIM~") != std::string::npos);
	CHECK(hinttext::Rewrite("HELP31", 0) == nullptr);
	CHECK(hinttext::Rewrite("ANSWER", bindings::kPhone) != nullptr);
	CHECK(hinttext::Rewrite("BANK_C", bindings::kAll) == nullptr);
	CHECK(hinttext::Rewrite("HELP33", bindings::kAll) != nullptr); // HELP32's twin
	CHECK(hinttext::Rewrite("GEN3_40", bindings::kAll) == nullptr); // A fires a bike's guns: the game's text holds
}

int main()
{
	TestExpandReplacesActionToken();
	TestExpandDropsUnknownAndKeepsTwo();
	TestExpandTruncatesAtCapacity();
	TestExpandLeavesNoOpenToken();
	TestPastToken();
	TestNoTextEndsWithAToken();
	TestRewrites();
	if (gFailures) {
		printf("%d hinttext test(s) failed\n", gFailures);
		return 1;
	}
	printf("all hinttext tests passed\n");
	return 0;
}
