#include "dentmodel.h"
#include <cmath>
#include <cstdio>
#include <limits>
int main(){
 int fail=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fail;}};
 dentmodel::Impact hit;using dentmodel::Make;using dentmodel::Move;
 check(!Make({0,2,0},{0,-1,0},3,hit),"small parking touch leaves metal intact");
 check(Make({0,2,0},{0,-2,0},13,hit),"hard collision creates normalized dent");
 auto p=Move({0,2,0},{0,2,0},hit);check(p.y<1.83f&&p.y>1.81f,"harder impact pushes contact inward");
 auto far=Move({0,-2,0},{0,-2,0},hit);check(far.y==-2,"opposite end of car remains intact");
 auto edge=Move({1.25f,2,0},{1.25f,2,0},hit);check(edge.x==1.25f&&edge.y==2,"radius boundary is continuous and unchanged");
 auto middle=Move({.625f,2,0},{.625f,2,0},hit);check(middle.y>p.y&&middle.y<2,"dent falls off smoothly with distance");
 for(int i=0;i<1000;i++)p=Move({0,2,0},p,hit);
 check(std::fabs(p.y-1.75f)<1e-5f,"repeated crashes cannot exceed total 25 cm cap");
 dentmodel::Impact light;Make({0,2,0},{0,-1,0},5,light);check(Move({0,2,0},{0,2,0},light).y>1.95f,"lighter hit makes smaller dent");
 check(!Make({NAN,2,0},{0,-1,0},10,hit)&&!Make({0,2,0},{0,0,0},10,hit)&&!Make({0,2,0},{0,-1,0},INFINITY,hit),"invalid contacts cannot create geometry");
 check(dentmodel::Body("chassis_hi")&&dentmodel::Body("door_lf_hi_dam")&&dentmodel::Body("bonnet_hi_ok")&&dentmodel::Body("bump_rear_hi_ok"),"native body damage variants qualify");
 check(!dentmodel::Body("wheel_lf_dummy")&&!dentmodel::Body("chassis_vlo")&&!dentmodel::Body("windscreen_hi_ok")&&!dentmodel::Body("extra1"),"wheels LOD glass and interiors remain rigid");
 check(dentmodel::Region({0,2,0},{0,-1,0})==0&&dentmodel::Region({0,-2,0},{0,1,0})==1&&dentmodel::Region({-1,0,0},{1,0,0})==2&&dentmodel::Region({1,0,0},{-1,0,0})==3&&dentmodel::Region({0,0,1},{0,0,-1})==4,"separate sides own independent contact episodes");
 float matrix[16]={0,1,0,0,-1,0,0,0,0,0,1,0,10,20,30,0};Vec3 local;
 check(dentmodel::Local(matrix,{8,21,33},false,local)&&local.x==1&&local.y==2&&local.z==3,"rotated translated contact stays attached to car");
 check(dentmodel::Local(matrix,{0,-1,0},true,local)&&local.x==-1&&local.y==0,"direction ignores car translation");
 matrix[0]=matrix[1]=0;check(!dentmodel::Local(matrix,{0,0,0},false,local),"singular frame cannot deform geometry");
 printf("dentmodel: %s\n",fail?"FAILED":"all passed");return fail?1:0;
}
