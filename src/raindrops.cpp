#include "raindrops.h"
#include "camera.h"
#include "config.h"
#include "draw.h"
#include "dropmodel.h"
#include "gamedrops.h"
#include "hooks.h"
#include "game.h"
#include "log.h"
#include "minimap.h"
#include "shaders.h"
#include "weather.h"
#include "worlddraw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <vector>

using namespace game;
namespace raindrops {
namespace {

// CVehicle / CPhysical (macVC 1.0.5217).
constexpr size_t kMoveSpeed = 0x84, kVehicleType = 0x348;
constexpr uint8_t kNoRainZone = 0x08; // CCullZones' no-rain attribute (as CWeather::AddRain tests it)
constexpr float kOffGlass = 0.008f;   // m a drop sits in front of its glass (towards him), clear of its depth

const int32_t *gArea;
const uint8_t *gCameraZone, *gPlayerZone;
dropmodel::Drops gDrops;
bool gInstalled;
uint32_t gLastMs;
const uint32_t *gFrameCounter;
void (*orig_RenderFirstPersonVehicle)();
// This frame's (Step, once a frame): the car he's in, its triangles, the mode.
struct Now {
	uint32_t frame = ~0u;
	const uint8_t *car = nullptr;
	std::vector<worlddraw::Face> faces;
	dropmodel::DropMode mode = dropmodel::DropMode::None;
	CVector eye{};
} gNow;
bool gTried, gReady;
void *gDropShader, *gGlassShader;
int uFrame;
Im2DCanvas gCanvas;
using worlddraw::Face;

// No roof over his head (the game's open-top cars, or nothing over his eye):
// drops fall on him, not the glass. Asked each time (some roofs are extras).
bool TopDown(const uint8_t *car, const std::vector<Face> &faces, CVector eye)
{
	if (worlddraw::OpenTop(car))
		return true;
	bool roof = false;
	for (const Face &f : faces) {
		const float low = std::min({f.a.z, f.b.z, f.c.z});
		const float x0 = std::min({f.a.x, f.b.x, f.c.x}), x1 = std::max({f.a.x, f.b.x, f.c.x});
		const float y0 = std::min({f.a.y, f.b.y, f.c.y}), y1 = std::max({f.a.y, f.b.y, f.c.y});
		roof |= !f.glass && low > eye.z && low < eye.z + 0.8f && eye.x > x0 - 0.3f && eye.x < x1 + 0.3f &&
		        eye.y > y0 - 0.3f && eye.y < y1 + 0.3f;
	}
	return !roof;
}

const char *const kScreenVertex = R"(
uniform vec4 u_xform;
VSIN(ATTRIB_POS) vec4 in_pos;
VSOUT vec2 v_tex0;
VSOUT vec4 v_color;
void main(void)
{
	gl_Position = in_pos;
	gl_Position.w = 1.0;
	gl_Position.xy = gl_Position.xy * u_xform.xy + u_xform.zw;
	v_tex0 = in_tex0;
	v_color = in_color;
}
)";

// A drop: the frame round it seen through it upside down (a drop is a strong
// lens), its rim darker, a glint up on its left. A trail: the wet line a
// running drop leaves, a faint smear. Alpha-blended. The same body serves the
// lens (on the screen) and the windscreen (on the glass, in the world: its
// size on the screen from how fast its own coordinates change across it).
const char *const kDropBody = R"(
uniform sampler2D tex0;
uniform vec4 u_dropFrame; // frame width, height (px); on the glass (1) or the lens; frame px per screen px
FSIN vec2 v_tex0;
FSIN vec4 v_color;        // its radius (screen px / 255; on the glass, unused), a trail or not, its shape's seed, its strength
vec3 Frame(vec2 px)
{
	vec2 size = vec2(textureSize(tex0, 0));
	px = clamp(px, vec2(0.5), u_dropFrame.xy - 0.5);
	return texture(tex0, (px + vec2(0.0, size.y - u_dropFrame.y)) / size).rgb;
}
void main(void)
{
	float mask = 1.0;
	vec2 local = v_tex0;
	float radius = u_dropFrame.z > 0.5 ? 1.0 / max(length(vec2(dFdx(local.x), dFdy(local.x))), 0.0001)
	                                   : v_color.r * 255.0 * u_dropFrame.w;
	// Its own up, on the screen (gl_FragCoord's y runs up): the lens's local y runs down the screen, the glass's up it.
	vec2 up = u_dropFrame.z > 0.5 ? local : vec2(local.x, -local.y);
	if (v_color.g > 0.5) {
		// The wet line a running drop leaves: a faint smear.
		float across = 1.0 - local.x * local.x;
		vec3 c = Frame(gl_FragCoord.xy + vec2(local.x * radius * 0.5, 0.0));
		FRAGCOLOR(vec4(c, across * (1.0 - abs(local.y)) * 0.35 * mask * v_color.a));
		return;
	}
	// Not quite round...
	float seed = v_color.b * 6.2831853;
	float turn = atan(local.y, local.x + 0.00001);
	float wobble = 1.0 + 0.07 * sin(turn * 3.0 + seed) + 0.04 * sin(turn * 5.0 - seed * 2.0);
	float r2 = dot(local, local) / (wobble * wobble);
	if (r2 >= 1.0)
		discard;
	// ...and the world through it upside down, smaller and soft (a drop is a strong lens).
	vec2 at = gl_FragCoord.xy - up * radius - up * radius * 2.2;
	vec3 c = vec3(0.0);
	for (int i = 0; i < 5; i++) {
		vec2 o = vec2(float(i) - 2.0, float((i * 3) % 5) - 2.0) * radius * 0.12;
		c += Frame(at + o);
	}
	c *= 0.2;
	c *= mix(1.0, 0.7, smoothstep(0.6, 1.0, r2)); // its rim a little darker
	vec2 g = local - vec2(-0.35, -0.45);
	c += vec3(exp(-dot(g, g) * 30.0) * 0.45); // a glint up on its left
	FRAGCOLOR(vec4(c, (1.0 - smoothstep(0.75, 1.0, r2)) * mask * v_color.a));
}
)";

