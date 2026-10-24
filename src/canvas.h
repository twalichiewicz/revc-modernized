#pragma once
// Where minimap triangles go: the game's RwIm2D pipeline in the plugin
// (draw.h), a software rasteriser in tools/preview.
#include "types.h"

struct Vtx {
	float x, y, u, v;
	float depth = -1.0f; // 0 (near) .. 1 (far) for depth-tested drawing; < 0: HUD default
};

enum class DepthMode {
	kOff,       // plain 2D drawing (the HUD default)
	kWrite,     // write depth without testing: clears an area to the given depth
	kTestWrite, // depth-tested 3D drawing
};

class Canvas {
public:
	virtual ~Canvas() = default;
	virtual void SetTexture(void *texture) = 0; // nullptr draws untextured
	virtual void SetDepthMode(DepthMode mode) = 0;
	virtual void Tri(const Vtx &a, const Vtx &b, const Vtx &c, CRGBA ca, CRGBA cb, CRGBA cc) = 0;
	virtual void Flush() = 0;

	void Tri(const Vtx &a, const Vtx &b, const Vtx &c, CRGBA col) { Tri(a, b, c, col, col, col); }
	void Fan(const Vtx *v, int n, CRGBA col);
	void Quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, CRGBA col);
	void VerticalGradient(float x, float y, float w, float h, CRGBA top, CRGBA bottom);
};
