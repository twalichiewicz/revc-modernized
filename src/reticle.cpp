#include "reticle.h"
#include "config.h"
#include "draw.h"
#include "game.h"
#include "shaders.h"
#include <algorithm>
#include <cmath>

namespace reticle {
namespace {
Im2DCanvas gCanvas;
void Paint(float x, float y, float h, float spread, float tanHalf, float opacity)
{
	const auto settings = Settings();
	const auto shape = reticlemodel::Build(settings, x, y, h, spread, tanHalf);
	CRGBA ink = reticlemodel::Color(settings.color), edge{0, 0, 0, 150};
	ink.a = uint8_t(ink.a * std::clamp(opacity, 0.0f, 1.0f));
	edge.a = uint8_t(edge.a * std::clamp(opacity, 0.0f, 1.0f));
	SetHudRenderStates();
	gCanvas.SetTexture(nullptr);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i < shape.count; i++) {
			const auto &r = shape.rects[i];
			const float o = pass == 0 ? shape.edge : 0;
			gCanvas.Quad({r.x - o, r.y - o}, {r.x + r.w + o, r.y - o}, {r.x + r.w + o, r.y + r.h + o},
			             {r.x - o, r.y + r.h + o}, pass == 0 ? edge : ink);
		}
	gCanvas.Flush();
}
} // namespace
reticlemodel::Settings Settings()
{
	return reticlemodel::Sanitize({gConfig.crosshairStyle, gConfig.crosshairColor, gConfig.crosshairSize,
	                               gConfig.crosshairThickness, gConfig.crosshairDot, gConfig.crosshairDynamic});
}
void Change(reticlemodel::Row row, int direction)
{
	auto settings = Settings();
	reticlemodel::Change(settings, row, direction);
	gConfig.crosshairStyle = settings.style;
	gConfig.crosshairColor = settings.color;
	gConfig.crosshairSize = settings.size;
	gConfig.crosshairThickness = settings.thickness;
	gConfig.crosshairDot = settings.dot;
	gConfig.crosshairDynamic = settings.dynamic;
	for (const auto &value : reticlemodel::Values(settings))
		SaveValue("Crosshair", value.key, value.value);
}
void Draw(float x, float y, float spread, float opacity)
{
	float proj[16], view[16];
	const float tanHalf = shaders::CameraMatrices(proj, view) && proj[5] != 0 ? 1 / std::fabs(proj[5]) : 0.6f;
	Paint(x, y, float(game::ScreenHeight()), spread, tanHalf, opacity);
}
void Preview(float x, float y, float h, float spread) { Paint(x, y, h, spread, 0.6f, 1); }
} // namespace reticle
