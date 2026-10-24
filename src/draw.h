#pragma once
// Canvas backed by the game's RwIm2D pipeline (batched triangle lists).
#include "canvas.h"
#include "game.h"

#include <vector>

class Im2DCanvas : public Canvas {
public:
	using Canvas::Tri;
	void SetTexture(void *raster) override; // an RwRaster*, or nullptr
	void SetDepthMode(DepthMode mode) override;
	void Tri(const Vtx &a, const Vtx &b, const Vtx &c, CRGBA ca, CRGBA cb, CRGBA cc) override;
	void Flush() override;

private:
	void Push(const Vtx &v, CRGBA col);

	std::vector<uint8_t> buf_;
	int count_ = 0;
	void *raster_ = nullptr;
};

// Render states for alpha-blended HUD drawing without depth.
void SetHudRenderStates();

// Uploads an RGBA image as a game texture; returns its raster (nullptr on failure).
RwRaster *CreateRaster(int width, int height, const std::vector<uint8_t> &rgba);
