#include "BodyContact.h"
#include <cmath>
#include <cstdio>
int main(){int fail=0;auto check=[&](bool ok,const char*name){if(!ok){std::printf("FAIL %s\n",name);++fail;}};Vec3 n;float depth;
 n={0,0,1};check(!bodycontact::Sweep({0,0,-.2f},{0,0,.4f},{0,0,0},n,{0,0,1},true,depth),"material emerging from floor beneath carrier cannot push it downward");
 n={0,0,1};check(bodycontact::Sweep({0,0,.2f},{0,0,-.4f},{0,0,0},n,{0,0,1},true,depth)&&n.z==1&&std::fabs(depth-.4f)<1e-6f,"incoming floor strike retains upward support");
 n={1,0,0};check(bodycontact::Sweep({-.2f,0,0},{.4f,0,0},{0,0,0},n,{-.5f,0,0},true,depth)&&n.x==-1&&std::fabs(depth-.4f)<1e-6f,"thin wall back face remains collidable");
 n={1,0,0};check(bodycontact::Sweep({.2f,0,0},{-.4f,0,0},{0,0,0},n,{.5f,0,0},true,depth)&&n.x==1&&std::fabs(depth-.4f)<1e-6f,"thin wall front face remains collidable");
 n={0,0,1};check(bodycontact::Sweep({0,0,-.2f},{0,0,.4f},{0,0,0},n,{0,0,-1},true,depth)&&n.z==-1,"carrier approaching ceiling from below still collides");
 if(!fail)std::puts("Native body contact direction cases pass");return fail?1:0;}
