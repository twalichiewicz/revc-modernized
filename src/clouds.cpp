#include "clouds.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <thread>

namespace clouds {
namespace {

// The layer and its scale (m). The GLSL gets the same numbers (Constants()).
constexpr float kPlanet = 6360000.0f; // the earth's radius: the layer bends down to the horizon
constexpr float kBase = 900.0f, kTop = 2600.0f;
constexpr float kShapeTile = 3200.0f, kDetailTile = 350.0f, kWeatherTile = 32000.0f; // each repeats every
constexpr float kLeanX = 260.0f, kLeanY = 90.0f; // how far the tops lean downwind
constexpr float kSigma = 0.09f;                  // extinction at density 1, per m (crisp edges)
constexpr float kLightAbsorption = 0.2f;         // of it, towards the sun (more light gets in, as it scatters on)
constexpr float kErode = 0.6f;                   // how much the detail eats the edges
// Where they are narrows over the bottom of the layer, and towards each one's top (of its height).
constexpr float kRoundUnder = 0.15f, kRoundOver = 0.55f;
constexpr float kMaxDistance = 50000.0f, kMaxSpan = 12000.0f, kHaze = 16000.0f;
constexpr float kWindX = 7.0f, kWindY = 3.0f; // m/s
constexpr float kStride = 120.0f, kStrideGrowth = 0.025f; // m between samples outside cloud (and per m away)
constexpr int kSteps = 128, kLightSteps = 5;

// --- Noise -----------------------------------------------------------------

uint32_t Hash(uint32_t x)
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return x;
}
uint32_t Hash(int x, int y, int z, uint32_t seed)
{
	return Hash(uint32_t(x) * 73856093u ^ Hash(uint32_t(y) * 19349663u ^ Hash(uint32_t(z) * 83492791u ^ seed)));
}
float Unit(uint32_t h) { return float(h & 0xffffff) / float(0x1000000); }
int Wrap(int i, int n) { return ((i % n) + n) % n; }
float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// Gradient noise repeating every `period` (x, y, z in cells), about -1..1.
float Perlin(float x, float y, float z, int period, uint32_t seed)
{
	const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
	const float fx = x - ix, fy = y - iy, fz = z - iz;
	auto corner = [&](int dx, int dy, int dz) {
		static const float kDirs[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
		                                   {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
		const float *g = kDirs[Hash(Wrap(ix + dx, period), Wrap(iy + dy, period), Wrap(iz + dz, period), seed) % 12];
		return g[0] * (fx - dx) + g[1] * (fy - dy) + g[2] * (fz - dz);
	};
	const float u = Fade(fx), v = Fade(fy), w = Fade(fz);
	return Lerp(Lerp(Lerp(corner(0, 0, 0), corner(1, 0, 0), u), Lerp(corner(0, 1, 0), corner(1, 1, 0), u), v),
	            Lerp(Lerp(corner(0, 0, 1), corner(1, 0, 1), u), Lerp(corner(0, 1, 1), corner(1, 1, 1), u), v), w);
}

// Octaves of it from `period` cells across the tile (u, v, w 0..1), each
// finer and fainter; not normalised (it reaches past ±1, for the billows).
float PerlinFbm(float u, float v, float w, int period, int octaves, uint32_t seed)
{
	float sum = 0.0f, amp = 1.0f;
	for (int o = 0; o < octaves; o++, period *= 2, amp *= 0.5548f) // 2^-0.85
		sum += amp * Perlin(u * period, v * period, w * period, period, seed + o);
	return sum;
}

// One random point in each of `cells`³ cells (repeating): 1 at a point, 0 a cell away.
float Worley(float u, float v, float w, int cells, uint32_t seed)
{
	const float x = u * cells, y = v * cells, z = w * cells;
	const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
	float best = 1e9f;
	for (int dz = -1; dz <= 1; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++) {
				const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
				const uint32_t h = Hash(Wrap(cx, cells), Wrap(cy, cells), Wrap(cz, cells), seed);
				const float px = cx + Unit(h) - x, py = cy + Unit(Hash(h)) - y, pz = cz + Unit(Hash(h ^ 0x9e3779b9u)) - z;
				best = std::min(best, px * px + py * py + pz * pz);
			}
	return 1.0f - std::min(std::sqrt(best), 1.0f);
}
float WorleyFbm(float u, float v, float w, int cells, uint32_t seed)
{
	return Worley(u, v, w, cells, seed) * 0.625f + Worley(u, v, w, cells * 2, seed + 1) * 0.25f +
	       Worley(u, v, w, cells * 4, seed + 2) * 0.125f;
}

uint8_t Byte(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// Runs fn(row) for rows 0..n-1 over the cores.
void Rows(int n, const std::function<void(int)> &fn)
{
	const int threads = std::max(1u, std::thread::hardware_concurrency());
	std::vector<std::thread> pool;
	for (int t = 0; t < threads; t++)
		pool.emplace_back([&, t] {
			for (int r = t; r < n; r += threads)
				fn(r);
		});
	for (std::thread &th : pool)
		th.join();
}

// Spreads one channel evenly over 0..1 by rank, so a cover of 0.4 covers 40% of the sky.
void Equalise(std::vector<uint8_t> &rgba, int channel, const std::vector<float> &values)
{
	std::vector<int> order(values.size());
	std::iota(order.begin(), order.end(), 0);
	std::sort(order.begin(), order.end(), [&](int a, int b) { return values[a] < values[b]; });
	for (size_t rank = 0; rank < order.size(); rank++)
		rgba[order[rank] * 4 + channel] = Byte(float(rank) / float(order.size() - 1));
}

// --- The same clouds on the CPU (as the GLSL's texture(), repeating) --------

float Sample3(const std::vector<uint8_t> &v, int size, float u, float w, float s, int channel)
{
	const float x = u * size - 0.5f, y = w * size - 0.5f, z = s * size - 0.5f;
	const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
	const float fx = x - ix, fy = y - iy, fz = z - iz;
	auto at = [&](int dx, int dy, int dz) {
		return v[((size_t(Wrap(iz + dz, size)) * size + Wrap(iy + dy, size)) * size + Wrap(ix + dx, size)) * 4 + channel] /
		       255.0f;
	};
	return Lerp(Lerp(Lerp(at(0, 0, 0), at(1, 0, 0), fx), Lerp(at(0, 1, 0), at(1, 1, 0), fx), fy),
	            Lerp(Lerp(at(0, 0, 1), at(1, 0, 1), fx), Lerp(at(0, 1, 1), at(1, 1, 1), fx), fy), fz);
}
float Sample2(const std::vector<uint8_t> &v, int size, float u, float w, int channel)
{
	const float x = u * size - 0.5f, y = w * size - 0.5f;
	const int ix = int(std::floor(x)), iy = int(std::floor(y));
	const float fx = x - ix, fy = y - iy;
	auto at = [&](int dx, int dy) {
		return v[(size_t(Wrap(iy + dy, size)) * size + Wrap(ix + dx, size)) * 4 + channel] / 255.0f;
	};
	return Lerp(Lerp(at(0, 0), at(1, 0), fx), Lerp(at(0, 1), at(1, 1), fx), fy);
}
float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
float Remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }
float Smoothstep(float a, float b, float v)
{
	const float t = Saturate((v - a) / (b - a));
	return t * t * (3.0f - 2.0f * t);
}

// As Density() in the GLSL: keep the two in step.
float Density(const Noise &n, const Weather &weather, float px, float py, float h)
{
	if (h <= 0.0f || h >= 1.0f)
		return 0.0f;
	float wx, wy;
	Wind(weather.time, wx, wy);
	const float qx = px + wx, qy = py + wy;
	// The weather map leans with height too (so their sides do), and where
	// they are narrows near the bottom and the top: rounded undersides and
	// domes, not flat-bottomed blocks with straight sides.
	const float lx = (qx + kLeanX * h) / kWeatherTile, ly = (qy + kLeanY * h) / kWeatherTile;
	const float type = Saturate(Sample2(n.weather, kWeatherSize, lx, ly, 1) + weather.rain * 0.5f);
	const float top = Lerp(0.3f, 1.0f, type), up = Saturate(h / top);
	const float cover = weather.cover;
	const float coverage = Smoothstep(1.0f - cover, 1.0f - cover + 0.3f, Sample2(n.weather, kWeatherSize, lx, ly, 0)) *
	                       Smoothstep(0.0f, kRoundUnder, h) * (1.0f - Smoothstep(kRoundOver, 1.0f, up));
	if (coverage <= 0.0f)
		return 0.0f;
	const float su = (qx + kLeanX * h) / kShapeTile, sv = (qy + kLeanY * h) / kShapeTile,
	            sw = (h * (kTop - kBase) + weather.time * 0.5f) / kShapeTile;
	const float fbm = Sample3(n.shape, kShapeSize, su, sv, sw, 1) * 0.625f + Sample3(n.shape, kShapeSize, su, sv, sw, 2) * 0.25f +
	                  Sample3(n.shape, kShapeSize, su, sv, sw, 3) * 0.125f;
	const float profile = Smoothstep(0.0f, 0.07f, h) * (1.0f - up * up);
	float base = Remap(Sample3(n.shape, kShapeSize, su, sv, sw, 0), fbm - 1.0f, 1.0f, 0.0f, 1.0f) * profile;
	base = Saturate(Remap(base, 1.0f - coverage, 1.0f, 0.0f, 1.0f)) * coverage;
	if (base <= 0.0f)
		return 0.0f;
	const float k = kShapeTile / kDetailTile;
	const float dw = sw * k + weather.time * 0.002f;
	const float dfbm = Sample3(n.detail, kDetailSize, su * k, sv * k, dw, 0) * 0.625f +
	                   Sample3(n.detail, kDetailSize, su * k, sv * k, dw, 1) * 0.25f +
	                   Sample3(n.detail, kDetailSize, su * k, sv * k, dw, 2) * 0.125f;
	const float erode = Lerp(dfbm, 1.0f - dfbm, Saturate(h * 6.0f)) * kErode;
	return Saturate(Remap(base, erode, 1.0f, 0.0f, 1.0f));
}

std::string Constants()
{
	char s[1024];
	snprintf(s, sizeof s,
	         "const float kPlanet = %.1f, kBase = %.1f, kTop = %.1f;\n"
	         "const float kShapeTile = %.1f, kDetailTile = %.1f, kWeatherTile = %.1f;\n"
	         "const vec2 kLean = vec2(%.1f, %.1f);\n"
	         "const float kSigma = %.4f, kLightAbsorption = %.3f, kErode = %.3f;\n"
	         "const float kRoundUnder = %.3f, kRoundOver = %.3f;\n"
	         "const float kMaxDistance = %.1f, kMaxSpan = %.1f, kHaze = %.1f;\n"
	         "const float kStride = %.1f, kStrideGrowth = %.4f;\n"
	         "const int kSteps = %d, kLightSteps = %d, kShapeSize = %d, kDetailSize = %d;\n",
	         kPlanet, kBase, kTop, kShapeTile, kDetailTile, kWeatherTile, kLeanX, kLeanY, kSigma, kLightAbsorption, kErode, kRoundUnder, kRoundOver, kMaxDistance, kMaxSpan,
	         kHaze, kStride, kStrideGrowth, kSteps, kLightSteps, kShapeSize, kDetailSize);
	return s;
}

const char *const kMarch = R"(
uniform mediump sampler3D u_shape;
uniform mediump sampler3D u_detail;
uniform sampler2D u_weather;
uniform vec4 u_cloudView;   // time s, cover 0..1, rain 0..1, frame number
uniform vec4 u_cloudWind;   // how far the wind has carried them (m, x and y), -, the clouds' height in pixels
uniform vec4 u_cloudSun;    // towards the light (the sun, or the moon at night), how strong it is
uniform vec4 u_cloudSunCol; // its colour
uniform vec4 u_cloudSky;    // the sky overhead
uniform vec4 u_cloudLow;    // the horizon (the haze)
FSIN vec3 v_near;
FSIN vec3 v_far;
FSIN float v_span;

float Saturate(float x) { return clamp(x, 0.0, 1.0); }
float Remap(float v, float a, float b, float c, float d) { return c + (v - a) / (b - a) * (d - c); }

// Height (0 at the layer's base, 1 at its top) of a point, over the curve of
// the earth below the camera at camXY.
float LayerHeight(vec3 p, vec2 camXY)
{
	vec2 off = p.xy - camXY;
	return (p.z + dot(off, off) * (0.5 / kPlanet) - kBase) / (kTop - kBase);
}

// Where a ray from altitude alt going up dz crosses the sphere r m above the
// sea: near and far (m), both negative if it doesn't.
vec2 Shell(float alt, float dz, float r)
{
	float b = (kPlanet + alt) * dz;
	float c = (alt - r) * (2.0 * kPlanet + alt + r);
	float disc = b * b - c;
	if (disc < 0.0)
		return vec2(-1.0);
	float q = -b - (b >= 0.0 ? 1.0 : -1.0) * sqrt(disc);
	return vec2(min(q, c / q), max(q, c / q));
}

// As Density() on the CPU (clouds.cpp): keep the two in step. The noise is
// read at the level of detail for `size` m (the GPU's own choice isn't
// defined in a loop that ends differently for each pixel).
float Density(vec3 p, float h, bool detail, float size)
{
	if (h <= 0.0 || h >= 1.0)
		return 0.0;
	vec2 q = p.xy + u_cloudWind.xy;
	// The weather map leans with height too (so their sides do), and where
	// they are narrows near the bottom and the top: rounded undersides and
	// domes, not flat-bottomed blocks with straight sides.
	vec2 weather = textureLod(u_weather, (q + kLean * h) / kWeatherTile, 0.0).rg;
	float type = Saturate(weather.g + u_cloudView.z * 0.5);
	float top = mix(0.3, 1.0, type), up = Saturate(h / top);
	float cover = u_cloudView.y;
	float coverage = smoothstep(1.0 - cover, 1.0 - cover + 0.3, weather.r) * smoothstep(0.0, kRoundUnder, h) *
	                 (1.0 - smoothstep(kRoundOver, 1.0, up));
	if (coverage <= 0.0)
		return 0.0;
	vec3 s3 = vec3(q + kLean * h, h * (kTop - kBase) + u_cloudView.x * 0.5) / kShapeTile;
	vec4 s = textureLod(u_shape, s3, max(log2(size * float(kShapeSize) / kShapeTile), 0.0));
	float fbm = s.g * 0.625 + s.b * 0.25 + s.a * 0.125;
	// Narrowing upwards into domes and towers (lower for flatter kinds).
	float profile = smoothstep(0.0, 0.07, h) * (1.0 - up * up);
	float base = Remap(s.r, fbm - 1.0, 1.0, 0.0, 1.0) * profile;
	base = Saturate(Remap(base, 1.0 - coverage, 1.0, 0.0, 1.0)) * coverage;
	if (base <= 0.0 || !detail)
		return base;
	// Eaten away at the edges: wisps at the bottom, billows higher up.
	float k = kShapeTile / kDetailTile;
	vec3 d = textureLod(u_detail, vec3(s3.xy * k, s3.z * k + u_cloudView.x * 0.002),
	                    max(log2(size * float(kDetailSize) / kDetailTile), 0.0)).rgb;
	float dfbm = d.r * 0.625 + d.g * 0.25 + d.b * 0.125;
	float erode = mix(dfbm, 1.0 - dfbm, Saturate(h * 6.0)) * kErode;
	return Saturate(Remap(base, erode, 1.0, 0.0, 1.0));
}

// The cloud between a point and the light (optical depth), in steps growing
// to reach 1.3 km: the billows nearby (with their detail: they shade each
// other), then the bulk of the cloud.
float LightDepth(vec3 p, vec3 L, vec2 camXY)
{
	float depth = 0.0, stride = 20.0, along = 0.0;
	for (int i = 0; i < kLightSteps; i++) {
		along += stride * 0.5;
		vec3 q = p + L * along;
		depth += Density(q, LayerHeight(q, camXY), i < 2, stride * 0.5) * stride;
		along += stride * 0.5;
		stride *= 2.5;
	}
	return depth * kSigma * kLightAbsorption;
}

// Henyey-Greenstein, 1 where it scatters evenly; two lobes: forwards (the
// silver lining looking towards the sun) and a little back.
float HG(float c, float g)
{
	float g2 = g * g;
	return (1.0 - g2) / pow(1.0 + g2 - 2.0 * g * c, 1.5);
}
float Phase(float c, float k) { return mix(HG(c, 0.6 * k), HG(c, -0.25 * k), 0.3); }

float Hash(vec2 p)
{
	vec3 q = fract(p.xyx * 0.1031);
	q += dot(q, q.yzx + 33.33);
	return fract((q.x + q.y) * q.z);
}

void main(void)
{
	vec3 ro = v_near, rd = normalize(v_far - v_near);
	float alt = ro.z; // Vice City's sea is at 0
	vec2 base = Shell(alt, rd.z, kBase), top = Shell(alt, rd.z, kTop);
	float t0 = -1.0, t1 = -1.0;
	if (alt < kBase) {
		t0 = base.y;
		t1 = top.y;
	} else if (alt < kTop) {
		t0 = 0.0;
		t1 = base.x > 0.0 ? base.x : top.y;
	} else if (top.x > 0.0) {
		t0 = top.x;
		t1 = base.x > 0.0 ? base.x : top.y;
	}
	t1 = min(t1, t0 + kMaxSpan);

	vec3 scat = vec3(0.0);
	float T = 1.0, tSum = 0.0, wSum = 0.0;
	if (t0 >= 0.0 && t0 < kMaxDistance && t1 > t0) {
		vec3 L = normalize(u_cloudSun.xyz);
		float cosT = dot(rd, L);
		float rain = u_cloudView.z;
		vec3 sun = u_cloudSunCol.rgb * u_cloudSun.w * (1.0 - 0.6 * rain) * 1.6;
		// How wide a pixel is per m away (the detail is read at that size).
		float pixel = v_span / u_cloudWind.w;
		float sigma = kSigma * (1.0 + rain);
		// Long cheap steps through clear sky (no detail); in cloud, back a
		// step and on in third steps with the detail, until it's clear
		// again for a while. Steps grow with distance (they cover as much of
		// the screen), and start somewhere else each frame (the frames are
		// averaged).
		float t = t0 + max(kStride, t0 * kStrideGrowth) * fract(Hash(gl_FragCoord.xy) + u_cloudView.w * 0.61803399);
		bool inside = false;
		int clear = 0;
		for (int i = 0; i < kSteps; i++) {
			if (t > t1)
				break;
			float stride = max(kStride, t * kStrideGrowth);
			vec3 p = ro + rd * t;
			float h = LayerHeight(p, ro.xy);
			if (!inside) {
				if (Density(p, h, false, stride) > 0.0) {
					inside = true;
					clear = 0;
					t = max(t - stride * 0.75, t0);
				} else {
					t += stride;
				}
				continue;
			}
			stride *= 0.34;
			float d = Density(p, h, true, t * pixel);
			if (d > 0.003) {
				clear = 0;
				float lightDepth = LightDepth(p, L, ro.xy) * (1.0 + rain);
				// Light scattered many times (Wrenninge): each octave
				// fainter, reaching deeper, less forward.
				float lum = 0.0, a = 1.0, b = 1.0, c = 1.0;
				for (int o = 0; o < 3; o++) {
					lum += b * Phase(cosT, c) * exp(-lightDepth * a);
					a *= 0.3;
					b *= 0.55;
					c *= 0.5;
				}
				// Darker edges facing away from the sun ("powder").
				float powder = mix(1.0 - exp(-d * 8.0), 1.0, Saturate(cosT * 0.5 + 0.5));
				vec3 ambient = mix(u_cloudLow.rgb * 0.7, u_cloudSky.rgb, h) * (0.5 + 0.5 * h);
				vec3 light = sun * lum * powder + ambient;
				light = mix(light, vec3(dot(light, vec3(0.3, 0.5, 0.2))) * 0.8, rain * 0.6);
				float trans = exp(-d * sigma * stride);
				float took = T * (1.0 - trans);
				scat += took * light;
				tSum += took * t;
				wSum += took;
				T *= trans;
				if (T < 0.02)
					break;
			} else if (++clear > 6) {
				inside = false;
			}
			t += stride;
		}
	}
	float alpha = 1.0 - T;
	float tRep = wSum > 0.0 ? tSum / wSum : (t0 > 0.0 ? t0 : 20000.0);
	scat = mix(u_cloudLow.rgb * alpha, scat, exp(-tRep / kHaze)); // into the haze with distance
	FRAGCOLOR(vec4(scat, alpha));
}
)";

// Each frame marches one pixel of every 2x2 (u_march, half the size each
// way); the rest come from the last frame, where their clouds were then
// (taken as mid-layer: they're far enough that it hardly matters). The
// marched pixel is blended with its own past too, which smooths the noise.
const char *const kResolve = R"(
uniform sampler2D u_march;   // this frame's quarter of the pixels
uniform sampler2D u_history; // last frame's, all of them
uniform vec4 u_cloudPick;    // which pixel of each 2x2 was marched (x, y), how much of the past to keep
uniform mat4 u_cloudPrev;    // last frame's projection * view
FSIN vec3 v_near;
FSIN vec3 v_far;
FSIN vec2 v_screen;

vec2 Shell(float alt, float dz, float r)
{
	float b = (kPlanet + alt) * dz;
	float c = (alt - r) * (2.0 * kPlanet + alt + r);
	float disc = b * b - c;
	if (disc < 0.0)
		return vec2(-1.0);
	float q = -b - (b >= 0.0 ? 1.0 : -1.0) * sqrt(disc);
	return vec2(min(q, c / q), max(q, c / q));
}

void main(void)
{
	ivec2 px = ivec2(gl_FragCoord.xy);
	vec3 ro = v_near, rd = normalize(v_far - v_near);
	vec2 mid = Shell(ro.z, rd.z, (kBase + kTop) * 0.5);
	float t = mid.y > 0.0 ? (mid.x > 0.0 ? mid.x : mid.y) : 20000.0;
	vec4 was = u_cloudPrev * vec4(ro + rd * min(t, kMaxDistance), 1.0);
	vec2 uv = was.xy / max(was.w, 1e-4) * 0.5 + 0.5;
	bool have = u_cloudPick.z > 0.0 && was.w > 0.0 && uv == clamp(uv, 0.0, 1.0);
	vec4 old = have ? textureLod(u_history, uv, 0.0) : textureLod(u_march, v_screen, 0.0);
	if ((px.x & 1) == int(u_cloudPick.x) && (px.y & 1) == int(u_cloudPick.y)) {
		vec4 fresh = texelFetch(u_march, px / 2, 0);
		FRAGCOLOR(have ? mix(fresh, old, u_cloudPick.z) : fresh);
	} else {
		FRAGCOLOR(old);
	}
}
)";

} // namespace