// A windscreen's drop, on its glass in the world.
const char *const kGlassVertex = R"(
VSIN(ATTRIB_POS) vec3 in_pos;
VSOUT vec2 v_tex0;
VSOUT vec4 v_color;
void main(void)
{
	v_tex0 = in_tex0;
	v_color = in_color;
	gl_Position = u_proj * u_view * (u_world * vec4(in_pos, 1.0));
}
)";

bool Prepare()
{
	if (gTried)
		return gReady;
	gTried = true;
	if (!shaders::LoadGL())
		return false;
	uFrame = shaders::Uniform("u_dropFrame");
	gDropShader = shaders::Create("rain drops", kScreenVertex, kDropBody);
	gGlassShader = shaders::Create("rain drops (on the glass)", kGlassVertex, kDropBody);
	gReady = gDropShader && gGlassShader;
	return gReady;
}

// The windscreen's drops on its glass: each where the ray from his eye
// through its place in the windscreen's window (fixed to the car) meets the
// car's glass, lying on it, a little in front; drawn in the world, depth-tested,
// so the dash, pillars and roof hide what's behind them. With the frame (for
// the view through them) as the texture.
void DrawOnGlass(const std::vector<Face> &faces, const uint8_t *car, CVector eye, void *frame)
{
	std::vector<dropmodel::Pane> glass;
	for (const Face &f : faces)
		if (f.glass)
			glass.push_back({f.a, f.b, f.c});
	if (glass.empty())
		return;
	const worlddraw::Axes axes = worlddraw::VehicleAxes(car);
	auto world = [&](Vec3 d) {
		return Vec3{axes.right.x * d.x + axes.forward.x * d.y + axes.up.x * d.z, axes.right.y * d.x + axes.forward.y * d.y + axes.up.y * d.z,
		            axes.right.z * d.x + axes.forward.z * d.y + axes.up.z * d.z};
	};
	api.RwRenderStateSet(kStateTextureRaster, frame);
	api.RwRenderStateSet(kStateZTest, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateZWrite, nullptr);
	api.RwRenderStateSet(kStateVertexAlpha, reinterpret_cast<void *>(1));
	api.RwRenderStateSet(kStateFog, nullptr);
	api.RwRenderStateSet(kStateSrcBlend, reinterpret_cast<void *>(kBlendSrcAlpha));
	api.RwRenderStateSet(kStateDestBlend, reinterpret_cast<void *>(kBlendInvSrcAlpha));
	const Vec3 from{eye.x, eye.y, eye.z};
	worlddraw::Batch batch(gGlassShader);
	for (const dropmodel::Drop &d : gDrops.All()) {
		const Vec3 dir = world(dropmodel::WindscreenDirection(d.pos));
		float t;
		Vec3 n;
		if (!dropmodel::HitGlass(glass, from, dir, t, n) || t > 3.0f)
			continue; // no glass that way (a pillar, an open side)
		// Up the glass (the car's up, laid on it) and across it.
		Vec3 up = axes.up;
		const float into = up.x * n.x + up.y * n.y + up.z * n.z;
		up = {up.x - n.x * into, up.y - n.y * into, up.z - n.z * into};
		const float ul = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
		if (ul < 1e-4f)
			continue;
		up = {up.x / ul, up.y / ul, up.z / ul};
		const Vec3 across{up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x};
		const float r = dropmodel::WindscreenAngle(d.radius) * t;
		const Vec3 c{from.x + dir.x * (t - kOffGlass), from.y + dir.y * (t - kOffGlass), from.z + dir.z * (t - kOffGlass)};
		auto at = [&](float a, float b) { // across, up (m) from its middle
			return Vec3{c.x + across.x * a + up.x * b, c.y + across.y * a + up.y * b, c.z + across.z * a + up.z * b};
		};
		const uint8_t strength = 230;
		if (d.trail > 0.002f) { // the wet line above it, up the glass
			const float h = dropmodel::WindscreenAngle(d.trail) * t, half = r * 0.45f;
			const Vec3 q[4] = {at(-half, 0), at(half, 0), at(half, h), at(-half, h)};
			const float u[4] = {-1, 1, 1, -1}, v[4] = {0, 0, 1, 1};
			for (int i : {0, 1, 2, 0, 2, 3})
				batch.Put(q[i], 0, 255, 0, u[i], v[i], uint8_t(strength * 0.8f));
		}
		const Vec3 q[4] = {at(-r, -r), at(r, -r), at(r, r), at(-r, r)};
		const float u[4] = {-1, 1, 1, -1}, v[4] = {-1, -1, 1, 1};
		for (int i : {0, 1, 2, 0, 2, 3})
			batch.Put(q[i], 0, 0, uint8_t(d.seed * 255), u[i], v[i], strength);
	}
}

