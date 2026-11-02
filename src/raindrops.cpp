#include "raindrops.h"
#include "camera.h"
#include "config.h"
#include "draw.h"
#include "dropmodel.h"
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
constexpr int kStateCull = 20, kCullNone = 1;
constexpr int kMaskUnit = 12;         // texture units: librw 0-3, the clouds 4-8, the lighting 9-11
constexpr int kMaskDown = 2;          // the glass mask at half the frame's resolution

int (*gStateGet)(int state, void *value);
const int32_t *gArea;
const uint8_t *gCameraZone, *gPlayerZone;
dropmodel::Drops gDrops;
uint32_t gLastMs;
bool gTried, gReady;
void *gDropShader, *gMaskShader;
int uFrame;
shaders::Target gMask;
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
// running drop leaves, a faint smear. Alpha-blended; the windscreen's mask
// keeps both off everything but the car's glass.
const char *const kDropFragment = R"(
uniform sampler2D tex0;
uniform sampler2D u_dropMask;
uniform vec4 u_dropFrame; // frame width, height (px); the mask on; frame px per screen px
FSIN vec2 v_tex0;
FSIN vec4 v_color;
vec3 Frame(vec2 px)
{
	vec2 size = vec2(textureSize(tex0, 0));
	px = clamp(px, vec2(0.5), u_dropFrame.xy - 0.5);
	return texture(tex0, (px + vec2(0.0, size.y - u_dropFrame.y)) / size).rgb;
}
void main(void)
{
	float mask = u_dropFrame.z > 0.5 ? texture(u_dropMask, gl_FragCoord.xy / u_dropFrame.xy).r : 1.0;
	float radius = v_color.r * 255.0 * u_dropFrame.w;
	vec2 local = v_tex0;
	vec2 up = vec2(local.x, -local.y); // the screen's y runs down, gl_FragCoord's up
	if (v_color.g > 0.5) {
		float across = 1.0 - local.x * local.x;
		vec3 c = Frame(gl_FragCoord.xy + vec2(local.x * radius * 0.6, 0.0));
		FRAGCOLOR(vec4(c * 0.95, across * (1.0 - local.y) * 0.45 * mask * v_color.a));
		return;
	}
	float r2 = dot(local, local);
	if (r2 >= 1.0)
		discard;
	vec2 middle = gl_FragCoord.xy - up * radius;
	vec3 c = Frame(middle - up * radius * 1.8);
	c *= mix(1.0, 0.55, smoothstep(0.55, 1.0, r2));
	float glint = pow(max(0.0, 1.0 - length(local - vec2(-0.35, -0.4)) * 3.2), 3.0);
	c += vec3(glint * 0.7);
	FRAGCOLOR(vec4(c, smoothstep(1.0, 0.82, r2) * mask * v_color.a));
}
)";

// The car's triangles from the camera: its glass white, the rest black.
const char *const kMaskVertex = R"(
VSIN(ATTRIB_POS) vec3 in_pos;
VSOUT vec4 v_colour;
void main(void)
{
	v_colour = in_color;
	gl_Position = u_proj * u_view * (u_world * vec4(in_pos, 1.0));
}
)";
const char *const kMaskFragment = R"(
FSIN vec4 v_colour;
void main(void)
{
	FRAGCOLOR(vec4(v_colour.rgb, 1.0));
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
	gDropShader = shaders::Create("rain drops", kScreenVertex, kDropFragment);
	gMaskShader = shaders::Create("rain drops (the glass)", kMaskVertex, kMaskFragment);
	gReady = gDropShader && gMaskShader;
	if (gReady)
		shaders::SetSampler(gDropShader, "u_dropMask", kMaskUnit);
	return gReady;
}

// The glass, white, where it isn't behind the car's own dash, pillars or roof:
// the car's triangles drawn farthest first (the mask has no depth of its own).
bool DrawMask(const std::vector<Face> &faces, CVector eye, float w, float h)
{
	if (!shaders::Resize(gMask, std::max(1, int(w) / kMaskDown), std::max(1, int(h) / kMaskDown)))
		return false;
	std::vector<std::pair<float, const Face *>> order;
	order.reserve(faces.size());
	for (const Face &f : faces) {
		const float x = (f.a.x + f.b.x + f.c.x) / 3 - eye.x, y = (f.a.y + f.b.y + f.c.y) / 3 - eye.y,
		            z = (f.a.z + f.b.z + f.c.z) / 3 - eye.z;
		order.push_back({x * x + y * y + z * z, &f});
	}
	std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	const shaders::DrawInto into(gMask);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	shaders::OverrideIm2D(nullptr);
	gCanvas.SetTexture(nullptr);
	gCanvas.Quad({0, 0}, {W, 0}, {W, H}, {0, H}, {0, 0, 0, 255});
	gCanvas.Flush();
	void *cull = nullptr;
	gStateGet(kStateCull, &cull);
	api.RwRenderStateSet(kStateCull, reinterpret_cast<void *>(kCullNone)); // the glass seen from inside: its back
	api.RwRenderStateSet(kStateZTest, nullptr);
	{
		worlddraw::Batch batch(gMaskShader);
		for (const auto &[distance, f] : order) {
			const uint8_t c = f->glass ? 255 : 0;
			batch.Put(f->a, c, c, c, 0, 0);
			batch.Put(f->b, c, c, c, 0, 0);
			batch.Put(f->c, c, c, c, 0, 0);
		}
	}
	api.RwRenderStateSet(kStateCull, cull);
	return true;
}

