#include "airframedamage.h"
#include "carglass.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include "partsmodel.h"
#include "skinmodel.h"
#include "airframeroute.h"
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace airframedamage {namespace {
constexpr size_t kSlots=256,kStride=0x6f0;void **gPool;bool gReady;uint64_t gSerial;
void (*gAuto)(uint8_t *),(*gPlane)(uint8_t *),(*gHeli)(uint8_t *),(*gBoat)(uint8_t *),(*gFix)(uint8_t *),(*gDelete)(uint8_t *),(*gRestart)();
void (*gFlying)(uint8_t *,int),(*gMove)(uint8_t *,float,float,float),(*gTurn)(uint8_t *,float,float,float,float,float,float);
struct Identity {uint8_t *v=nullptr;const void *base=nullptr,*flags=nullptr,*vtable=nullptr;int ref=-1,model=-1,kind=-1;};
bool Same(const Identity &a,const Identity &b){return a.v==b.v&&a.base==b.base&&a.flags==b.flags&&a.vtable==b.vtable&&a.ref==b.ref&&a.model==b.model&&a.kind==b.kind;}
bool Identify(const uint8_t *v,Identity &id)
{
 if(!v||!gPool||!*gPool||!game::api.PoolsGetVehicle)return false;const auto pool=static_cast<const uint8_t *>(*gPool);auto base=*reinterpret_cast<uint8_t *const *>(pool),flags=*reinterpret_cast<uint8_t *const *>(pool+8);const int n=*reinterpret_cast<const int *>(pool+16);
 const uintptr_t a=reinterpret_cast<uintptr_t>(v),b=reinterpret_cast<uintptr_t>(base);if(!base||!flags||n<=0||n>100000||a<b||(a-b)%kStride)return false;const size_t index=(a-b)/kStride;if(index>=size_t(n)||index>=kSlots||(flags[index]&0x80))return false;
 const int ref=int(index*256)|flags[index];if(game::api.PoolsGetVehicle(ref)!=v||(*reinterpret_cast<const uint32_t *>(v+0x60)&7)!=2)return false;
 id={const_cast<uint8_t *>(v),base,flags,*reinterpret_cast<void *const *>(v),ref,game::EntityModel(v),*reinterpret_cast<const int *>(v+0x348)};return true;
}
bool Eligible(const Identity &id){return gReady&&id.vtable&&id.model>=0&&id.model<6500&&game::IsAircraft(id.v)&&(((id.kind==0&&id.model!=194)||id.kind==3)?gConfig.heliDamage:gConfig.planes)&&!game::Wrecked(id.v)&&(id.v[0x297]&0x40);}
void (*gUpdateMatrix)(void *),(*gUpdateFrame)(uint8_t *);
struct History {uint8_t lostRotors=0;airframeroute::State route;Identity id;airframemodel::State state;airframemodel::Layout layout;uint64_t revision=0;void *clump=nullptr,*source=nullptr;uint8_t *info=nullptr,*col=nullptr;uint32_t clock=0;bool seen=false;float health=1000;std::array<partsmodel::Episode,6> contacts{};};
std::array<History,kSlots> gHistory;uint32_t gClock;bool gSeen;
uint64_t Serial(){if(++gSerial==0)++gSerial;return gSerial;}
void Observe(){if(game::api.timeMs){const auto now=*game::api.timeMs;if(gSeen&&now<gClock)gHistory={};gClock=now;gSeen=true;}}
History *Record(const Identity &id)
{
 if(!game::api.modelInfoPtrs)return nullptr;auto info=game::api.modelInfoPtrs[id.model];auto col=info?*reinterpret_cast<uint8_t **>(info+0x20):nullptr;auto source=info?*reinterpret_cast<void **>(info+0x30):nullptr;auto clump=game::EntityClump(id.v);if(!info||info[0x1d]!=6||!col||!source||!clump)return nullptr;
 auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)){h={};h.id=id;h.revision=Serial();h.health=game::VehicleHealth(id.v);}
 if(h.info!=info||h.col!=col||h.source!=source||h.clump!=clump){airframemodel::Layout layout;if(!airframemodel::Build(id.model,id.kind,*reinterpret_cast<const Vec3 *>(col+0x10),*reinterpret_cast<const Vec3 *>(col+0x1c),layout))return nullptr;h.layout=layout;h.info=info;h.col=col;h.source=source;h.clump=clump;h.lostRotors=0;h.revision=Serial();}
 return &h;
}
History *Current(uint8_t *v,const Token &token)
{
 Identity id;if(!token.revision||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&((1u<<17)|(1u<<25)))||game::EntityClump(v)!=token.clump||std::memcmp(v+8,token.pose.data(),sizeof token.pose))return nullptr;
 auto *h=Record(id);return h&&h->revision==token.revision?h:nullptr;
}
void SyncRotors(History &h)
{
 if(!h.layout.helicopter||!game::api.AircraftRotorLoss)return;
 const auto id=h.id;const auto revision=h.revision;
 for(int rotor=0;rotor<2;rotor++){
  const uint8_t bit=uint8_t(1u<<rotor);const int module=rotor?airframemodel::TailRotor:airframemodel::MainRotor;
  if(h.state.damage[module]<1||(h.lostRotors&bit))continue;
  if(game::api.AircraftRotorLoss(id.v,rotor==0)){
   Identity now;if(!Identify(id.v,now)||!Same(id,now)||h.revision!=revision||game::EntityClump(id.v)!=h.clump)return;
   h.lostRotors|=bit;Log("airframe part loss: ref %d model %d %s rotor",id.ref,id.model,rotor?"tail":"main");
  }
 }
}
airframemodel::Effects Read(const uint8_t *v){Identity id;if(!Identify(v,id)||!Eligible(id))return {};const auto &h=gHistory[size_t(id.ref>>8)];if(Same(id,h.id))return airframemodel::Read(h.state,h.layout);airframemodel::Effects e;e.managed=true;return e;}
firemodel::Input Fire(const uint8_t *v){Identity id;if(!Identify(v,id)||!Eligible(id))return {};const auto &h=gHistory[size_t(id.ref>>8)];return Same(id,h.id)?airframemodel::Fire(h.state,(v[0x295]&16)!=0):firemodel::Input{};}
void Burn(uint8_t *v,float dt){Identity id;if(!std::isfinite(dt)||dt<=0||dt>1||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&(1u<<26)))return;if(auto *h=Record(id)){for(int i:{airframemodel::EngineL,airframemodel::EngineR,airframemodel::Electrical,airframemodel::FuelL,airframemodel::FuelR})h->state.damage[i]=std::min(1.f,h->state.damage[i]+.02f*dt);}}
void Collision(uint8_t *v,Vec3 point,float severity)
{
 Identity id;if(!std::isfinite(severity)||severity<=3||!game::api.timeMs||!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&(1u<<27)))return;Observe();auto *h=Record(id);if(!h)return;
 const int region=point.x<h->layout.body.lo.x*.3f?0:point.x>h->layout.body.hi.x*.3f?1:point.y<0?2:3;
 if(!partsmodel::Contact(h->contacts[region],*game::api.timeMs))return;const auto before=h->state.damage;airframemodel::Impact(h->state,h->layout,point,severity-3);if(h->state.damage!=before&&game::api.RearmVehicleFire)game::api.RearmVehicleFire(v);partsmodel::Commit(h->contacts[region]);
 Log("airframe collision: ref %d model %d local %.2f %.2f %.2f severity %.2f",id.ref,id.model,point.x,point.y,point.z,severity);
}
struct FlightScope {FlightScope *previous;uint8_t *v;bool used=false;};thread_local FlightScope *gFlightScope;
void FlightUsed(uint8_t *v){for(auto *scope=gFlightScope;scope;scope=scope->previous)if(scope->v==v)scope->used=true;}
void Flying(uint8_t *v,int model)
{
 Identity id;const bool owned=Identify(v,id)&&Eligible(id);const auto damage=owned?Read(v):airframemodel::Effects{};
 const Vec3 before=owned?*reinterpret_cast<const Vec3 *>(v+0x84):Vec3{},angular=owned?*reinterpret_cast<const Vec3 *>(v+0x90):Vec3{};const auto clump=owned?game::EntityClump(v):nullptr;
 std::array<float,16> pose{};if(owned)std::memcpy(pose.data(),v+8,sizeof pose);
 FlightScope scope{gFlightScope,v};gFlightScope=&scope;struct Restore {FlightScope *previous;~Restore(){gFlightScope=previous;}} restore{scope.previous};gFlying(v,model);
 Identity now;if(!owned||scope.used||!Identify(v,now)||!Same(id,now)||!Eligible(now)||game::EntityClump(v)!=clump||std::memcmp(pose.data(),v+8,sizeof pose)||!game::api.timeStep)return;
 const float dt=std::clamp(*game::api.timeStep,0.f,3.f)/50,mass=*reinterpret_cast<const float *>(v+0xcc),turnMass=*reinterpret_cast<const float *>(v+0xd0);if(!std::isfinite(dt)||dt<=0||!std::isfinite(mass)||mass<=0||!std::isfinite(turnMass)||turnMass<=0)return;
 const auto r=*reinterpret_cast<const Vec3 *>(v+8),f=*reinterpret_cast<const Vec3 *>(v+0x18),u=*reinterpret_cast<const Vec3 *>(v+0x28),speed=*reinterpret_cast<const Vec3 *>(v+0x84),spin=*reinterpret_cast<const Vec3 *>(v+0x90);
 auto finite=[](Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};if(!finite(r)||!finite(f)||!finite(u)||!finite(before)||!finite(angular)||!finite(speed)||!finite(spin))return;
 const auto delta=skinmodel::Sub(speed,before);const bool heli=(id.kind==0&&id.model!=194)||id.kind==3;
 const float thrust=std::max(0.f,skinmodel::Dot(delta,f))*(1-damage.power),lift=std::max(0.f,skinmodel::Dot(delta,u))*(1-(heli?damage.power*damage.rotor:(damage.leftLift+damage.rightLift)*.5f));
 const float drag=std::min(.1f,damage.drag*dt*.3f);gMove(v,(-f.x*thrust-u.x*lift-speed.x*drag)*mass,(-f.y*thrust-u.y*lift-speed.y*drag)*mass,(-f.z*thrust-u.z*lift-speed.z*drag)*mass);
 const auto turn=skinmodel::Sub(spin,angular);const float pitch=-skinmodel::Dot(turn,r)*(1-damage.pitch),roll=-skinmodel::Dot(turn,f)*(1-damage.roll)+(damage.leftLift-damage.rightLift)*dt*.02f,yaw=-skinmodel::Dot(turn,u)*(1-damage.yaw)+(heli?(1-damage.antiTorque)*dt*.03f:0);
 const auto com=*reinterpret_cast<const Vec3 *>(v+0xe4);if(!finite(com))return;const Vec3 centre{r.x*com.x+f.x*com.y+u.x*com.z,r.y*com.x+f.y*com.y+u.y*com.z,r.z*com.x+f.z*com.y+u.z*com.z};
 auto torque=[&](Vec3 force,Vec3 lever,float amount){gTurn(v,force.x*amount*turnMass,force.y*amount*turnMass,force.z*amount*turnMass,centre.x+lever.x,centre.y+lever.y,centre.z+lever.z);};torque(u,f,pitch);torque(r,u,roll);torque(f,r,yaw);
}
void Repair(uint8_t *v){Identity id;if(Identify(v,id))gHistory[size_t(id.ref>>8)]={};carglass::Restore(v);if(game::api.RepairVehicleFire)game::api.RepairVehicleFire(v);}
void Tick(uint8_t *v)
{
 Identity id;if(!Identify(v,id)||!Eligible(id))return;Observe();auto *h=Record(id);if(!h)return;const float health=game::VehicleHealth(v);
 if(std::isfinite(health)&&health>h->health+1){Repair(v);h=Record(id);if(!h)return;}h->health=health;
 const uint32_t now=game::api.timeMs?*game::api.timeMs:0;if(h->seen&&now>=h->clock)airframemodel::Tick(h->state,(now-h->clock)*.001f,(v[0x295]&16)!=0);h->seen=true;h->clock=now;SyncRotors(*h);
}
void Auto(uint8_t *v){Tick(v);gAuto(v);}void Routed(uint8_t *v,void (*native)(uint8_t *))
{
 Tick(v);Identity id;History *h=nullptr;bool owned=Identify(v,id)&&(Eligible(id)||(game::Wrecked(v)&&gReady&&game::IsAircraft(v)&&(v[0x297]&0x40)));
 if(owned){h=Record(id);owned=h!=nullptr;}
 const bool nativeCrash=owned&&id.kind==4&&v[0x384]!=0;if(nativeCrash)owned=false;
 const uint64_t revision=owned?h->revision:0;std::array<float,16> input{},observed{};if(owned)std::memcpy(observed.data(),v+8,sizeof observed);
 if(owned){airframeroute::Before(h->route,reinterpret_cast<float *>(v+8));std::memcpy(input.data(),v+8,sizeof input);}
 FlightScope scope{gFlightScope,v};gFlightScope=&scope;struct Restore {FlightScope *previous;~Restore(){gFlightScope=previous;}} restore{scope.previous};native(v);
 Identity now;if(!owned||scope.used||!Identify(v,now)||!Same(id,now)||h->revision!=revision||!game::api.timeStep)return;
 auto e=airframemodel::Read(h->state,h->layout);if(game::Wrecked(v)){e.power=e.rotor=e.leftLift=e.rightLift=0;}
 if(e.power==1&&e.rotor==1&&e.leftLift==1&&e.rightLift==1&&e.pitch==1&&e.roll==1&&e.yaw==1&&e.antiTorque==1&&!h->route.seen)return;
 std::array<float,16> pose;std::memcpy(pose.data(),v+8,sizeof pose);const float dt=std::clamp(*game::api.timeStep,0.f,3.f)/50;
 if(!airframeroute::After(h->route,input,pose,e,id.kind==3,dt))return;
 // A damaged native route can descend to a real surface. Keep the wreck at
 // that contact rather than letting the next route update pull it underground.
 if(h->route.descent>1&&!h->route.grounded&&game::api.ProcessVerticalLine){alignas(8) uint8_t point[0x28]{};uint8_t *entity=nullptr;const CVector start{pose[12],pose[13],std::max(input[14],pose[14])+1};
  const bool hit=game::api.ProcessVerticalLine(start,pose[14]-1,point,entity,true,false,false,true,false,false,nullptr);Identity current;if(!Identify(v,current)||!Same(id,current)||h->revision!=revision)return;
  const auto p=*reinterpret_cast<const Vec3 *>(point);if(hit&&std::isfinite(p.z)&&p.z>=pose[14]-1){pose[14]=p.z+1;h->route.grounded=true;h->route.ground={pose[12],pose[13],pose[14]};h->route.applied=pose;if(!(game::EntityFlags(v)&(1u<<27))){*reinterpret_cast<float *>(v+0x2a0)=0;v[0x295]&=uint8_t(~16);v[0x60]=(v[0x60]&7)|0x28;}}
 }
 std::memcpy(v+8,pose.data(),sizeof pose);const auto delta=skinmodel::Sub(airframeroute::Row(pose,12),airframeroute::Row(observed,12));*reinterpret_cast<Vec3 *>(v+0x84)=h->route.grounded?Vec3{}:Vec3{delta.x/std::max(.001f,dt*50),delta.y/std::max(.001f,dt*50),delta.z/std::max(.001f,dt*50)};
 gUpdateMatrix(v+8);gUpdateFrame(v);
}
void Plane(uint8_t *v){Routed(v,gPlane);}void Heli(uint8_t *v){Routed(v,gHeli);}void Boat(uint8_t *v){Tick(v);gBoat(v);}
void Fix(uint8_t *v){Repair(v);gFix(v);}void Delete(uint8_t *v){Identity id;if(Identify(v,id)){auto &h=gHistory[size_t(id.ref>>8)];if(Same(h.id,id)){h.revision=Serial();h.clump=nullptr;}}gDelete(v);}void Restart(){gHistory={};gRestart();}
}
bool Enabled(const uint8_t *v){Identity id;return Identify(v,id)&&Eligible(id);}
Token BeginBullet(uint8_t *v){Observe();Identity id;if(!Identify(v,id)||!Eligible(id)||(game::EntityFlags(v)&((1u<<17)|(1u<<25))))return {};auto *h=Record(id);if(!h)return {};Token t;t.revision=h->revision;t.clump=h->clump;std::memcpy(t.pose.data(),v+8,sizeof t.pose);return t;}
bool Projectile(uint8_t *v,const Token &token,const projectilemodel::Ray &ray,std::array<float,projectilemodel::kLayers> *passed,int exposedRotor)
{
 Observe();auto *h=Current(v,token);if(!h)return false;const auto before=h->state.damage;
 const auto result=airframemodel::Projectile(h->state,h->layout,ray);if(passed)*passed=result.layerRemaining;if(!result.valid)return false;
 // A certified rotating blade can extend outside the static body COL. Its
 // actual visible surface is the contact, rather than a guessed body volume.
 if(h->layout.helicopter&&(exposedRotor==airframemodel::MainRotor||exposedRotor==airframemodel::TailRotor)){
  float energy=std::min(ray.budget,1000.f);bool contact=false;
  for(size_t i=0;i<ray.count;i++)if(std::fabs(ray.layers[i].fraction-ray.entry)<1e-5f){
   energy=std::max(0.f,energy-projectilemodel::Cost(ray.layers[i].material,ray.layers[i].incidence));contact=true;break;
  }
  if(contact)h->state.damage[exposedRotor]=std::min(1.f,std::max(h->state.damage[exposedRotor],before[exposedRotor]+energy/(exposedRotor==airframemodel::MainRotor?100.f:60.f)));
 }
 for(int i=0;i<airframemodel::Count;i++)if(h->state.damage[i]>before[i])Log("airframe projectile: ref %d model %d component %d integrity %.0f%%",h->id.ref,h->id.model,i,100*(1-h->state.damage[i]));
 if(h->state.damage!=before&&game::api.RearmVehicleFire)game::api.RearmVehicleFire(v);
 if(Current(v,token))SyncRotors(*h);return true;
}

