#include "grade.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "shaders.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <string>

using namespace game;

namespace grade {
namespace {

constexpr float kBloom = 0.6f, kShafts = 0.45f;
constexpr float kContrast = 0.18f, kSaturation = 1.1f, kVignette = 0.22f, kExposure = 1.0f;

// Bloom and the sun's shafts are worked out at a quarter of the frame's
// resolution each way (they're soft anyway): the bright parts of the frame,
// blurred both ways for the bloom and smeared towards the sun for the
// shafts. The last pass, at the frame's resolution, adds them and grades.
constexpr int kDown = 4;
// Texture units of ours (librw binds 0-3, the clouds 4-8).
constexpr int kSourceUnit = 9, kBloomUnit = 10, kShaftsUnit = 11;

// The frame in CPostFX's back buffer: a power of two in size, the picture in
// its top rows (as the game's colour filter reads it, flipped).
const char *const kCommon = R"(
uniform sampler2D tex0;
uniform vec4 u_gradeFrame; // frame width, height (px), bloom, shafts
vec3 Frame(vec2 px)
{
	vec2 size = vec2(textureSize(tex0, 0));
	px = clamp(px, vec2(0.5), u_gradeFrame.xy - 0.5);
	return texture(tex0, (px + vec2(0.0, size.y - u_gradeFrame.y)) / size).rgb;
}
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
// What of a colour is bright enough to glow.
vec3 Bright(vec3 c)
{
	float l = Luma(c);
	return c * max(l - 0.72, 0.0) / max(l, 0.001);
}
)";

// The bright parts of each 4x4 of the frame (four taps, each between four pixels).
const char *const kBrightFragment = R"(
void main(void)
{
	vec2 c = (gl_FragCoord.xy - 0.5) * 4.0 + 2.0;
	vec3 sum = Bright(Frame(c + vec2(-1.0, -1.0))) + Bright(Frame(c + vec2(1.0, -1.0))) +
	           Bright(Frame(c + vec2(-1.0, 1.0))) + Bright(Frame(c + vec2(1.0, 1.0)));
	FRAGCOLOR(vec4(sum * 0.25, 1.0));
}
)";

// A Gaussian blur one way (nine taps in five), every other texel.
const char *const kBlurFragment = R"(
uniform sampler2D u_gradeSource;
uniform vec4 u_gradeStep; // between taps (uv)
void main(void)
{
	vec2 uv = gl_FragCoord.xy / vec2(textureSize(u_gradeSource, 0)), d1 = u_gradeStep.xy * 1.3846, d2 = u_gradeStep.xy * 3.2308;
	vec3 c = texture(u_gradeSource, uv).rgb * 0.2270;
	c += (texture(u_gradeSource, uv + d1).rgb + texture(u_gradeSource, uv - d1).rgb) * 0.3162;
	c += (texture(u_gradeSource, uv + d2).rgb + texture(u_gradeSource, uv - d2).rgb) * 0.0703;
	FRAGCOLOR(vec4(c, 1.0));
}
)";

// Shafts: the bright parts on the way to the sun, smeared along it.
const char *const kShaftsFragment = R"(
uniform sampler2D u_gradeSource;
uniform vec4 u_gradeSun; // the sun on screen (frame px from the bottom left), how much its shafts show
void main(void)
{
	vec2 size = vec2(textureSize(u_gradeSource, 0)) * 4.0;
	vec2 uv = gl_FragCoord.xy * 4.0 / size, stride = (u_gradeSun.xy / size - uv) / 32.0;
	vec3 sum = vec3(0.0);
	float w = 1.0;
	for (int i = 0; i < 32; i++) {
		uv += stride;
		sum += texture(u_gradeSource, uv).rgb * w;
		w *= 0.95;
	}
	FRAGCOLOR(vec4(sum * u_gradeSun.z / 32.0, 1.0));
}
)";

const char *const kGradeFragment = R"(
uniform sampler2D u_gradeBloom;
uniform sampler2D u_gradeShafts;
uniform vec4 u_gradeSun;  // the sun on screen, how much its shafts show
uniform vec4 u_gradeLook; // contrast, saturation, vignette, exposure
// As it is up to 0.8, then rolled off towards 1 (EDR highlights too).
vec3 Shoulder(vec3 c) { return min(c, 0.8) + 0.2 * (1.0 - exp(-max(c - 0.8, 0.0) / 0.2)); }
void main(void)
{
	vec2 px = gl_FragCoord.xy;
	vec2 quarter = px / (vec2(textureSize(u_gradeBloom, 0)) * 4.0);
	vec3 glow = texture(u_gradeBloom, quarter).rgb * u_gradeFrame.z;
	if (u_gradeSun.z > 0.0)
		glow += texture(u_gradeShafts, quarter).rgb * u_gradeFrame.w;
	vec3 c = Shoulder((Frame(px) + glow) * u_gradeLook.w);
	c = mix(c, c * c * (3.0 - 2.0 * c), u_gradeLook.x);
	c = mix(vec3(Luma(c)), c, u_gradeLook.y);
	vec2 q = px / u_gradeFrame.xy - 0.5;
	c *= 1.0 - u_gradeLook.z * dot(q, q) * 1.8;
	FRAGCOLOR(vec4(clamp(c, 0.0, 1.0), 1.0));
}
)";

