#include "mechanicalmodel.h"
#include <algorithm>
#include <cmath>
namespace mechanicalmodel {namespace {
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Box Around(Vec3 p,Vec3 half){return {{p.x-half.x,p.y-half.y,p.z-half.z},{p.x+half.x,p.y+half.y,p.z+half.z}};}
float Entry(const Box &box,Vec3 p,Vec3 d)
{
 float near=0,far=1;const float a[]={p.x,p.y,p.z},v[]={d.x,d.y,d.z},lo[]={box.lo.x-.25f,box.lo.y-.25f,box.lo.z-.25f},hi[]={box.hi.x+.25f,box.hi.y+.25f,box.hi.z+.25f};
 for(int i=0;i<3;i++){
  if(std::fabs(v[i])<1e-7f){if(a[i]<lo[i]||a[i]>hi[i])return -1;continue;}
  float l=(lo[i]-a[i])/v[i],h=(hi[i]-a[i])/v[i];if(l>h)std::swap(l,h);near=std::max(near,l);far=std::min(far,h);if(near>far)return -1;
 }
 return near;
}
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
 out.wheels=w;out.volumes[0]=Around({0,engineY,z},{std::min(.75f,width*.6f),std::clamp((front-rear)*.21f,.45f,.8f),.45f});
 out.volumes[1]=Around({0,model==210?engineY:hi.y-.2f,z-.1f},{std::min(.9f,width*.75f),.2f,.4f});
 for(int i=0;i<4;i++)out.volumes[i+2]=Around(w[i],{.3f,.4f,.4f});return true;
}
std::array<float,kModules> Doses(const Layout &layout,const Hit &hit)
{
 std::array<float,kModules> doses{};
 if(!Finite(hit.point)||!Finite(hit.inward)||!std::isfinite(hit.severity)||hit.severity<=4.5f)return doses;
 const float len=std::sqrt(hit.inward.x*hit.inward.x+hit.inward.y*hit.inward.y+hit.inward.z*hit.inward.z);if(!std::isfinite(len)||len<1e-6f)return doses;
 const float reach=std::min(1.0f,.08f+hit.severity*.035f),dose=std::min(.55f,(hit.severity-4.5f)/26);
 const Vec3 d{hit.inward.x/len*reach,hit.inward.y/len*reach,hit.inward.z/len*reach};
 for(int i=0;i<kModules;i++){const float t=Entry(layout.volumes[i],hit.point,d);if(t>=0)doses[i]=dose*(1-.55f*t)*(i==1?1.35f:1);}
 return doses;
}
void Apply(State &s,const std::array<float,kModules> &dose,Vec3 inward)
{
 for(int i=0;i<kModules;i++)if(std::isfinite(dose[i])&&dose[i]>0){
  s.damage[i]=Unit(s.damage[i]+dose[i]);
  if(i>=2){const int w=i-2;const float sign=std::fabs(inward.x)>.1f?std::copysign(1.0f,inward.x):(w<2?-1.0f:1.0f);
   s.toe[w]=std::clamp(s.toe[w]+sign*dose[i]*.10f,-.10f,.10f);}
 }
}
void Tick(State &s,float dt,bool on,float load,float speed)
{
 if(!std::isfinite(dt)||dt<=0||dt>.25f||!std::isfinite(load)||!std::isfinite(speed))return;
 const float damage=Unit(s.damage[1]);s.coolant=Unit(s.coolant-dt*.025f*damage*damage);
 if(on){const float heat=.012f+.018f*Unit(std::fabs(load));const float cooling=(.02f+.10f*s.coolant*(1-.8f*damage))*(1+.5f*Unit(std::fabs(speed)/30));
  const float target=heat/cooling;s.heat=target+(s.heat-target)*std::exp(-cooling*dt);
 }else s.heat*=std::exp(-.025f*dt);
 s.heat=std::clamp(s.heat,0.0f,2.0f);
}
float Power(const State &s)
{
 if(s.damage[0]>=.98f||s.heat>=1.25f)return 0;
 const float thermal=1-.9f*Unit((s.heat-.65f)/.6f);return std::clamp((1-.85f*s.damage[0])*thermal,0.0f,1.0f);
}
WheelEffect Wheel(const State &s,int i)
{
 if(i<0||i>=4)return {};const float d=Unit(s.damage[i+2]);return {1-.55f*d,1-.35f*d,1-.50f*d,s.toe[i]};
}
const char *Name(int i){static const char *names[]={"engine","cooling","wheel_lf","wheel_lr","wheel_rf","wheel_rr"};return i>=0&&i<kModules?names[i]:"unknown";}
}
