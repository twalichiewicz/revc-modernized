#include "draw.h"

#include <cstring>

using game::api;

namespace {
constexpr int kMaxBatchVerts = 1200; // keep well under the Im2D buffer size
}

void SetHudRenderStates()
{
	auto set = [](int state, intptr_t value) { api.RwRenderStateSet(state, reinterpret_cast<void *>(value)); };
	set(game::kStateFog, 0);
	set(game::kStateZTest, 0);
	set(game::kStateZWrite, 0);
	set(game::kStateVertexAlpha, 1);
	set(game::kStateSrcBlend, game::kBlendSrcAlpha);
	set(game::kStateDestBlend, game::kBlendInvSrcAlpha);
	set(game::kStateTextureFilter, game::kFilterLinear);
	set(game::kStateTextureAddress, game::kAddressClamp);
}

RwRaster *CreateRaster(int width, int height, const std::vector<uint8_t> &rgba)
{
	RwImage *image = api.RwImageCreate(width, height, 32);
	if (!image)
		return nullptr;
	api.RwImageAllocatePixels(image);
	uint8_t *pixels = api.RwImageGetPixels(image);
	const int stride = api.RwImageGetStride(image);
	for (int y = 0; pixels && y < height; y++)
		memcpy(pixels + size_t(y) * stride, rgba.data() + size_t(y) * width * 4, size_t(width) * 4);
	RwRaster *raster = api.RwRasterCreate(width, height, 32, game::kRasterTypeTexture | game::kRasterFormat8888);
	if (raster)
		api.RwRasterSetFromImage(raster, image);
	api.RwImageDestroy(image);
	return raster;
}

void Im2DCanvas::SetTexture(void *raster)
{
	if (raster != raster_)
		Flush();
	raster_ = raster;
}

// RwIm2D honours per-vertex screen z: fragments must be opaque enough to pass
// the alpha test for their depth to be written.
void Im2DCanvas::SetDepthMode(DepthMode mode)
{
	Flush();
	api.RwRenderStateSet(game::kStateZTest, reinterpret_cast<void *>(intptr_t(mode == DepthMode::kTestWrite)));
	api.RwRenderStateSet(game::kStateZWrite, reinterpret_cast<void *>(intptr_t(mode != DepthMode::kOff)));
}

void Im2DCanvas::Push(const Vtx &v, CRGBA col)
{
	if (buf_.size() < static_cast<size_t>((count_ + 1) * game::kIm2DVertexSize))
		buf_.resize((count_ + 64) * game::kIm2DVertexSize);
	auto *vert = reinterpret_cast<RwIm2DVertex *>(buf_.data() + count_ * game::kIm2DVertexSize);
	// Same per-vertex values CSprite2d::SetVertices uses.
	const float recipZ = *api.spriteRecipNearClip;
	api.VertSetScreenX(vert, v.x);
	api.VertSetScreenY(vert, v.y);
	api.VertSetScreenZ(vert, v.depth < 0.0f ? *api.spriteNearScreenZ + 0.0001f : v.depth);
	api.VertSetCameraZ(vert, *api.spriteNearCamZ);
	api.VertSetRecipCameraZ(vert, recipZ);
	api.VertSetIntRGBA(vert, col.r, col.g, col.b, col.a);
	api.VertSetU(vert, v.u, recipZ);
	api.VertSetV(vert, v.v, recipZ);
	count_++;
}

void Im2DCanvas::Tri(const Vtx &a, const Vtx &b, const Vtx &c, CRGBA ca, CRGBA cb, CRGBA cc)
{
	if (count_ + 3 > kMaxBatchVerts)
		Flush();
	Push(a, ca);
	Push(b, cb);
	Push(c, cc);
}

void Im2DCanvas::Flush()
{
	if (count_ == 0)
		return;
	api.RwRenderStateSet(game::kStateTextureRaster, raster_);
	api.RwIm2DRenderPrimitive(game::kPrimTriList, reinterpret_cast<RwIm2DVertex *>(buf_.data()), count_);
	count_ = 0;
}
