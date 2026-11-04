#include "mechanicalmodel.h"
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace mechanicalmodel;
int main(){int fails=0;auto check=[&](bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fails;}};
 std::array<Vec3,4> wheels{{{-1,1.4f,-.35f},{-1,-1.4f,-.35f},{1,1.4f,-.35f},{1,-1.4f,-.35f}}};
 Layout front,rear;check(Build(150,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},wheels,front)&&Build(211,{-1.2f,-2.5f,-.7f},{1.2f,2.5f,1.2f},wheels,rear),"native car geometry creates per-model layout");
 auto nose=Doses(front,{{0,2.4f,.1f},{0,-1,0},24,3});auto tail=Doses(rear,{{0,-2.4f,.1f},{0,1,0},24,4});
 check(nose[0]>0&&nose[1]>0&&tail[0]>0,"deep bay impacts reach actual front/rear engine volume");
 check(Doses(rear,{{0,2.4f,.1f},{0,-1,0},24,3})[0]==0,"Deluxo frontal damage does not injure rear engine");
 auto cabin=Doses(front,{{1.2f,0,.6f},{-1,0,0},12,5});check(cabin[0]==0&&cabin[1]==0,"cabin damage preserves engine and cooling");
 auto corner=Doses(front,{{-1,1.4f,-.35f},{1,0,0},20,13});check(corner[2]>0&&corner[3]==0&&corner[4]==0&&corner[5]==0,"left-front wheel hit leaves other wheel assemblies intact");
 check(Doses(front,{{0,2.4f,.1f},{0,-1,0},3,3})==std::array<float,kModules>{},"parking touches do not damage mechanics");
 State s;Apply(s,corner,{1,0,0});auto wheel=Wheel(s,0);check(wheel.grip<1&&wheel.drive<1&&wheel.brake<1&&wheel.toe>0&&Wheel(s,2).grip==1,"one bent assembly changes its own performance");
 for(int i=0;i<100;i++)Apply(s,corner,{1,0,0});wheel=Wheel(s,0);check(wheel.grip>=.4499f&&wheel.drive>=.6499f&&wheel.brake>=0&&std::fabs(s.toe[0])<=.10001f&&std::fabs(wheel.toe)<=.16001f,"repeated wheel damage stays bounded");
 s={};Apply(s,nose,{0,-1,0});check(Power(s)<1&&Power(s)>0,"engine injury reduces delivered power");
 for(int i=0;i<20;i++)Apply(s,nose,{0,-1,0});check(Power(s)==0,"destroyed engine delivers no torque");
 State intact;for(int i=0;i<30000;i++)Tick(intact,.02f,true,1,0);check(Power(intact)>.99f&&intact.coolant==1,"intact cooling remains stable under sustained throttle");
 State a,b;a.damage[1]=b.damage[1]=1;for(int i=0;i<9000;i++)Tick(a,.02f,true,1,15);for(int i=0;i<4500;i++)Tick(b,.04f,true,1,15);
 check(a.coolant<.01f&&a.heat>.8f&&Power(a)<.5f,"breached cooling leaks and progressively removes power");
 check(std::fabs(a.heat-b.heat)<.005f&&std::fabs(a.coolant-b.coolant)<.005f,"thermal effects agree across frame rates");
 float heat=a.heat;for(int i=0;i<2000;i++)Tick(a,.02f,false,0,0);check(a.heat<heat,"stopped engine cools");
 check(Doses(front,{{NAN,0,0},{1,0,0},20,3})==std::array<float,kModules>{},"invalid impact cannot damage modules");
 check(!Build(150,{-1,-2,-1},{1,2,1},{},front),"missing wheel geometry cannot invent internals");
 // Dedicated failures must change only their own consumers, independently of hub damage.
 check(kModules==14,"fourteen independent mechanical modules");
 if(kModules==14){
  auto module=[](const char *name){for(int i=0;i<kModules;i++)if(!std::strcmp(Name(i),name))return i;return -1;};
  const int brake=module("brake_lf"),rack=module("steering"),gearbox=module("drivetrain"),fuel=module("fuel"),electric=module("electrical");
  check(brake>=0&&rack>=0&&gearbox>=0&&fuel>=0&&electric>=0,"named individual systems");
  if(brake>=0&&rack>=0&&gearbox>=0&&fuel>=0&&electric>=0){
   std::array<float,kModules> independent{};independent[brake]=.6f;State applied;Apply(applied,independent,{1,0,0});
   check(applied.toe==std::array<float,4>{},"brake damage does not index or bend wheel alignment");
   independent={};independent[rack]=1;Apply(applied,independent,{1,0,0});
   check(Wheel(applied,0).steering<.21f&&Wheel(applied,2).toe>0&&Wheel(applied,1).toe==0&&Wheel(applied,1).steering==1,"rack injury reduces front steering with signed bias and leaves rear direction intact");
   State brakeOnly;brakeOnly.damage[brake]=.8f;auto e=Wheel(brakeOnly,0);
   check(e.brake<.3f&&e.grip==1&&e.drive==1&&e.toe==0&&Wheel(brakeOnly,2).brake==1,"caliper damage independently removes one wheel brake authority");
   brakeOnly.damage[brake]=1;check(Wheel(brakeOnly,0).brake==0,"destroyed brake has no service braking");
   State drive;drive.damage[gearbox]=.5f;check(Power(drive)>.55f&&Power(drive)<.65f,"gearbox injury reduces drive with healthy engine");
   drive.damage[gearbox]=1;check(Power(drive)==0,"destroyed drivetrain disconnects drive");
   State battery;battery.damage[electric]=.5f;check(Power(battery)>.6f&&Power(battery)<.7f,"electrical injury weakens ignition separately from engine integrity");
   check(Lighting(battery)>.59f&&Lighting(battery)<.61f,"damaged supply dims lights progressively");
   battery.damage[electric]=1;check(Lighting(battery)==0,"failed supply extinguishes lamps");check(Power(battery)==0,"destroyed electrical supply removes ignition torque");
   State leak;leak.damage[fuel]=1;check(Power(leak)==1,"tank breach does not instantly empty the tank");
   for(int i=0;i<90;i++)Tick(leak,1,false,0,0);check(Power(leak)==0,"parked breached tank drains and removes drive");
   State fine,coarse;fine.damage[1]=coarse.damage[1]=fine.damage[fuel]=coarse.damage[fuel]=1;
   for(int i=0;i<18000;i++)Tick(fine,.01f,true,1,15);for(int i=0;i<600;i++)Tick(coarse,.3f,true,1,15);
   check(Power(fine)==0&&Power(coarse)==0&&std::fabs(fine.heat-coarse.heat)<.01f&&std::fabs(fine.coolant-coarse.coolant)<.005f,"slow updates integrate reservoir failure like ordinary frame rates");
   State stopped;stopped.damage[fuel]=1;for(int i=0;i<100;i++)Tick(stopped,5,true,1,0);check(Power(stopped)==1,"large discontinuities cannot drain fuel");
   check(corner[brake]>0&&corner[brake+1]==0&&corner[brake+2]==0&&corner[brake+3]==0,"corner contact reaches only its local brake");
   auto rackHit=Doses(front,{{0,1.4f,-.3f},{0,-1,0},12,3});check(rackHit[rack]>0,"front axle impact reaches steering rack");
   auto tankHit=Doses(front,{{0,-2.4f,-.1f},{0,1,0},24,4});check(tankHit[fuel]>0&&tankHit[gearbox]==0&&tankHit[electric]==0,"front-engine rear hit breaches tank without gearbox or battery damage");
   auto rearTank=Doses(rear,{{0,2.4f,-.1f},{0,-1,0},24,3});check(rearTank[fuel]>0&&rearTank[gearbox]==0&&rearTank[electric]==0,"rear-engine front tank remains separate from rear drivetrain");
  }
 }
 printf("mechanicalmodel: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}
