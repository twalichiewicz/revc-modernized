#pragma once
// Our own shaders in the game's renderer (librw's gl3 backend, OpenGL ES
// through ANGLE on this port), the way reVC's colour filter does it
// (CPostFX): built with the game's GLSL version line and headers, so they see
// its uniforms (u_proj, u_view, u_world, the fog), and drawn through its own
// immediate-mode paths with the shader swapped in. Shared by the clouds
// (sky.cpp) and the lighting pass (grade.cpp).
#include "game.h"

namespace shaders {

bool Install();
bool Ready();

// A vec4 uniform of ours. Registered before the shaders that use it are built
// (each shader takes the uniforms that exist when it's made).
int Uniform(const char *name);
void Set(int uniform, float x, float y, float z, float w = 0.0f);
// A shader from the bodies of a vertex and a fragment shader (the game's
// headers go first); null if it doesn't build, which is logged.
void *Create(const char *name, const char *vertex, const char *fragment);
// The vertex shader for whole-screen passes through RwIm2D (as the game's
// im2d shader places them); v_tex0 is passed on.
extern const char *const kScreenVertex;

// RwIm2D draws use `shader` (null: the game's own again).
void OverrideIm2D(void *shader);
// A mat4 uniform of ours (16 floats, as GL has them: column by column).
int UniformMatrix(const char *name);
void SetMatrix(int uniform, const float *m);
// The camera's projection * view, as the game's shaders have it (librw keeps
// them as it sets them); false if this build keeps them elsewhere.
bool ViewProjection(float out[16]);

// GL objects of our own beside librw's, through its GL entry points (glad).
// Our textures go on units librw never binds (4 and up), so its idea of
// what's bound stays true; framebuffers and the viewport are put back.
bool LoadGL(); // on the render thread, once GL is up; false if any is missing
// RGBA, linear and mipmapped, repeating; 0 on failure.
unsigned Texture3D(int unit, int size, const uint8_t *rgba);
unsigned Texture2D(int unit, int width, int height, const uint8_t *rgba);
// A texture to draw into (RGBA, linear, clamped) with its framebuffer.
struct Target {
	unsigned texture = 0, framebuffer = 0;
	int width = 0, height = 0;
};
// (Re)makes it at this size if it isn't; false on failure.
bool Resize(Target &target, int width, int height);
// Draws go into the target while this lasts.
class DrawInto {
public:
	explicit DrawInto(const Target &target);
	~DrawInto();

private:
	int framebuffer_ = 0, viewport_[4] = {};
};
// Points a sampler of `shader` at a texture unit.
void SetSampler(void *shader, const char *name, int unit);
void BindTexture(int unit, bool is3D, unsigned texture);

// The frame drawn so far, copied into a texture (CPostFX's back buffer, a
// power of two in size with the picture in its top rows: gl_FragCoord plus the
// difference in height, over its size); null if the game has none.
void *GrabFrame();
// The size of what's being drawn to, in pixels (gl_FragCoord's units).
void FrameSize(float &width, float &height);

// Render states, saved and put back around a pass.
class SavedStates {
public:
	SavedStates();
	~SavedStates();

private:
	void *values_[9];
};

// The time of day and the weather, as the game has them this frame.
struct Sky {
	CVector sun;        // towards it
	float daylight;     // 0 at night, 1 by day
	CVector top, bottom; // the sky's colours, 0..1
	CVector sunColour;
	float cloudCover, fog, rain;
};
Sky CurrentSky();
// Game time in seconds (it stops with the game).
float Seconds();
// The camera: where it is and which way it looks.
CVector CameraPosition();
CVector CameraForward();

} // namespace shaders
