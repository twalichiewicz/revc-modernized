#pragma once
// The brick phone that phone.cpp slides up for calls, as geometry on a Canvas
// (no game dependencies), so tools/preview can draw it too.
#include "canvas.h"

// In units of its width: the body's height, and the antenna above it.
constexpr float kBrickPhoneHeight = 2.75f, kBrickPhoneAntenna = 0.62f;

struct BrickPhone {
	float x = 0, y = 0, unit = 100;        // the body's top-left on screen, and its width in pixels
	float tilt = 0;                        // radians about the bottom centre: it rocks as it rings
	const char *line1 = "", *line2 = "";   // the LCD, in 5x7 dots (A-Z, 0-9, ':', '*', '#')
	float scroll = 0;                      // seconds since the lines changed: long ones scroll past
	float sendGlow = 0;                    // 0..1: the green send key lit up
};

void DrawBrickPhone(Canvas &canvas, const BrickPhone &phone);
// Where a point of the phone (units of its width, y down from the top of the body) is on screen.
Vec2 BrickPhonePoint(const BrickPhone &phone, float x, float y);
// The LCD's vertical centre on screen, for things placed beside it.
float BrickPhoneLcdCentre(const BrickPhone &phone);