const char *const kVertex = R"(
uniform vec4 u_xform;
uniform vec4 u_cloudJitter; // where the rays go, moved from the pixels' centres (clip units)
VSIN(ATTRIB_POS) vec4 in_pos;
VSOUT vec3 v_near;
VSOUT vec3 v_far;
VSOUT vec2 v_screen;
VSOUT float v_span; // the view's height 1 m away
void main(void)
{
	gl_Position = in_pos;
	gl_Position.w = 1.0;
	gl_Position.xy = gl_Position.xy * u_xform.xy + u_xform.zw;
	// The corners' view rays, near and far: flat across the screen, so they interpolate exactly.
	mat4 back = inverse(u_proj * u_view);
	vec2 at = gl_Position.xy + u_cloudJitter.xy;
	vec4 a = back * vec4(at, -1.0, 1.0), b = back * vec4(at, 1.0, 1.0);
	v_near = a.xyz / a.w;
	v_far = b.xyz / b.w;
	v_screen = gl_Position.xy * 0.5 + 0.5;
	v_span = 2.0 / u_proj[1][1];
}
)";

const char *const kCompositeFragment = R"(
uniform sampler2D u_clouds;
FSIN vec2 v_screen;
void main(void)
{
	vec4 c = texture(u_clouds, v_screen); // premultiplied
	if (c.a < 0.004)
		discard;
	FRAGCOLOR(c);
}
)";

