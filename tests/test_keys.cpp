// Unit tests for the port's key codes and their names: make test
#include "keys.h"

#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static void TestIniNames()
{
	CHECK(keys::IniName(1000) == "ESC");
	CHECK(keys::IniName(1032) == "PAD5");
	CHECK(keys::IniName(1046) == "LSHIFT");
	CHECK(keys::IniName(' ') == "SPC");
	CHECK(keys::IniName('Q') == "Q");
	CHECK(keys::IniName(keys::kMouseBase + 2) == "MIDDLE");
	CHECK(keys::IniName(keys::kNone).empty());
}

static void TestFromIniName()
{
	CHECK(keys::FromIniName("LSHIFT", false) == 1046);
	CHECK(keys::FromIniName("LEFT", false) == 1021);                // the arrow key
	CHECK(keys::FromIniName("LEFT", true) == keys::kMouseBase + 1); // the mouse button
	CHECK(keys::FromIniName("SPC", false) == ' ');
	CHECK(keys::FromIniName("#", false) == '#');
	CHECK(keys::FromIniName("NOPE", false) == keys::kNone);
}

static void TestParseAndFormat()
{
	const keys::Binding b = keys::Parse("kbd:Q,2ndKbd:F1");
	CHECK(b.key[0] == 'Q' && b.key[1] == 1001);
	CHECK(keys::Format(b) == "kbd:Q,2ndKbd:F1");
	const keys::Binding m = keys::Parse("mouse:MIDDLE");
	CHECK(m.key[0] == keys::kMouseBase + 2 && m.key[1] == keys::kNone);
	CHECK(keys::Format(m) == "mouse:MIDDLE");
	CHECK(keys::Parse("kbd:PADINS,2ndKbd:LCTRL,mouse:LEFT,joy:1").key[1] == 1049); // a third key is dropped
	CHECK(keys::Parse("").Empty());
	CHECK(keys::Format(keys::Binding{}).empty());
}

// A game action's keyboard key, extra key and mouse button: all three, none dropped.
static void TestGameCodesKeepTheMouse()
{
	uint16_t codes[3];
	CHECK(keys::GameCodes(1038, 1049, 1, codes) == 3); // PADINS, LCTRL, the left button
	CHECK(codes[0] == 1038 && codes[1] == 1049 && codes[2] == keys::kMouseBase + 1);
	CHECK(keys::GameCodes(1056, 0, 3, codes) == 1 && codes[0] == keys::kMouseBase + 3); // rsNULL, none, right
}

static void TestDisplayNames()
{
	CHECK(keys::DisplayName(keys::kMouseBase + 1) == "LMB");
	CHECK(keys::DisplayName(keys::kMouseBase + 4) == "MS WHEEL UP");
	CHECK(keys::DisplayName(' ') == "SPC");
	CHECK(keys::DisplayName(1046) == "LSHIFT");
}

int main()
{
	TestIniNames();
	TestFromIniName();
	TestParseAndFormat();
	TestGameCodesKeepTheMouse();
	TestDisplayNames();
	if (gFailures) {
		printf("%d key test(s) failed\n", gFailures);
		return 1;
	}
	printf("all key tests passed\n");
	return 0;
}
