// A material probe whose centre is inside a solid box exits by the nearest face.
#include "BodyContact.h"
#include <cmath>
#include <cstdio>
#include <limits>
namespace {
int failures=0;
void Check(bool ok,const char *name){if(!ok){std::printf("FAIL %s\n",name);++failures;}}
bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
// reVC src/collision/Collision.cpp ProcessSphereBox, centre-inside branch, as
// in the original game: normal from the box centre, point 1 m along it, depth
// the largest of the three closer-side distances.
void NativeInside(Vec3 c,Vec3 lo,Vec3 hi,Vec3 &point,Vec3 &normal,float &depth){
 const Vec3 mid{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f},d{c.x-mid.x,c.y-mid.y,c.z-mid.z};
 const float len=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);normal={d.x/len,d.y/len,d.z/len};point={c.x-normal.x,c.y-normal.y,c.z-normal.z};
 const float dx=d.x>0?hi.x-c.x:c.x-lo.x,dy=d.y>0?hi.y-c.y:c.y-lo.y,dz=d.z>0?hi.z-c.z:c.z-lo.z;
 depth=dx>dy&&dx>dz?dx:dy>dz?dy:dz;
}
}
int main(){
 const float r=.02f;
 // veg_palm04 (generic.col): one trunk box. Playtest run: a Deluxo's front
 // bumper corner 5 cm inside its +x face, 0.618 m above its base.
 const Vec3 lo{-1.381f,-.595f,-.073f},hi{0,.595f,27.993f},corner{-.05f,.30f,.545f};
 {Vec3 p,n;float depth;NativeInside(corner,lo,hi,p,n,depth);
  Check(n.z<-.99f&&Near(depth,.618f,1e-3f)&&Near(p.z-corner.z,-n.z,1e-4f),"stand-in reproduces the native straight-down 0.618 m reading");}
 {Vec3 p,n;float depth;const bool inside=bodycontact::InsideBox(corner,r,lo,hi,p,n,depth);
  Check(inside,"probe centre inside the trunk is an inside contact");
  Check(Near(n.x,1)&&Near(n.y,0)&&Near(n.z,0),"nearest-face normal: out through +x, not down");
  Check(Near(depth,.05f+r),"depth: distance to the nearest face plus the probe radius");
  Check(Near(p.x,0)&&Near(p.y,corner.y)&&Near(p.z,corner.z),"contact point on the nearest face, beside the probe");}
 // Every face is chosen when it is the nearest.
 {const Vec3 a{-1,-1,-1},b{1,1,1};struct Case{Vec3 c,n;}cases[]={{{-.9f,0,0},{-1,0,0}},{{.9f,0,0},{1,0,0}},{{0,-.9f,0},{0,-1,0}},{{0,.9f,0},{0,1,0}},{{0,0,-.9f},{0,0,-1}},{{0,0,.9f},{0,0,1}}};
  for(auto &k:cases){Vec3 p,n;float depth;const bool inside=bodycontact::InsideBox(k.c,r,a,b,p,n,depth);Check(inside&&Near(n.x,k.n.x)&&Near(n.y,k.n.y)&&Near(n.z,k.n.z)&&Near(depth,.1f+r),"each face exits when nearest");}}
 // Outside or on the surface is the native outside case, not this one.
 {Vec3 p,n;float depth;
  Check(!bodycontact::InsideBox({.5f,0,.5f},r,lo,hi,p,n,depth),"outside the box is not an inside contact");
  Check(!bodycontact::InsideBox({0,0,.5f},r,lo,hi,p,n,depth),"on a face is not an inside contact");
  const float nan=std::numeric_limits<float>::quiet_NaN();
  Check(!bodycontact::InsideBox({nan,0,.5f},r,lo,hi,p,n,depth),"non-finite probe is rejected");}
 if(failures)return 1;std::printf("Body inside-box cases pass\n");return 0;
}