std::string MarchFragment() { return Constants() + kMarch; }
std::string ResolveFragment() { return Constants() + kResolve; }

void Wind(float time, float &x, float &y)
{
	// Wrapped a whole number of weather tiles, so it stays precise.
	x = std::fmod(time * kWindX, kWeatherTile * 4.0f);
	y = std::fmod(time * kWindY, kWeatherTile * 4.0f);
}

Noise MakeNoise()
{
	Noise n;
	n.shape.resize(size_t(kShapeSize) * kShapeSize * kShapeSize * 4);
	Rows(kShapeSize, [&](int z) {
		for (int y = 0; y < kShapeSize; y++)
			for (int x = 0; x < kShapeSize; x++) {
				const float u = (x + 0.5f) / kShapeSize, v = (y + 0.5f) / kShapeSize, w = (z + 0.5f) / kShapeSize;
				const float g = WorleyFbm(u, v, w, 4, 11), b = WorleyFbm(u, v, w, 8, 21), a = WorleyFbm(u, v, w, 16, 31);
				const float billows = std::fabs(PerlinFbm(u, v, w, 4, 7, 1)); // billowy Perlin
				uint8_t *p = &n.shape[((size_t(z) * kShapeSize + y) * kShapeSize + x) * 4];
				p[0] = Byte(Remap(billows, 0.0f, 1.0f, g, 1.0f)); // Perlin-Worley
				p[1] = Byte(g);
				p[2] = Byte(b);
				p[3] = Byte(a);
			}
	});
	n.detail.resize(size_t(kDetailSize) * kDetailSize * kDetailSize * 4);
	Rows(kDetailSize, [&](int z) {
		for (int y = 0; y < kDetailSize; y++)
			for (int x = 0; x < kDetailSize; x++) {
				const float u = (x + 0.5f) / kDetailSize, v = (y + 0.5f) / kDetailSize, w = (z + 0.5f) / kDetailSize;
				uint8_t *p = &n.detail[((size_t(z) * kDetailSize + y) * kDetailSize + x) * 4];
				p[0] = Byte(WorleyFbm(u, v, w, 2, 41));
				p[1] = Byte(WorleyFbm(u, v, w, 4, 51));
				p[2] = Byte(WorleyFbm(u, v, w, 8, 61));
				p[3] = 255;
			}
	});
	// The weather map is flat: z stays at 0 (the noise repeats in it anyway).
	const size_t texels = size_t(kWeatherSize) * kWeatherSize;
	std::vector<float> coverage(texels), type(texels);
	Rows(kWeatherSize, [&](int y) {
		for (int x = 0; x < kWeatherSize; x++) {
			const float u = (x + 0.5f) / kWeatherSize, v = (y + 0.5f) / kWeatherSize;
			coverage[size_t(y) * kWeatherSize + x] = PerlinFbm(u, v, 0.0f, 4, 6, 71) * 0.7f + Worley(u, v, 0.0f, 6, 81) * 0.3f;
			type[size_t(y) * kWeatherSize + x] = PerlinFbm(u, v, 0.5f, 2, 4, 91);
		}
	});
	n.weather.assign(texels * 4, 255);
	Equalise(n.weather, 0, coverage);
	Equalise(n.weather, 1, type);
	for (size_t i = 0; i < texels; i++)
		n.weather[i * 4 + 2] = 0;
	return n;
}

float Transmittance(const Noise &noise, const Weather &weather, const float from[3], const float dir[3])
{
	if (dir[2] <= 0.01f)
		return 1.0f; // under the horizon: the layer is too far away to matter
	// Through the layer (flat is close enough for the sun's one ray), as far as
	// the GLSL marches.
	const float t0 = std::max(0.0f, (kBase - from[2]) / dir[2]);
	const float t1 = std::min((kTop - from[2]) / dir[2], t0 + kMaxSpan);
	if (t1 <= t0)
		return 1.0f;
	constexpr int kStepsCpu = 48;
	const float dt = (t1 - t0) / kStepsCpu;
	float depth = 0.0f;
	for (int i = 0; i < kStepsCpu; i++) {
		const float t = t0 + (i + 0.5f) * dt;
		const float z = from[2] + dir[2] * t;
		depth += Density(noise, weather, from[0] + dir[0] * t, from[1] + dir[1] * t, (z - kBase) / (kTop - kBase)) * dt;
	}
	return std::exp(-depth * kSigma * (1.0f + weather.rain));
}

} // namespace clouds
