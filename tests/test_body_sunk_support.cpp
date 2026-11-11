// Material vertices that slip just below a floor are pushed back up through it,
// for a body on its wheels, its side or its roof.
#include "BodyContact.h"
#include <cmath>
#include <cstdio>
#include <limits>
namespace {
int failures=0;
void Check(bool ok,const char *name){if(!ok){std::printf("FAIL %s\n",name);++failures;}}
bool Near(float a,float b,float e=1e-4f){return std::fabs(a-b)<=e;}
}
int main(){
 using namespace bodycontact;
 // Landstalker-sized box, centre 1.2 m above a street at z=0.
 const Vec3 centre{0,0,1.2f},half{.95f,2.4f,.9f};
 // The previous reference: the local top-centre carried by the pose.
 auto localTop=[&](Vec3 up){return Vec3{centre.x+up.x*half.z,centre.y+up.y*half.z,centre.z+up.z*half.z};};
 {const Vec3 top=WorldTop(centre,{1,0,0},{0,1,0},{0,0,1},half);Check(Near(top.z,2.1f),"upright: world top is the roof");}
 {const Vec3 up{0,0,-1},top=WorldTop(centre,{-1,0,0},{0,1,0},up,half);
  Check(localTop(up).z<.5f,"stand-in: on its roof the local top is under the car");
  Check(Near(top.z,2.1f),"on its roof: world top is the underside, still above the street");}
 {const Vec3 top=WorldTop(centre,{0,0,1},{0,1,0},{-1,0,0},half);Check(Near(top.z,1.2f+.95f),"on its side: world top is the side panel");}
 {const float s=std::sqrt(.5f);const Vec3 top=WorldTop(centre,{1,0,0},{0,s,s},{0,-s,s},half);Check(Near(top.z,1.2f+s*2.4f+s*.9f),"pitched 45 degrees: the highest corner");}
 const float r=.02f;const Vec3 floorUp{0,0,1},onFloor{.3f,.4f,0},above=WorldTop(centre,{-1,0,0},{0,1,0},{0,0,-1},half);
 // A roof vertex 1 cm below the street reads the street's underside: normal
 // down, native depth = skin - distance = 0.01.
 {Vec3 n;float exit;const bool sunk=PassedThroughFace({0,0,-1},floorUp,onFloor,above,.01f,r,n,exit);
  Check(sunk,"vertex 1 cm under the street of an above-street body has sunk");
  Check(Near(n.z,1)&&Near(n.x,0)&&Near(n.y,0),"it exits up through the street");
  Check(Near(exit,.03f),"exit depth: distance below plus the skin");}
 {Vec3 n;float exit;
  Check(!PassedThroughFace({0,0,1},floorUp,onFloor,above,.01f,r,n,exit),"vertex resting above the street is not sunk");
  Check(!PassedThroughFace({0,0,-1},{0,0,-1},onFloor,above,.01f,r,n,exit),"vertex under a ceiling keeps the ceiling's push");
  Check(!PassedThroughFace({-1,0,0},{1,0,0},onFloor,above,.01f,r,n,exit),"thin wall seen from behind stays two-sided");
  Check(!PassedThroughFace({0,0,-1},floorUp,{0,0,5},above,.01f,r,n,exit),"body wholly under a deck (tunnel) is left native");
  Check(!PassedThroughFace({0,0,-1},floorUp,onFloor,above,std::numeric_limits<float>::quiet_NaN(),r,n,exit),"non-finite depth is rejected");}
 // A wall works the same way: a body in front of a face whose vertex reads the
 // face from behind has pushed that vertex through it (clipping into a building).
 {Vec3 n;float exit;const Vec3 wallOut{0,1,0},onWall{.2f,0,1},bodyFront{0,2.4f,1.2f};
  Check(PassedThroughFace({0,-1,0},wallOut,onWall,bodyFront,.015f,r,n,exit)&&Near(n.y,1)&&Near(exit,.025f),"vertex behind a facade of a car in front of it exits back out");
  Check(!PassedThroughFace({0,1,0},wallOut,onWall,bodyFront,.015f,r,n,exit),"vertex touching the facade from the front is not through it");}
 // Rest-probe depth is skin minus the distance above the face; crossing depth
 // is the distance below it. Only a probe touching from outside is resting.
 Check(Resting(.019f,false,false),"probe 0.1 cm above the street is resting");
 Check(!Resting(.019f,false,true),"vertex 1.9 cm through the street is not resting");
 Check(!Resting(.05f,false,false),"deeper than the skin is not resting");
 // Penetration slop: a car pressed on a wall may cross it by a few millimetres
 // a frame without native stuck handling freezing it in place (a 31 degree wedge).
 Check(Resting(.002f,false,true),"a 2 mm crossing is within the penetration slop");
 Check(!Resting(.006f,false,true),"a 6 mm crossing is penetration");
 Check(!Resting(.01f,true,false),"a yielded contact is counted by the yield, not as resting");
 if(failures)return 1;std::printf("Body sunk-support cases pass\n");return 0;
}
