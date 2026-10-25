#include "shaders.h"
#include "log.h"

#include "hooks.h"

#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>

using namespace game;

namespace shaders {
namespace {

// librw's uniform types (rw::gl3::UniformType): the game's own vec4s are 1.
constexpr int kUniformVec4 = 1, kUniformMat4 = 3;
// Where librw keeps the projection and view matrices it sets (from the image
// base), and the instructions in setProjectionMatrix / setViewMatrix that say
// so (adrp x1 / add x1, x1, #0xc6c, then #0xcac).
constexpr uintptr_t kProjection = 0x7f4c6c, kView = 0x7f4cac;
constexpr uintptr_t kSetProjection = 0x2b947c, kSetView = 0x2b94a0;
constexpr uint32_t kSetProjectionCode[2] = {0xf00029c1, 0x9131b021}, kSetViewCode[2] = {0xf00029c1, 0x9132b021};

// GL enums.
enum : unsigned {
	kGLTexture2D = 0x0DE1,
	kGLTexture3D = 0x806F,
	kGLTexture0 = 0x84C0,
	kGLRGBA = 0x1908,
	kGLRGBA8 = 0x8058,
	kGLUnsignedByte = 0x1401,
	kGLMinFilter = 0x2801,
	kGLMagFilter = 0x2800,
	kGLWrapS = 0x2802,
	kGLWrapT = 0x2803,
	kGLWrapR = 0x8072,
	kGLLinear = 0x2601,
	kGLLinearMipmapLinear = 0x2703,
	kGLRepeat = 0x2901,
	kGLClampToEdge = 0x812F,
	kGLFramebuffer = 0x8D40,
	kGLColorAttachment0 = 0x8CE0,
	kGLFramebufferComplete = 0x8CD5,
	kGLFramebufferBinding = 0x8CA6,
	kGLViewport = 0x0BA2,
	kGLActiveTexture = 0x84E0,
	kGLCurrentProgram = 0x8B8D,
	kGLPixelUnpackBuffer = 0x88EC,
	kGLPixelUnpackBufferBinding = 0x88EF,
	kGLNoError = 0,
};

// librw's glad entry points (pointers it fills in when GL starts).
struct GL {
	void (*ActiveTexture)(unsigned);
	void (*BindTexture)(unsigned, unsigned);
	void (*GenTextures)(int, unsigned *);
	void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
	void (*TexImage3D)(unsigned, int, int, int, int, int, int, unsigned, unsigned, const void *);
	void (*TexParameteri)(unsigned, unsigned, int);
	void (*GenerateMipmap)(unsigned);
	void (*DeleteTextures)(int, const unsigned *);
	void (*GenFramebuffers)(int, unsigned *);
	void (*DeleteFramebuffers)(int, const unsigned *);
	void (*FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
	unsigned (*CheckFramebufferStatus)(unsigned);
	void (*GetIntegerv)(unsigned, int *);
	unsigned (*GetError)();
	void (*Viewport)(int, int, int, int);
	void (*UseProgram)(unsigned);
	int (*GetUniformLocation)(unsigned, const char *);
	void (*Uniform1i)(int, int);
	void (*BindBuffer)(unsigned, unsigned);
} gl;
void (*gBindFramebuffer)(unsigned); // rw::gl3::bindFramebuffer: librw remembers what's bound
// The render states a pass sets (SetHudRenderStates and the texture):
// TEXTURERASTER (read back as a pointer), TEXTUREADDRESS, ZTEST, ZWRITE,
// TEXTUREFILTER, SRCBLEND, DESTBLEND, VERTEXALPHA, FOG (each 32 bits).
constexpr int kSaved[] = {1, 2, 6, 8, 9, 10, 11, 12, 14};
constexpr int kNumSaved = sizeof kSaved / sizeof kSaved[0];

void *(*gCreate)(const char **vertex, const char **fragment);
int (*gRegister)(const char *name, int type, int count);
void (*gSetUniform)(int index, void *data);
const char **gDecl, **gHeaderVert, **gHeaderFrag;
void **gIm2DOverride;
void (*gGetBackBuffer)(void *camera);
void **gBackBuffer;
const uint8_t *gScene; // Scene: the world, then the camera
int (*gRasterWidth)(const void *raster), (*gRasterHeight)(const void *raster);
void *(*gCameraRaster)(const void *camera);
int (*gStateGet)(int state, void *value);
const CVector *gVectorToSun;
const int32_t *gStoredValue;
const int32_t *gSkyTop, *gSkyBottom, *gSunCore; // red, green, blue in a row
const float *gCloudCover, *gFog, *gRain;
bool gReady;

template <typename T>
bool Bind(T &out, const char *name)
{
	out = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, name));
	if (!out)
		Log("shaders: missing %s", name);
	return out != nullptr;
}

CVector Colour(const int32_t *rgb) { return {rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f}; }

} // namespace

