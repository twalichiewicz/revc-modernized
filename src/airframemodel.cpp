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
 const float root=l.helicopter?.62f:.24f;
 l.structure[0]=box(-root-.06f,-root+.06f,l.helicopter?.55f:.32f,l.helicopter?.90f:.62f,.2f,.7f);
 l.structure[1]=box(root-.06f,root+.06f,l.helicopter?.55f:.32f,l.helicopter?.90f:.62f,.2f,.7f);
 l.structure[2]=box(-.35f,.35f,.20f,.30f,.3f,.85f);
 if(l.helicopter){
  // The cabin cage and the whole tail boom carry loads, including transverse
  // rounds away from the attachment. Static COL bounds include the long boom;
  // a tiny root-only slab leaves most visible skin mechanically disconnected.
  l.structure[0]=box(-1,0,.30f,1,.08f,.88f);
  l.structure[1]=box(0,1,.30f,1,.08f,.88f);
  l.structure[2]=box(-.38f,.38f,0,.30f,.25f,1);
 }
 return true;
}
Section SectionCut(const Layout &l,int section)
{
 const float centre=(l.body.lo.x+l.body.hi.x)*.5f,half=(l.body.hi.x-l.body.lo.x)*.5f;
 const float side=l.helicopter?.62f:.24f;
 if(section==0)return {{-1,0,0},-(centre-half*side)};
 if(section==1)return {{1,0,0},centre+half*side};
 return {{0,-1,0},-(l.body.lo.y+(l.body.hi.y-l.body.lo.y)*.25f)};
}
bool Volume(const State &s,const Layout &l,int module,Box &box)
{
 if(module<0||module>=Count+3||(module==MainRotor&&(s.missingRotors&1))||(module==TailRotor&&(s.missingRotors&2)))return false;box=module<Count?l.volumes[size_t(module)]:l.structure[size_t(module-Count)];
 for(int part=0;part<3;part++)if(s.separated&(1u<<part)){
  // Body/boom cuts do not slice the independently attached spinning disc.
  if(l.helicopter&&module==MainRotor)continue;const auto cut=SectionCut(l,part);
  if(cut.normal.x>0)box.hi.x=std::min(box.hi.x,cut.offset);else if(cut.normal.x<0)box.lo.x=std::max(box.lo.x,-cut.offset);
  if(cut.normal.y>0)box.hi.y=std::min(box.hi.y,cut.offset);else if(cut.normal.y<0)box.lo.y=std::max(box.lo.y,-cut.offset);
 }
 return box.lo.x<box.hi.x&&box.lo.y<box.hi.y&&box.lo.z<box.hi.z;
}
namespace {
void BreakSystems(State &s,const Layout &l,int part)
{
 auto separated=s;separated.separated|=uint8_t(1u<<part);
 for(int i=0;i<Count;i++){
  if((l.helicopter&&(i==MainRotor||i==WingL||i==WingR||i==Elevator||i==Rudder||i==AileronL||i==AileronR))||(!l.helicopter&&(i==MainRotor||i==TailRotor)))continue;const auto &original=l.volumes[size_t(i)];Box retained;
  const float whole=(original.hi.x-original.lo.x)*(original.hi.y-original.lo.y)*(original.hi.z-original.lo.z);
  const float remaining=Volume(separated,l,i,retained)?(retained.hi.x-retained.lo.x)*(retained.hi.y-retained.lo.y)*(retained.hi.z-retained.lo.z):0;
  s.damage[size_t(i)]=std::max(s.damage[size_t(i)],Clamp(1-remaining/whole));
 }
}
}
projectilemodel::Result Projectile(State &s,const Layout &l,const projectilemodel::Ray &ray)
{
 static_assert(int(Count)==int(mechanicalmodel::kModules));
 float resistance[]={65,65,8,8,14,14,16,16,12,12,15,12,28,28};
 const float capacity[]={140,140,45,45,160,160,90,90,55,55,55,45,100,60};
 if(l.single)resistance[EngineR]=0;
 if(l.helicopter)for(int i:{WingL,WingR,Elevator,Rudder,AileronL,AileronR})resistance[i]=0;
 else resistance[MainRotor]=resistance[TailRotor]=0;
 auto physical=l.volumes;projectilemodel::Structure frame{l.structure,{80,80,65},{500,500,350}};
 if(l.helicopter)frame.capacity={180,180,150};
 for(int i=0;i<Count;i++)if(!Volume(s,l,i,physical[size_t(i)])){physical[size_t(i)]=l.body;resistance[i]=0;}
 for(int i=0;i<3;i++)if(!Volume(s,l,Count+i,frame.volumes[size_t(i)])){frame.volumes[size_t(i)]=l.body;frame.resistance[size_t(i)]=0;}
 auto body=l.body;if(l.helicopter)for(int rotor:{MainRotor,TailRotor})if(resistance[rotor]>0){const auto b=physical[size_t(rotor)];body.lo={std::min(body.lo.x,b.lo.x),std::min(body.lo.y,b.lo.y),std::min(body.lo.z,b.lo.z)};body.hi={std::max(body.hi.x,b.hi.x),std::max(body.hi.y,b.hi.y),std::max(body.hi.z,b.hi.z)};}
 const auto hit=projectilemodel::Absorb(body,physical,resistance,capacity,ray,&frame);if(!hit.valid)return hit;
 for(int i=0;i<Count;i++)s.damage[i]=Clamp(s.damage[i]+hit.doses[i]);
 for(int part=0;part<3;part++){s.structure[size_t(part)]=Clamp(s.structure[size_t(part)]+hit.structuralDoses[size_t(part)]);if(s.structure[size_t(part)]>=1)BreakSystems(s,l,part);}
 if(l.single)s.damage[EngineR]=s.damage[EngineL];return hit;
}
void Impact(State &s,const Layout &l,Vec3 p,float severity,Vec3 inward)
{
 if(!Finite(p)||!std::isfinite(severity)||severity<=0)return;
 if(!Finite(inward))return;
 if((inward.x*inward.x+inward.y*inward.y+inward.z*inward.z)<1e-8f){
  const float distance[]={std::fabs(p.x-l.body.lo.x),std::fabs(p.x-l.body.hi.x),std::fabs(p.y-l.body.lo.y),std::fabs(p.y-l.body.hi.y),std::fabs(p.z-l.body.lo.z),std::fabs(p.z-l.body.hi.z)};
  const int nearest=int(std::min_element(distance,distance+6)-distance);inward={float(nearest==0)-float(nearest==1),float(nearest==2)-float(nearest==3),float(nearest==4)-float(nearest==5)};
 }
 Projectile(s,l,materialmodel::ContactRay(p,inward,severity));
}
void Tick(State &s,float dt,bool running)
{
 if(!std::isfinite(dt)||dt<=0||dt>1)return;
 const float leak=std::max(s.damage[FuelL],s.damage[FuelR]);s.fuel=std::max(0.f,s.fuel-dt*(leak*.02f+(running?.00012f:0)));
 const float target=running?.4f+std::max(s.damage[EngineL],s.damage[EngineR]):.2f;s.heat+=(target-s.heat)*(1-std::exp(-dt*.08f));
}
Effects Read(const State &s,const Layout &l)
{
 Effects e;e.managed=true;e.power=s.fuel>0?1-(s.damage[EngineL]+s.damage[EngineR])*.5f:0;
 e.leftLift=l.helicopter?1:(1-.85f*s.damage[WingL])*(1-s.structure[0]);e.rightLift=l.helicopter?1:(1-.85f*s.damage[WingR])*(1-s.structure[1]);const float hydraulic=1-.9f*s.damage[Hydraulics];
 e.pitch=(l.helicopter?1:1-s.damage[Elevator])*hydraulic;e.roll=(l.helicopter?1:1-(s.damage[AileronL]+s.damage[AileronR])*.5f)*hydraulic;e.yaw=(l.helicopter?1:1-s.damage[Rudder])*hydraulic;
 e.rotor=l.helicopter?1-s.damage[MainRotor]:1;e.antiTorque=l.helicopter?(1-s.damage[TailRotor])*(1-s.structure[2]):1;e.drag=std::min(1.f,s.holes*.1f+.2f*(s.structure[0]+s.structure[1]+s.structure[2])+.15f*(s.damage[WingL]+s.damage[WingR]));return e;
}
firemodel::Input Fire(const State &s,bool running){firemodel::Input f;f.heat=s.heat;f.fuel=s.fuel;f.engine=std::max(s.damage[EngineL],s.damage[EngineR]);f.electrical=s.damage[Electrical];f.breach=std::max(s.damage[FuelL],s.damage[FuelR]);f.running=running;return f;}
}