struct Stats {
	double ms = 0;
	size_t drops = 0, frames = 0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} gStats;

// The cost, every 20 s.
void Noted(const char *mode, std::chrono::steady_clock::time_point start)
{
	gStats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	gStats.drops += gDrops.All().size();
	if (++gStats.frames && std::chrono::steady_clock::now() - gStats.since > std::chrono::seconds(20)) {
		Log("rain drops: on the %s, %zu a frame, %.3f ms a frame to send", mode, gStats.drops / gStats.frames,
		    gStats.ms / double(gStats.frames));
		gStats = {};
	}
}

bool Sheltered()
{
	return (gArea && *gArea != 0) || (gCameraZone && (*gCameraZone & kNoRainZone)) ||
	       (gPlayerZone && (*gPlayerZone & kNoRainZone));
}

// Once a frame, on the first of the two calls: what he's in, how it's raining
// on it, and the drops moved on.
bool Step()
{
	if (!gConfig.rainDrops || !gInstalled || !api.timeMs)
		return false;
	if (gFrameCounter && *gFrameCounter == gNow.frame)
		return !gDrops.All().empty();
	gNow.frame = gFrameCounter ? *gFrameCounter : gNow.frame + 1;
	const uint32_t now = *api.timeMs;
	const float dt = gLastMs && now >= gLastMs ? std::min(0.25f, float(now - gLastMs) / 1000.0f) : 0.0f;
	gLastMs = now;
	const rainmodel::Weather &w = weather::Now();
	gNow.faces.clear();
	gNow.car = nullptr;
	gNow.mode = dropmodel::DropMode::None;
	if (w.rain < rainmodel::kRaining && gDrops.All().empty())
		return false; // dry, and nothing left on the glass
	const uint8_t *car = camera::InSeat() ? camera::SeatVehicle() : nullptr;
	const int type = car ? *reinterpret_cast<const int32_t *>(car + kVehicleType) : -1;
	const CVector eye = shaders::CameraPosition();
	if (car && (type == dropmodel::kCar || type == dropmodel::kHeli || type == dropmodel::kPlane))
		gNow.faces = worlddraw::VehicleFaces(car);
	const bool topDown = type == dropmodel::kCar && !gNow.faces.empty() && TopDown(car, gNow.faces, eye);
	const dropmodel::DropMode mode = dropmodel::ModeFor(camera::FirstPersonOnFoot(), car != nullptr, type, topDown, gArea && *gArea != 0);
	const mapdata::Map *map = minimap::RainMap();
	const bool exposed = !Sheltered() && !(map && mapdata::CoveredAt(*map, {eye.x, eye.y, eye.z + 0.3f}));
	float speed = 0;
	if (car) {
		const CVector v = *reinterpret_cast<const CVector *>(car + kMoveSpeed);
		speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * 50.0f; // a frame's worth at 50 a second
	}
	gDrops.Update(w.rain, exposed, speed, mode, dt);
	gNow.car = car;
	gNow.mode = mode;
	gNow.eye = eye;
	return !gDrops.All().empty() && Prepare();
}