const char *const kScreenVertex = R"(
uniform vec4 u_xform;
VSIN(ATTRIB_POS) vec4 in_pos;
VSOUT vec2 v_tex0;
void main(void)
{
	gl_Position = in_pos;
	gl_Position.w = 1.0;
	gl_Position.xy = gl_Position.xy * u_xform.xy + u_xform.zw;
	v_tex0 = in_tex0;
}
)";

bool Install()
{
	gReady = Bind(gCreate, "_ZN2rw3gl36Shader6createEPPKcS4_") &&
	         Bind(gRegister, "_ZN2rw3gl315registerUniformEPKcNS0_11UniformTypeEi") &&
	         Bind(gSetUniform, "_ZN2rw3gl310setUniformEiPv") && Bind(gDecl, "_ZN2rw3gl310shaderDeclE") &&
	         Bind(gHeaderVert, "_ZN2rw3gl315header_vert_srcE") && Bind(gHeaderFrag, "_ZN2rw3gl315header_frag_srcE") &&
	         Bind(gIm2DOverride, "_ZN2rw3gl318im2dOverrideShaderE") &&
	         Bind(gGetBackBuffer, "_ZN7CPostFX13GetBackBufferEPN2rw6CameraE") &&
	         Bind(gBackBuffer, "_ZN7CPostFX11pBackBufferE") && Bind(gScene, "Scene") &&
	         Bind(gRasterWidth, "_Z16RwRasterGetWidthPKN2rw6RasterE") &&
	         Bind(gRasterHeight, "_Z17RwRasterGetHeightPKN2rw6RasterE") &&
	         Bind(gCameraRaster, "_Z17RwCameraGetRasterPKN2rw6CameraE") &&
	         Bind(gStateGet, "_Z16RwRenderStateGet13RwRenderStatePv") &&
	         Bind(gVectorToSun, "_ZN10CTimeCycle13m_VectorToSunE") &&
	         Bind(gStoredValue, "_ZN10CTimeCycle20m_CurrentStoredValueE") &&
	         Bind(gSkyTop, "_ZN10CTimeCycle19m_nCurrentSkyTopRedE") &&
	         Bind(gSkyBottom, "_ZN10CTimeCycle22m_nCurrentSkyBottomRedE") &&
	         Bind(gSunCore, "_ZN10CTimeCycle20m_nCurrentSunCoreRedE") &&
	         Bind(gCloudCover, "_ZN8CWeather13CloudCoverageE") && Bind(gFog, "_ZN8CWeather9FoggynessE") &&
	         Bind(gRain, "_ZN8CWeather4RainE") && api.timeMs && api.theCamera;
	return gReady;
}

bool Ready() { return gReady; }

int Uniform(const char *name) { return gReady ? gRegister(name, kUniformVec4, 1) : -1; }

void Set(int uniform, float x, float y, float z, float w)
{
	if (uniform < 0)
		return;
	float v[4] = {x, y, z, w};
	gSetUniform(uniform, v);
}

void *Create(const char *name, const char *vertex, const char *fragment)
{
	if (!gReady || !*gDecl)
		return nullptr;
	const char *vs[] = {*gDecl, *gHeaderVert, vertex, nullptr};
	const char *fs[] = {*gDecl, *gHeaderFrag, fragment, nullptr};
	void *shader = gCreate(vs, fs);
	Log(shader ? "shaders: %s built" : "shaders: %s didn't build (see the game's console); left as it was", name);
	return shader;
}

