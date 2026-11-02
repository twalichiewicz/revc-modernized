#include "rtshadows.h"
#include "hooks.h"
#include "log.h"
#include "rtgeometry.h"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rtshadows {
namespace {

constexpr double kEvery = 30.0; // s between lines of a kind after the first few
constexpr int kFirst = 3;

void (*orig_Debug)(const char *format, ...);
void (*orig_SetDynamicTriangles)(const float *tris, int count);
void (*orig_AddTriangles)(const float *tris, int count);
void (*orig_AddTranslucentTriangles)(const float *tris, int count);
bool (*orig_TraceShadows)(const uint8_t *view, int width, int height, uint8_t *a, uint8_t *b, uint8_t *c);
bool gChecking, gCheckingScene, gCheckingView;
// Written on the main thread; the watchdog reads them only while that thread
// is stuck (in the trace, say).
rtgeometry::Report gLast;
struct Scene { long long triangles = 0, unfit = 0, collapsed = 0; } gStatic, gTranslucent;
struct View { float camera[3] = {}, sun[3] = {}; int lights = 0; long long skipped = 0; } gView;
std::vector<float> gFit;
unsigned long long gFramesLeftOut, gOriginFrames;
int gLeftOutLogs, gCollapsedLogs, gSceneLogs, gViewLogs;

// MtlRtView (macVC 1.0.5217, as mtlrtTraceShadows copies it into the kernel's
// RTParams): 22 floats from 0, the lights' 0x40-byte records from 0x58, their
// count at 0x2058 (the kernel takes at most 128).
constexpr size_t kViewLights = 0x58, kViewLightCount = 0x2058;
constexpr int kMaxLights = 128;

void Collapsed(const char *what, const rtgeometry::Report &report)
{
	// At the world's origin: an object whose matrix is all zeros, one the game
	// has made but not yet placed, say. Each time (they come in bursts).
	if (report.atOrigin && (++gOriginFrames <= 16 || gOriginFrames % 300 == 0))
		Log("rt shadows: %d %s triangles at the world's origin (an object whose matrix is all zeros), left out; "
		    "frame %llu with any",
		    report.atOrigin, what, gOriginFrames);
	// Elsewhere: something shrunk to nothing where it stands (hidden?), and how far from the camera.
	if (report.collapsed > report.atOrigin && gCollapsedLogs < 4) {
		++gCollapsedLogs;
		const float *p = report.point, *c = gView.camera;
		const float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
		Log("rt shadows: %d %s triangles collapsed to a point at %.2f %.2f %.2f, %.2f m from the camera, left out",
		    report.collapsed - report.atOrigin, what, p[0], p[1], p[2], std::sqrt(dx * dx + dy * dy + dz * dz));
	}
}
void AddScene(Scene &scene, const char *what, void (*orig)(const float *, int), const float *tris, int count)
{
	rtgeometry::Report report;
	static std::vector<float> fit;
	const bool left = rtgeometry::Clean(tris, count, fit, report);
	scene.triangles += report.triangles - report.dropped - report.collapsed;
	scene.unfit += report.dropped;
	scene.collapsed += report.collapsed;
	Collapsed(what, report);
	if (report.dropped && gSceneLogs < 8) {
		++gSceneLogs;
		const float *b = report.bad;
		Log("rt shadows: left out %d of %d %s triangles unfit to trace; the first, #%d: (%g %g %g) (%g %g %g) "
		    "(%g %g %g)",
		    report.dropped, report.triangles, what, report.firstBad, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
		    b[8]);
	}
	if (left)
		orig(fit.data(), int(fit.size() / 9));
	else
		orig(tris, count);
}

// One kind of line: the first few as they come, then one every kEvery
// seconds with how many there were in between.
struct Kind {
	const char *match;
	int seen = 0;
	int since = 0;
	double last = 0.0;
};
Kind gKinds[] = {{"trace #"}, {"acceleration structure built"}, {"MetalRT"}};

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void Note(char *line)
{
	if (!strstr(line, "MetalRT"))
		return;
	const char *text = strncmp(line, "[DBG]: ", 7) ? line : line + 7;
	if (char *end = strchr(line, '\n'))
		*end = '\0';
	for (Kind &kind : gKinds)
		if (strstr(text, kind.match)) {
			kind.seen++;
			const double now = Now();
			if (kind.seen <= kFirst || now - kind.last >= kEvery) {
				if (kind.since)
					Log("rt shadows: %s (%d more since the last)", text, kind.since);
				else
					Log("rt shadows: %s", text);
				kind.last = now;
				kind.since = 0;
			} else {
				kind.since++;
			}
			return;
		}
}

void Hook_Debug(const char *format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof line, format, args);
	va_end(args);
	orig_Debug("%s", line);
	Note(line);
}