// A windscreen's drops, as soon as the frame has the car in it (its depth
// hides those behind the dash, pillars and roof; after the lighting pass the
// frame's depth is no longer the scene's).
void Hook_RenderFirstPersonVehicle()
{
	orig_RenderFirstPersonVehicle();
	if (!Step() || gNow.mode != dropmodel::DropMode::Windscreen || gNow.faces.empty() || !gNow.car)
		return;
	float fw, fh;
	shaders::FrameSize(fw, fh);
	const shaders::SavedStates saved;
	void *frame = shaders::GrabFrame();
	if (!frame)
		return;
	shaders::Set(uFrame, fw, fh, 1.0f, fw / float(ScreenWidth()));
	const auto start = std::chrono::steady_clock::now();
	DrawOnGlass(gNow.faces, gNow.car, gNow.eye, frame);
	Noted("windscreen", start);
}

} // namespace

// The lens's drops (bikes, boats, open cars): over the graded frame, under the HUD.
void Draw()
{
	if (!Step() || gNow.mode != dropmodel::DropMode::Lens)
		return;
	const auto start = std::chrono::steady_clock::now();
	struct Timed {
		std::chrono::steady_clock::time_point start;
		~Timed() { Noted("lens", start); }
	} timed{start};
	float fw, fh;
	shaders::FrameSize(fw, fh);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const shaders::SavedStates saved;
	void *frame = shaders::GrabFrame();
	if (!frame)
		return;
	shaders::Set(uFrame, fw, fh, 0.0f, fw / W);
	SetHudRenderStates();
	shaders::OverrideIm2D(gDropShader);
	gCanvas.SetTexture(frame);
	const uint8_t strength = 230;
	for (const dropmodel::Drop &d : gDrops.All()) {
		const float r = d.radius * H, x = d.pos.x * W, y = d.pos.y * H, top = y - d.trail * H;
		const uint8_t rpx = uint8_t(std::clamp(r, 1.0f, 255.0f));
		if (d.trail > 0.002f) { // the wet line above it, fading to the top
			const float half = r * 0.45f;
			const CRGBA c{rpx, 255, 0, uint8_t(strength * 0.8f)};
			gCanvas.Tri({x - half, y, -1, 0}, {x + half, y, 1, 0}, {x + half, top, 1, 1}, c);
			gCanvas.Tri({x - half, y, -1, 0}, {x + half, top, 1, 1}, {x - half, top, -1, 1}, c);
		}
		const CRGBA c{rpx, 0, uint8_t(d.seed * 255), strength};
		gCanvas.Tri({x - r, y - r, -1, -1}, {x + r, y - r, 1, -1}, {x + r, y + r, 1, 1}, c);
		gCanvas.Tri({x - r, y - r, -1, -1}, {x + r, y + r, 1, 1}, {x - r, y + r, -1, 1}, c);
	}
	gCanvas.Flush();
	shaders::OverrideIm2D(nullptr);
}

bool Install()
{
	if (!gConfig.rainDrops)
		return true;
	gArea = static_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN5CGame8currAreaE"));
	gCameraZone = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CCullZones19CurrentFlags_CameraE"));
	gPlayerZone = static_cast<const uint8_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN10CCullZones19CurrentFlags_PlayerE"));
	gFrameCounter = static_cast<const uint32_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN6CTimer14m_FrameCounterE"));
	gInstalled = weather::Install() && worlddraw::Install();
	// After the game draws the car he's in (the gun's hook there, if it's on, first).
	if (gInstalled && !(orig_RenderFirstPersonVehicle = reinterpret_cast<void (*)()>(
	                        hooks::Chain("_ZN9CRenderer24RenderFirstPersonVehicleEv", reinterpret_cast<void *>(Hook_RenderFirstPersonVehicle)))))
		Log("rain drops: the car's hook is missing (re-run install.sh); none on windscreens");
	if (gInstalled && !gamedrops::Install())
		Log("rain drops: the game's own screen drops can't be turned off (re-run install.sh); both show");
	const bool ready = gInstalled;
	Log("rain drops: %s", ready ? "on" : "unavailable");
	return ready;
}

} // namespace raindrops