void OverrideIm2D(void *shader) { *gIm2DOverride = shader; }

int UniformMatrix(const char *name) { return gReady ? gRegister(name, kUniformMat4, 1) : -1; }

void SetMatrix(int uniform, const float *m)
{
	if (uniform >= 0)
		gSetUniform(uniform, const_cast<float *>(m));
}

bool ViewProjection(float out[16])
{
	static int found = -1;
	const auto *base = static_cast<const uint8_t *>(hooks::ImageBase());
	if (found < 0) {
		const auto *p = reinterpret_cast<const uint32_t *>(base + kSetProjection);
		const auto *v = reinterpret_cast<const uint32_t *>(base + kSetView);
		found = p[0] == kSetProjectionCode[0] && p[1] == kSetProjectionCode[1] && v[0] == kSetViewCode[0] &&
		        v[1] == kSetViewCode[1];
		if (!found)
			Log("shaders: librw's camera matrices aren't where expected");
	}
	if (!found)
		return false;
	float proj[16], view[16];
	memcpy(proj, base + kProjection, sizeof proj);
	memcpy(view, base + kView, sizeof view);
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++) {
			float sum = 0.0f;
			for (int k = 0; k < 4; k++)
				sum += proj[k * 4 + r] * view[c * 4 + k];
			out[c * 4 + r] = sum;
		}
	return true;
}

bool LoadGL()
{
	static int loaded = -1;
	if (loaded >= 0)
		return loaded;
	bool ok = true;
	auto load = [&](auto &fn, const char *name) {
		auto *slot = static_cast<void *const *>(dlsym(RTLD_MAIN_ONLY, name));
		fn = slot ? reinterpret_cast<std::remove_reference_t<decltype(fn)>>(*slot) : nullptr;
		if (!fn) {
			Log("shaders: no %s", name);
			ok = false;
		}
	};
	load(gl.ActiveTexture, "glad_glActiveTexture");
	load(gl.BindTexture, "glad_glBindTexture");
	load(gl.GenTextures, "glad_glGenTextures");
	load(gl.TexImage2D, "glad_glTexImage2D");
	load(gl.TexImage3D, "glad_glTexImage3D");
	load(gl.TexParameteri, "glad_glTexParameteri");
	load(gl.GenerateMipmap, "glad_glGenerateMipmap");
	load(gl.DeleteTextures, "glad_glDeleteTextures");
	load(gl.GenFramebuffers, "glad_glGenFramebuffers");
	load(gl.DeleteFramebuffers, "glad_glDeleteFramebuffers");
	load(gl.FramebufferTexture2D, "glad_glFramebufferTexture2D");
	load(gl.CheckFramebufferStatus, "glad_glCheckFramebufferStatus");
	load(gl.GetIntegerv, "glad_glGetIntegerv");
	load(gl.GetError, "glad_glGetError");
	load(gl.Viewport, "glad_glViewport");
	load(gl.UseProgram, "glad_glUseProgram");
	load(gl.GetUniformLocation, "glad_glGetUniformLocation");
	load(gl.Uniform1i, "glad_glUniform1i");
	load(gl.BindBuffer, "glad_glBindBuffer");
	gBindFramebuffer = reinterpret_cast<void (*)(unsigned)>(dlsym(RTLD_MAIN_ONLY, "_ZN2rw3gl315bindFramebufferEj"));
	if (!gBindFramebuffer) {
		Log("shaders: no rw::gl3::bindFramebuffer");
		ok = false;
	}
	loaded = ok;
	return ok;
}