// Metal's acceleration structures are undefined for non-finite or absurd
// vertices, and the trace then waits on the GPU with no timeout: a freeze.
void Hook_SetDynamicTriangles(const float *tris, int count)
{
	rtgeometry::Report report;
	if (!rtgeometry::Clean(tris, count, gFit, report)) {
		gLast = report;
		orig_SetDynamicTriangles(tris, count);
		return;
	}
	gLast = report;
	Collapsed("moving", report);
	if (report.dropped && (gLeftOutLogs < 8 || ++gFramesLeftOut % 600 == 0)) {
		++gLeftOutLogs;
		const float *b = report.bad;
		Log("rt shadows: left out %d of %d moving triangles unfit to trace; the first, #%d: (%g %g %g) (%g %g %g) "
		    "(%g %g %g)",
		    report.dropped, report.triangles, report.firstBad, b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8]);
	}
	orig_SetDynamicTriangles(gFit.data(), int(gFit.size() / 9));
}

void Hook_AddTriangles(const float *tris, int count) { AddScene(gStatic, "static", orig_AddTriangles, tris, count); }
void Hook_AddTranslucentTriangles(const float *tris, int count)
{
	AddScene(gTranslucent, "translucent", orig_AddTranslucentTriangles, tris, count);
}

// Its rays come from the camera and the far plane's corners, and go to the sun
// and each light: one that isn't finite makes the frame's rays undefined. That
// frame isn't traced (false: the game's own "no shadows this frame").
bool Hook_TraceShadows(const uint8_t *view, int width, int height, uint8_t *a, uint8_t *b, uint8_t *c)
{
	if (!view)
		return orig_TraceShadows(view, width, height, a, b, c);
	const float *head = reinterpret_cast<const float *>(view);
	const int lights = std::clamp(*reinterpret_cast<const int32_t *>(view + kViewLightCount), 0, kMaxLights);
	std::memcpy(gView.camera, head, sizeof gView.camera);
	std::memcpy(gView.sun, head + 15, sizeof gView.sun);
	gView.lights = lights;
	const int bad = rtgeometry::BadViewField(head, reinterpret_cast<const float *>(view + kViewLights), lights);
	if (bad < 0)
		return orig_TraceShadows(view, width, height, a, b, c);
	++gView.skipped;
	if (gViewLogs < 8 || gView.skipped % 600 == 0) {
		++gViewLogs;
		if (bad < 22)
			Log("rt shadows: a frame's view isn't finite (its %s, float %d); not traced",
			    bad < 3 ? "camera" : bad < 15 ? "far plane" : bad < 18 ? "sun" : "parameters", bad);
		else
			Log("rt shadows: a frame's light %d of %d isn't finite (float %d of its 16); not traced", (bad - 22) / 16 + 1,
			    lights, (bad - 22) % 16);
	}
	return false;
}

} // namespace

bool Install()
{
	gChecking = hooks::Install("_ZN2rw3gl324mtlrtSetDynamicTrianglesEPKfi", Hook_SetDynamicTriangles,
	                           orig_SetDynamicTriangles);
	gCheckingScene = hooks::Install("_ZN2rw3gl317mtlrtAddTrianglesEPKfi", Hook_AddTriangles, orig_AddTriangles) &&
	                 hooks::Install("_ZN2rw3gl328mtlrtAddTranslucentTrianglesEPKfi", Hook_AddTranslucentTriangles,
	                                orig_AddTranslucentTriangles);
	gCheckingView = hooks::Install("_ZN2rw3gl317mtlrtTraceShadowsEPKNS0_9MtlRtViewEiiPhS4_S4_", Hook_TraceShadows,
	                               orig_TraceShadows);
	if (!gChecking || !gCheckingScene || !gCheckingView)
		Log("rt shadows: not every input is checked before the trace (re-run install.sh): moving %d, scene %d, view %d",
		    gChecking, gCheckingScene, gCheckingView);
	return hooks::Install("_Z9re3_debugPKcz", Hook_Debug, orig_Debug);
}

void LogLastGeometry()
{
	if (gChecking) {
		const rtgeometry::Report r = gLast;
		Log("rt shadows: the last trace's moving geometry: %d triangles (%d unfit, %d collapsed, %d of them at the "
		    "origin left out), x %.0f..%.0f, y %.0f..%.0f, z %.0f..%.0f m, its longest edge %.1f m",
		    r.triangles, r.dropped, r.collapsed, r.atOrigin, r.lo[0], r.hi[0], r.lo[1], r.hi[1], r.lo[2], r.hi[2],
		    r.longestEdge);
	}
	if (gCheckingScene)
		Log("rt shadows: its scene so far: %lld static triangles (%lld unfit, %lld collapsed left out), %lld translucent "
		    "(%lld, %lld)",
		    gStatic.triangles, gStatic.unfit, gStatic.collapsed, gTranslucent.triangles, gTranslucent.unfit,
		    gTranslucent.collapsed);
	if (gCheckingView)
		Log("rt shadows: its view: camera %.1f %.1f %.1f, sun %.2f %.2f %.2f, %d lights; %lld frames not traced",
		    gView.camera[0], gView.camera[1], gView.camera[2], gView.sun[0], gView.sun[1], gView.sun[2], gView.lights,
		    gView.skipped);
}

} // namespace rtshadows
