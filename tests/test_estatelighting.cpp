// Run the actual grading hook against a frame and capture its GPU uniforms.
// A lost area binding, wrong area branch, or unused policy must fail here.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

static void *GradeSymbol(void *, const char *);
#define dlsym GradeSymbol
#include "../src/grade.cpp"
#undef dlsym

namespace game { Api api{}; }
Config gConfig;
void Log(const char *, ...) {}

static int gFailures, gArea = 2, gNativeCalls, gNativeType, gNativeAlpha, gLookUploads;
static bool gHaveArea = true, gShaderReady = true, gFrameReady = true, gScoped;
static float gSunX = 320, gSunY = 180;
static bool gSunHidden;
static std::array<float, 4> gLook{}, gFrameUniform{};
alignas(8) static uint8_t gCamera[0xd0]{}, gScreen[16]{};

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %d: %s\n", __LINE__, #cond); ++gFailures; } } while (0)
static bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }
static void Native(uint8_t *camera)
{
	++gNativeCalls;
	gNativeType = *reinterpret_cast<int32_t *>(camera + 0xb8);
	gNativeAlpha = *reinterpret_cast<int32_t *>(camera + 0xc8);
}
static void *GradeSymbol(void *, const char *name)
{
	if (!std::strcmp(name, "_ZN5CGame8currAreaE")) return gHaveArea ? &gArea : nullptr;
	if (!std::strcmp(name, "_ZN8CCoronas10SunScreenXE")) return &gSunX;
	if (!std::strcmp(name, "_ZN8CCoronas10SunScreenYE")) return &gSunY;
	if (!std::strcmp(name, "_ZN8CCoronas18SunBlockedByCloudsE")) return &gSunHidden;
	return nullptr;
}
namespace hooks {
void *Install(const char *, void *) { return reinterpret_cast<void *>(Native); }
}
namespace sniper { bool Active() { return gScoped; } }
void SetHudRenderStates() {}
void Im2DCanvas::SetTexture(void *) {}
void Im2DCanvas::SetDepthMode(DepthMode) {}
void Im2DCanvas::Tri(const Vtx &, const Vtx &, const Vtx &, CRGBA, CRGBA, CRGBA) {}
void Im2DCanvas::Flush() {}
void Canvas::Quad(Vec2, Vec2, Vec2, Vec2, CRGBA) {}
namespace shaders {
const char *const kScreenVertex = "";
bool Ready() { return gShaderReady; }
bool LoadGL() { return true; }
int Uniform(const char *name)
{
	return !std::strcmp(name, "u_gradeLook") ? 1 : !std::strcmp(name, "u_gradeFrame") ? 2 : 3;
}
void Set(int uniform, float x, float y, float z, float w)
{
	if (uniform == 1) { gLook = {x, y, z, w}; ++gLookUploads; }
	if (uniform == 2) gFrameUniform = {x, y, z, w};
}
void *Create(const char *, const char *, const char *) { return reinterpret_cast<void *>(1); }
void SetSampler(void *, const char *, int) {}
void OverrideIm2D(void *) {}
void *GrabFrame() { return gFrameReady ? reinterpret_cast<void *>(1) : nullptr; }
void FrameSize(float &width, float &height) { width = 640; height = 360; }
bool Resize(Target &, int, int) { return true; }
Sky CurrentSky() { Sky sky{}; sky.sun = {0, 0, 1}; return sky; }
CVector CameraForward() { return {0, 1, 0}; }
void BindTexture(int, bool, unsigned) {}
SavedStates::SavedStates() { (void)values_; }
SavedStates::~SavedStates() {}
DrawInto::DrawInto(const Target &) { (void)framebuffer_; (void)viewport_; }
DrawInto::~DrawInto() {}
}

// The GLSL's low-gray path: no bright pass or shoulder below .5; saturation
// preserves gray. Evaluate the uniforms emitted by the real hook, not a
// separate copy of the area decision. q is distance from screen center.
static float Gray(float value, float qx, float qy)
{
	const float c = value * gLook[3];
	const float contrast = c + (c * c * (3 - 2 * c) - c) * gLook[0];
	return contrast * (1 - gLook[2] * (qx * qx + qy * qy) * 1.8f);
}
static bool Start(bool enabled = true)
{
	gConfig.modernLighting = enabled;
	grade::gGradeReady = grade::gTried = grade::gReady = false;
	gLookUploads = gNativeCalls = 0;
	grade::SlowMotion(0);
	return grade::Install();
}
static void Frame() { grade::Hook_RenderMotionBlur(gCamera); }

int main()
{
	game::api.rsGlobal = gScreen;
	*reinterpret_cast<int32_t *>(gScreen + 8) = 640;
	*reinterpret_cast<int32_t *>(gScreen + 12) = 360;
	game::api.RwRenderStateSet = [](int, void *) {};
	*reinterpret_cast<int32_t *>(gCamera + 0xb8) = 2;
	*reinterpret_cast<int32_t *>(gCamera + 0xc8) = 5;
	CHECK(Start());
	Frame();
	CHECK(gNativeCalls == 1 && gNativeType == 2 && gNativeAlpha == 5 && gLookUploads == 1);
	for (float gray : {0.0f, .01f, .03f, .1f, .2f, .4f}) {
		CHECK(Near(Gray(gray, 0, 0), gray));
		CHECK(Near(Gray(gray, .5f, .5f), gray));
	}
	CHECK(Near(gLook[1], 1.1f) && Near(gLook[3], 1));
	CHECK(Near(gFrameUniform[2], .6f) && Near(gFrameUniform[3], .45f));

	// The area is read every frame, including returning directly outside.
	gArea = 0;
	Frame();
	CHECK(Near(Gray(.1f, 0, 0), .08704f));
	CHECK(Near(Gray(.1f, .5f, .5f), .06980608f));
	gArea = 3; // another interior, not an estate-specific exception
	Frame();
	CHECK(Near(Gray(.1f, .5f, .5f), .1f));
	grade::SlowMotion(.5f);
	Frame();
	CHECK(Near(gLook[1], .7975f) && Near(gLook[2], .15f));
	CHECK(Near(Gray(.1f, .5f, .5f), .0865f));

	// An unavailable optional binding preserves the established outdoor look.
	gHaveArea = false;
	CHECK(Start());
	Frame();
	CHECK(Near(Gray(.1f, .5f, .5f), .06980608f));
	gHaveArea = true;
	CHECK(Start(false));
	Frame();
	CHECK(gNativeCalls == 1 && gLookUploads == 0);
	gShaderReady = false;
	CHECK(!Start());
	Frame();
	CHECK(gNativeCalls == 1 && gLookUploads == 0);
	gShaderReady = true;
	CHECK(Start());
	gFrameReady = false;
	Frame();
	CHECK(gNativeCalls == 1 && gLookUploads == 0);

	// Native blur still runs first and temporary sniper edits still restore.
	gFrameReady = true;
	gScoped = true;
	*reinterpret_cast<int32_t *>(gCamera + 0xb8) = 1;
	*reinterpret_cast<int32_t *>(gCamera + 0xc8) = 100;
	Frame();
	CHECK(gNativeType == 0 && gNativeAlpha == 0);
	CHECK(*reinterpret_cast<int32_t *>(gCamera + 0xb8) == 1 && *reinterpret_cast<int32_t *>(gCamera + 0xc8) == 100);
	if (gFailures) return 1;
	std::puts("all estate lighting hook tests passed");
}
