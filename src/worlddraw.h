#pragma once
// World geometry of our own drawn with our own shaders through the game's
// RwIm3D (the rain, #32: wet ground, streaks, splashes). librw here has no
// im3d override, so the shader goes in between RwIm3DTransform (which picks the
// game's) and the draw (whose flushCache sends the camera and fog to whichever
// shader is current).
#include "types.h"

#include <cstdint>
#include <vector>

namespace worlddraw {

struct Vertex {
	float x, y, z, nx, ny, nz;
	uint8_t r, g, b, a;
	float u, v;
};

bool Install(); // false if a binding is missing

// Triangles (three Put()s each) for one shader, sent in batches the 16-bit indices allow.
class Batch {
public:
	explicit Batch(void *shader) : shader_(shader) {}
	~Batch() { Flush(); }
	Batch(const Batch &) = delete;
	Batch &operator=(const Batch &) = delete;
	void Put(Vec3 p, uint8_t r, uint8_t g, uint8_t b, float u, float v, uint8_t a = 255);
	void Flush();

private:
	void *shader_;
	std::vector<Vertex> vertices_;
};

// The game's point lights this frame (CPointLights: lamps, and the headlights
// it registers), not its darkening or fog-only kinds.
struct Light {
	Vec3 pos, colour;
	float radius;
};
std::vector<Light> PointLights();

uint8_t Byte(float x); // 0..1 to 0..255, clamped

// Vehicles: every live one (CPools::ms_pVehiclePool), its axes, its drawn
// triangles in the world (glass or not; not the hidden damaged pieces or the
// far versions), and whether it's a car with its top off.
std::vector<uint8_t *> Vehicles();
struct Axes {
	Vec3 right, forward, up, pos;
};
Axes VehicleAxes(const uint8_t *vehicle);
struct Face {
	Vec3 a, b, c;
	bool glass;
};
std::vector<Face> VehicleFaces(const uint8_t *vehicle); // empty if the geometry can't be read
bool OpenTop(const uint8_t *car);                       // CAutomobile::IsOpenTopCar

} // namespace worlddraw
