#pragma once
#include "types.h"
namespace dentmodel {
constexpr float kCap=.25f;
struct Impact {Vec3 point{},inward{};float depth=0,radius=1.25f;};
bool Make(Vec3 point,Vec3 inward,float severity,Impact &);
Vec3 Move(Vec3 original,Vec3 current,const Impact &);
int Region(Vec3 point,Vec3 inward); // front/rear/left/right/roof: 0..4
bool Body(const char *frame);
// Invert the RenderWare affine matrix; directions ignore its translation.
bool Local(const float *matrix,Vec3 input,bool direction,Vec3 &out);
}
