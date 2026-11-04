#include "mechanicalmodel.h"
#include <cstdio>
#include <cmath>
using namespace mechanicalmodel;
int main(){int fails=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fails;}};
 std::array<Vec3,4> wheels{{{-1,1.4f,-.35f},{-1,-1.4f,-.35f},{1,1.4f,-.35f},{1,-1.4f,-.35f}}};
 Layout front,rear;check(Build(150,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},wheels,front)&&Build(211,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},wheels,rear),"native car geometry creates per-model layout");
 auto nose=Doses(front,{{0,2.4f,.1f},{0,-1,0},24,3});auto tail=Doses(rear,{{0,-2.4f,.1f},{0,1,0},24,4});
 check(nose[0]>0&&nose[1]>0&&tail[0]>0,"deep bay impacts reach actual front/rear engine volume");
 check(Doses(rear,{{0,2.4f,.1f},{0,-1,0},24,3})[0]==0,"Deluxo frontal damage does not injure rear engine");
 auto cabin=Doses(front,{{1.2f,0,.6f},{-1,0,0},12,5});check(cabin[0]==0&&cabin[1]==0,"cabin damage preserves engine and cooling");
 auto corner=Doses(front,{{-1,1.4f,-.35f},{1,0,0},20,13});check(corner[2]>0&&corner[3]==0&&corner[4]==0&&corner[5]==0,"left-front wheel hit leaves other wheel assemblies intact");
 check(Doses(front,{{0,2.4f,.1f},{0,-1,0},3,3})==std::array<float,6>{},"parking touches do not damage mechanics");
 State s;Apply(s,corner,{1,0,0});auto wheel=Wheel(s,0);check(wheel.grip<1&&wheel.drive<1&&wheel.brake<1&&wheel.toe>0&&Wheel(s,2).grip==1,"one bent assembly changes its own performance");
 for(int i=0;i<100;i++)Apply(s,corner,{1,0,0});wheel=Wheel(s,0);check(wheel.grip>=.4499f&&wheel.drive>=.6499f&&wheel.brake>=.4999f&&std::fabs(wheel.toe)<=.10001f,"repeated wheel damage stays bounded");
 s={};Apply(s,nose,{0,-1,0});check(Power(s)<1&&Power(s)>0,"engine injury reduces delivered power");
 for(int i=0;i<20;i++)Apply(s,nose,{0,-1,0});check(Power(s)==0,"destroyed engine delivers no torque");
 State intact;for(int i=0;i<30000;i++)Tick(intact,.02f,true,1,0);check(Power(intact)>.99f&&intact.coolant==1,"intact cooling remains stable under sustained throttle");
 State a,b;a.damage[1]=b.damage[1]=1;for(int i=0;i<9000;i++)Tick(a,.02f,true,1,15);for(int i=0;i<4500;i++)Tick(b,.04f,true,1,15);
 check(a.coolant<.01f&&a.heat>.8f&&Power(a)<.5f,"breached cooling leaks and progressively removes power");
 check(std::fabs(a.heat-b.heat)<.005f&&std::fabs(a.coolant-b.coolant)<.005f,"thermal effects agree across frame rates");
 float heat=a.heat;for(int i=0;i<2000;i++)Tick(a,.02f,false,0,0);check(a.heat<heat,"stopped engine cools");
 check(Doses(front,{{NAN,0,0},{1,0,0},20,3})==std::array<float,6>{},"invalid impact cannot damage modules");
 check(!Build(150,{-1,-2,-1},{1,2,1},{},front),"missing wheel geometry cannot invent internals");
 printf("mechanicalmodel: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}
