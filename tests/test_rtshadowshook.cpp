// The ray-traced shadows' moving geometry through the actual hook: what the
// game's own mtlrtSetDynamicTriangles receives, what's logged, and the freeze
// report's line.
#include "../src/rtshadows.cpp"
#include <cmath>
#include <limits>
#include <string>

static std::vector<std::string> gLogs;
void Log(const char *format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof line, format, args);
	va_end(args);
	gLogs.push_back(line);
}
static std::vector<float> gGiven;
static int gGivenCount = -1;
static void NativeSetDynamic(const float *tris, int count)
{
	gGivenCount = count;
	gGiven.assign(tris, tris + size_t(count) * 9);
}
static void NativeDebug(const char *, ...) {}
static std::vector<float> gStaticGiven;
static void NativeAddStatic(const float *tris, int count) { gStaticGiven.insert(gStaticGiven.end(), tris, tris + size_t(count) * 9); }
static void NativeAddTranslucent(const float *, int) {}
static int gTraces;
static bool NativeTrace(const uint8_t *, int, int, uint8_t *, uint8_t *, uint8_t *)
{
	++gTraces;
	return true;
}
namespace hooks {
void *Install(const char *name, void *)
{
	if (!std::strcmp(name, "_ZN2rw3gl324mtlrtSetDynamicTrianglesEPKfi"))
		return reinterpret_cast<void *>(NativeSetDynamic);
	if (!std::strcmp(name, "_Z9re3_debugPKcz"))
		return reinterpret_cast<void *>(NativeDebug);
	if (!std::strcmp(name, "_ZN2rw3gl317mtlrtAddTrianglesEPKfi"))
		return reinterpret_cast<void *>(NativeAddStatic);
	if (!std::strcmp(name, "_ZN2rw3gl328mtlrtAddTranslucentTrianglesEPKfi"))
		return reinterpret_cast<void *>(NativeAddTranslucent);
	if (!std::strcmp(name, "_ZN2rw3gl317mtlrtTraceShadowsEPKNS0_9MtlRtViewEiiPhS4_S4_"))
		return reinterpret_cast<void *>(NativeTrace);
	return nullptr;
}
} // namespace hooks

static int failures;
#define CHECK(c)                                                                                                       \
	do {                                                                                                           \
		if (!(c)) {                                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #c);                                               \
			++failures;                                                                                    \
		}                                                                                                      \
	} while (0)

int main()
{
	CHECK(rtshadows::Install());
	// Fit: the game's own data, as it was.
	float tris[27] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 5, 5, 5, 6, 5, 5, 5, 6, 5, -3, -3, 1, -2, -3, 1, -3, -2, 1};
	rtshadows::Hook_SetDynamicTriangles(tris, 3);
	CHECK(gGivenCount == 3 && gGiven.size() == 27 && gGiven[9] == 5 && gLogs.empty());
	// A NaN vertex: that triangle never reaches the trace; it's logged.
	tris[13] = std::numeric_limits<float>::quiet_NaN();
	rtshadows::Hook_SetDynamicTriangles(tris, 3);
	CHECK(gGivenCount == 2 && gGiven[0] == 0 && gGiven[9] == -3);
	for (float v : gGiven) CHECK(std::isfinite(v));
	CHECK(gLogs.size() == 1 && gLogs[0].find("left out 1 of 3") != std::string::npos &&
	      gLogs[0].find("#1") != std::string::npos);
	// The freeze report: what the last trace was given.
	gLogs.clear();
	rtshadows::LogLastGeometry();
	CHECK(gLogs.size() == 3 && gLogs[0].find("3 triangles (1 unfit, 0 collapsed") != std::string::npos);
	// Logged a few times, then now and then, not every frame.
	gLogs.clear();
	for (int i = 0; i < 100; ++i)
		rtshadows::Hook_SetDynamicTriangles(tris, 3);
	CHECK(gLogs.size() == 7 && gGivenCount == 2);

	// The static scene as it streams in: the same check, and collapsed triangles
	// (an object of zeros at the origin) left out and logged once.
	gLogs.clear();
	float scene[36] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
	for (int i = 0; i < 9; ++i) scene[9 + i] = 0, scene[18 + i] = 0; // two at the origin
	const float good[9] = {4, 4, 4, 5, 4, 4, 4, 5, 4};
	std::memcpy(scene + 27, good, sizeof good);
	rtshadows::Hook_AddTriangles(scene, 4);
	CHECK(gStaticGiven.size() == 18 && gStaticGiven[9] == 4);
	CHECK(gLogs.size() == 1 && gLogs[0].find("2 static triangles at the world's origin") != std::string::npos);
	// Shrunk to nothing where it stands: where, and how far from the camera.
	gLogs.clear();
	float hidden[9] = {3, 4, 12, 3, 4, 12, 3, 4, 12};
	rtshadows::Hook_SetDynamicTriangles(hidden, 1);
	CHECK(gGivenCount == 0);
	CHECK(gLogs.size() == 1 && gLogs[0].find("1 moving triangles collapsed to a point at 3.00 4.00 12.00") != std::string::npos);

	// The view: traced when finite; a frame whose camera or a light isn't, not.
	alignas(16) static uint8_t view[0x2060] = {};
	float *head = reinterpret_cast<float *>(view);
	for (int i = 0; i < 22; ++i) head[i] = 1;
	*reinterpret_cast<int32_t *>(view + 0x2058) = 2;
	float *light = reinterpret_cast<float *>(view + 0x58);
	for (int i = 0; i < 32; ++i) light[i] = .5f;
	gLogs.clear();
	CHECK(rtshadows::Hook_TraceShadows(view, 720, 405, nullptr, nullptr, nullptr) && gTraces == 1 && gLogs.empty());
	head[1] = std::numeric_limits<float>::quiet_NaN();
	CHECK(!rtshadows::Hook_TraceShadows(view, 720, 405, nullptr, nullptr, nullptr) && gTraces == 1);
	CHECK(gLogs.size() == 1 && gLogs[0].find("its camera") != std::string::npos);
	head[1] = 1;
	light[16 + 4] = std::numeric_limits<float>::infinity(); // the second light's beam
	CHECK(!rtshadows::Hook_TraceShadows(view, 720, 405, nullptr, nullptr, nullptr) && gTraces == 1);
	CHECK(gLogs.size() == 2 && gLogs[1].find("light 2 of 2") != std::string::npos);
	*reinterpret_cast<int32_t *>(view + 0x2058) = 1; // that light's not one it has
	CHECK(rtshadows::Hook_TraceShadows(view, 720, 405, nullptr, nullptr, nullptr) && gTraces == 2);

	// The freeze report: the moving geometry, the scene, the view.
	gLogs.clear();
	rtshadows::LogLastGeometry();
	CHECK(gLogs.size() == 3 && gLogs[1].find("2 static triangles (0 unfit, 2 collapsed") != std::string::npos &&
	      gLogs[2].find("1 lights; 2 frames not traced") != std::string::npos);

	if (failures)
		return 1;
	std::puts("rtshadowshook: all ray-traced geometry hook tests passed");
}
