// The rain's world geometry (#32) as it goes to the game's RwIm3D: in batches
// librw's buffers hold (10,000 vertices and indices), with both sides drawn
// (the game culls back faces while the world draws), and the cull mode put back.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <vector>

static void *Symbol(void *, const char *);
#define dlsym Symbol
#include "../src/worlddraw.cpp"
#undef dlsym

namespace game { Api api{}; }

static int gFailures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++gFailures;                                                                                    \
		}                                                                                                      \
	} while (0)

constexpr intptr_t kCullBack = 2, kCullNone = 1;
static intptr_t gCull = kCullBack, gCullAtDraw = -1;
static std::vector<uint32_t> gSent, gDrawn;
static int gUses;
static void *gShaderAtDraw, *gCurrent;

static void *Transform(worlddraw::Vertex *, uint32_t n, float *, uint32_t)
{
	gSent.push_back(n);
	gCurrent = nullptr; // the game's own shader, as im3DTransform picks it
	return reinterpret_cast<void *>(1);
}
static int Render(int, uint16_t *indices, int n)
{
	gDrawn.push_back(uint32_t(n));
	gCullAtDraw = gCull;
	gShaderAtDraw = gCurrent;
	for (int i = 0; i < n; i++)
		if (indices[i] != uint16_t(i))
			return 0;
	return 1;
}
static int End() { return 1; }
static void Use(void *shader)
{
	++gUses;
	gCurrent = shader;
}
static int StateGet(int state, void *value)
{
	if (state == 20)
		*static_cast<int32_t *>(value) = int32_t(gCull);
	return 1;
}
static void *Symbol(void *, const char *name)
{
	if (!std::strcmp(name, "_Z15RwIm3DTransformPN2rw3gl310Im3DVertexEjPNS_6MatrixEj")) return reinterpret_cast<void *>(Transform);
	if (!std::strcmp(name, "_Z28RwIm3DRenderIndexedPrimitive15RwPrimitiveTypePti")) return reinterpret_cast<void *>(Render);
	if (!std::strcmp(name, "_Z9RwIm3DEndv")) return reinterpret_cast<void *>(End);
	if (!std::strcmp(name, "_ZN2rw3gl36Shader3useEv")) return reinterpret_cast<void *>(Use);
	if (!std::strcmp(name, "_Z16RwRenderStateGet13RwRenderStatePv")) return reinterpret_cast<void *>(StateGet);
	return nullptr;
}

static void TestBatchesFitTheGameBuffer()
{
	gSent.clear();
	gDrawn.clear();
	int shader = 0;
	{
		worlddraw::Batch batch(&shader);
		for (int i = 0; i < 30000; i++) // 10,000 triangles: a downpour's streaks
			batch.Put({float(i), 0, 0}, 255, 255, 255, 0, 0);
	}
	uint32_t total = 0;
	bool fit = true;
	for (uint32_t n : gSent) {
		fit &= n <= 10000 && n % 3 == 0;
		total += n;
	}
	CHECK(fit && total == 30000 && gSent.size() == gDrawn.size() && gSent.size() >= 4);
	CHECK(gShaderAtDraw == &shader); // ours, swapped in after the transform
}

static void TestDrawsBothSides()
{
	gCull = kCullBack; // as RenderEverythingBarRoads leaves it
	int shader = 0;
	{
		worlddraw::Batch batch(&shader);
		for (int i = 0; i < 3; i++)
			batch.Put({float(i), 0, 0}, 255, 255, 255, 0, 0);
	}
	CHECK(gCullAtDraw == kCullNone && gCull == kCullBack);
}

int main()
{
	game::api.RwRenderStateSet = [](int state, void *value) {
		if (state == 20)
			gCull = reinterpret_cast<intptr_t>(value);
	};
	CHECK(worlddraw::Install());
	TestBatchesFitTheGameBuffer();
	TestDrawsBothSides();
	if (gFailures)
		return 1;
	std::puts("worlddraw: all world drawing tests passed");
}
