#include "airframemodel.h"
#include <algorithm>
#include <cmath>
namespace airframemodel {namespace {
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float Clamp(float f){return std::clamp(f,0.f,1.f);}
}
bool Build(int model,int kind,Vec3 lo,Vec3 hi,Layout &l)
{
 if(!Finite(lo)||!Finite(hi)||lo.x>=hi.x||lo.y>=hi.y||lo.z>=hi.z)return false;
 l={};l.body={lo,hi};l.helicopter=(kind==0&&model!=194)||kind==3;l.single=l.helicopter||model==181||model==190||model==194;
 const float x=(hi.x-lo.x)*.5f,y=hi.y-lo.y,z=hi.z-lo.z,cx=(lo.x+hi.x)*.5f;
 auto box=[&](float ax,float bx,float ay,float by,float az,float bz){return Box{{cx+ax*x,lo.y+ay*y,lo.z+az*z},{cx+bx*x,lo.y+by*y,lo.z+bz*z}};};
 l.volumes[EngineL]=l.single?box(-.15f,.15f,.65f,.95f,.3f,.7f):box(-.55f,-.25f,.38f,.62f,.2f,.6f);
 l.volumes[EngineR]=l.single?l.volumes[EngineL]:box(.25f,.55f,.38f,.62f,.2f,.6f);
 l.volumes[FuelL]=box(-.45f,-.05f,.3f,.58f,.2f,.45f);l.volumes[FuelR]=box(.05f,.45f,.3f,.58f,.2f,.45f);
 l.volumes[WingL]=box(-1,-.24f,.3f,.62f,.1f,.65f);l.volumes[WingR]=box(.24f,1,.3f,.62f,.1f,.65f);
 l.volumes[Elevator]=box(-.45f,.45f,0,.16f,.2f,.65f);l.volumes[Rudder]=box(-.08f,.08f,0,.18f,.5f,1);
 l.volumes[AileronL]=box(-1,-.5f,.27f,.38f,.1f,.65f);l.volumes[AileronR]=box(.5f,1,.27f,.38f,.1f,.65f);
 l.volumes[Hydraulics]=box(-.12f,.12f,.3f,.55f,.15f,.4f);l.volumes[Electrical]=box(-.15f,.15f,.6f,.82f,.3f,.65f);
 l.volumes[MainRotor]=box(-1,1,.15f,.95f,.85f,1);l.volumes[TailRotor]=box(-.2f,.2f,0,.15f,.45f,.9f);
 return true;
}
projectilemodel::Result Projectile(State &s,const Layout &l,const projectilemodel::Ray &ray)
{
 static_assert(int(Count)==int(mechanicalmodel::kModules));
 float resistance[]={65,65,8,8,14,14,16,16,12,12,15,12,28,28};
 const float capacity[]={140,140,45,45,160,160,90,90,55,55,55,45,100,60};
 if(l.single)resistance[EngineR]=0;
 if(l.helicopter)for(int i:{WingL,WingR,Elevator,Rudder,AileronL,AileronR})resistance[i]=0;
 else resistance[MainRotor]=resistance[TailRotor]=0;
 const auto hit=projectilemodel::Absorb(l.body,l.volumes,resistance,capacity,ray);if(!hit.valid)return hit;
 for(int i=0;i<Count;i++)s.damage[i]=Clamp(s.damage[i]+hit.doses[i]);
 if(l.single)s.damage[EngineR]=s.damage[EngineL];return hit;
}
void Impact(State &s,const Layout &l,Vec3 p,float severity)
{
 if(!Finite(p)||!std::isfinite(severity)||severity<=0)return;
 for(int i=0;i<Count;i++){
  if((l.helicopter&&(i==WingL||i==WingR||i==Elevator||i==Rudder||i==AileronL||i==AileronR))||(!l.helicopter&&(i==MainRotor||i==TailRotor)))continue;
  const auto &b=l.volumes[i];if(p.x>=b.lo.x&&p.x<=b.hi.x&&p.y>=b.lo.y&&p.y<=b.hi.y&&p.z>=b.lo.z&&p.z<=b.hi.z)s.damage[i]=Clamp(s.damage[i]+severity*.1f);
 }
 if(l.single)s.damage[EngineR]=s.damage[EngineL];
}
void Tick(State &s,float dt,bool running)
{
 if(!std::isfinite(dt)||dt<=0||dt>1)return;
 const float leak=std::max(s.damage[FuelL],s.damage[FuelR]);s.fuel=std::max(0.f,s.fuel-dt*(leak*.02f+(running?.00012f:0)));
 const float target=running?.4f+std::max(s.damage[EngineL],s.damage[EngineR]):.2f;s.heat+=(target-s.heat)*(1-std::exp(-dt*.08f));
}
Effects Read(const State &s,const Layout &)
{
 Effects e;e.managed=true;e.power=s.fuel>0?1-(s.damage[EngineL]+s.damage[EngineR])*.5f:0;
 e.leftLift=1-.85f*s.damage[WingL];e.rightLift=1-.85f*s.damage[WingR];const float hydraulic=1-.9f*s.damage[Hydraulics];
 e.pitch=(1-s.damage[Elevator])*hydraulic;e.roll=(1-(s.damage[AileronL]+s.damage[AileronR])*.5f)*hydraulic;e.yaw=(1-s.damage[Rudder])*hydraulic;
 e.rotor=1-s.damage[MainRotor];e.antiTorque=1-s.damage[TailRotor];e.drag=std::min(1.f,s.holes*.1f+.15f*(s.damage[WingL]+s.damage[WingR]));return e;
}
firemodel::Input Fire(const State &s,bool running){firemodel::Input f;f.heat=s.heat;f.fuel=s.fuel;f.engine=std::max(s.damage[EngineL],s.damage[EngineR]);f.electrical=s.damage[Electrical];f.breach=std::max(s.damage[FuelL],s.damage[FuelR]);f.running=running;return f;}
}
