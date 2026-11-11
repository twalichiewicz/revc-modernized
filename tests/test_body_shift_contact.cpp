// Material contacts during native shift passes and floor emergence.
#include "BodyContact.h"
#include <cmath>
#include <cstdio>
int main(){
 int fail=0;auto check=[&](bool ok,const char *name){if(!ok){std::printf("FAIL %s\n",name);++fail;}};Vec3 n;float depth;
 // A shift pass moves without CCD. Its own move through a floor is reported,
 // with minimal depth, so ProcessShift undoes the move instead of shifting.
 n={0,0,1};check(bodycontact::ShiftCrossing({0,0,.05f},{0,0,-.3f},{0,0,0},n,depth)&&n.z==1&&depth==bodycontact::kShiftDepth,"shift move through a floor is caught with minimal depth");
 n={0,0,1};check(bodycontact::Sweep({0,0,.05f},{0,0,-.3f},{0,0,0},n,{0,0,1.2f},true,depth)&&std::fabs(depth-.3f)<1e-6f,"frame sweep keeps its full crossing depth");
 n={1,0,0};check(bodycontact::ShiftCrossing({.2f,0,0},{-.4f,0,0},{0,0,0},n,depth)&&n.x==1&&depth==bodycontact::kShiftDepth,"shift move entering a wall face is caught");
 n={1,0,0};check(!bodycontact::ShiftCrossing({-.2f,0,0},{.4f,0,0},{0,0,0},n,depth),"shift move leaving through a back face resolves earlier penetration");
 n={0,0,1};check(!bodycontact::ShiftCrossing({0,0,-.2f},{0,0,.1f},{0,0,0},n,depth),"shift move lets material emerge upward through a floor");
 // A sinking carrier: its origin is already below the road, its roof is not.
 // The emergence reference is the body's top at the sweep start pose.
 const Vec3 sunkOrigin{0,0,-.15f},startTop{0,0,.75f};
 n={0,0,1};check(bodycontact::Sweep({0,0,-.3f},{0,0,.05f},{0,0,0},n,sunkOrigin,true,depth)&&n.z==-1,"the old origin reference pushes an emerging vertex of a sinking car down");
 n={0,0,1};check(!bodycontact::Sweep({0,0,-.3f},{0,0,.05f},{0,0,0},n,startTop,true,depth),"the start-pose top reference lets a sinking car's material emerge");
 // A body wholly under a slab still meets its floor face from below.
 n={0,0,1};check(bodycontact::Sweep({0,0,-.2f},{0,0,.05f},{0,0,0},n,{0,0,-.05f},true,depth)&&n.z==-1,"a body wholly below a slab still collides with it");
 // Moving actors' faces never receive the static-floor exemption.
 n={0,0,1};check(bodycontact::Sweep({0,0,-.2f},{0,0,.1f},{0,0,0},n,{0,0,1},false,depth)&&n.z==-1,"another vehicle's floor face stays two-sided in the frame sweep");
 if(!fail)std::puts("Body shift contact cases pass");return fail?1:0;
}
