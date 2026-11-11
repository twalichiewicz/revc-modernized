// Material contacts handed to native collision keep their depth.
#include "BodyContact.h"
#include <cmath>
#include <cstdio>
#include <limits>
namespace {
// reVC src/collision/ColPoint.cpp, like the original game: assignment copies
// point, normal and surfaces, but not depth ("no depth?").
struct ColPoint {
 Vec3 point{},normal{};float depth=0;unsigned char surfaceA=0,pieceA=0,surfaceB=0,pieceB=0;
 ColPoint &operator=(const ColPoint &o){point=o.point;normal=o.normal;surfaceA=o.surfaceA;pieceA=o.pieceA;surfaceB=o.surfaceB;pieceB=o.pieceB;return *this;}
};
// ProcessShiftSectorList translates the actor by the depth-weighted normals.
Vec3 NativeShift(const ColPoint *points,int count){
 int most=0;for(int j=1;j<count;++j)if(points[j].depth>points[most].depth)most=j;Vec3 shift{};
 for(int j=0;j<count;++j){shift.x+=points[j].normal.x*points[j].depth*1.2f/count;shift.y+=points[j].normal.y*points[j].depth*1.2f/count;shift.z+=points[j].normal.z*points[j].depth*1.2f/count;}
 shift.x+=points[most].normal.x*points[most].depth*.2f;shift.y+=points[most].normal.y*points[most].depth*.2f;shift.z+=points[most].normal.z*points[most].depth*.2f;return shift;
}
}
int main(){
 int fail=0;auto check=[&](bool ok,const char *name){if(!ok){std::printf("FAIL %s\n",name);++fail;}};
 // The shift pass's colpoints are an uninitialised stack array; the collision
 // pass's are static and hold earlier depths. Seed both kinds of leftovers.
 const float stale[]={std::numeric_limits<float>::quiet_NaN(),2.0e37f,1.0f,-3.3575f};
 for(float leftover:stale){
  ColPoint points[2];for(auto &p:points)p.depth=leftover;
  ColPoint floor;floor.point={0,0,0};floor.normal={0,0,1};floor.depth=bodycontact::kShiftDepth;floor.surfaceB=1;
  ColPoint rest;rest.point={.5f,0,0};rest.normal={0,0,1};rest.depth=.012f;rest.surfaceB=4;
  bodycontact::Store(points[0],floor);bodycontact::Store(points[1],rest);
  check(points[0].depth==bodycontact::kShiftDepth&&points[1].depth==.012f,"stored material contacts carry their own depth");
  check(points[0].surfaceB==1&&points[1].surfaceB==4&&points[1].point.x==.5f&&points[0].normal.z==1,"stored contacts keep point, normal and surfaces");
  const Vec3 shift=NativeShift(points,2);
  check(std::isfinite(shift.z)&&shift.z>=0&&shift.z<.01f,"native shift from material contacts stays within their depth");
 }
 // The engine's assignment alone leaves the leftover depth behind.
 ColPoint leaked;leaked.depth=2.0e37f;ColPoint floor;floor.normal={0,0,1};floor.depth=bodycontact::kShiftDepth;leaked=floor;
 check(leaked.depth==2.0e37f,"stand-in reproduces the engine's depth-dropping assignment");
 if(!fail)std::puts("Body contact depth cases pass");return fail?1:0;
}
