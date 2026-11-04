#include "carmechanics.h"
#include "mechanicalmodel.h"
#include "partsmodel.h"
#include "game.h"
#include "config.h"
#include "hooks.h"
#include "log.h"
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace carmechanics {namespace {
constexpr size_t kStride=0x6f0,kSlots=256;
void **gPool;bool gReady;uint32_t gEpoch;
uint64_t gRevision;
uint64_t NextRevision(){if(++gRevision==0)++gRevision;return gRevision;}
void *(*gFrame)(void *,int);float *(*gMatrix)(void *);
int (*gWheelStatus)(void *,int);void (*gBurst)(uint8_t *,uint8_t,bool);
void (*gControl)(uint8_t *),(*gFix)(uint8_t *),(*gDelete)(uint8_t *),(*gRestart)();
float (*gCalculate)(uint8_t *,const float &,uint8_t &,float &,const float &,bool);
void (*gWheel)(uint8_t *,CVector &,CVector &,CVector &,CVector &,int,float,float,float,int8_t,float *,int32_t *,uint16_t);
struct Identity {uint8_t *v=nullptr;const uint8_t *base=nullptr,*flags=nullptr;int ref=-1,model=-1;void *vtable=nullptr;};
bool Same(const Identity &a,const Identity &b){return a.v==b.v&&a.base==b.base&&a.flags==b.flags&&a.ref==b.ref&&a.model==b.model&&a.vtable==b.vtable;}
bool Identify(uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return false;
 const auto pool=static_cast<const uint8_t *>(*gPool);
 const auto base=*reinterpret_cast<uint8_t *const *>(pool),flags=*reinterpret_cast<uint8_t *const *>(pool+8);
 const int n=*reinterpret_cast<const int *>(pool+16);const uintptr_t a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);
 if(!base||!flags||n<=0||n>100000||a<b||(a-b)%kStride)return false;
 const size_t index=(a-b)/kStride;if(index>=size_t(n)||index>=kSlots||(flags[index]&0x80))return false;
 const int ref=int(index*256)|flags[index];if(game::api.PoolsGetVehicle(ref)!=v||(game::EntityFlags(v)&7)!=2)return false;
 id={v,base,flags,ref,game::EntityModel(v),*reinterpret_cast<void **>(v)};return true;
}
bool Eligible(const Identity &id){return id.vtable&&id.model>=0&&id.model<6500&&id.model!=162&&id.model!=171&&
 *reinterpret_cast<const int *>(id.v+0x348)==0&&!game::IsAircraft(id.v)&&!game::Wrecked(id.v)&&game::EntityClump(id.v)&&
 (id.v[0x297]&0x40)&&!(game::EntityFlags(id.v)&(1u<<27));}
struct History {
 Identity id;mechanicalmodel::State state;mechanicalmodel::Layout layout;uint64_t revision=0;
 std::array<partsmodel::Episode,mechanicalmodel::kModules> episodes{};
 uint8_t *info=nullptr,*col=nullptr;void *source=nullptr;bool layoutValid=false,tickSeen=false;uint32_t lastTick=0;int powerBand=10,fuelBand=10;
};
std::array<History,kSlots> gHistory;
History &Record(const Identity &id){auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;h.revision=NextRevision();}return h;}
bool Layout(History &h)
{
 if(!game::api.modelInfoPtrs)return false;
 auto info=game::api.modelInfoPtrs[h.id.model];if(!info||info[0x1d]!=6)return false;
 auto source=*reinterpret_cast<void **>(info+0x30);auto col=*reinterpret_cast<uint8_t **>(info+0x20);if(!source||!col)return false;
 if(h.layoutValid&&h.info==info&&h.source==source&&h.col==col)return true;
 std::array<Vec3,4> wheels;const int ids[]={4,6,1,3}; // native GetWheelPosn table: LF LR RF RR
 for(int i=0;i<4;i++){void *frame=gFrame(source,ids[i]);if(!frame)return false;const float *m=gMatrix(frame);if(!m)return false;wheels[i]={m[12],m[13],m[14]};}
 const auto lo=*reinterpret_cast<const Vec3 *>(col+0x10),hi=*reinterpret_cast<const Vec3 *>(col+0x1c);
 mechanicalmodel::Layout layout;if(!mechanicalmodel::Build(h.id.model,lo,hi,wheels,layout))return false;
 h.layout=layout;h.info=info;h.col=col;h.source=source;h.layoutValid=true;return true;
}
bool LayoutCurrent(const History &h,const Identity &id)
{
 return game::api.modelInfoPtrs&&game::api.modelInfoPtrs[id.model]==h.info&&h.info&&
  *reinterpret_cast<void **>(h.info+0x30)==h.source&&*reinterpret_cast<uint8_t **>(h.info+0x20)==h.col;
}
bool BulletCurrent(uint8_t *v,const BulletToken &token,Identity &id)
{
 if(!gReady||!gConfig.carMechanics||!token.revision||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&(1u<<17)))return false;
 const auto &h=gHistory[size_t(id.ref>>8)];
 return Same(h.id,id)&&h.revision==token.revision&&h.layoutValid&&game::EntityClump(v)==token.clump&&
  *reinterpret_cast<void **>(v+0x160)==token.handling&&h.info==token.info&&h.col==token.col&&h.source==token.source&&
  LayoutCurrent(h,id)&&!std::memcmp(v+8,token.transform.data(),sizeof token.transform);
}
struct Controller;thread_local Controller *gController;
struct Controller {
 Controller *previous=gController;Identity id;History *history=nullptr;void *clump=nullptr;uint8_t *handling=nullptr;uint32_t epoch=gEpoch;bool valid=false;
 explicit Controller(uint8_t *v){
  if(gReady&&gConfig.carMechanics&&game::api.timeMs&&Identify(v,id)&&Eligible(id)){
   auto &h=Record(id);if(Layout(h)){history=&h;clump=game::EntityClump(v);handling=*reinterpret_cast<uint8_t **>(v+0x160);valid=handling!=nullptr;}
  }
  gController=this;
 }
 ~Controller(){gController=previous;}
};
bool Current(const Controller *c)
{
 if(!gReady||!gConfig.carMechanics||!c||!c->valid||!c->history||c->epoch!=gEpoch)return false;
 Identity now;if(!Identify(c->id.v,now)||!Same(now,c->id)||!Eligible(now)||game::EntityClump(now.v)!=c->clump||*reinterpret_cast<uint8_t **>(now.v+0x160)!=c->handling)return false;
 return LayoutCurrent(*c->history,now);
}
void Invalidate(uint8_t *v){for(auto c=gController;c;c=c->previous)if(c->id.v==v)c->valid=false;}
void Report(History &h)
{
 const float power=mechanicalmodel::Power(h.state);const int band=int(power*10);
 const int fuelBand=int(h.state.fuel*10);
 if(band!=h.powerBand||fuelBand!=h.fuelBand){h.powerBand=band;h.fuelBand=fuelBand;
  Log("car mechanics: model %d power %.0f%%, coolant %.0f%%, heat %.2f, fuel %.0f%%",h.id.model,power*100,h.state.coolant*100,h.state.heat,h.state.fuel*100);}
}
void Control(uint8_t *v)
{
 Controller scope(v);
 if(Current(&scope)){
  auto &h=*scope.history;const uint32_t now=*game::api.timeMs;
  if(h.tickSeen&&now>=h.lastTick){const float dt=(now-h.lastTick)*.001f;const auto speed=reinterpret_cast<const CVector *>(v+0x84);
   const float magnitude=50*std::sqrt(speed->x*speed->x+speed->y*speed->y+speed->z*speed->z);
   mechanicalmodel::Tick(h.state,dt,(v[0x295]&0x10)!=0,*reinterpret_cast<const float *>(v+0x28c),magnitude);}
  h.tickSeen=true;h.lastTick=now;Report(h);
 }
 gControl(v);
}
float Calculate(uint8_t *transmission,const float &gas,uint8_t &gear,float &timer,const float &speed,bool cheat)
{
 auto c=gController;
 // Handling is shared among cars. The real native actor references, as well
 // as the original transmission allocation, establish which actor owns this.
 const bool owned=Current(c)&&transmission==c->handling+0x34&& &gas==reinterpret_cast<const float *>(c->id.v+0x28c)&&
  &gear==c->id.v+0x2a4&& &timer==reinterpret_cast<float *>(c->id.v+0x2a8);
 const float result=gCalculate(transmission,gas,gear,timer,speed,cheat);
 return owned&&c==gController&&Current(c)&&std::isfinite(result)?result*mechanicalmodel::Power(c->history->state):result;
}
void Wheel(uint8_t *v,CVector &f,CVector &r,CVector &speed,CVector &point,int count,float thrust,float brake,float grip,int8_t wheel,float *spin,int32_t *state,uint16_t status)
{
 auto c=gController;
 if(!Current(c)||c->id.v!=v||wheel<0||wheel>=4||spin!=reinterpret_cast<float *>(v+0x5ec+4*wheel)||
    !std::isfinite(thrust)||!std::isfinite(brake)||!std::isfinite(grip)){
  gWheel(v,f,r,speed,point,count,thrust,brake,grip,wheel,spin,state,status);return;
 }
 const auto effect=mechanicalmodel::Wheel(c->history->state,wheel);
 if(effect.grip==1&&effect.drive==1&&effect.brake==1&&effect.toe==0&&effect.steering==1){gWheel(v,f,r,speed,point,count,thrust,brake,grip,wheel,spin,state,status);return;}
 float turn=effect.toe;
 if(effect.steering!=1){
  const auto &forward=game::EntityForward(v);
  const float across=-(r.x*forward.x+r.y*forward.y+r.z*forward.z),along=f.x*forward.x+f.y*forward.y+f.z*forward.z;
  // Native control projects actor forward onto the contact plane before
  // steering (0x24b1a4..0x24b278). Measure within that plane: its tilt must
  // not become steering input. Skip a degenerate forward projection.
  const float angle=std::atan2(across,along);
  if(std::isfinite(angle)&&std::isfinite(across)&&std::isfinite(along)&&std::hypot(across,along)>1e-6f&&std::fabs(angle)<1.57f)turn+=(effect.steering-1)*angle;
 }
 const float cs=std::cos(turn),sn=std::sin(turn);
 CVector forward{f.x*cs+r.x*sn,f.y*cs+r.y*sn,f.z*cs+r.z*sn},right{r.x*cs-f.x*sn,r.y*cs-f.y*sn,r.z*cs-f.z*sn};
 gWheel(v,forward,right,speed,point,count,thrust*effect.drive,brake*effect.brake,grip*effect.grip,wheel,spin,state,status);
}
void Fix(uint8_t *v){Invalidate(v);Identity id;if(Identify(v,id)){auto &h=gHistory[size_t(id.ref>>8)];h={};h.id=id;h.revision=NextRevision();}gFix(v);}
void Delete(uint8_t *v){Invalidate(v);Identity id;if(Identify(v,id)){auto &h=gHistory[size_t(id.ref>>8)];if(Same(h.id,id))h.revision=NextRevision();}gDelete(v);} // retire pending geometry, preserve physical damage
void Restart(){++gEpoch;for(auto c=gController;c;c=c->previous)c->valid=false;gHistory={};gRestart();}
} // namespace
bool Enabled(){return gReady&&gConfig.carMechanics;}
BulletToken BeginBullet(uint8_t *v)
{
 Identity id;if(!gReady||!gConfig.carMechanics||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&(1u<<17)))return {};
 auto &h=Record(id);BulletToken token;token.revision=h.revision;token.clump=game::EntityClump(v);token.handling=*reinterpret_cast<void **>(v+0x160);
 std::memcpy(token.transform.data(),v+8,sizeof token.transform);
 if(!token.handling||!Layout(h))return {};
 token.info=h.info;token.col=h.col;token.source=h.source;
 Identity current;if(!BulletCurrent(v,token,current))return {};return token;
}
void Projectile(uint8_t *v,const BulletToken &token,const projectilemodel::Ray &ray)
{
 Identity id;if(!BulletCurrent(v,token,id))return;auto &h=gHistory[size_t(id.ref>>8)];
 const auto hit=projectilemodel::Doses(h.layout,ray);if(!hit.valid)return;
 const auto before=h.state.damage;mechanicalmodel::Apply(h.state,hit.doses);
 for(int i=0;i<mechanicalmodel::kModules;i++)if(h.state.damage[i]>before[i])
  Log("car projectiles: model %d %s integrity %.0f%%",id.model,mechanicalmodel::Name(i),100*(1-h.state.damage[i]));
 Report(h);
 for(int i=0;i<4;i++)if(hit.doses[mechanicalmodel::kWheelLF+i]>0&&h.state.damage[mechanicalmodel::kWheelLF+i]>=.75f){
  Identity now;if(!BulletCurrent(v,token,now))return;const int status=gWheelStatus(v+0x34c,i);
  if(status==0&&BulletCurrent(v,token,now))gBurst(v,uint8_t(i),false);
 }
}
firemodel::Input FireInput(const uint8_t *v)
{
 firemodel::Input out;Identity id;if(!gReady||!gConfig.carMechanics||!Identify(const_cast<uint8_t *>(v),id)||!Eligible(id))return out;
 out.health=game::VehicleHealth(v);out.running=(v[0x295]&0x10)!=0;const auto &h=gHistory[size_t(id.ref>>8)];
 if(Same(h.id,id)&&h.layoutValid&&LayoutCurrent(h,id)){out.heat=h.state.heat;out.fuel=h.state.fuel;out.engine=h.state.damage[mechanicalmodel::kEngine];out.electrical=h.state.damage[mechanicalmodel::kElectrical];out.breach=h.state.damage[mechanicalmodel::kFuel];}
 return out;
}
void Burn(uint8_t *v,float dt)
{
 Identity id;if(!gReady||!gConfig.carMechanics||!std::isfinite(dt)||dt<=0||dt>1||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&(1u<<26)))return;
 auto &h=Record(id);const auto revision=h.revision;if(!Layout(h))return;Identity now;if(!Identify(v,now)||!Same(id,now)||!Eligible(now)||h.revision!=revision)return;
 std::array<float,mechanicalmodel::kModules> dose{};dose[mechanicalmodel::kEngine]=.018f*dt;dose[mechanicalmodel::kCooling]=.025f*dt;
 dose[mechanicalmodel::kElectrical]=.03f*dt;dose[mechanicalmodel::kFuel]=.01f*dt;mechanicalmodel::Apply(h.state,dose);Report(h);
}
float LightingPower(const uint8_t *v)
{
 Identity id;if(!gReady||!gConfig.carMechanics||!Identify(const_cast<uint8_t *>(v),id)||!Eligible(id))return 1;
 const auto &h=gHistory[size_t(id.ref>>8)];
 // Rendering occurs outside ProcessControl. Validate existing history without
 // creating a new record, ticking reservoirs or depending on a render clump.
 if(!Same(h.id,id)||!h.layoutValid||!game::api.modelInfoPtrs||game::api.modelInfoPtrs[id.model]!=h.info||!h.info||
    *reinterpret_cast<void **>(h.info+0x30)!=h.source||*reinterpret_cast<uint8_t **>(h.info+0x20)!=h.col)return 1;
 return mechanicalmodel::Lighting(h.state);
}
void Collision(uint8_t *v,const carimpact::Collision &hit)
{
 auto c=gController;if(!Current(c)||c->id.v!=v||!game::api.timeMs)return;
 auto &h=*c->history;const auto before=h.state.damage;auto dose=mechanicalmodel::Doses(h.layout,{hit.point,hit.inward,hit.severity,hit.piece});
 const uint32_t now=*game::api.timeMs;
 for(int i=0;i<mechanicalmodel::kModules;i++)if(dose[i]>0){if(partsmodel::Contact(h.episodes[i],now))partsmodel::Commit(h.episodes[i]);else dose[i]=0;}
 mechanicalmodel::Apply(h.state,dose,hit.inward);
 for(int i=0;i<mechanicalmodel::kModules;i++)if(h.state.damage[i]>before[i])
  Log("car mechanics: model %d %s integrity %.0f%%",c->id.model,mechanicalmodel::Name(i),100*(1-h.state.damage[i]));
 for(int i=0;i<4;i++)if(dose[i+2]>0&&h.state.damage[i+2]>=.75f){
  if(!Current(c))return;if(gWheelStatus(v+0x34c,i)==0)gBurst(v,uint8_t(i),false);
 }
}
bool Install()
{
 if(gReady||!gConfig.carMechanics)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(gPool,"_ZN6CPools15ms_pVehiclePoolE");B(gFrame,"_ZN15CClumpModelInfo14GetFrameFromIdEPN2rw5ClumpEi");B(gMatrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");
 B(gWheelStatus,"_ZN14CDamageManager14GetWheelStatusEi");B(gBurst,"_ZN11CAutomobile9BurstTyreEhb");
#undef B
 if(!game::api.PoolsGetVehicle||!game::api.timeMs||!game::api.modelInfoPtrs)return false;
#define C(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 C(gControl,"_ZN11CAutomobile14ProcessControlEv",Control);C(gFix,"_ZN11CAutomobile3FixEv",Fix);C(gDelete,"_ZN7CEntity14DeleteRwObjectEv",Delete);
 C(gRestart,"_ZN6CWorld15ClearForRestartEv",Restart);C(gCalculate,"_ZN13cTransmission26CalculateDriveAccelerationERKfRhRfS1_b",Calculate);
 C(gWheel,"_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet",Wheel);
#undef C
 gReady=gControl&&gFix&&gDelete&&gRestart&&gCalculate&&gWheel;
 if(gReady)Log("car mechanics: localized engine, cooling, wheels, brakes, steering, drivetrain, fuel and electrical failures enabled");return gReady;
}
}
