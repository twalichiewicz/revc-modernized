// Unit tests for the patch table's name hash (FNV-1a, 32 bits), which tools/patch_macvc.py computes too: make test
#include "hookname.h"

#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static void TestPublishedVectors()
{
	CHECK(hooks::NameHash("") == 0x811c9dc5u);
	CHECK(hooks::NameHash("a") == 0xe40c292cu);
	CHECK(hooks::NameHash("foobar") == 0xbf9cf968u);
}

// The patcher's own value for one of ours (python3 -c in the Makefile's check).
static void TestAHookName() { CHECK(hooks::NameHash("_ZN4CHud4DrawEv") == HOOKNAME_CHUD_DRAW); }

int main()
{
	TestPublishedVectors();
	TestAHookName();
	if (gFailures) {
		printf("%d hook name test(s) failed\n", gFailures);
		return 1;
	}
	printf("all hook name tests passed\n");
	return 0;
}
