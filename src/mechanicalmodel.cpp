#include "mechanicalmodel.h"
#include "materialmodel.h"
#include <algorithm>
#include <cmath>
namespace mechanicalmodel {namespace {
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Box Around(Vec3 p,Vec3 half){return {{p.x-half.x,p.y-half.y,p.z-half.z},{p.x+half.x,p.y+half.y,p.z+half.z}};}
float Unit(float x){return std::clamp(x,0.0f,1.0f);}
}
bool Build(int model,Vec3 lo,Vec3 hi,const std::array<Vec3,4> &w,Layout &out)
{
 if(!Finite(lo)||!Finite(hi)||hi.x-lo.x<.5f||hi.x-lo.x>8||hi.y-lo.y<1||hi.y-lo.y>20||hi.z-lo.z<.3f||hi.z-lo.z>8)return false;
 for(auto p:w)if(!Finite(p)||std::fabs(p.x)>5||std::fabs(p.y)>10||std::fabs(p.z)>5)return false;
 if(w[0].x>=0||w[1].x>=0||w[2].x<=0||w[3].x<=0||w[0].y<=w[1].y+.5f||w[2].y<=w[3].y+.5f)return false;
 const float front=(w[0].y+w[2].y)*.5f,rear=(w[1].y+w[3].y)*.5f,width=(hi.x-lo.x)*.5f;
 const float z=(w[0].z+w[1].z+w[2].z+w[3].z)*.25f+.55f;
 float engineY=front-.2f;if(model==211||model==210)engineY=rear-.15f;
 if(model==141||model==145||model==236)engineY=rear+.55f;
 out.body={lo,hi};out.wheels=w;out.volumes[0]=Around({0,engineY,z},{std::min(.75f,width*.6f),std::clamp((front-rear)*.21f,.45f,.8f),.45f});
 out.volumes[1]=Around({0,model==210?engineY:hi.y-.2f,z-.1f},{std::min(.9f,width*.75f),.2f,.4f});
 for(int i=0;i<4;i++){
  out.volumes[kWheelLF+i]=Around(w[i],{.3f,.4f,.4f});
  auto caliper=w[i];caliper.x+=i<2?.12f:-.12f;
  out.volumes[kBrakeLF+i]=Around(caliper,{.15f,.2f,.22f});
 }
 out.volumes[kSteering]=Around({0,front-.15f,z-.45f},{std::min(.7f,width*.65f),.18f,.18f});
 out.volumes[kDrivetrain]=Around({0,engineY+(engineY>0?-.4f:.4f),z-.2f},{.35f,.35f,.3f});
 out.volumes[kFuel]=Around({0,engineY>0?rear+.1f:front-.05f,z-.3f},{std::min(.65f,width*.55f),.45f,.25f});
 out.volumes[kElectrical]=Around({width*.45f,engineY,z+.15f},{.22f,.3f,.25f});
 return true;
}
std::array<float,kModules> Doses(const Layout &layout,const Hit &hit){return materialmodel::Contact(layout,hit.point,hit.inward,hit.severity).doses;}
void Apply(State &s,const std::array<float,kModules> &dose)
{
 for(int i=0;i<kModules;i++)if(std::isfinite(dose[i])&&dose[i]>0)s.damage[i]=Unit(s.damage[i]+dose[i]);
}
void Apply(State &s,const std::array<float,kModules> &dose,Vec3 inward)
{
 Apply(s,dose);
 for(int i=0;i<kModules;i++)if(std::isfinite(dose[i])&&dose[i]>0){
  if(i>=kWheelLF&&i<=kWheelRR){const int w=i-kWheelLF;const float sign=std::fabs(inward.x)>.1f?std::copysign(1.0f,inward.x):(w<2?-1.0f:1.0f);
   s.toe[w]=std::clamp(s.toe[w]+sign*dose[i]*.10f,-.10f,.10f);}
  if(i==kSteering&&std::isfinite(inward.x))s.steeringBias=std::clamp(s.steeringBias+std::clamp(inward.x,-1.0f,1.0f)*dose[i]*.04f,-.06f,.06f);
 }
}
void Tick(State &s,float dt,bool on,float load,float speed)
{
 if(!std::isfinite(dt)||dt<=0||dt>1||!std::isfinite(load)||!std::isfinite(speed))return;
 const int steps=int(std::ceil(dt/.05f));const float step=dt/steps,damage=Unit(s.damage[kCooling]),tank=Unit(s.damage[kFuel]);
 for(int i=0;i<steps;i++){
  s.coolant=Unit(s.coolant-step*.025f*damage*damage);s.fuel=Unit(s.fuel-step*.012f*tank*tank);
  if(on){const float heat=.012f+.018f*Unit(std::fabs(load));const float cooling=(.02f+.10f*s.coolant*(1-.8f*damage))*(1+.5f*Unit(std::fabs(speed)/30));
   const float target=heat/cooling;s.heat=target+(s.heat-target)*std::exp(-cooling*step);
  }else s.heat*=std::exp(-.025f*step);
 }
 s.heat=std::clamp(s.heat,0.0f,2.0f);
}
float Power(const State &s)
{
 if(s.damage[kEngine]>=.98f||s.damage[kDrivetrain]>=.98f||s.damage[kElectrical]>=.98f||s.fuel<=.01f||s.heat>=1.25f)return 0;
 const float thermal=1-.9f*Unit((s.heat-.65f)/.6f);
 return std::clamp((1-.85f*s.damage[kEngine])*(1-.8f*s.damage[kDrivetrain])*(1-.65f*s.damage[kElectrical])*thermal,0.0f,1.0f);
}
float Lighting(const State &s){return s.damage[kElectrical]>=.98f?0:1-.8f*Unit(s.damage[kElectrical]);}
WheelEffect Wheel(const State &s,int i)
{
 if(i<0||i>=4)return {};const float d=Unit(s.damage[kWheelLF+i]),brake=Unit(s.damage[kBrakeLF+i]);const bool front=i==0||i==2;
 return {1-.55f*d,1-.35f*d,(1-.50f*d)*(brake>=.98f?0:1-.9f*brake),s.toe[i]+(front?s.steeringBias:0),front?1-.8f*Unit(s.damage[kSteering]):1};
}
const char *Name(int i){static const char *names[]={"engine","cooling","wheel_lf","wheel_lr","wheel_rf","wheel_rr","brake_lf","brake_lr","brake_rf","brake_rr","steering","drivetrain","fuel","electrical"};return i>=0&&i<kModules?names[i]:"unknown";}
}
