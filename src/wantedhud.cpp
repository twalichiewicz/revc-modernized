#include "wantedhud.h"
#include "game.h"
#include "heat.h"
#include "police.h"
#include "shapes.h"
#include "sight.h"
#include "wanted.h"
#include "witness.h"

#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <unordered_map>
#include <vector>

using namespace game;

namespace wantedhud {
namespace {

constexpr float kPi = 3.14159265f;
// Sizes at 1080p (as the approved mockup): the icons, the gap between them and
// from the map, the eyes' distance in from the screen's edge.
constexpr float kIcon = 34.0f, kGap = 6.0f, kFromMap = 8.0f, kRim = 1.5f, kMeter = 2.8f;
constexpr CRGBA kDisc{12, 14, 20, 184}, kHole{12, 14, 20, 235}, kWhite{255, 255, 255, 255};
constexpr CRGBA kCallRed{226, 75, 75, 255}, kEyeYellow{240, 192, 64, 255}, kFaint{255, 255, 255, 46};
constexpr CRGBA kSearchFill{80, 130, 255, 46}, kSearchRim{120, 170, 255, 242}, kPoliceBlue{60, 110, 255, 255};
constexpr size_t kPedSize = 0x8d0, kVehicleSize = 0x6f0, kLawFlags = 0x295; // pool entries; CVehicle's law flag (bit 0)
constexpr int32_t kCopType = 6;      // ePedType
constexpr uint8_t kWreckedStatus = 5; // eEntityStatus, bits 3-7 of +0x60
constexpr float kStruckAlpha = 0.38f;
constexpr float kEyeAbove = 0.6f; // m over a cop's head
constexpr float kRingX = 0.30f, kRingY = 0.34f; // the eyes' ring about the middle, as a share of the screen
constexpr float kEase = 14.0f;                  // how fast an eye slides to where it should be (1/s)

bool (*gCalcScreenCoors)(const CVector &in, CVector *out, float *w, float *h, bool farClip);
bool gLookedUp;
uint8_t **gPedPool, **gVehiclePool, **gHelis;
std::unordered_map<int, Vec2> gShown; // where each eye was drawn last frame
uint32_t gLastEyes;

float Scale() { return ScreenHeight() / 1080.0f; }
CRGBA Faded(CRGBA c, float a) { return {c.r, c.g, c.b, uint8_t(c.a * std::clamp(a, 0.0f, 1.0f))}; }

// --- Shapes, in screen px ----------------------------------------------------------------

// (With smooth edges: shapes.cpp.)
void Disc(Canvas &c, Vec2 at, float r, CRGBA col) { shapes::Disc(c, at, r, col); }
void Arc(Canvas &c, Vec2 at, float r, float width, float share, CRGBA col) { shapes::Ring(c, at, r, width, share, col); }
void Line(Canvas &c, Vec2 a, Vec2 b, float width, CRGBA col) { shapes::Line(c, a, b, width, col); }
void Polygon(Canvas &c, const std::vector<Vec2> &p, CRGBA col) { shapes::Fill(c, p, col); }

// --- Glyphs: the mockup's SVG symbols, on a 24-unit box ----------------------------------

struct Box {
	Vec2 at; // the box's centre on screen
	float u; // px a unit
	Vec2 P(float x, float y) const { return {at.x + (x - 12) * u, at.y + (y - 12) * u}; }
};

void Cubic(std::vector<Vec2> &out, const Box &b, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
{
	constexpr int kSteps = 8;
	for (int i = 1; i <= kSteps; i++) {
		const float t = float(i) / kSteps, s = 1 - t;
		const float x = s * s * s * p0.x + 3 * s * s * t * p1.x + 3 * s * t * t * p2.x + t * t * t * p3.x;
		const float y = s * s * s * p0.y + 3 * s * s * t * p1.y + 3 * s * t * t * p2.y + t * t * t * p3.y;
		out.push_back(b.P(x, y));
	}
}

void Shirt(Canvas &c, const Box &b, CRGBA col)
{
	Polygon(c, {b.P(8, 3), b.P(12, 5), b.P(16, 3), b.P(21, 7), b.P(18, 10), b.P(16, 8.5f), b.P(16, 21), b.P(8, 21),
	            b.P(8, 8.5f), b.P(6, 10), b.P(3, 7)},
	        col);
}

void Car(Canvas &c, const Box &b, CRGBA col, CRGBA hole)
{
	Polygon(c, {b.P(5, 11), b.P(7, 6), b.P(17, 6), b.P(19, 11), b.P(20.5f, 11), b.P(20.5f, 17), b.P(3.5f, 17),
	            b.P(3.5f, 11)},
	        col);
	Disc(c, b.P(7, 17), 2 * b.u, col);
	Disc(c, b.P(17, 17), 2 * b.u, col);
	Polygon(c, {b.P(7.5f, 11), b.P(16.5f, 11), b.P(15.3f, 8), b.P(8.7f, 8)}, hole);
}

void Phone(Canvas &c, const Box &b, CRGBA col)
{
	std::vector<Vec2> p = {b.P(7, 3), b.P(11, 3), b.P(12, 8), b.P(9.5f, 9.5f), b.P(11.6f, 12.4f), b.P(14.5f, 14.5f),
	                       b.P(16, 12), b.P(21, 13), b.P(21, 17)};
	Cubic(p, b, {21, 17}, {21, 18}, {20, 19}, {19, 19});
	Cubic(p, b, {19, 19}, {10.7f, 19}, {5, 13.3f}, {5, 5});
	Cubic(p, b, {5, 5}, {5, 4}, {6, 3}, {7, 3});
	p.pop_back(); // (back where it began)
	Polygon(c, p, col);
}

void Magnifier(Canvas &c, const Box &b, CRGBA col)
{
	Arc(c, b.P(10, 10), 6 * b.u, 2.4f * b.u, 1.0f, col);
	Line(c, b.P(14.5f, 14.5f), b.P(20, 20), 2.6f * b.u, col);
	Disc(c, b.P(20, 20), 1.3f * b.u, col); // its round end
}

void Badge(Canvas &c, const Box &b, CRGBA col)
{
	Polygon(c, {b.P(12, 2), b.P(14.2f, 4.6f), b.P(17.6f, 4.2f), b.P(18, 7.6f), b.P(20.6f, 10), b.P(18, 12),
	            b.P(20.6f, 14), b.P(18, 16.4f), b.P(17.6f, 19.8f), b.P(14.2f, 19.4f), b.P(12, 22), b.P(9.8f, 19.4f),
	            b.P(6.4f, 19.8f), b.P(6, 16.4f), b.P(3.4f, 14), b.P(6, 12), b.P(3.4f, 10), b.P(6, 7.6f), b.P(6.4f, 4.2f),
	            b.P(9.8f, 4.6f)},
	        col);
}

void EyeGlyph(Canvas &c, const Box &b, CRGBA col, CRGBA hole)
{
	std::vector<Vec2> p = {b.P(12, 5)};
	Cubic(p, b, {12, 5}, {6.5f, 5}, {3, 12}, {3, 12});
	Cubic(p, b, {3, 12}, {3, 12}, {6.5f, 19}, {12, 19});
	Cubic(p, b, {12, 19}, {17.5f, 19}, {21, 12}, {21, 12});
	Cubic(p, b, {21, 12}, {21, 12}, {17.5f, 5}, {12, 5});
	p.pop_back();
	Polygon(c, p, col);
	Disc(c, b.P(12, 12), 4 * b.u, hole);
	Disc(c, b.P(12, 12), 2 * b.u, col);
}

enum class Glyph { Phone, Search, Badge, Outfit, Car, Eye };

// A round icon `d` across: the dark disc, a white rim, the glyph; struck, it's
// faded under a red diagonal; `meter` (0..1, < 0 none) is a ring round it.
void Icon(Canvas &c, Vec2 at, float d, Glyph g, bool struck, float meter, CRGBA meterColour)
{
	const float s = Scale(), r = d / 2, a = struck ? kStruckAlpha : 1.0f;
	Disc(c, at, r, Faded(kDisc, a));
	Arc(c, at, r - kRim * s / 2, kRim * s, 1.0f, Faded(kWhite, a));
	const Box b{at, d * 0.56f / 24.0f};
	const CRGBA col = Faded(kWhite, a), hole = Faded(kHole, a);
	switch (g) {
	case Glyph::Phone: Phone(c, b, col); break;
	case Glyph::Search: Magnifier(c, b, col); break;
	case Glyph::Badge: Badge(c, b, col); break;
	case Glyph::Outfit: Shirt(c, b, col); break;
	case Glyph::Car: Car(c, b, col, hole); break;
	case Glyph::Eye: EyeGlyph(c, b, col, hole); break;
	}
	if (meter >= 0.0f) {
		Arc(c, at, r - kMeter * s / 2, kMeter * s, 1.0f, kFaint);
		if (meter > 0.0f)
			Arc(c, at, r - kMeter * s / 2, kMeter * s, std::min(meter, 1.0f), meterColour);
	}
	if (struck)
		Line(c, {at.x + r * 0.62f, at.y - r * 0.62f}, {at.x - r * 0.62f, at.y + r * 0.62f}, 2.5f * s, kCallRed);
}

template <typename Each> void ForEach(uint8_t **pool, size_t stride, Each &&each)
{
	const uint8_t *p = pool ? *pool : nullptr;
	if (!p)
		return;
	uint8_t *entries = *reinterpret_cast<uint8_t *const *>(p);
	const uint8_t *flags = *reinterpret_cast<const uint8_t *const *>(p + 0x8);
	const int32_t size = *reinterpret_cast<const int32_t *>(p + 0x10);
	for (int i = 0; i < size; i++)
		if (!(flags[i] & 0x80))
			each(entries + size_t(i) * stride);
}

// From a police car, the other police on the map: their cars and helicopters
// (bigger), cops on foot (small), and the units out in the city (rings).
void Police(Canvas &canvas, const std::function<bool(float x, float y, Vec2 &out)> &toScreen)
{
	const float s = Scale();
	const uint8_t *player = api.FindPlayerPed();
	const uint8_t *mine = player && PedInVehicle(player) ? PedVehicle(player) : nullptr;
	auto dot = [&](const CVector &at, float r, bool ring) {
		Vec2 p;
		if (!toScreen(at.x, at.y, p))
			return;
		Disc(canvas, p, r + 1.2f * s, kWhite);
		Disc(canvas, p, r, kPoliceBlue);
		if (ring)
			Disc(canvas, p, r * 0.45f, kWhite);
	};
	for (const CVector &at : police::UnitsAbout())
		dot(at, 3.5f * s, true);
	ForEach(gVehiclePool, kVehicleSize, [&](const uint8_t *car) {
		if (car != mine && (car[kLawFlags] & 1) && (car[0x60] >> 3) != kWreckedStatus)
			dot(EntityPos(car), 4.5f * s, false);
	});
	ForEach(gPedPool, kPedSize, [&](const uint8_t *ped) {
		if (PedType(ped) == kCopType && !PedInVehicle(ped) && Health(ped) > 0.0f)
			dot(EntityPos(ped), 2.5f * s, false);
	});
	for (int i = 0; gHelis && i < 4; i++)
		if (const uint8_t *heli = gHelis[i])
			dot(EntityPos(heli), 6.0f * s, false);
}

struct Shown {
	Glyph glyph;
	bool struck;
	float meter;
};

std::vector<Shown> Icons()
{
	std::vector<Shown> icons;
	if (!wanted::Active())
		return icons;
	const heat::Model &m = wanted::Model();
	if (const float call = witness::Calling(); call > 0.0f)
		icons.push_back({Glyph::Phone, false, call});
	if (m.state() == heat::State::Searching)
		icons.push_back({Glyph::Search, false, -1.0f});
	if (m.state() == heat::State::Apb)
		icons.push_back({Glyph::Badge, false, -1.0f});
	if (m.state() == heat::State::Clear || m.state() == heat::State::Reported)
		return icons;
	const heat::Looks looks = wanted::Looks();
	if (m.description().haveOutfit)
		icons.push_back({Glyph::Outfit, m.OutfitStruck(looks), -1.0f});
	if (m.description().haveVehicle)
		icons.push_back({Glyph::Car, m.VehicleStruck(looks), -1.0f});
	return icons;
}

void LookUp()
{
	if (gLookedUp)
		return;
	gLookedUp = true;
	gCalcScreenCoors = reinterpret_cast<decltype(gCalcScreenCoors)>(
	    dlsym(RTLD_MAIN_ONLY, "_ZN7CSprite15CalcScreenCoorsERKN2rw3V3dEPS1_PfS5_b"));
	gPedPool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools11ms_pPedPoolE"));
	gVehiclePool = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN6CPools15ms_pVehiclePoolE"));
	gHelis = static_cast<uint8_t **>(dlsym(RTLD_MAIN_ONLY, "_ZN5CHeli6pHelisE"));
}

} // namespace

float RowHeight(float)
{
	return Icons().empty() ? 0.0f : kIcon * Scale();
}

void Draw(Canvas &canvas, float mapX, float mapY, float mapW, float mapH)
{
	const float s = Scale(), d = kIcon * s;
	float x = mapX + mapW + kFromMap * s + d / 2;
	const float y = mapY + mapH - d / 2;
	canvas.SetTexture(nullptr);
	for (const Shown &icon : Icons()) {
		Icon(canvas, {x, y}, d, icon.glyph, icon.struck, icon.meter, kCallRed);
		x += d + kGap * s;
	}
	canvas.Flush();
}

void DrawEyes(Canvas &canvas)
{
	LookUp();
	if (!gCalcScreenCoors || !wanted::Active())
		return;
	const uint32_t now = *api.timeMs;
	const float dt = gLastEyes && now >= gLastEyes ? std::min(now - gLastEyes, 100u) / 1000.0f : 0.0f;
	gLastEyes = now;
	if (sight::Eyes().empty()) {
		gShown.clear();
		return;
	}
	const float s = Scale(), d = kIcon * s, W = float(ScreenWidth()), H = float(ScreenHeight());
	// The ring they sit on once the cop's off towards the edge (or behind), as
	// damage indicators do: always on screen, at his bearing.
	const float rx = W * kRingX, ry = H * kRingY;
	const CVector camera = *reinterpret_cast<const CVector *>(api.theCamera + 0x30); // (its matrix is first: no vtable)
	std::unordered_map<int, Vec2> shown;
	canvas.SetTexture(nullptr);
	for (const sight::Eye &eye : sight::Eyes()) {
		// Where the cop is this frame (sight looks only every 50 ms).
		CVector at = eye.head;
		if (eye.handle >= 0)
			if (const uint8_t *ped = api.PoolsGetPed(eye.handle))
				at = PedInVehicle(ped) ? EntityPos(PedVehicle(ped)) : EntityPos(ped);
		at.z += eye.handle >= 0 ? kEyeAbove + 0.8f : kEyeAbove;
		CVector out{0, 0, 0};
		float w = 0, h = 0;
		const bool ahead = gCalcScreenCoors(at, &out, &w, &h, false);
		Vec2 dir{out.x - W / 2, out.y - H / 2};
		if (!ahead) {
			const float turn = std::remainder(HeadingOf(at.x - camera.x, at.y - camera.y) - CameraHeading(), 2 * kPi);
			dir = {-std::sin(turn), -std::cos(turn)};
		}
		const float e = std::sqrt((dir.x * dir.x) / (rx * rx) + (dir.y * dir.y) / (ry * ry));
		const bool onRing = !ahead || e > 1.0f;
		Vec2 p = onRing && e > 1e-4f ? Vec2{W / 2 + dir.x / e, H / 2 + dir.y / e} : Vec2{out.x, out.y};
		// Eased, so it slides rather than jumps.
		if (auto it = gShown.find(eye.handle); it != gShown.end() && dt > 0.0f) {
			const float k = std::min(1.0f, dt * kEase);
			p = {it->second.x + (p.x - it->second.x) * k, it->second.y + (p.y - it->second.y) * k};
		}
		shown[eye.handle] = p;
		if (onRing) { // a pointer out his way
			const float len = std::max(std::hypot(p.x - W / 2, p.y - H / 2), 1e-3f);
			const Vec2 u{(p.x - W / 2) / len, (p.y - H / 2) / len}, n{-u.y, u.x};
			const float tip = d / 2 + 9.0f * s, base = d / 2 + 2.0f * s, half = 6.0f * s;
			shapes::Fill(canvas, {{p.x + u.x * tip, p.y + u.y * tip}, {p.x + u.x * base + n.x * half, p.y + u.y * base + n.y * half},
			                      {p.x + u.x * base - n.x * half, p.y + u.y * base - n.y * half}},
			             kEyeYellow);
		}
		Icon(canvas, p, d, Glyph::Eye, false, eye.fill, kEyeYellow);
	}
	gShown = std::move(shown);
	canvas.Flush();
}

void DrawOnMap(Canvas &canvas, const std::function<void(const Vec2 *world, int n, CRGBA col)> &fillWorld,
               const std::function<bool(float x, float y, Vec2 &out)> &toScreen)
{
	if (!wanted::Active())
		return;
	const heat::Model &m = wanted::Model();
	canvas.SetTexture(nullptr);
	// The search and the other units: what a police car's radio and screen tell him.
	const bool inPoliceCar = wanted::InPoliceCar();
	if (m.state() == heat::State::Searching && inPoliceCar) {
		// The area they're searching: a tinted disc, its rim dashed.
		constexpr int kSlices = 48, kDashes = 40;
		const Vec2 c{m.searchCentre().x, m.searchCentre().y};
		const float r = m.searchRadius(), rim = std::max(1.5f, *api.radarRange * 0.018f);
		for (int i = 0; i < kSlices; i++) {
			const float a0 = 2 * kPi * i / kSlices, a1 = 2 * kPi * (i + 1) / kSlices;
			const Vec2 slice[3] = {c, {c.x + r * std::cos(a0), c.y + r * std::sin(a0)},
			                       {c.x + r * std::cos(a1), c.y + r * std::sin(a1)}};
			fillWorld(slice, 3, kSearchFill);
		}
		for (int i = 0; i < kDashes; i += 2) {
			const float a0 = 2 * kPi * i / kDashes, a1 = 2 * kPi * (i + 1) / kDashes;
			const float in = r - rim / 2, out = r + rim / 2;
			const Vec2 dash[4] = {{c.x + in * std::cos(a0), c.y + in * std::sin(a0)},
			                      {c.x + out * std::cos(a0), c.y + out * std::sin(a0)},
			                      {c.x + out * std::cos(a1), c.y + out * std::sin(a1)},
			                      {c.x + in * std::cos(a1), c.y + in * std::sin(a1)}};
			fillWorld(dash, 4, kSearchRim);
		}
	}
	if (inPoliceCar) {
		LookUp();
		Police(canvas, toScreen);
	}
	// Witnesses running for a phone, or off to call: red dots.
	const float s = Scale();
	for (const CVector &runner : witness::Runners()) {
		Vec2 p;
		if (!toScreen(runner.x, runner.y, p))
			continue;
		Disc(canvas, p, 5.0f * s, kWhite);
		Disc(canvas, p, 3.5f * s, kCallRed);
	}
	canvas.Flush();
}

} // namespace wantedhud