void (*orig_RenderMotionBlur)(uint8_t *camera);
const float *gSunX, *gSunY; // CCoronas::SunScreenX/Y: the game's screen
const bool *gSunHidden;     // CCoronas::SunBlockedByClouds
bool gTried, gReady;
void *gBright, *gBlur, *gShafts, *gGrade;
int uFrame, uSun, uLook, uStep;
shaders::Target gBrightTarget, gBlurTarget, gShaftsTarget;
Im2DCanvas gCanvas;

void *Make(const char *name, const char *body)
{
	return shaders::Create(name, shaders::kScreenVertex, (std::string(kCommon) + body).c_str());
}

bool Prepare()
{
	if (gTried)
		return gReady;
	gTried = true;
	if (!shaders::LoadGL())
		return false;
	uFrame = shaders::Uniform("u_gradeFrame");
	uSun = shaders::Uniform("u_gradeSun");
	uLook = shaders::Uniform("u_gradeLook");
	uStep = shaders::Uniform("u_gradeStep");
	gBright = Make("lighting (bright parts)", kBrightFragment);
	gBlur = Make("lighting (blur)", kBlurFragment);
	gShafts = Make("lighting (shafts)", kShaftsFragment);
	gGrade = Make("lighting", kGradeFragment);
	gReady = gBright && gBlur && gShafts && gGrade;
	if (!gReady)
		return false;
	shaders::SetSampler(gBlur, "u_gradeSource", kSourceUnit);
	shaders::SetSampler(gShafts, "u_gradeSource", kSourceUnit);
	shaders::SetSampler(gGrade, "u_gradeBloom", kBloomUnit);
	shaders::SetSampler(gGrade, "u_gradeShafts", kShaftsUnit);
	return true;
}

void Pass(void *shader, void *raster)
{
	shaders::OverrideIm2D(shader);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	gCanvas.SetTexture(raster);
	gCanvas.Quad({0, 0}, {W, 0}, {W, H}, {0, H}, {255, 255, 255, 255});
	gCanvas.Flush();
}

void PassInto(const shaders::Target &target, void *shader, void *raster)
{
	const shaders::DrawInto into(target);
	Pass(shader, raster);
}

// How much of the sun's shafts to show: by day, with it ahead and on (or near) the screen.
float SunShows(const shaders::Sky &sky, float x, float y)
{
	const CVector f = shaders::CameraForward();
	const float ahead = std::clamp((f.x * sky.sun.x + f.y * sky.sun.y + f.z * sky.sun.z - 0.3f) / 0.4f, 0.0f, 1.0f);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const float off = std::max({-x, x - W, -y, y - H, 0.0f}) / (0.4f * H); // past the edge, in fractions of a screen
	return sky.daylight * ahead * std::clamp(1.0f - off, 0.0f, 1.0f) * (*gSunHidden ? 0.3f : 1.0f);
}

void Hook_RenderMotionBlur(uint8_t *camera)
{
	orig_RenderMotionBlur(camera);
	if (!gConfig.modernLighting || !Prepare())
		return;
	void *frame = shaders::GrabFrame();
	if (!frame)
		return;
	float w, h;
	shaders::FrameSize(w, h);
	const int qw = std::max(1, (int(w) + kDown - 1) / kDown), qh = std::max(1, (int(h) + kDown - 1) / kDown);
	if (!shaders::Resize(gBrightTarget, qw, qh) || !shaders::Resize(gBlurTarget, qw, qh) ||
	    !shaders::Resize(gShaftsTarget, qw, qh))
		return;
	const shaders::Sky sky = shaders::CurrentSky();
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const float shows = SunShows(sky, *gSunX, *gSunY);
	shaders::Set(uFrame, w, h, kBloom, kShafts);
	shaders::Set(uSun, *gSunX * w / W, h - *gSunY * h / H, shows);
	shaders::Set(uLook, kContrast, kSaturation, kVignette, kExposure);

	const shaders::SavedStates saved;
	SetHudRenderStates();
	api.RwRenderStateSet(kStateVertexAlpha, nullptr); // the quarter passes write as they are
	PassInto(gBrightTarget, gBright, frame);
	shaders::BindTexture(kSourceUnit, false, gBrightTarget.texture);
	if (shows > 0.0f)
		PassInto(gShaftsTarget, gShafts, nullptr);
	shaders::Set(uStep, 2.0f / qw, 0.0f, 0.0f);
	PassInto(gBlurTarget, gBlur, nullptr);
	shaders::BindTexture(kSourceUnit, false, gBlurTarget.texture);
	shaders::Set(uStep, 0.0f, 2.0f / qh, 0.0f);
	PassInto(gBrightTarget, gBlur, nullptr); // the bloom, back where the bright parts were
	shaders::BindTexture(kBloomUnit, false, gBrightTarget.texture);
	shaders::BindTexture(kShaftsUnit, false, gShaftsTarget.texture);
	Pass(gGrade, frame);
	shaders::OverrideIm2D(nullptr);
}

} // namespace

bool Install()
{
	if (!gConfig.modernLighting)
		return true;
	gSunX = static_cast<const float *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CCoronas10SunScreenXE"));
	gSunY = static_cast<const float *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CCoronas10SunScreenYE"));
	gSunHidden = static_cast<const bool *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CCoronas18SunBlockedByCloudsE"));
	return shaders::Ready() && gSunX && gSunY && gSunHidden &&
	       hooks::Install("_ZN7CCamera16RenderMotionBlurEv", Hook_RenderMotionBlur, orig_RenderMotionBlur);
}

} // namespace grade
