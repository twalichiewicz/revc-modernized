#include "game.h"
#include "config.h"
#include "hooks.h"
#include "mechanicalmodel.h"
#include <array>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <stdexcept>
static void *Lookup(void *,const char *);
#define dlsym Lookup
#include "../src/carmechanics.cpp"
#undef dlsym
namespace game {Api api{};}
Config gConfig;void Log(const char *,...){}
namespace fixture {
using Control=void (*)(uint8_t *);
using Calc=float (*)(uint8_t *,const float &,uint8_t &,float &,const float &,bool);
using WheelFn=void (*)(uint8_t *,CVector &,CVector &,CVector &,CVector &,int,float,float,float,int8_t,float *,int32_t *,uint16_t);
Control control,fix,removeRw;void (*restart)();Calc calc;WheelFn wheel;
alignas(16) uint8_t cars[2][0x6f0]{},pool[24]{},handling[0xdc]{},infos[2][0x260]{},cols[2][0x68]{};
uint8_t flags[2]={1,1};void *poolPointer=pool;std::array<uint8_t *,6500> models{};uint32_t now=1000;
struct Frame{float m[16]{};};Frame frames[4];int modelClump;
int fails=0,bursts=0,nativeFixes=0;bool nested=false,repairInside=false,recycleInside=false,throwInside=false,foreignCalc=false,deleteInside=false;
std::array<std::vector<carimpact::Collision>,2> hits;
float steerAngle=0;float torque[2]{},foreignTorque=0;const uint8_t *retained=nullptr;
struct WCall {float thrust,brake,grip,toe;CVector direction;};WCall wheels[2][4];
float &F(uint8_t *p,size_t at){return *reinterpret_cast<float *>(p+at);}
void Check(bool b,const char *s){if(!b){printf("FAIL: %s\n",s);++fails;}}
uint8_t *Car(int ref){int i=ref>>8;return i>=0&&i<2&&!(flags[i]&0x80)&&ref==((i<<8)|flags[i])?cars[i]:nullptr;}
void *FrameId(void *,int id){int ids[]={4,6,1,3};for(int i=0;i<4;i++)if(id==ids[i])return &frames[i];return nullptr;}
float *Matrix(void *p){return static_cast<Frame *>(p)->m;}
int Status(void *p,int i){return static_cast<uint8_t *>(p)[5+i];}
void Burst(uint8_t *v,uint8_t w,bool random){Check(w<4&&!random,"native puncture gets exact wheel without random extra forces");++bursts;if(!(v[0x299]&2))v[0x34c+5+w]=1;}
float CalcNative(uint8_t *t,const float &,uint8_t &,float &timer,const float &,bool){retained=t;F(t,0x5c)=3.25f;timer+=.01f;return .04f;}
void WheelNative(uint8_t *v,CVector &f,CVector &,CVector &,CVector &,int n,float drive,float brake,float grip,int8_t w,float *spin,int32_t *state,uint16_t status){
 const int i=v==cars[0]?0:1;Check(n==4&&status==0,"native wheel count/status preserved");wheels[i][w]={drive,brake,grip,f.x,f};*spin=12.5f;*state=7;
}
void ControlNative(uint8_t *v){const int i=v==cars[0]?0:1;
 if(throwInside){throwInside=false;throw std::runtime_error("native interrupted");}
 for(auto h:hits[i])carmechanics::Collision(v,h);
 if(nested&&i==0){nested=false;control(cars[1]);}
 if(repairInside){repairInside=false;fix(v);}
 if(deleteInside){deleteInside=false;removeRw(v);*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(0x9000);}
 if(recycleInside){recycleInside=false;++flags[i];}
 auto t=*reinterpret_cast<uint8_t **>(v+0x160)+0x34;float velocity=.2f;
 torque[i]=calc(t,F(v,0x28c),v[0x2a4],F(v,0x2a8),velocity,false);
 if(foreignCalc){float gas=1,timer=0;uint8_t gear=1;foreignTorque=calc(t,gas,gear,timer,velocity,false);}
 for(int w=0;w<4;w++){const float angle=w==0||w==2?steerAngle:0;CVector f{std::sin(angle),std::cos(angle),0},r{std::cos(angle),-std::sin(angle),0},speed{0,.2f,0},point{w<2?-1.f:1.f,w%2?-1.4f:1.4f,-.35f};int32_t state=0;
  wheel(v,f,r,speed,point,4,.02f,.01f,.05f,int8_t(w),&F(v,0x5ec+4*w),&state,0);
  Check(f.x==std::sin(angle)&&f.y==std::cos(angle)&&r.x==std::cos(angle)&&r.y==-std::sin(angle)&&F(v,0x5ec+4*w)==12.5f&&state==7,"wheel directions remain caller-owned and native output pointers survive");
 }
 Check(*reinterpret_cast<uint8_t **>(v+0x160)==handling,"controller keeps original shared handling allocation");
}
void FixNative(uint8_t *v){++nativeFixes;memset(v+0x34c,0,24);F(v,0x2a0)=1000;}
void RemoveNative(uint8_t *){}void RestartNative(){}
void Reset(int model=150){
 for(auto &v:cars)fix(v);memset(cars,0,sizeof cars);memset(handling,0,sizeof handling);memset(infos,0,sizeof infos);memset(cols,0,sizeof cols);
 nested=repairInside=recycleInside=throwInside=foreignCalc=deleteInside=false;steerAngle=0;hits={};bursts=0;now+=1000;models.fill(nullptr);
 for(int i=0;i<2;i++){
  flags[i]=uint8_t((flags[i]+1)&0x7f);auto v=cars[i];*reinterpret_cast<uint32_t *>(v+0x60)=2|0x20;v[0x297]=0x40;v[0x295]=0x10;
  *reinterpret_cast<void **>(v)=reinterpret_cast<void *>(uintptr_t(0x1000+i));*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(uintptr_t(0x2000+i));
  *reinterpret_cast<int16_t *>(v+0x6a)=int16_t(model);*reinterpret_cast<uint8_t **>(v+0x160)=handling;F(v,0x28c)=1;F(v,0x2a0)=1000;F(v,8)=F(v,0x1c)=F(v,0x30)=1;
 }
 auto info=infos[0];models[model]=info;info[0x1d]=6;*reinterpret_cast<void **>(info+0x30)=&modelClump;*reinterpret_cast<void **>(info+0x20)=cols[0];
 *reinterpret_cast<CVector *>(cols[0]+0x10)={-1.2f,-2.5f,-.7f};*reinterpret_cast<CVector *>(cols[0]+0x1c)={1.2f,2.5f,1.2f};
 for(int w=0;w<4;w++){memset(frames[w].m,0,64);frames[w].m[0]=frames[w].m[5]=frames[w].m[10]=1;frames[w].m[12]=w<2?-1.f:1.f;frames[w].m[13]=w%2?-1.4f:1.4f;frames[w].m[14]=-.35f;}
}
void Run(uint32_t dt=20){now+=dt;control(cars[0]);}
carimpact::Collision Nose(float severity=30){return {{0,2.4f,.1f},{0,-1,0},severity,3};}
}
namespace hooks {void *Chain(const char *s,void *r){using namespace fixture;
#define H(n,v,T) if(!strcmp(s,n)){auto old=v;v=reinterpret_cast<T>(r);return reinterpret_cast<void *>(old);}
 H("_ZN11CAutomobile14ProcessControlEv",control,Control);H("_ZN11CAutomobile3FixEv",fix,Control);H("_ZN7CEntity14DeleteRwObjectEv",removeRw,Control);
 H("_ZN6CWorld15ClearForRestartEv",restart,decltype(restart));H("_ZN13cTransmission26CalculateDriveAccelerationERKfRhRfS1_b",calc,Calc);
 H("_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet",wheel,WheelFn);
#undef H
 return nullptr;
}}
static void *Lookup(void *,const char *s){using namespace fixture;
 if(!strcmp(s,"_ZN6CPools15ms_pVehiclePoolE"))return &poolPointer;
 if(!strcmp(s,"_ZN15CClumpModelInfo14GetFrameFromIdEPN2rw5ClumpEi"))return reinterpret_cast<void *>(FrameId);
 if(!strcmp(s,"_Z16RwFrameGetMatrixPN2rw5FrameE"))return reinterpret_cast<void *>(Matrix);
 if(!strcmp(s,"_ZN14CDamageManager14GetWheelStatusEi"))return reinterpret_cast<void *>(Status);
 if(!strcmp(s,"_ZN11CAutomobile9BurstTyreEhb"))return reinterpret_cast<void *>(Burst);return nullptr;
}
int main(){using namespace fixture;
 control=ControlNative;fix=FixNative;removeRw=RemoveNative;restart=RestartNative;calc=CalcNative;wheel=WheelNative;
 *reinterpret_cast<void **>(pool)=cars;*reinterpret_cast<void **>(pool+8)=flags;*reinterpret_cast<int *>(pool+16)=2;
 game::api.PoolsGetVehicle=Car;game::api.timeMs=&now;game::api.modelInfoPtrs=models.data();
 gConfig.carMechanics=false;Check(carmechanics::Install()&&control==ControlNative,"startup off installs no mechanics hooks");gConfig.carMechanics=true;Check(carmechanics::Install(),"mechanics hooks install");
 Reset();Run();Check(torque[0]==.04f,"intact car delivers original native torque");
 hits[0]={Nose()};nested=foreignCalc=true;Run();const float hurt=torque[0];Check(hurt<.04f&&hurt>0&&torque[1]==.04f&&foreignTorque==.04f,"engine hit affects own native actor refs even with shared handling and nested cars");
 Check(retained==handling+0x34&&F(handling,0x90)==3.25f&&F(cars[0],0x2a8)>0,"native transmission retains real allocation scratch and output writes");
 for(int i=0;i<100;i++)Run();Check(std::fabs(torque[0]-hurt)<1e-6f,"continuous contact cannot consume every mechanical stage");Run(350);Check(torque[0]<hurt,"separate impact deepens engine damage");
 fix(cars[0]);hits={};Run();Check(torque[0]==.04f,"repair restores full engine output");
 Reset(211);hits[0]={Nose()};Run();Check(torque[0]==.04f,"Deluxo frontal hit preserves rear engine initially");hits[0]={{{0,-2.4f,.1f},{0,1,0},30,4}};Run(350);Check(torque[0]<.04f,"Deluxo rear-bay hit reduces power");
 Reset();hits[0]={{{-1,1.4f,-.35f},{1,0,0},30,13}};Run();Check(wheels[0][0].grip<.05f&&wheels[0][0].brake<.01f&&wheels[0][0].toe>0&&wheels[0][2].grip==.05f,"localized corner changes only that native wheel");
 Run(350);Run(350);Check(bursts==1&&Status(cars[0]+0x34c,0)==1,"severely crushed assembly requests native puncture once");fix(cars[0]);hits={};Run();Check(wheels[0][0].grip==.05f&&wheels[0][0].toe==0,"repair restores wheel performance and alignment");
 Reset();cars[0][0x299]|=2;hits[0]={{{-1,1.4f,-.35f},{1,0,0},30,13}};Run();Run(350);Run(350);Check(Status(cars[0]+0x34c,0)==0,"native tyre proof remains authoritative");
 Reset();hits[0]={Nose()};Run();hits={};*reinterpret_cast<void **>(cars[0]+0x58)=reinterpret_cast<void *>(0x9000);Run();Check(torque[0]<.04f,"render-only clump replacement does not heal engine");
 deleteInside=true;Run();Check(torque[0]==.04f,"render deletion inside controller retires in-flight physics ownership");Run();Check(torque[0]<.04f,"next controller retains damage through actual render deletion");
 restart();Run();Check(torque[0]==.04f,"world restart clears module state");
 Reset();hits[0]={Nose()};repairInside=true;Run();Check(torque[0]==.04f,"repair inside controller invalidates in-flight penalties");
 Reset();hits[0]={Nose()};Run();hits={};++flags[0];Run();Check(torque[0]==.04f,"recycled actor cannot inherit engine damage");
 Reset();hits[0]={Nose()};recycleInside=true;Run();Check(torque[0]==.04f,"recycling inside native controller cannot scale new actor");
 Reset();carmechanics::Collision(cars[0],Nose());Run();Check(torque[0]==.04f,"unscoped event cannot damage modules");
 Reset();cars[0][0x63]|=8;hits[0]={Nose()};Run();Check(torque[0]==.04f,"native collision proof car remains native");
 Reset();hits[0]={Nose()};gConfig.carMechanics=false;Run();Check(torque[0]==.04f,"runtime off is native pass-through");gConfig.carMechanics=true;
 Reset();hits[0]={{{0,2.5f,.1f},{0,-1,0},8,3}};for(int i=0;i<7;i++)Run(350);hits={};for(int i=0;i<1800;i++)Run(100);Check(torque[0]<.02f,"cooling-only damage eventually reduces performance while running");
 fix(cars[0]);Run();Check(torque[0]==.04f,"repair refills coolant and clears heat penalty");
 Reset();hits[0]={{{0,2.5f,.1f},{0,-1,0},8,3}};for(int i=0;i<7;i++)Run(350);hits={};for(int i=0;i<800;i++)Run(100);
 const float warm=torque[0];Check(warm>0&&warm<.04f,"warming cooling failure has observable partial power penalty");
 Run(5000);Check(torque[0]==warm,"large control discontinuity cannot inject thermal damage");now-=10000;Run(0);Check(torque[0]==warm,"clock rewind preserves damage without extra heat or healing");Run(100);Check(torque[0]<warm,"ordinary control resumes thermal progression after discontinuity");
 Reset();throwInside=true;try{Run();}catch(const std::runtime_error &){}carmechanics::Collision(cars[0],Nose());Run();Check(torque[0]==.04f,"exception unwinds mechanics ownership");
 // Isolate dedicated modules from the damage to nearby metal/hubs.
 Reset();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kBrakeLF]=1;}Run();
 Check(wheels[0][0].brake==0&&wheels[0][0].grip==.05f&&wheels[0][0].thrust==.02f&&wheels[0][2].brake==.01f,"destroyed LF brake removes only its native service brake input");
 Reset();steerAngle=.4f;Run();const float healthySteer=wheels[0][0].toe;
 {carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kSteering]=1;scope.history->state.steeringBias=.02f;}Run();
 Check(wheels[0][0].toe>0&&wheels[0][0].toe<healthySteer*.4f&&wheels[0][2].toe==wheels[0][0].toe&&wheels[0][1].toe==0,"rack failure reduces both front wheel steering in the native solver");
 steerAngle=-.4f;Run();Check(wheels[0][0].toe<0&&wheels[0][0].toe>-.15f,"rack authority preserves opposite steering sign");
 fix(cars[0]);Run();Check(std::fabs(wheels[0][0].toe-std::sin(steerAngle))<1e-6f,"repair restores full native steering direction");
 Reset();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kFuel]=1;}Run();
 Check(torque[0]==.04f,"breached tank initially retains native torque");cars[0][0x295]&=~0x10;
 for(int i=0;i<300;i++)Run(300);cars[0][0x295]|=0x10;Run();Check(torque[0]==0,"parked fuel leak depletes through slow controller updates");
 fix(cars[0]);Run();Check(torque[0]==.04f,"native repair refills fuel reservoir");
 Reset();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kDrivetrain]=1;}nested=true;Run();
 Check(torque[0]==0&&torque[1]==.04f,"disconnected drivetrain leaves nested shared-handling actor powered");
 // Native producer projects forward onto contact plane before steering.
 for(bool rotated:{false,true})for(float angle:{0.0f,.4f,-.4f}){
  Reset();if(rotated){F(cars[0],8)=0;F(cars[0],0xc)=-1;F(cars[0],0x18)=1;F(cars[0],0x1c)=0;}
  carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kSteering]=1;
  CVector base{-.288675135f,.866025404f,-.408248290f},right{.816496581f,0,-.577350269f};
  if(rotated){base={base.y,-base.x,base.z};right={right.y,-right.x,right.z};}
  const float cs=std::cos(angle),sn=std::sin(angle);
  CVector f{base.x*cs+right.x*sn,base.y*cs+right.y*sn,base.z*cs+right.z*sn},r{right.x*cs-base.x*sn,right.y*cs-base.y*sn,right.z*cs-base.z*sn},speed{},point{};int32_t status=0;
  wheel(cars[0],f,r,speed,point,4,0,0,.1f,0,&F(cars[0],0x5ec),&status,0);
  const auto observed=wheels[0][0].direction;const float along=observed.x*base.x+observed.y*base.y+observed.z*base.z,across=observed.x*right.x+observed.y*right.y+observed.z*right.z;
  Check(std::fabs(std::atan2(across,along)-angle*.2f)<1e-5f,"rack authority acts only on native steering, not contact-plane tilt or actor heading");
 }
 Reset();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kElectrical]=.5f;}Run();
 Check(carmechanics::LightingPower(cars[0])==.6f&&carmechanics::LightingPower(cars[1])==1,"render query reads only injured actor supply outside control scope");
 removeRw(cars[0]);*reinterpret_cast<void **>(cars[0]+0x58)=reinterpret_cast<void *>(0x9000);
 Check(carmechanics::LightingPower(cars[0])==.6f,"render replacement retains electrical failure");
 gConfig.carMechanics=false;Check(carmechanics::LightingPower(cars[0])==1,"runtime off restores native lighting");gConfig.carMechanics=true;
 cars[0][0x63]|=8;Check(carmechanics::LightingPower(cars[0])==1,"native protected actor retains native lighting");cars[0][0x63]&=~8;
 ++flags[0];Check(carmechanics::LightingPower(cars[0])==1,"recycled actor cannot inherit electrical dimming");
 Run();{carmechanics::Controller scope(cars[0]);scope.history->state.damage[mechanicalmodel::kElectrical]=1;}
 Check(carmechanics::LightingPower(cars[0])==0,"failed supply remains visible outside native control");
 fix(cars[0]);Check(carmechanics::LightingPower(cars[0])==1,"repair immediately restores lamp supply");
 Reset();projectilemodel::Layer bodyPanel{1.f/12,1,projectilemodel::Material::Body};
 projectilemodel::Ray bullet{{0,3,.2f},{0,-3,.2f},1.f/12,1,20,&bodyPanel,1};
 auto token=carmechanics::BeginBullet(cars[0]);Check(token.revision&&carmechanics::gController==nullptr,"bullet lease works outside controller without claiming a physics scope");
 carmechanics::Projectile(cars[0],token,bullet);Run();const float shotPower=torque[0];Check(shotPower>0&&shotPower<.04f,"certified projectile injury reaches native engine power on next control");
 token=carmechanics::BeginBullet(cars[0]);carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]<shotPower,"separate rounds accumulate without collision contact cooldown");
 token=carmechanics::BeginBullet(cars[0]);fix(cars[0]);carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"repair retires pending projectile lease");
 token=carmechanics::BeginBullet(cars[0]);++flags[0];carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"recycled actor cannot receive a pending round");
 token=carmechanics::BeginBullet(cars[0]);restart();carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"restart retires pending round even before first new controller");
 token=carmechanics::BeginBullet(cars[0]);removeRw(cars[0]);carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"render deletion retires a pending round even if its clump address is reused");token=carmechanics::BeginBullet(cars[0]);removeRw(cars[0]);*reinterpret_cast<void **>(cars[0]+0x58)=reinterpret_cast<void *>(0x9000);carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"changed render clump rejects old hit geometry");
 token=carmechanics::BeginBullet(cars[0]);F(cars[0],0x38)=1;carmechanics::Projectile(cars[0],token,bullet);Run();Check(torque[0]==.04f,"moved actor cannot receive a stale local ray");
 Reset();cars[0][0x62]|=2;Check(!carmechanics::BeginBullet(cars[0]).revision,"bulletproof actor rejects projectile lease");
 Reset();gConfig.carMechanics=false;Check(!carmechanics::BeginBullet(cars[0]).revision,"mechanics off cannot begin projectile effects");gConfig.carMechanics=true;
 Reset();projectilemodel::Layer tyrePanel{0,1,projectilemodel::Material::Rubber};projectilemodel::Ray wheelShot{{-1.5f,1.4f,-.35f},{0,1.4f,-.35f},0,1,40,&tyrePanel,1};
 for(int i=0;i<8;i++){token=carmechanics::BeginBullet(cars[0]);carmechanics::Projectile(cars[0],token,wheelShot);}Run();
 Check(wheels[0][0].grip<.05f&&wheels[0][0].brake<.01f&&Status(cars[0]+0x34c,0)==1,"local projectile path changes wheel/caliper performance and composes with native punctures");
 Check(carmechanics::gHistory[0].state.toe==std::array<float,4>{}&&carmechanics::gHistory[0].state.steeringBias==0,"projectile integrity damage has no invented collision bending impulse");
 printf("carmechanics: %s\n",fails?"FAILED":"all passed");return fails?1:0;
}
