#include "wheel.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "log.h"
#include "radiologos.h"
#include "text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

using namespace game;

namespace wheel {
namespace {

enum class Kind { kNone, kWeapons, kRadio };

struct Segment {
	bool present;          // something to pick
	void *icon;            // RwRaster*; without one the name is drawn instead
	const uint16_t *name;  // game text, else `label`
	char label[24];
	char detail[16];       // ammo
	int value;             // weapon slot / radio channel
};

constexpr int kMaxSegments = 12;
constexpr float kSlowMotion = 0.25f;
constexpr float kDeadZone = 0.5f;
constexpr float kPi = 3.14159265f;

// By eWeaponType, in data/weapon.dat order.
constexpr const char *kWeaponNames[] = {
    "Fist",         "Brass Knuckles", "Screwdriver",    "Golf Club",      "Nightstick", "Knife",
    "Baseball Bat", "Hammer",         "Meat Cleaver",   "Machete",        "Katana",     "Chainsaw",
    "Grenades",     "Remote Grenades", "Tear Gas",      "Molotovs",       "Rocket",     "Pistol",
    "Colt Python",  "Shotgun",        "Combat Shotgun", "Stubby Shotgun", "Tec-9",      "Uzi",
    "Silenced Ingram", "MP5",         "M4",             "Ruger",          "Sniper Rifle", "Laser Sniper",
    "Rocket Launcher", "Flamethrower", "M60",           "Minigun",        "Detonator",  "Heli Cannon",
    "Camera"};
constexpr int kNumWeaponNames = sizeof kWeaponNames / sizeof kWeaponNames[0];

constexpr CRGBA kSegment{12, 12, 14, 215}, kSegmentEmpty{12, 12, 14, 140}, kSelectedTint{255, 255, 255, 55};
constexpr CRGBA kCentre{8, 8, 10, 225}, kAccent{255, 255, 255, 240}, kDim{0, 0, 0, 120};

Kind gKind = Kind::kNone;
Segment gSegments[kMaxSegments];
int gCount, gSelected;
float gSavedTimeScale;
uint8_t *gVehicle;
Im2DCanvas gCanvas;
void *gLogos[radiologos::kCount];
bool gLogosLoaded;

void LoadLogos()
{
	if (gLogosLoaded)
		return;
	gLogosLoaded = true;
	radiologos::Logos logos;
	if (!radiologos::Load(SupportDir() + "/vcmodernhud/radio.bin", logos)) {
		Log("wheel: radio.bin missing, station names only (re-run install.sh)");
		return;
	}
	for (int i = 0; i < radiologos::kCount; i++)
		if (!logos[i].empty())
			gLogos[i] = CreateRaster(radiologos::kSize, radiologos::kSize, logos[i]);
}

void SetLabel(Segment &s, const char *ascii)
{
	size_t i = 0;
	for (; ascii[i] && i + 1 < sizeof s.label; i++)
		s.label[i] = char(toupper(static_cast<unsigned char>(ascii[i])));
	s.label[i] = '\0';
}

// Slot 0 (fist / brass knuckles) at the top, then clockwise through the slots.
bool BuildWeapons(uint8_t *ped)
{
	gCount = kWeaponSlots;
	for (int slot = 0; slot < kWeaponSlots; slot++) {
		Segment &s = gSegments[slot] = {};
		s.value = slot;
		uint8_t *weapon = PedWeapon(ped, slot);
		const int32_t type = WeaponType(weapon);
		s.present = slot == 0 || (type != 0 && api.WeaponHasAmmoToBeUsed(weapon));
		if (!s.present)
			continue;
		SetLabel(s, type >= 0 && type < kNumWeaponNames ? kWeaponNames[type] : "?");
		// The fist is CHud::Sprites[0]; everything else has a model icon.
		RwTexture *icon = type == 0 ? *reinterpret_cast<RwTexture **>(api.hudSprites) : WeaponIcon(type);
		s.icon = icon ? api.RwTextureGetRaster(icon) : nullptr;
		if (slot >= 2) // melee has no ammo
			snprintf(s.detail, sizeof s.detail, "%d", WeaponAmmoTotal(weapon));
	}
	gSelected = std::clamp(PedCurrentWeaponSlot(ped), 0, kWeaponSlots - 1);
	return true;
}

// Radio off at the top, then the stations clockwise (and the MP3 player if it has tracks).
bool BuildRadio(uint8_t *vehicle)
{
	if (!RadioUsable(vehicle))
		return false;
	LoadLogos();
	const bool mp3 = api.IsMP3RadioChannelAvailable(api.dmAudio);
	const int tuned = RadioTuning(vehicle);
	gCount = 0;
	gSelected = 0;
	for (int i = 0; i < kRadioChannels; i++) {
		const int channel = (i + kRadioOff) % kRadioChannels; // off first
		if (channel == kRadioMP3 && !mp3)
			continue;
		Segment &s = gSegments[gCount] = {};
		s.present = true;
		s.value = channel;
		char key[8];
		if (channel < radiologos::kCount)
			snprintf(key, sizeof key, "FEA_FM%d", channel);
		else
			snprintf(key, sizeof key, "%s", channel == kRadioMP3 ? "FEA_MP3" : "FEA_NON");
		s.name = api.TextGet(api.theText, key);
		s.icon = channel < radiologos::kCount ? gLogos[channel] : nullptr;
		if (channel == tuned)
			gSelected = gCount;
		gCount++;
	}
	return gCount > 0;
}

float SegmentAngle(int i) { return -kPi / 2 + i * 2 * kPi / gCount; } // 0 at the top, clockwise (y down)

void Sector(Vec2 c, float r0, float r1, float a0, float a1, CRGBA col)
{
	const int steps = std::max(2, int((a1 - a0) / 0.06f));
	for (int k = 0; k < steps; k++) {
		const float t0 = a0 + (a1 - a0) * k / steps, t1 = a0 + (a1 - a0) * (k + 1) / steps;
		const Vtx p0{c.x + r0 * cosf(t0), c.y + r0 * sinf(t0), 0, 0}, p1{c.x + r1 * cosf(t0), c.y + r1 * sinf(t0), 0, 0};
		const Vtx p2{c.x + r1 * cosf(t1), c.y + r1 * sinf(t1), 0, 0}, p3{c.x + r0 * cosf(t1), c.y + r0 * sinf(t1), 0, 0};
		gCanvas.Tri(p0, p1, p2, col);
		gCanvas.Tri(p0, p2, p3, col);
	}
}

void Image(void *raster, float cx, float cy, float size, uint8_t alpha)
{
	const float h = size / 2;
	const Vtx tl{cx - h, cy - h, 0, 0}, tr{cx + h, cy - h, 1, 0}, br{cx + h, cy + h, 1, 1}, bl{cx - h, cy + h, 0, 1};
	gCanvas.SetTexture(raster);
	const CRGBA col{255, 255, 255, alpha};
	gCanvas.Tri(tl, tr, br, col);
	gCanvas.Tri(tl, br, bl, col);
}

template <typename Str>
void CentredText(float cx, float cy, Str str, const text::Style &style)
{
	float t, b;
	text::LineExtent(style, 0.0f, t, b);
	text::Draw(cx - text::Width(str, style) / 2, cy - (t + b) / 2, str, style);
}

void DrawName(const Segment &s, float cx, float cy, const text::Style &style)
{
	if (s.name)
		CentredText(cx, cy, s.name, style);
	else
		CentredText(cx, cy, s.label, style);
}

} // namespace

bool Open()
{
	uint8_t *ped = api.FindPlayerPed();
	if (!ped || gKind != Kind::kNone)
		return false;
	if (PedInVehicle(ped)) {
		gVehicle = PedVehicle(ped);
		gKind = gVehicle && BuildRadio(gVehicle) ? Kind::kRadio : Kind::kNone;
	} else {
		gKind = BuildWeapons(ped) ? Kind::kWeapons : Kind::kNone;
	}
	if (gKind == Kind::kNone)
		return false;
	gSavedTimeScale = *api.timeScale;
	*api.timeScale = std::min(gSavedTimeScale, kSlowMotion);
	return true;
}

bool IsOpen() { return gKind != Kind::kNone; }

void *StationLogo(int channel)
{
	LoadLogos();
	return channel >= 0 && channel < radiologos::kCount ? gLogos[channel] : nullptr;
}

void Steer(float x, float y)
{
	if (gKind == Kind::kNone)
		return;
	const uint8_t *ped = api.FindPlayerPed();
	if (!ped || PedInVehicle(ped) != (gKind == Kind::kRadio)) { // got in or out meanwhile
		Close(false);
		return;
	}
	*api.timeScale = std::min(gSavedTimeScale, kSlowMotion); // in case something reset it
	if (x * x + y * y < kDeadZone * kDeadZone)
		return;
	const float step = 2 * kPi / gCount;
	const int i = ((int(lroundf(atan2f(x, -y) / step)) % gCount) + gCount) % gCount;
	if (gSegments[i].present)
		gSelected = i;

}

void Close(bool apply)
{
	if (gKind == Kind::kNone)
		return;
	*api.timeScale = gSavedTimeScale;
	uint8_t *ped = api.FindPlayerPed();
	const Segment &s = gSegments[gSelected];
	if (apply && ped && s.present) {
		if (gKind == Kind::kWeapons && !PedInVehicle(ped)) {
			if (s.value != PedCurrentWeaponSlot(ped))
				PlayerWeaponSlotToSelect(ped) = int8_t(s.value); // CPlayerPed::ProcessWeaponSwitch does the rest
		} else if (gKind == Kind::kRadio && PedInVehicle(ped) && PedVehicle(ped) == gVehicle) {
			int steps = ((s.value - RadioTuning(gVehicle)) % kRadioChannels + kRadioChannels) % kRadioChannels;
			if (steps > kRadioChannels / 2)
				steps -= kRadioChannels;
			RetuneBy(gVehicle, steps, 2);
		}
	}
	gKind = Kind::kNone;
}

void Draw()
{
	if (gKind == Kind::kNone)
		return;
	const float W = float(ScreenWidth()), H = float(ScreenHeight());
	const Vec2 c{W / 2, H / 2};
	const float outer = H * 0.31f, inner = H * 0.135f, accent = H * 0.009f;
	const float step = 2 * kPi / gCount, gap = H * 0.004f / inner;

	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	gCanvas.Quad({0, 0}, {W, 0}, {W, H}, {0, H}, kDim);
	for (int i = 0; i < gCount; i++) {
		const float a = SegmentAngle(i), a0 = a - step / 2 + gap, a1 = a + step / 2 - gap;
		Sector(c, inner, outer, a0, a1, gSegments[i].present ? kSegment : kSegmentEmpty);
		if (i == gSelected) {
			Sector(c, inner, outer, a0, a1, kSelectedTint);
			Sector(c, outer + accent * 0.5f, outer + accent * 1.5f, a0, a1, kAccent);
		}
	}
	Sector(c, 0.0f, inner - H * 0.008f, 0.0f, 2 * kPi, kCentre); // behind the name
	gCanvas.Flush();

	const float mid = (inner + outer) / 2;
	const float iconSize = (outer - inner) * (gKind == Kind::kRadio ? 0.78f : 0.62f);
	for (int i = 0; i < gCount; i++) {
		const Segment &s = gSegments[i];
		if (s.present && s.icon)
			Image(s.icon, c.x + mid * cosf(SegmentAngle(i)), c.y + mid * sinf(SegmentAngle(i)), iconSize,
			      i == gSelected ? 255 : 190);
	}
	gCanvas.Flush();
	gCanvas.SetTexture(nullptr);

	// Segments without an icon show their name; the centre names the selection.
	const text::Style small{1, H * 0.3f / 480, H * 0.42f / 448, {255, 255, 255, 220}};
	for (int i = 0; i < gCount; i++) {
		const Segment &s = gSegments[i];
		if (s.present && !s.icon)
			DrawName(s, c.x + mid * cosf(SegmentAngle(i)), c.y + mid * sinf(SegmentAngle(i)), small);
	}
	const Segment &sel = gSegments[gSelected];
	if (sel.present) {
		const text::Style name{1, H * 0.5f / 480, H * 0.72f / 448, {255, 255, 255, 255}};
		const bool detail = sel.detail[0] != '\0';
		DrawName(sel, c.x, c.y - (detail ? H * 0.02f : 0.0f), name);
		if (detail)
			CentredText(c.x, c.y + H * 0.028f, sel.detail, text::Style{1, small.scaleX, small.scaleY, {255, 255, 255, 170}});
	}
	text::Flush();
}

} // namespace wheel