struct Stats {
	double ms = 0;
	size_t drops = 0, frames = 0;
	std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} gStats;

bool Sheltered()
{
	return (gArea && *gArea != 0) || (gCameraZone && (*gCameraZone & kNoRainZone)) ||
	       (gPlayerZone && (*gPlayerZone & kNoRainZone));
}

} // namespace

void Draw()
{
	if (!gConfig.rainDrops || !api.timeMs)
		return;
	const uint32_t now = *api.timeMs;
	const float dt = gLastMs && now >= gLastMs ? std::min(0.25f, float(now - gLastMs) / 1000.0f) : 0.0f;
	gLastMs = now;
	const rainmodel::Weather &w = weather::Now();
	if (w.rain < rainmodel::kRaining && gDrops.All().empty())
		return; // dry, and nothing left on the glass
	const uint8_t *car = camera::InSeat() ? camera::SeatVehicle() : nullptr;
	const int type = car ? *reinterpret_cast<const int32_t *>(car + kVehicleType) : -1;
	const CVector eye = shaders::CameraPosition();
	std::vector<Face> faces;
	if (car && (type == dropmodel::kCar || type == dropmodel::kHeli || type == dropmodel::kPlane))
		faces = worlddraw::VehicleFaces(car);
	const bool topDown = type == dropmodel::kCar && !faces.empty() && TopDown(car, faces, eye);
	const dropmodel::DropMode mode = dropmodel::ModeFor(camera::FirstPersonOnFoot(), car != nullptr, type, topDown, gArea && *gArea != 0);
	const mapdata::Map *map = minimap::RainMap();
	const bool exposed = !Sheltered() && !(map && mapdata::CoveredAt(*map, {eye.x, eye.y, eye.z + 0.3f}));
	float speed = 0;
	if (car) {
		const CVector v = *reinterpret_cast<const CVector *>(car + kMoveSpeed);
		speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z) * 50.0f; // a frame's worth at 50 a second
	}
	gDrops.Update(w.rain, exposed, speed, mode, dt);
	if (gDrops.All().empty() || !Prepare())
		return;
	const auto start = std::chrono::steady_clock::now();
	struct Timed {
		std::chrono::steady_clock::time_point start;
		const char *mode;
		~Timed()
		{
			gStats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			gStats.drops += gDrops.All().size();
			if (++gStats.frames && std::chrono::steady_clock::now() - gStats.since > std::chrono::seconds(20)) {
				Log("rain drops: on the %s, %zu a frame, %.3f ms a frame to send", mode, gStats.drops / gStats.frames,
				    gStats.ms / double(gStats.frames));
				gStats = {};
			}
		}
	} timed{start, mode == dropmodel::DropMode::Windscreen ? "windscreen" : "lens"};
	const bool masked = mode == dropmodel::DropMode::Windscreen;
	if (masked && faces.empty())
		return; // no glass to put them on
	float fw, fh;
	shaders::FrameSize(fw, fh);
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const shaders::SavedStates saved;
	if (masked && !DrawMask(faces, eye, fw, fh))
		return;
	void *frame = shaders::GrabFrame();
	if (!frame)
		return;
	if (masked)
		shaders::BindTexture(kMaskUnit, false, gMask.texture);
	shaders::Set(uFrame, fw, fh, masked ? 1.0f : 0.0f, fw / W);
	SetHudRenderStates();
	shaders::OverrideIm2D(gDropShader);
	gCanvas.SetTexture(frame);
	const uint8_t strength = 230;
	for (const dropmodel::Drop &d : gDrops.All()) {
		const float r = d.radius * H, x = d.pos.x * W, y = d.pos.y * H;
		const uint8_t rpx = uint8_t(std::clamp(r, 1.0f, 255.0f));
		if (d.trail > 0.002f) { // the wet line above it, fading to the top
			const float top = y - d.trail * H, half = r * 0.45f;
			const CRGBA c{rpx, 255, 0, uint8_t(strength * 0.8f)};
			gCanvas.Tri({x - half, y, -1, 0}, {x + half, y, 1, 0}, {x + half, top, 1, 1}, c);
			gCanvas.Tri({x - half, y, -1, 0}, {x + half, top, 1, 1}, {x - half, top, -1, 1}, c);
		}
		const CRGBA c{rpx, 0, 0, strength};
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
	gStateGet = reinterpret_cast<int (*)(int, void *)>(dlsym(RTLD_MAIN_ONLY, "_Z16RwRenderStateGet13RwRenderStatePv"));
	const bool ready = weather::Install() && worlddraw::Install() && gStateGet;
	Log("rain drops: %s", ready ? "on" : "unavailable");
	return ready;
}

} // namespace raindrops