namespace {

// Binds `texture` on our unit for a moment (the active unit is put back).
class OnUnit {
public:
	OnUnit(int unit, unsigned target, unsigned texture)
	{
		gl.GetIntegerv(kGLActiveTexture, &active_);
		gl.ActiveTexture(kGLTexture0 + unit);
		gl.BindTexture(target, texture);
	}
	~OnUnit() { gl.ActiveTexture(unsigned(active_)); }

private:
	int active_ = int(kGLTexture0);
};

// Uploads read from client memory, not a buffer librw may have bound.
class NoUnpackBuffer {
public:
	NoUnpackBuffer()
	{
		gl.GetIntegerv(kGLPixelUnpackBufferBinding, &buffer_);
		if (buffer_)
			gl.BindBuffer(kGLPixelUnpackBuffer, 0);
	}
	~NoUnpackBuffer()
	{
		if (buffer_)
			gl.BindBuffer(kGLPixelUnpackBuffer, unsigned(buffer_));
	}

private:
	int buffer_ = 0;
};

void Filtering(unsigned target, bool mipmaps, unsigned wrap)
{
	gl.TexParameteri(target, kGLMinFilter, int(mipmaps ? kGLLinearMipmapLinear : kGLLinear));
	gl.TexParameteri(target, kGLMagFilter, int(kGLLinear));
	gl.TexParameteri(target, kGLWrapS, int(wrap));
	gl.TexParameteri(target, kGLWrapT, int(wrap));
	if (target == kGLTexture3D)
		gl.TexParameteri(target, kGLWrapR, int(wrap));
}

unsigned Checked(unsigned texture, const char *what)
{
	const unsigned error = gl.GetError();
	if (error == kGLNoError)
		return texture;
	Log("shaders: making %s failed (GL error 0x%x)", what, error);
	gl.DeleteTextures(1, &texture);
	return 0;
}

} // namespace

unsigned Texture3D(int unit, int size, const uint8_t *rgba)
{
	while (gl.GetError() != kGLNoError) {
	}
	unsigned texture = 0;
	gl.GenTextures(1, &texture);
	const OnUnit on(unit, kGLTexture3D, texture);
	const NoUnpackBuffer unpack;
	gl.TexImage3D(kGLTexture3D, 0, int(kGLRGBA8), size, size, size, 0, kGLRGBA, kGLUnsignedByte, rgba);
	gl.GenerateMipmap(kGLTexture3D);
	Filtering(kGLTexture3D, true, kGLRepeat);
	return Checked(texture, "a 3D texture");
}

unsigned Texture2D(int unit, int width, int height, const uint8_t *rgba)
{
	while (gl.GetError() != kGLNoError) {
	}
	unsigned texture = 0;
	gl.GenTextures(1, &texture);
	const OnUnit on(unit, kGLTexture2D, texture);
	const NoUnpackBuffer unpack;
	gl.TexImage2D(kGLTexture2D, 0, int(kGLRGBA8), width, height, 0, kGLRGBA, kGLUnsignedByte, rgba);
	gl.GenerateMipmap(kGLTexture2D);
	Filtering(kGLTexture2D, true, kGLRepeat);
	return Checked(texture, "a texture");
}

bool Resize(Target &target, int width, int height)
{
	if (target.texture && target.width == width && target.height == height)
		return true;
	if (target.texture) {
		gl.DeleteFramebuffers(1, &target.framebuffer);
		gl.DeleteTextures(1, &target.texture);
	}
	target = {};
	while (gl.GetError() != kGLNoError) {
	}
	unsigned texture = 0, framebuffer = 0;
	gl.GenTextures(1, &texture);
	{
		// Unit 4: one of ours, so nothing of librw's is unbound.
		const OnUnit on(4, kGLTexture2D, texture);
		const NoUnpackBuffer unpack;
		gl.TexImage2D(kGLTexture2D, 0, int(kGLRGBA8), width, height, 0, kGLRGBA, kGLUnsignedByte, nullptr);
		Filtering(kGLTexture2D, false, kGLClampToEdge);
	}
	int previous = 0;
	gl.GetIntegerv(kGLFramebufferBinding, &previous);
	gl.GenFramebuffers(1, &framebuffer);
	gBindFramebuffer(framebuffer);
	gl.FramebufferTexture2D(kGLFramebuffer, kGLColorAttachment0, kGLTexture2D, texture, 0);
	const bool complete = gl.CheckFramebufferStatus(kGLFramebuffer) == kGLFramebufferComplete;
	gBindFramebuffer(unsigned(previous));
	const unsigned error = gl.GetError();
	if (!complete || error != kGLNoError) {
		Log("shaders: a %dx%d target didn't work (complete %d, GL error 0x%x)", width, height, complete, error);
		gl.DeleteFramebuffers(1, &framebuffer);
		gl.DeleteTextures(1, &texture);
		return false;
	}
	target = {texture, framebuffer, width, height};
	return true;
}

