#include "gameplaypage.h"
#include "controlsmodel.h"
#include "draw.h"
#include "game.h"
#include "log.h"
#include "menulabels.h"
#include "reticle.h"
#include "text.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace game;
namespace gameplaypage {
namespace {
static_assert(gameplaymodel::kPageSize == kMenuPageSize && gameplaymodel::kEntrySize == kMenuEntrySize &&
              gameplaymodel::kEntries == kMenuEntries);
bool gAvailable, gReady, gFrontendRelease;
gameplaymodel::Focus gFocus;
controlsmodel::MouseGate gMouse;
Im2DCanvas gCanvas;
menulabels::Batch gLabels;
constexpr CRGBA kWhite{255, 255, 255, 255}, kPink{255, 150, 225, 255}, kDim{255, 255, 255, 180};
struct Stage {
	float scale, ox;
	Vec2 P(float x, float y) const { return {ox + x * scale, y * scale}; }
	float L(float v) const { return v * scale; }
};
Stage Now()
{
	const float scale = ScreenHeight() / 900.0f;
	return {scale, (ScreenWidth() - 1600 * scale) / 2};
}
text::Style Font(int font, float px, CRGBA color)
{
	static float unit[3];
	if (!unit[font]) {
		float top, bottom;
		text::LineExtent({int16_t(font), 1, 1, color}, 0, top, bottom);
		unit[font] = std::max(1.0f, bottom - top);
	}
	const auto scale = controlsmodel::ScaleFor(Now().L(px), unit[font]);
	return {int16_t(font), scale.x, scale.y, color};
}
void Label(float x, float y, const std::string &value, const text::Style &style) { gLabels.Add(x, y, value, style); }
struct Target {
	float x0, y0, x1, y1;
	int row, change;
};
std::vector<Target> gTargets;
const Target *Hit(uint8_t *menu)
{
	const float x = MenuMouseX(menu), y = MenuMouseY(menu);
	for (const auto &target : gTargets)
		if (x >= target.x0 && x < target.x1 && y >= target.y0 && y < target.y1)
			return &target;
	return nullptr;
}
const uint16_t *Wide(const char *ascii, uint16_t (&out)[32])
{
	size_t i = 0;
	for (; ascii[i] && i + 1 < 32; i++)
		out[i] = uint8_t(ascii[i]);
	out[i] = 0;
	return out;
}
} // namespace
void SetAvailable(bool available) { gAvailable = available; }
void Prepare(uint8_t *menu)
{
	if (!gAvailable || gReady)
		return;
	const auto registration = gameplaymodel::Prepare(api.menuScreens, gameplaymodel::kPages * kMenuPageSize);
	if (!registration.ready) {
		static bool logged;
		if (!logged)
			Log("gameplay page: spare native page or Options table unavailable; item not inserted");
		logged = true;
		return;
	}
	gReady = true;
	if (registration.changed && MenuScreen(menu) == kPageOptions)
		MenuOptionCount(menu) = api.GetOptionCount(kPageOptions);
	Log("gameplay page: Options entry %d, native page %d", registration.entry, kPage);
}
void Enter(uint8_t *menu)
{
	if (!gReady)
		return;
	MenuOption(menu) = 0;
	MenuOptionCount(menu) = 1;
	gFocus = {};
	gMouse = {};
	gFrontendRelease = false;
	gTargets.clear();
}
void Draw(uint8_t *)
{
	if (!gReady)
		return;
	const Stage st = Now();
	gTargets.clear();
	gLabels.Clear();
	// Drain native title/Back text before custom geometry; custom labels are
	// submitted only after every geometry batch, including the preview.
	text::Flush();
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	auto &wrap = *reinterpret_cast<float *>(api.fontDetails + 0x2c);
	const float savedWrap = wrap;
	api.FontSetWrapx(float(ScreenWidth()));
	gCanvas.Quad(st.P(190, 205), st.P(1410, 205), st.P(1410, 705), st.P(190, 705), {0, 0, 0, 158});
	Label(st.ox + st.L(250), st.L(245), "crosshair", Font(2, 38, kPink));
	static const char *names[] = {"STYLE",      "SIZE",           "THICKNESS",       "COLOR",
	                              "CENTER DOT", "DYNAMIC SPREAD", "RESTORE DEFAULTS"};
	const auto settings = reticle::Settings();
	for (int i = 0; i < int(reticlemodel::Row::kCount); i++) {
		const float y = 300 + 55.0f * i;
		const bool selected = gFocus.row == i;
		if (selected)
			gCanvas.Quad(st.P(220, y - 24), st.P(995, y - 24), st.P(995, y + 24), st.P(220, y + 24),
			             {255, 150, 225, 46});
		Label(st.ox + st.L(250), st.L(y), names[i], Font(1, 27, selected ? kPink : kWhite));
		Label(st.ox + st.L(750), st.L(y), reticlemodel::Label(settings, reticlemodel::Row(i)), Font(1, 27, kWhite));
		if (i != int(reticlemodel::Row::kDefaults)) {
			const CRGBA ink = selected ? kPink : kDim;
			gCanvas.Tri({st.ox + st.L(680), st.L(y), 0, 0}, {st.ox + st.L(693), st.L(y - 7), 0, 0},
			            {st.ox + st.L(693), st.L(y + 7), 0, 0}, ink);
			gCanvas.Tri({st.ox + st.L(970), st.L(y), 0, 0}, {st.ox + st.L(957), st.L(y - 7), 0, 0},
			            {st.ox + st.L(957), st.L(y + 7), 0, 0}, ink);
			gTargets.push_back({st.ox + st.L(660), st.L(y - 24), st.ox + st.L(720), st.L(y + 24), i, -1});
		}
		gTargets.push_back({st.ox + st.L(220), st.L(y - 24), st.ox + st.L(995), st.L(y + 24), i, 1});
	}
	Label(st.ox + st.L(1070), st.L(300), "preview", Font(2, 34, kPink));
	Label(st.ox + st.L(1050), st.L(570), "APPLIES TO ALL AIMING", Font(1, 19, kDim));
	Label(st.ox + st.L(250), st.L(745), "LEFT / RIGHT TO CHANGE - SETTINGS SAVE AUTOMATICALLY", Font(1, 22, kWhite));
	gCanvas.Flush();
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	reticle::Preview(st.ox + st.L(1190), st.L(445), float(ScreenHeight()), .007f * (1 + std::sin(float(seconds) * 2)));
	gLabels.AfterGeometry(gCanvas, [](const menulabels::Label &label) {
		float top, bottom;
		text::LineExtent(label.style, 0, top, bottom);
		text::Draw(label.x, label.centerY - (top + bottom) / 2, label.value.c_str(), label.style);
	});
	text::Flush();
	api.FontSetWrapx(savedWrap);
}
bool UserInput(uint8_t *menu)
{
	if (!gReady)
		return false;
	gFrontendRelease = false;
	if (!api.newMouseState[0] && api.oldMouseState[0]) {
		gFrontendRelease = gMouse.Release();
		if (!gFrontendRelease)
			api.oldMouseState[0] = 0;
	}
	if (LeftClickJustDown()) {
		const Target *hit = Hit(menu);
		gMouse.Press(hit != nullptr);
		if (hit) {
			api.oldMouseState[0] = 1;
			gFocus.row = hit->row;
			reticle::Change(reticlemodel::Row(hit->row), hit->change);
		}
	}
	return false;
}
void ProcessInput(uint8_t *, uint8_t &down, uint8_t &up, uint8_t &select, uint8_t &back, int8_t &change)
{
	if (!gReady || back || (select && gFrontendRelease))
		return;
	gameplaymodel::Input input;
	if (up)
		input = gameplaymodel::Input::Up;
	else if (down)
		input = gameplaymodel::Input::Down;
	else if (change < 0)
		input = gameplaymodel::Input::Left;
	else if (change > 0)
		input = gameplaymodel::Input::Right;
	else if (select)
		input = gameplaymodel::Input::Select;
	else
		return;
	const auto action = gameplaymodel::Apply(gFocus, input);
	if (action.change)
		reticle::Change(action.row, action.change);
	if (action.handled)
		down = up = select = 0, change = 0;
}
const uint16_t *Text(const char *key)
{
	if (!gReady || !key)
		return nullptr;
	static uint16_t entry[32], title[32];
	if (!std::strcmp(key, "FEO_GMP"))
		return Wide("Gameplay", entry);
	if (!std::strcmp(key, "FET_GMP"))
		return Wide("GAMEPLAY", title);
	return nullptr;
}
} // namespace gameplaypage
