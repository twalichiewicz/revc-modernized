// Unit tests for writing one setting back to an ini: make test
#include "inifile.h"

#include <cstdio>

static int gFailures;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
			gFailures++;                                                                                               \
		}                                                                                                              \
	} while (0)

static void TestReplacesAndKeepsComment()
{
	CHECK(inifile::WithValue("[Minimap]\nLookSensitivity=1    ; right stick\n", "Minimap", "LookSensitivity", "1.25") ==
	      "[Minimap]\nLookSensitivity=1.25    ; right stick\n");
}

static void TestAddsToSection()
{
	CHECK(inifile::WithValue("[Minimap]\nGPS=1\n[Keys]\n; MOD_RELOAD=kbd:R\n", "Keys", "MOD_RELOAD", "kbd:T") ==
	      "[Minimap]\nGPS=1\n[Keys]\n; MOD_RELOAD=kbd:R\nMOD_RELOAD=kbd:T\n");
}

static void TestAddsSection()
{
	CHECK(inifile::WithValue("[Minimap]\nGPS=1\n", "Keys", "MOD_RELOAD", "kbd:T") ==
	      "[Minimap]\nGPS=1\n[Keys]\nMOD_RELOAD=kbd:T\n");
}

static void TestSameKeyInDifferentSections()
{
	CHECK(inifile::WithValue("[Minimap]\nSize=1.25\n[Crosshair]\nSize=1\n", "Crosshair", "Size", "2") ==
	      "[Minimap]\nSize=1.25\n[Crosshair]\nSize=2\n");
	CHECK(inifile::WithValue("[Minimap]\nSize=1.25\n", "Crosshair", "Size", "2") ==
	      "[Minimap]\nSize=1.25\n[Crosshair]\nSize=2\n");
}

int main()
{
	TestReplacesAndKeepsComment();
	TestAddsToSection();
	TestAddsSection();
	TestSameKeyInDifferentSections();
	if (gFailures) {
		printf("%d inifile test(s) failed\n", gFailures);
		return 1;
	}
	printf("all inifile tests passed\n");
	return 0;
}
