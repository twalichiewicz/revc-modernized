#include "../tools/host/WingMapping.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
 double a[3]={2.625,0,1},b[3]={5,0,1},c[3]={2.625,-1,1},p[3]={3.21875,-.25,1},w[3];
 assert(WingBarycentric(p,a,b,c,w));assert(std::fabs(w[0]-.5)<1e-10&&std::fabs(w[1]-.25)<1e-10&&std::fabs(w[2]-.25)<1e-10);
 double damagedZ[3]={1,.4,.8};assert(std::fabs(w[0]*damagedZ[0]+w[1]*damagedZ[1]+w[2]*damagedZ[2]-.8)<1e-10);
 p[2]=1.01;assert(!WingBarycentric(p,a,b,c,w));p[2]=1;p[0]=6;assert(!WingBarycentric(p,a,b,c,w));assert(!WingBarycentric(a,a,a,c,w));
 puts("wing clipped-vertex mapping: all passed");
}
