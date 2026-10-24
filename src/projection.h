#pragma once
// Perspective ("tilted 3D") projection of the radar's ground plane onto a
// rectangle on screen. No game dependencies, so it is unit-tested.
//
// Ground space: the player is the origin, +y points the way the camera faces,
// one unit is one radar range (CRadar::m_radarRange world units). This is the
// game's own radar space, so tiles, blips and the route all share it.
#include "types.h"

struct ViewParams {
	float tiltDeg = 50.0f; // 0 = flat top-down map
	float camDist = 3.5f;  // virtual camera distance; smaller = stronger perspective
	float zoom = 1.5f;     // scale at the player, in rect half-heights per radar range
	float anchor = 0.45f;  // how far below the rect centre the player sits (half-heights)
};

// A polygon vertex being clipped: ground position plus a payload (world
// position) that is interpolated along with it.
struct ClipVert {
	Vec2 g;
	Vec2 world;
};

class Projector {
public:
	// x, y: top-left of the rectangle in pixels.
	void Setup(const ViewParams &params, float x, float y, float w, float h);

	// Ground -> pixel. Returns false for points at or behind the camera plane.
	bool GroundToScreen(Vec2 g, Vec2 &px) const { return Project(g, 0.0f, px); }
	// A point `height` radar-range units above the ground.
	bool Project(Vec2 g, float height, Vec2 &px) const;
	// Camera-space depth (larger = farther), for back-to-front sorting.
	float Depth(Vec2 g, float height) const;
	// Where the virtual camera stands over the ground plane, and how high.
	Vec2 CameraGround() const { return {0.0f, focus_ - dist_ * sin_}; }
	float CameraHeight() const { return dist_ * cos_; }
	// Screen pixels per ground unit at the player's depth.
	float PixelsPerUnit() const { return k_ * dist_ / (dist_ - focus_ * sin_) * 0.5f * h_; }
	// Normalised rect coordinates (x in [-aspect, aspect], y in [-1, 1], +y up) -> ground.
	Vec2 NormToGround(Vec2 n) const;

	bool Inside(Vec2 g) const;
	// Moves an outside point onto the visible edge along the ray from the player.
	Vec2 ClampToEdge(Vec2 g) const;
	// Ground distance from the player to the farthest visible corner.
	float MaxVisibleDistance() const;
	// Visible ground area, counter-clockwise: bottom-left, bottom-right, top-right, top-left.
	const Vec2 *Corners() const { return corners_; }

	// Sutherland-Hodgman against the visible area. `out` needs room for n + 4 vertices.
	int ClipPolygon(const ClipVert *in, int n, ClipVert *out) const;
	// Clips a segment in place; false if nothing of it is visible.
	bool ClipSegment(Vec2 &a, Vec2 &b) const;

	float X() const { return x_; }
	float Y() const { return y_; }
	float W() const { return w_; }
	float H() const { return h_; }

private:
	float EdgeSide(int edge, Vec2 p) const;

	float x_ = 0, y_ = 0, w_ = 1, h_ = 1;
	float sin_ = 0, cos_ = 1, dist_ = 1, k_ = 1, focus_ = 0;
	Vec2 corners_[4] = {};
};
