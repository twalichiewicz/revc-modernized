#pragma once
// Volumetric clouds, as modern games draw them (Horizon Zero Dawn's Nubis): a
// layer of cloud over the curve of the earth, shaped by tileable noise
// volumes (Perlin-Worley billows, Worley detail eroding the edges), spread
// over the sky by a weather map, lit by the sun through the cloud between
// (with the extra light of scattering many times, the silver lining towards
// the sun and the darker "powder" edges) and the sky all round. Here are the
// noise, the shaders and the same clouds on the CPU (how much of the sun they
// hide). No game code, so it also builds on its own for testing.
#include <cstdint>
#include <string>
#include <vector>

namespace clouds {

constexpr int kShapeSize = 128, kDetailSize = 64, kWeatherSize = 512;

struct Noise {
	std::vector<uint8_t> shape;   // kShapeSize³ RGBA: Perlin-Worley; Worley at three frequencies
	std::vector<uint8_t> detail;  // kDetailSize³ RGBA: Worley at three frequencies
	std::vector<uint8_t> weather; // kWeatherSize² RGBA: coverage, cloud type (each spread evenly over 0..1)
};
// The same every time; takes a moment, spread over the cores.
Noise MakeNoise();

// The weather the clouds are drawn in.
struct Weather {
	float time;  // s (game time: they stop with the game)
	float cover; // how much of the sky they cover, 0..1
	float rain;  // 0..1: taller, darker, greyer
};
// How far the wind has carried them (m).
void Wind(float time, float &x, float &y);

// The bodies of the shaders (after the game's GLSL headers), drawn as three
// whole-screen passes each frame:
//  1. march: one pixel of every 2x2 of the clouds' resolution (a quarter of
//     them, a different one each frame of four), into a target half the size
//     each way (u_cloudJitter moves the rays onto those pixels);
//  2. resolve: the clouds at their resolution, those pixels new (blended with
//     their past) and the rest from the last frame, reprojected;
//  3. composite: over the sky, at the frame's resolution.
// The vertex shader is for all three: each pixel's view ray (v_near to v_far)
// and its place on the screen (v_screen, 0..1). Colours are premultiplied,
// alpha the clouds' cover.
extern const char *const kVertex;
std::string MarchFragment();
std::string ResolveFragment();
extern const char *const kCompositeFragment;
// The pixel of each 2x2 marched on a frame.
inline void Pick(int frame, int &x, int &y)
{
	static const int kOrder[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
	x = kOrder[frame & 3][0];
	y = kOrder[frame & 3][1];
}

// How much sunlight gets through the clouds from `from` along `dir` (unit), 0..1.
float Transmittance(const Noise &noise, const Weather &weather, const float from[3], const float dir[3]);

} // namespace clouds