void Perforated(uint8_t *v,const Token &token,float area){if(auto *h=Current(v,token))if(std::isfinite(area)&&area>0)h->state.holes=std::min(10.f,h->state.holes+area);}
bool ComponentHit(uint8_t *v,Vec3 from,Vec3 to,float entry,float &closest,Vec3 &normal)
{
 Identity id;if(!Identify(v,id)||!Eligible(id)||!std::isfinite(entry)||entry<0||entry>=closest)return false;const auto &h=gHistory[size_t(id.ref>>8)];if(!Same(h.id,id)||!h.info)return false;
 const auto delta=skinmodel::Sub(to,from);const float a[]={from.x,from.y,from.z},d[]={delta.x,delta.y,delta.z};for(int j=0;j<3;j++)if(!std::isfinite(a[j])||!std::isfinite(d[j]))return false;bool hit=false;
 for(int i=0;i<airframemodel::Count;i++){
  if((h.layout.helicopter&&(i==airframemodel::WingL||i==airframemodel::WingR||i==airframemodel::Elevator||i==airframemodel::Rudder||i==airframemodel::AileronL||i==airframemodel::AileronR))||(!h.layout.helicopter&&(i==airframemodel::MainRotor||i==airframemodel::TailRotor)))continue;
  if(h.layout.single&&i==airframemodel::EngineR)continue;
  const auto &box=h.layout.volumes[i];const float lo[]={box.lo.x,box.lo.y,box.lo.z},hi[]={box.hi.x,box.hi.y,box.hi.z};float near=entry,far=closest;Vec3 n{};bool valid=true;
  for(int axis=0;axis<3;axis++){if(std::fabs(d[axis])<1e-7f){if(a[axis]<lo[axis]||a[axis]>hi[axis])valid=false;continue;}float l=(lo[axis]-a[axis])/d[axis],r=(hi[axis]-a[axis])/d[axis];float sign=-1;if(l>r){std::swap(l,r);sign=1;}if(l>near){near=l;n={float(axis==0)*sign,float(axis==1)*sign,float(axis==2)*sign};}far=std::min(far,r);if(near>=far)valid=false;}
  if(valid&&near<closest){closest=near;normal=n;if(skinmodel::Dot(n,n)<.1f){const float len=std::sqrt(skinmodel::Dot(delta,delta));if(len<=1e-7f)return false;normal={-delta.x/len,-delta.y/len,-delta.z/len};}hit=true;}
 }return hit;
}
bool Install()
{
 if(gReady)return true;gPool=reinterpret_cast<void **>(dlsym(RTLD_MAIN_ONLY,"_ZN6CPools15ms_pVehiclePoolE"));gUpdateMatrix=reinterpret_cast<decltype(gUpdateMatrix)>(dlsym(RTLD_MAIN_ONLY,"_ZN7CMatrix8UpdateRWEv"));gUpdateFrame=reinterpret_cast<decltype(gUpdateFrame)>(dlsym(RTLD_MAIN_ONLY,"_ZN7CEntity13UpdateRwFrameEv"));gMove=reinterpret_cast<decltype(gMove)>(dlsym(RTLD_MAIN_ONLY,"_ZN9CPhysical14ApplyMoveForceEfff"));gTurn=reinterpret_cast<decltype(gTurn)>(dlsym(RTLD_MAIN_ONLY,"_ZN9CPhysical14ApplyTurnForceEffffff"));if(!gPool||!gUpdateMatrix||!gUpdateFrame||!gMove||!gTurn||!game::api.PoolsGetVehicle||!game::api.timeMs)return false;
#define C(v,s,f) if(!v)v=reinterpret_cast<decltype(v)>(hooks::Chain(s,reinterpret_cast<void *>(f)))
 C(gFlying,"_ZN8CVehicle13FlyingControlE12eFlightModel",Flying);C(gAuto,"_ZN11CAutomobile14ProcessControlEv",Auto);C(gPlane,"_ZN6CPlane14ProcessControlEv",Plane);C(gHeli,"_ZN5CHeli14ProcessControlEv",Heli);C(gBoat,"_ZN5CBoat14ProcessControlEv",Boat);C(gFix,"_ZN11CAutomobile3FixEv",Fix);C(gDelete,"_ZN7CEntity14DeleteRwObjectEv",Delete);C(gRestart,"_ZN6CWorld15ClearForRestartEv",Restart);
#undef C
 gReady=gFlying&&gAuto&&gPlane&&gHeli&&gBoat&&gFix&&gDelete&&gRestart;if(gReady){game::api.AirframeFlightUsed=FlightUsed;game::api.AirframeEffects=Read;game::api.AirframeFire=Fire;game::api.AirframeBurn=Burn;game::api.AirframeCollision=Collision;Log("airframes: independent propulsion, lifting/control surfaces, fuel, electrical and rotor components enabled");}return gReady;
}
}
