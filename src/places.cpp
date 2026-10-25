#include "places.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "hud.h"
#include "log.h"
#include "story.h"
#include "text.h"

#include <cmath>
#include <dlfcn.h>

using namespace game;

namespace places {
namespace {

constexpr uint16_t kSpriteForSale = 25;
constexpr uint8_t kNotYetAlpha = 110;
constexpr float kIconUnits = 8.0f; // CRadar::DrawRadarSprite's half size, in 448ths of the screen height
constexpr int kMaxRestarts = 8;
constexpr float kSignNear = 30.0f;
constexpr CRGBA kNeededRing{255, 196, 48, 255};

const CVector *gHospitals, *gPolice;
const uint16_t *gNumHospitals, *gNumPolice;
bool gInstalled;
Im2DCanvas gCanvas;

bool Near(const CVector &p, float x, float y, float d) { return (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y) < d * d; }

void Disc(Vec2 c, float r, CRGBA col)
{
	constexpr int kSeg = 20;
	for (int i = 0; i < kSeg; i++) {
		const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
		gCanvas.Tri({c.x, c.y, 0, 0}, {c.x + r * cosf(a0), c.y + r * sinf(a0), 0, 0},
		            {c.x + r * cosf(a1), c.y + r * sinf(a1), 0, 0}, col);
	}
}

void Rect(float x0, float y0, float x1, float y1, CRGBA col) { gCanvas.Quad({x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, col); }

// A white badge with a red cross (no rim: the game's icons have none).
void Hospital(Vec2 c, float r)
{
	Disc(c, r * 0.9f, {245, 245, 245, 255});
	const float a = r * 0.5f, b = r * 0.17f;
	const CRGBA red{214, 40, 40, 255};
	Rect(c.x - a, c.y - b, c.x + a, c.y + b, red);
	Rect(c.x - b, c.y - a, c.x + b, c.y + a, red);
}

// A blue badge with a white star.
void Police(Vec2 c, float r)
{
	Disc(c, r * 0.9f, {36, 84, 196, 255});
	const float outer = r * 0.6f, inner = outer * 0.42f;
	Vtx v[11];
	v[0] = {c.x, c.y, 0, 0};
	for (int i = 0; i < 10; i++) {
		const float a = -1.5707963f + 3.14159265f * i / 5, rr = i % 2 ? inner : outer;
		v[i + 1] = {c.x + rr * cosf(a), c.y + rr * sinf(a), 0, 0};
	}
	for (int i = 1; i <= 10; i++)
		gCanvas.Tri(v[0], v[i], v[i % 10 + 1], {255, 255, 255, 255});
}

// Thin, round the icon: the story needs this one.
void Ring(Vec2 c, float r, CRGBA col)
{
	constexpr int kSeg = 28;
	const float in = r * 1.18f, out = r * 1.32f;
	for (int i = 0; i < kSeg; i++) {
		const float a0 = 6.2831853f * i / kSeg, a1 = 6.2831853f * (i + 1) / kSeg;
		gCanvas.Quad({c.x + in * cosf(a0), c.y + in * sinf(a0)}, {c.x + out * cosf(a0), c.y + out * sinf(a0)},
		             {c.x + out * cosf(a1), c.y + out * sinf(a1)}, {c.x + in * cosf(a1), c.y + in * sinf(a1)}, col);
	}
}

// "$70,000" on a dark tab under the icon.
void PriceTag(Vec2 at, float iconR, int32_t price)
{
	char label[24];
	hud::FormatMoney(price, label, sizeof label);
	const float s = float(ScreenHeight()) / 448.0f;
	const text::Style style{0, 0.3f * s, 0.6f * s, {255, 255, 255, 255}};
	float top, bottom;
	text::LineExtent(style, 0.0f, top, bottom);
	const float w = text::Width(label, style), pad = 2.5f * s, y = at.y + iconR + pad;
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	Rect(at.x - w / 2 - pad, y, at.x + w / 2 + pad, y + (bottom - top) + pad * 2, {16, 16, 18, 200});
	gCanvas.Flush();
	text::Draw(at.x - w / 2, y + pad - top, label, style);
}

} // namespace

bool Install()
{
	if (!gConfig.places)
		return true;
	gHospitals = static_cast<const CVector *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CRestart21HospitalRestartPointsE"));
	gPolice = static_cast<const CVector *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CRestart19PoliceRestartPointsE"));
	gNumHospitals = static_cast<const uint16_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CRestart24NumberOfHospitalRestartsE"));
	gNumPolice = static_cast<const uint16_t *>(dlsym(RTLD_MAIN_ONLY, "_ZN8CRestart22NumberOfPoliceRestartsE"));
	gInstalled = gHospitals && gPolice && gNumHospitals && gNumPolice;
	return gInstalled;
}

bool Replaces(const Blip &blip)
{
	if (!gInstalled || blip.sprite != kSpriteForSale)
		return false;
	for (const story::Business &b : story::kBusinesses)
		if (Near(blip.pos, b.x, b.y, kSignNear))
			return true;
	return false;
}

void Draw(const std::function<bool(float x, float y, Vec2 &out)> &toScreen, bool prices)
{
	if (!gInstalled)
		return;
	const float r = kIconUnits * float(ScreenHeight()) / 448.0f;
	Vec2 p;
	for (const story::Business &b : story::kBusinesses) {
		if (story::OwnedBlip(b) || !toScreen(b.x, b.y, p))
			continue;
		const uint8_t *sign = story::SaleSign(b);
		api.DrawRadarSprite(b.sprite, p.x, p.y, sign ? 255 : kNotYetAlpha);
		if (sign && prices)
			PriceTag(p, r, story::Price(sign));
	}

	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	for (const story::Business &b : story::kBusinesses) {
		const Blip *owned = story::OwnedBlip(b); // the game's icon, where it puts it
		if (story::Needed(b) && toScreen(owned ? owned->pos.x : b.x, owned ? owned->pos.y : b.y, p))
			Ring(p, r, kNeededRing);
	}
	for (int i = 0; i < std::min<int>(*gNumHospitals, kMaxRestarts); i++)
		if (toScreen(gHospitals[i].x, gHospitals[i].y, p))
			Hospital(p, r);
	for (int i = 0; i < std::min<int>(*gNumPolice, kMaxRestarts); i++)
		if (toScreen(gPolice[i].x, gPolice[i].y, p))
			Police(p, r);
	gCanvas.Flush();
}

} // namespace places