DrawInto::DrawInto(const Target &target)
{
	gl.GetIntegerv(kGLFramebufferBinding, &framebuffer_);
	gl.GetIntegerv(kGLViewport, viewport_);
	gBindFramebuffer(target.framebuffer);
	gl.Viewport(0, 0, target.width, target.height);
}

DrawInto::~DrawInto()
{
	gBindFramebuffer(unsigned(framebuffer_));
	gl.Viewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
}

void SetSampler(void *shader, const char *name, int unit)
{
	// rw::gl3::Shader starts with its GL program.
	const unsigned program = *static_cast<const unsigned *>(shader);
	int previous = 0;
	gl.GetIntegerv(kGLCurrentProgram, &previous);
	gl.UseProgram(program);
	gl.Uniform1i(gl.GetUniformLocation(program, name), unit);
	gl.UseProgram(unsigned(previous));
}

void BindTexture(int unit, bool is3D, unsigned texture)
{
	int active = int(kGLTexture0);
	gl.GetIntegerv(kGLActiveTexture, &active);
	gl.ActiveTexture(kGLTexture0 + unit);
	gl.BindTexture(is3D ? kGLTexture3D : kGLTexture2D, texture);
	gl.ActiveTexture(unsigned(active));
}

void *GrabFrame()
{
	void *camera = *reinterpret_cast<void *const *>(gScene + 8);
	if (!camera || !*gBackBuffer)
		return nullptr;
	gGetBackBuffer(camera);
	return *gBackBuffer;
}

void FrameSize(float &width, float &height)
{
	void *camera = *reinterpret_cast<void *const *>(gScene + 8);
	void *raster = camera ? gCameraRaster(camera) : nullptr;
	width = raster ? float(gRasterWidth(raster)) : float(ScreenWidth());
	height = raster ? float(gRasterHeight(raster)) : float(ScreenHeight());
}

SavedStates::SavedStates()
{
	for (int i = 0; i < kNumSaved; i++) {
		values_[i] = nullptr; // the 32-bit states only fill the low half
		gStateGet(kSaved[i], &values_[i]);
	}
}

SavedStates::~SavedStates()
{
	for (int i = 0; i < kNumSaved; i++)
		api.RwRenderStateSet(kSaved[i], values_[i]);
}

Sky CurrentSky()
{
	Sky s;
	const int stored = *gStoredValue;
	s.sun = gVectorToSun[stored >= 0 && stored < 16 ? stored : 0];
	const float len = std::sqrt(s.sun.x * s.sun.x + s.sun.y * s.sun.y + s.sun.z * s.sun.z);
	if (len > 1e-4f)
		s.sun = {s.sun.x / len, s.sun.y / len, s.sun.z / len};
	else
		s.sun = {0.0f, 0.0f, 1.0f};
	s.daylight = std::fmin(1.0f, std::fmax(0.0f, s.sun.z * 4.0f + 0.35f));
	s.top = Colour(gSkyTop);
	s.bottom = Colour(gSkyBottom);
	s.sunColour = Colour(gSunCore);
	s.cloudCover = *gCloudCover;
	s.fog = *gFog;
	s.rain = *gRain;
	return s;
}

float Seconds() { return float(*api.timeMs) / 1000.0f; }

// TheCamera's matrix: right, forward, up, position (16 bytes each).
CVector CameraPosition() { return *reinterpret_cast<const CVector *>(api.theCamera + 0x30); }
CVector CameraForward() { return *reinterpret_cast<const CVector *>(api.theCamera + 0x10); }

} // namespace shaders
