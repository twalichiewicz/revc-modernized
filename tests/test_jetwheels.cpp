// Native CPlane control -> physical wheel contacts -> native-style rolling.
#include "../src/game.h"
#include "../src/config.h"
#include "../src/jetwheels.h"
#include "../src/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
static void *WheelSymbol(void *,const char *);
static void *(*gWheelExtraSymbol)(const char *);
#ifndef JETWHEELS_BASELINE
#define dlsym WheelSymbol
#include "../src/jetwheels.cpp"
#undef dlsym
#else
namespace jetwheels {bool Install(){return true;}bool Grounded(const uint8_t *){return false;}bool Snapshot(const uint8_t *,std::array<WheelPose,3> &p){p={};return false;}}
#endif
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace {
alignas(16) uint8_t v[0x6f0]{},ped[0x900]{},pad[0x200]{},pool[0x20]{},handling[0xdc]{};
uint8_t *poolPtr=pool,flags[1]{1};uint32_t clockMs=1000;float step=1;
bool owner=true,mission=false,cutscene=false,ground=true,recycleRay=false;int fails=0,queries=0,wheelCalls=0,nativeCalls=0;
float gas=0,brake=0,steer=0,gear=1;void (*control)(uint8_t *);
CVector &V(uint8_t *p,int o){return *reinterpret_cast<CVector *>(p+o);}float &F(uint8_t *p,int o){return *reinterpret_cast<float *>(p+o);}
CVector Plus(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}CVector Minus(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}CVector Times(CVector a,float n){return {a.x*n,a.y*n,a.z*n};}
float DotW(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}CVector CrossW(CVector a,CVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
void Check(bool ok,const char *s){if(!ok){++fails;printf("FAIL: %s\n",s);}}
uint8_t *Player(){return ped;}uint8_t *Pad(int){return pad;}uint8_t *Vehicle(int ref){return owner&&ref==flags[0]&&!(flags[0]&128)?v:nullptr;}
bool Mission(){return mission;}int16_t Brake(uint8_t *){return int16_t(brake);}int16_t Steer(uint8_t *){return int16_t(steer);}
void Move(uint8_t *p,float x,float y,float z){V(p,0x84)=Plus(V(p,0x84),Times({x,y,z},1/F(p,0xcc)));}
void Turn(uint8_t *p,float x,float y,float z,float lx,float ly,float lz){
 const CVector com=Plus(Plus(Times(V(p,8),V(p,0xe4).x),Times(V(p,0x18),V(p,0xe4).y)),Times(V(p,0x28),V(p,0xe4).z));
 V(p,0x90)=Plus(V(p,0x90),Times(CrossW(Minus({lx,ly,lz},com),{x,y,z}),1/F(p,0xd0)));
}
CVector Speed(uint8_t *p,const CVector &r){return Plus(Plus(V(p,0x84),V(p,0x9c)),CrossW(Plus(V(p,0x90),V(p,0xa8)),r));}
bool Ray(const CVector &from,float to,uint8_t *point,uint8_t *&entity,bool buildings,bool,bool,bool,bool,bool,void *){
 ++queries;Check(buildings,"wheel ray includes real runway buildings");entity=nullptr;if(recycleRay){recycleRay=false;++flags[0];return false;}
 if(!ground||from.z<0||to>0)return false;std::memset(point,0,40);V(point,0)={from.x,from.y,0};V(point,0x10)={0,0,1};point[0x22]=1;return true;
}
void NativeWheel(uint8_t *p,CVector &forward,CVector &right,CVector &speed,CVector &point,int count,float thrust,float brakeForce,float adhesion,int8_t,float *,int32_t *state,uint16_t){
 ++wheelCalls;Check(p==v&&count>0&&count<=3&&thrust==0&&std::isfinite(adhesion)&&adhesion>=0,"base CVehicle::ProcessWheel receives exact CPlane-safe contact/count/thrust ABI");
 Check(std::fabs(DotW(forward,right))<.001f&&std::fabs(forward.z)<.01f&&std::fabs(right.z)<.01f,"native wheel directions are runway tangents");
 // Captured native ProcessWheel27cb28/27ccf8 divides requested slip by
 // grounded count, NOT adhesion.27cd58..cdc4 applies its traction circle.
 const float lateral=-DotW(speed,right)/count;
 const float roll=.03f/F(handling,4)*step/1.6666667f;
 const float limit=brakeForce>0?brakeForce:roll;
 float longitudinal=std::clamp(-DotW(speed,forward)/count,-limit,limit),side=lateral;
 const bool skidding=*state!=0;const float loss=F(handling,0xa4);
 const float cap=adhesion*step*(skidding?loss:1),length=std::hypot(longitudinal,side);
 if(length>cap&&length>0){const float scale=cap*(skidding?1:loss)/length;longitudinal*=scale;side*=scale;*state=2;}else *state=0;
 const CVector dv=Plus(Times(forward,longitudinal),Times(right,side));
 CVector turnDv=dv;
 if(brakeForce>0&&F(handling,0xc4)>0)turnDv=Minus(turnDv,Times(forward,longitudinal*F(handling,0xc4)));
 const float turnLength=std::sqrt(DotW(turnDv,turnDv));
 if(turnLength==0)return;
 const CVector direction=Times(turnDv,1/turnLength),cross=CrossW(point,direction);
 //27cecc..cefc computes the actual contact effective turn mass; using body
 // mass here silently exaggerates long-lever nose-wheel steering tenfold.
 const float effective=1/(1/F(p,0xcc)+DotW(cross,cross)/F(p,0xd0));
 const CVector force=Times(dv,F(p,0xcc)),turnForce=Times(turnDv,effective);
 Move(p,force.x,force.y,force.z);Turn(p,turnForce.x,turnForce.y,turnForce.z,point.x,point.y,point.z);
}
float Rotation(uint8_t *,int state,const CVector &forward,const CVector &speed,float radius){return state==3?0:-DotW(forward,speed)/radius*step;}
float Adhesion(uint8_t *point){Check(point[0x20]==29,"contact uses native tire surface29 with actual runway surfaceB");return 4.5f;}
float Wet(uint8_t){return 1;}
void Native(uint8_t *p){++nativeCalls;if(p[0x156]&2)V(p,0x84).z-=.008f*step;V(p,0x84)=Plus(V(p,0x84),Times(V(p,0x18),gas*.0032f*step));}
void Reset(){
#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(v,sizeof v);
#endif
 std::memset(v,0,sizeof v);std::memset(ped,0,sizeof ped);std::memset(pad,0,sizeof pad);std::memset(handling,0,sizeof handling);++flags[0];clockMs+=100;
 owner=ground=true;mission=cutscene=recycleRay=false;step=1;gas=brake=steer=0;gear=1;queries=wheelCalls=nativeCalls=0;
 *reinterpret_cast<void **>(v)=reinterpret_cast<void *>(uintptr_t(0x1000));*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(uintptr_t(0x2000));
 *reinterpret_cast<int32_t *>(v+0x60)=2;*reinterpret_cast<int16_t *>(v+0x6a)=248;*reinterpret_cast<int32_t *>(v+0x348)=4;v[0x294]=1;v[0x156]=2;v[0x295]=16;
 V(v,8)={1,0,0};V(v,0x18)={0,1,0};V(v,0x28)={0,0,1};V(v,0x38)={0,0,-jetwheels::kTireBottom};F(v,0xcc)=5000;F(v,0xd0)=20000;F(v,0xd4)=1;F(v,0x2a0)=1000;
 // Actual native DEADDODO handling used by the full-size CPlane actor.
 *reinterpret_cast<uint8_t **>(v+0x160)=handling;F(handling,4)=25500;F(handling,0x30)=.58f;F(handling,0x94)=.0016f;F(handling,0xa0)=30;F(handling,0xa4)=.6f;F(handling,0xc4)=.25f;
 ped[0x4c0]=1;*reinterpret_cast<uint8_t **>(ped+0x4b8)=v;*reinterpret_cast<uint8_t **>(v+0x218)=ped;
#if __has_feature(address_sanitizer)
 __asan_poison_memory_region(v+0x388,sizeof v-0x388);
#endif
}
void Step(){
 control(v);V(v,0x38)=Plus(V(v,0x38),Times(V(v,0x84),step));
 const CVector rotation=Times(V(v,0x90),step);
 for(int at:{8,0x18,0x28})V(v,at)=Plus(V(v,at),CrossW(rotation,V(v,at)));
 auto unit=[](CVector p){return Times(p,1/std::sqrt(DotW(p,p)));};
 V(v,0x28)=unit(V(v,0x28));V(v,8)=unit(CrossW(V(v,0x18),V(v,0x28)));V(v,0x18)=CrossW(V(v,0x28),V(v,8));
 clockMs+=uint32_t(step*20);
}
}
namespace airportjets {float GearDown(const uint8_t *p){return owner&&p==v?gear:0;}bool Owns(const uint8_t *p){return owner&&p==v&&game::EntityModel(p)==248&&*reinterpret_cast<const int32_t *>(p+0x348)==4;}}
#ifndef JETWHEELS_NATIVE_FLIGHT
namespace planeflight {void SetAirportWheelControl(bool (*)(const uint8_t *)) {}}
#endif
namespace hooks {void *Chain(const char *name,void *hook){if(std::strstr(name,"CPlane14ProcessControl")){auto old=control;control=reinterpret_cast<decltype(control)>(hook);return reinterpret_cast<void *>(old);}return nullptr;}}
[[maybe_unused]] static void *WheelSymbol(void *,const char *name){
#define B(n,f) if(std::strstr(name,n))return reinterpret_cast<void *>(f)
 B("ApplyMoveForce",Move);B("ApplyTurnForce",Turn);B("GetSpeed",Speed);B("ProcessWheelRotation",Rotation);B("ProcessWheelE",NativeWheel);B("GetAdhesiveLimit",Adhesion);B("GetWetMultiplier",Wet);B("GetBrake",Brake);B("GetSteeringLeftRight",Steer);B("IsPlayerOnAMission",Mission);
#undef B
 if(std::strstr(name,"ms_pVehiclePool"))return &poolPtr;if(std::strstr(name,"ms_running"))return &cutscene;
 return gWheelExtraSymbol?gWheelExtraSymbol(name):nullptr;
}
int main(){
 control=Native;game::api.FindPlayerPed=Player;game::api.GetPad=Pad;game::api.PoolsGetVehicle=Vehicle;game::api.ProcessVerticalLine=Ray;game::api.timeStep=&step;game::api.timeMs=&clockMs;
 *reinterpret_cast<uint8_t **>(pool)=v;*reinterpret_cast<uint8_t **>(pool+8)=flags;*reinterpret_cast<int32_t *>(pool+0x10)=1;
 Check(jetwheels::Install(),"real CPlane wheel hook installs");
 Reset();Step();std::array<jetwheels::WheelPose,3> poses{};
 Check(wheelCalls==3&&jetwheels::Grounded(v)&&jetwheels::Snapshot(v,poses),"three real tire contacts support a native CPlane without Automobile fields");
 Check(std::fabs(V(v,0x84).z)<.003f&&std::fabs(V(v,0x90).x)<.001f,"COM-balanced three-gear support cancels native gravity without a nose/tail tipping torque");
 Reset();for(int i=0;i<500;++i)Step();Check(std::fabs(V(v,0x38).z+jetwheels::kTireBottom)<.04f&&std::fabs(V(v,0x84).z)<.01f,"suspension settles near measured wheel hubs without bodyground dragging");
 Reset();gas=1;for(int i=0;i<250;++i)Step();jetwheels::Snapshot(v,poses);Check(V(v,0x84).y*50>30&&std::fabs(poses[0].spin)>.1f,"free rolling aircraft accelerates through real three-wheel contacts and renders physical wheel spin");
 const float start=V(v,0x84).y;gas=0;brake=255;for(int i=0;i<100;++i)Step();Check(V(v,0x84).y<start*.85f&&V(v,0x84).y>=-.001f,"native wheel brakes reduce taxi speed without reversing longitudinal velocity");
 Reset();V(v,0x84).y=.2f;steer=128;for(int i=0;i<10;++i)Step();jetwheels::Snapshot(v,poses);Check(poses[0].steer<0&&poses[1].steer==0&&V(v,0x90).z<0,"steered nose contact turns the body and visual assembly together; main gears remain straight");
 Reset();ground=false;Step();Check(!wheelCalls&&!jetwheels::Grounded(v)&&V(v,0x84).z<0,"airborne CPlane keeps native gravity with no invented ground friction or support");
 Reset();Step();gear=0;jetwheels::Snapshot(v,poses);Check(!poses[0].grounded&&!jetwheels::Grounded(v),"retracting gear immediately invalidates stale deployed-wheel support and pose before next physics tick");
 for(float deployment:{0.0f,.5f,.98f,.9995f}){Reset();gear=deployment;Step();Check(!queries&&!wheelCalls&&!jetwheels::Grounded(v)&&V(v,0x84).z<0,"stowed and traveling gear provide no invisible contact at their former extended hub positions");
  Check(jetwheels::Snapshot(v,poses)&&!poses[0].grounded&&poses[0].compression<0,"traveling wheels retain honest ungrounded animation state");}
 Reset();gear=.5f;Step();gear=1;Step();Check(wheelCalls==3&&jetwheels::Grounded(v),"fully deployed gear resumes measured physical tire support after travel");
 Reset();owner=false;Step();Check(!queries&&!wheelCalls&&nativeCalls==1,"ambient/unowned plane remains on its original native control path");
 Reset();recycleRay=true;Step();Check(!wheelCalls&&!jetwheels::Snapshot(v,poses),"generation recycle during a ground query cannot apply forces or retain old visual wheel state");
 for(float tick:{.25f,1.0f,3.0f}){Reset();step=tick;for(int i=0;i<int(500/tick);++i)Step();Check(std::fabs(V(v,0x38).z+jetwheels::kTireBottom)<.04f&&std::fabs(V(v,0x28).z-1)<.001f,"substepped spring support is stable across actual native frame lengths");}
 Reset();v[0x156]=0;Step();Check(wheelCalls==0&&std::fabs(V(v,0x84).z)<1e-6f,"native gravity-disabled scripted actor cannot receive invented spring support");
 Reset();Step();*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(uintptr_t(0x3000));Check(!jetwheels::Snapshot(v,poses),"new clump cannot reuse former tire animation state");
 Reset();Step();clockMs=1;Check(!jetwheels::Snapshot(v,poses),"timeline rewind discards all prior tire physics/animation cache");
 Reset();V(v,0xe4)={0,2,0};for(int i=0;i<300;++i)Step();Check(std::fabs(V(v,0x28).z-1)<.002f&&std::fabs(V(v,0x38).z+jetwheels::kTireBottom)<.04f,"normal loads follow actual forward-shifted COM rather than equal-three-spring tail tipping");
#if __has_feature(address_sanitizer)
 __asan_unpoison_memory_region(v,sizeof v);
#endif
 if(!fails)puts("jet wheels native: all passed");return fails?1:0;
}
