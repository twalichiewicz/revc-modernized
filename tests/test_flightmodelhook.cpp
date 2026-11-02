// Actual shared FlyingControl/DoBladeCollision/Automobile controller boundaries.
#include <dlfcn.h>
static void *FlightSymbol(void *,const char *);
#define dlsym FlightSymbol
#include "../src/flightmodel.cpp"
#undef dlsym
#include "../src/shotcollision.h"
#include "../src/nativebullet.h"
#include <cstdio>
namespace game { Api api{}; }
Config gConfig;
void Log(const char *,...){}
// Exercise the actual shared controller consumer, rather than a handled-bool
// stub that cannot catch a broken input/class/force ABI between the modules.
#define dlsym FlightSymbol
#include "../src/planeflight.cpp"
#undef dlsym
static bool gAcceptPolice=true;
namespace heli { bool FailRotor(uint8_t *,bool){return gAcceptPolice;} }
namespace {
alignas(16) uint8_t vehicles[2][0x6f0]{},player[0x900]{},pad[0x200]{},pool[0x20]{};
uint8_t *poolPtr=pool,flags[2]{1,1};float step=1;int fails=0,originalCalls=0,turnCalls=0;bool strike=false;
int16_t throttle=0,brake=0,pitch=0,roll=0,gunPitch=0,gunYaw=0;bool left=false,right=false,horn=false;
bool activeMission=false,cutscene=false;
bool OnMission(){return activeMission;}
using Fly=void (*)(uint8_t *,int);using Auto=void (*)(uint8_t *);using Blade=bool (*)(uint8_t *,CVector,uint8_t *,int16_t,float,float);
void NativeFly(uint8_t *,int);void NativeAuto(uint8_t *);bool NativeBlade(uint8_t *,CVector,uint8_t *,int16_t,float,float);
Fly fly=NativeFly;Auto automobile=NativeAuto;Blade blade=NativeBlade;
struct Frame {const char *name;Frame *parent=nullptr;std::array<float,16> matrix{};};
struct Atomic {Frame *frame;unsigned flags=4;};
Frame mainFrame{"toprotor"},solidMainFrame{"bonnet_hi_ok"},bodyFrame{"chassis_hi"},tailFrame{"rearrotor"};
Frame rootFrame{"root"};CVector nativeBladeOrigin{};
Atomic atomics[4]{{&mainFrame,4},{&solidMainFrame,4},{&bodyFrame,4},{&tailFrame,4}};
Atomic *currentAtomics=atomics;
void *Each(void *,void *(*f)(void *,void *),void *ctx){for(int i=0;i<4;++i)f(currentAtomics+i,ctx);return currentAtomics;}
void *FrameOf(const void *a){return static_cast<const Atomic *>(a)->frame;}
const char *Name(void *f){return static_cast<Frame *>(f)->name;}
void *Parent(const void *f){return static_cast<const Frame *>(f)->parent;}
float *FrameMatrix(void *f){return static_cast<Frame *>(f)->matrix.data();}
void *RootFrame(const void *){return &rootFrame;}
unsigned Flags(const void *a){return static_cast<const Atomic *>(a)->flags;}
void *SetFlags(void *a,unsigned flags){static_cast<Atomic *>(a)->flags=flags;return a;}
void NativeFix(uint8_t *v){*reinterpret_cast<float *>(v+0x2a0)=1000;*reinterpret_cast<float *>(v+0x5f0)=.22f;}
Auto fix=NativeFix;
using Impact=void (*)(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D);
unsigned impactCalls=0,deferredDamage=0;uint8_t *ignore=nullptr;bool cover=false;
int fireType=1;uint8_t *WeaponInfo(int){return reinterpret_cast<uint8_t *>(&fireType);}
Frame *meshFrames[2]{};
alignas(8) uint8_t bulletRecords[nativebullet::kCount*nativebullet::kStride]{};
uint8_t *scriptedHelis[4]{};
bool recycleBullet=false;
void *Particle(int32_t,const CVector &,const CVector &,uint8_t *,float,int32_t,int32_t,int32_t,int32_t){
 if(recycleBullet){recycleBullet=false;auto b=nativebullet::Read(bulletRecords,0);b.creator=vehicles[1];b.position={500,500,500};std::memcpy(bulletRecords,&b,sizeof b);}return nullptr;
}
void NativeImpact(uint8_t *,uint8_t *,uint8_t *,CVector *,CVector *,uint8_t *,CVector2D){++impactCalls;}
void NativeUpdate(){for(size_t i=0;i<nativebullet::kCount;++i)if(nativebullet::Read(bulletRecords,i).active){++deferredDamage;bulletRecords[i*nativebullet::kStride+0x14]=0;}}
Impact impact=NativeImpact;void (*bulletUpdate)()=NativeUpdate;
bool Sight(const CVector &a,const CVector &b,uint8_t *p,uint8_t *&entity,bool,bool,bool,bool,bool,bool,bool,bool){
 if(!cover || a.x<15 || b.x>15)return false;*reinterpret_cast<CVector *>(p)={15,a.y,a.z};entity=vehicles[1];return true;
}
CVector &Row(uint8_t *v,int o){return *reinterpret_cast<CVector *>(v+o);}float &Scalar(uint8_t *v,int o){return *reinterpret_cast<float *>(v+o);}
void Check(bool ok,const char *message){if(!ok){printf("FAIL: %s\n",message);++fails;}}
uint8_t *Player(){return player;}uint8_t *Pad(int){return pad;}
uint8_t *GetVehicle(int ref){int i=ref>>8;return i>=0&&i<2&&uint8_t(ref)==flags[i]?vehicles[i]:nullptr;}
int Ref(uint8_t *v){return v==vehicles[0]?flags[0]:256|flags[1];}
void Move(uint8_t *v,float x,float y,float z){const float m=Scalar(v,0xcc);Row(v,0x84).x+=x/m;Row(v,0x84).y+=y/m;Row(v,0x84).z+=z/m;}
void Turn(uint8_t *v,float x,float y,float z,float lx,float ly,float lz){++turnCalls;auto &w=Row(v,0x90);float t=Scalar(v,0xd0);w.x+=(ly*z-lz*y)/t;w.y+=(lz*x-lx*z)/t;w.z+=(lx*y-ly*x)/t;}
void NativeFly(uint8_t *v,int){++originalCalls;float f=.95f+.4f*(throttle-brake)/255.0f;if(Row(v,0x38).z>80)f*=10/(Row(v,0x38).z-70);Move(v,0,0,.008f*Scalar(v,0xcc)*step*f);}
bool NativeBlade(uint8_t *v,CVector at,uint8_t *,int16_t,float,float){nativeBladeOrigin=at;if(strike&&Scalar(v,0x5f0)>.15f)Scalar(v,0x5f0)=.15f;return false;}
void NativeAuto(uint8_t *v){Row(v,0x84).z-=.008f*step;if(Scalar(v,0x5f0)<.22f)Scalar(v,0x5f0)+=.001f;fly(v,6);blade(v,{},nullptr,3,6.15f,1);Row(v,0x38).z+=Row(v,0x84).z*step;}
int16_t Throttle(uint8_t *){return throttle;}int16_t Brake(uint8_t *){return brake;}int16_t Pitch(uint8_t *){return pitch;}int16_t Roll(uint8_t *){return roll;}
int16_t GunPitch(uint8_t *){return gunPitch;}int16_t GunYaw(uint8_t *){return gunYaw;}bool Left(uint8_t *){return left;}bool Right(uint8_t *){return right;}bool Horn(uint8_t *){return horn;}
void Reset(float height=2500){
 std::memset(vehicles,0,sizeof vehicles);std::memset(player,0,sizeof player);std::memset(pad,0,sizeof pad);step=1;throttle=brake=pitch=roll=gunPitch=gunYaw=0;left=right=horn=strike=false;
 for(auto &v:vehicles){Row(v,8)={1,0,0};Row(v,0x18)={0,1,0};Row(v,0x28)={0,0,1};Row(v,0x38)={0,0,height};Scalar(v,0xcc)=5000;Scalar(v,0xd0)=10000;Scalar(v,0x2a0)=1000;Scalar(v,0x5f0)=.22f;*reinterpret_cast<int16_t *>(v+0x6a)=217;*reinterpret_cast<int *>(v+0x60)=2;v[0x294]=1;v[0x295]=0x10;*reinterpret_cast<uint8_t **>(v+0x218)=player;}
 for(auto &a:atomics)a.flags=4;*reinterpret_cast<void **>(vehicles[0]+0x58)=atomics;
 for(auto &v:vehicles)v[0x156]=2;std::fill(scriptedHelis,scriptedHelis+4,nullptr);gAcceptPolice=true;
 currentAtomics=atomics;
 for(Frame *f:{&mainFrame,&solidMainFrame,&bodyFrame,&tailFrame}){f->matrix.fill(0);f->matrix[0]=f->matrix[5]=f->matrix[10]=f->matrix[15]=1;f->parent=&rootFrame;}
 meshFrames[0]=meshFrames[1]=nullptr;cover=false;impactCalls=deferredDamage=0;std::memset(bulletRecords,0,sizeof bulletRecords);
 fireType=1;
 activeMission=cutscene=false;
 recycleBullet=false;
 player[0x4c0]=1;*reinterpret_cast<uint8_t **>(player+0x4b8)=vehicles[0];++flags[0];++flags[1];originalCalls=turnCalls=0;
}
void Run(float seconds){for(int i=0;i<int(seconds*50/step);++i){Row(vehicles[0],0x84).z-=.008f*step;fly(vehicles[0],6);auto &p=Row(vehicles[0],0x38);const auto v=Row(vehicles[0],0x84);p.x+=v.x*step;p.y+=v.y*step;p.z+=v.z*step;}}
}
namespace shotcollision { bool TraceVehicleMesh(uint8_t *v,Vec3 a,Vec3 b,MeshHit &hit){
 int i=v==vehicles[0]?0:1;hit={};if(!meshFrames[i]||std::fabs(a.x-b.x)<1e-5f)return false;
 const float x=Row(v,0x38).x,t=(x-a.x)/(b.x-a.x);if(t<0||t>1)return false;
 hit.fraction=t;hit.position={x,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};hit.normal={1,0,0};hit.frame=meshFrames[i];return true;
} }
namespace hooks { void *Chain(const char *n,void *fn){
 if(!std::strcmp(n,"_ZN8CVehicle13FlyingControlE12eFlightModel")){auto old=fly;fly=reinterpret_cast<Fly>(fn);return reinterpret_cast<void *>(old);}
 if(!std::strcmp(n,"_ZN11CAutomobile14ProcessControlEv")){auto old=automobile;automobile=reinterpret_cast<Auto>(fn);return reinterpret_cast<void *>(old);}
 if(!std::strcmp(n,"_ZN8CVehicle16DoBladeCollisionE7CVectorR7CMatrixsff")){auto old=blade;blade=reinterpret_cast<Blade>(fn);return reinterpret_cast<void *>(old);}
 if(!std::strcmp(n,"_ZN11CAutomobile3FixEv")){auto old=fix;fix=reinterpret_cast<Auto>(fn);return reinterpret_cast<void *>(old);}
 if(!std::strcmp(n,"_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D")){auto old=impact;impact=reinterpret_cast<Impact>(fn);return reinterpret_cast<void *>(old);}
 if(!std::strcmp(n,"_ZN11CBulletInfo6UpdateEv")){auto old=bulletUpdate;bulletUpdate=reinterpret_cast<decltype(bulletUpdate)>(fn);return reinterpret_cast<void *>(old);}
 return nullptr;
}}
static void *FlightSymbol(void *,const char *n){
#define SYM(s,f) if(!std::strcmp(n,s))return reinterpret_cast<void *>(f)
 SYM("_ZN9CPhysical14ApplyMoveForceEfff",Move);SYM("_ZN9CPhysical14ApplyTurnForceEffffff",Turn);
 SYM("_ZN4CPad13GetAccelerateEv",Throttle);SYM("_ZN4CPad8GetBrakeEv",Brake);SYM("_ZN4CPad17GetSteeringUpDownEv",Pitch);SYM("_ZN4CPad20GetSteeringLeftRightEv",Roll);
 SYM("_ZN4CPad15GetCarGunUpDownEv",GunPitch);SYM("_ZN4CPad18GetCarGunLeftRightEv",GunYaw);SYM("_ZN4CPad11GetLookLeftEv",Left);SYM("_ZN4CPad12GetLookRightEv",Right);SYM("_ZN4CPad7GetHornEv",Horn);
 SYM("_ZN6CPools13GetVehicleRefEP8CVehicle",Ref);if(!std::strcmp(n,"_ZN6CPools15ms_pVehiclePoolE"))return &poolPtr;
 SYM("_ZN11CTheScripts18IsPlayerOnAMissionEv",OnMission);if(!std::strcmp(n,"_ZN12CCutsceneMgr10ms_runningE"))return &cutscene;
 if(!std::strcmp(n,"gaBulletInfo"))return bulletRecords;
 if(!std::strcmp(n,"_ZN5CHeli6pHelisE"))return scriptedHelis;
 SYM("_ZN9CParticle11AddParticleE13tParticleTypeRK7CVectorS3_P7CEntityfiiii",Particle);
 SYM("_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_",Each);SYM("_Z16RpAtomicGetFramePKN2rw6AtomicE",FrameOf);SYM("_Z16GetFrameNodeNamePN2rw5FrameE",Name);
 SYM("_Z16RwFrameGetParentPKN2rw5FrameE",Parent);SYM("_Z16RpAtomicGetFlagsPKN2rw6AtomicE",Flags);SYM("_Z16RpAtomicSetFlagsPN2rw6AtomicEj",SetFlags);
 SYM("_Z16RwFrameGetMatrixPN2rw5FrameE",FrameMatrix);SYM("_Z15RpClumpGetFramePKN2rw5ClumpE",RootFrame);
 return nullptr;
#undef SYM
}
int main(){
 game::api.FindPlayerPed=Player;game::api.GetPad=Pad;game::api.PoolsGetVehicle=GetVehicle;game::api.timeStep=&step;
 game::api.ProcessLineOfSight=Sight;game::api.ignoreEntity=&ignore;
 game::api.GetWeaponInfo=WeaponInfo;
 *reinterpret_cast<uint8_t **>(pool)=vehicles[0];*reinterpret_cast<uint8_t **>(pool+8)=flags;*reinterpret_cast<int *>(pool+0x10)=2;
 gConfig.heliDamage=true;Check(flightmodel::Install(),"real native flight/control/rotor hook chain installs");
 for(float dt:{.25f,1.0f,2.0f}){
  Reset();step=dt;Run(10);Check(std::fabs(Row(vehicles[0],0x38).z-2500)<2,"level helicopter hovers above cloud layer independently of frame step");
  Reset();step=dt;throttle=255;Run(30);Check(Row(vehicles[0],0x38).z>2750,"collective climbs above actual2600m cloud top");
  throttle=0;Run(10);Check(std::fabs(Row(vehicles[0],0x84).z*50)<1,"neutral collective recovers stable hover after climb");
 }
 Reset();roll=128;pitch=-128;right=true;fly(vehicles[0],6);Check(turnCalls>0&&Row(vehicles[0],0x90).x<0&&Row(vehicles[0],0x90).y>0&&Row(vehicles[0],0x90).z<0,"native cyclic/pedal impulses have correct pitch/roll/yaw signs at altitude");
 Reset();gConfig.planes=true;Check(!planeflight::Install(),"absent roster binding preserves shared Skimmer-only test environment");
 *reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=190;*reinterpret_cast<int32_t *>(vehicles[0]+0x348)=1;Row(vehicles[0],0x84)={0,1.2f,0};throttle=255;
 fly(vehicles[0],3);Check(originalCalls==0&&Row(vehicles[0],0x84).z>.008f&&turnCalls>0,"actual shared FlyingControl wrapper executes real above-cloud Skimmer force adapter before native fallback");
 vehicles[0][0x294]=2;activeMission=true;fly(vehicles[0],3);Check(originalCalls==1,"actual shared seam returns active mission Skimmer to original native controller");
 Reset();vehicles[0][0x294]=2;activeMission=true;fly(vehicles[0],6);Check(originalCalls==1,"active mission-created aircraft retain native flight owner");
 Reset();fly(vehicles[0],2);Check(originalCalls==1,"RC flight model retains native owner");
 Reset();vehicles[0][0x156]=0;fly(vehicles[0],6);Check(originalCalls==1,"native gravity-disabled aircraft retains its native force owner");
 Reset();*reinterpret_cast<int *>(vehicles[0]+0x348)=4;fly(vehicles[0],6);Check(originalCalls==1,"non-automobile class cannot enter helicopter force layout by model number alone");
 Reset();mainFrame.matrix[14]=2.25f;blade(vehicles[0],{0,0,1.9f},nullptr,3,6.15f,1);Check(std::fabs(nativeBladeOrigin.z-2.25f)<1e-6f,"native blade collision uses actual current drawn rotor frame rather than old bonnet offset");
 Reset();strike=true;Check(!blade(vehicles[0],{},nullptr,3,6.15f,1),"native false rotor return preserved after a genuine collision");
 Check(flightmodel::RotorFailed(vehicles[0]),"native real strike witness destroys rotor despite alwaysfalse return");
 strike=false;automobile(vehicles[0]);automobile(vehicles[0]);Check(Scalar(vehicles[0],0x5f0)<=0&&Row(vehicles[0],0x84).z<0,"native respool/controller cannot restore failed rotor lift");
 Check(!(atomics[0].flags&4)&&!(atomics[1].flags&4)&&(atomics[2].flags&4)&&(atomics[3].flags&4),"both solid/blurred failed main rotor disappear while body and tail remain visible");
 Atomic reloaded[4];std::copy(atomics,atomics+4,reloaded);for(auto &a:reloaded)a.flags=4;currentAtomics=reloaded;automobile(vehicles[0]);
 fix(vehicles[0]);Check(!flightmodel::RotorFailed(vehicles[0])&&(reloaded[0].flags&4)&&(reloaded[1].flags&4),"native repair restores owned rotor render flags and lift state");
 Check(!(atomics[0].flags&4)&&(reloaded[0].flags&4)&&(reloaded[1].flags&4),"repair visits only current atomics after same-clump geometry reload");currentAtomics=atomics;
 Check(!flightmodel::RotorFailed(vehicles[1]),"one helicopter rotor failure cannot transfer to another aircraft");
 ++flags[0];Check(!flightmodel::RotorFailed(vehicles[0]),"recycled pool generation has intact rotor");
 Reset();gConfig.heliDamage=false;strike=true;blade(vehicles[0],{},nullptr,3,6.15f,1);Check(!flightmodel::RotorFailed(vehicles[0]),"disabled helicopter damage retains native strike behavior");
 gConfig.heliDamage=true;
 CVector from{20,0,2502},to{-20,0,2502};alignas(16) uint8_t point[0x30]{};uint32_t weapon=26;
 Reset();meshFrames[0]=&mainFrame;
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(vehicles[0])&&impactCalls==0&&Scalar(vehicles[0],0x2a0)==1000,"three accepted actual main-rotor contacts destroy rotor without hull damage");
 meshFrames[0]=&tailFrame;for(int i=0;i<6;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!(atomics[3].flags&4),"tail rotor remains separately destructible after main-rotor failure");
 Reset();meshFrames[0]=&tailFrame;for(int i=0;i<5;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0]),"existing six-contact tail-rotor threshold is retained");impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(vehicles[0]),"sixth confirmed actual tail contact causes rotor failure");
 automobile(vehicles[0]);Check(Row(vehicles[0],0x90).z>0,"lost tail rotor produces uncontrolled yaw during native gravity descent");
 Reset();meshFrames[0]=&bodyFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&impactCalls==3,"actual nearer body mesh retains ordinary impact ownership");
 Reset();meshFrames[0]=&mainFrame;cover=true;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&impactCalls==3,"nearer native world cover cannot be shot through to reach rotor");
 Reset();meshFrames[0]=&mainFrame;fireType=2;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&impactCalls==3,"projectile fire type in a firearm slot retains native impact ownership");
 Reset();meshFrames[0]=meshFrames[1]=&mainFrame;Row(vehicles[1],0x38).x=10;
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&flightmodel::RotorFailed(vehicles[1]),"two collinear aircraft resolve only nearest actual rotor");
 Reset();meshFrames[0]=&mainFrame;
 for(int i=0;i<3;++i){nativebullet::Record record{};record.type=28;record.creator=player;record.active=1;record.position={from.x,from.y,from.z};record.velocity={-80,0,0};std::memcpy(bulletRecords,&record,sizeof record);bulletUpdate();}
 Check(flightmodel::RotorFailed(vehicles[0])&&deferredDamage==0,"deferred sniper rotor contact preconsumes before farther native inline damage");
 *reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=155;Check(!flightmodel::RotorFailed(vehicles[0]),"same pool slot model replacement cannot inherit another airframe rotor failure");
 Reset();meshFrames[0]=&mainFrame;for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 flightmodel::Frame(10000);for(auto &a:atomics)a.flags=4; // native loader has built the new world's intact geometry
 flightmodel::Frame(1);Check(!flightmodel::RotorFailed(vehicles[0])&&(atomics[0].flags&4),"loading older timeline discards failure without writing old render state onto new world geometry");
 Reset();*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=165;*reinterpret_cast<int *>(vehicles[0]+0x348)=3;scriptedHelis[0]=vehicles[0];gAcceptPolice=false;meshFrames[0]=&mainFrame;
 for(int i=0;i<3;++i)impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(!flightmodel::RotorFailed(vehicles[0])&&impactCalls==1,"declined native scripted-controller failure retains original impact ownership");
 gAcceptPolice=true;impact(reinterpret_cast<uint8_t *>(&weapon),player,nullptr,&from,&to,point,{});
 Check(flightmodel::RotorFailed(vehicles[0]),"declined scripted-controller callback cannot leave stale broken bit preventing recovery");
 Reset();meshFrames[0]=&mainFrame;
 nativebullet::Record deferred{};deferred.type=29;deferred.creator=player;deferred.active=1;deferred.position={20,0,2502};deferred.velocity={-80,0,0};std::memcpy(bulletRecords,&deferred,sizeof deferred);recycleBullet=true;bulletUpdate();
 const auto recycled=nativebullet::Read(bulletRecords,0);Check(deferredDamage==1&&recycled.creator==vehicles[1]&&recycled.position.x==500,"impact effects recycling a bullet row preserve new native bullet ownership");
 Check(!flightmodel::RotorFailed(reinterpret_cast<uint8_t *>(uintptr_t(1))),"invalid non-pool pointer is rejected before entity field access");
 if(!fails)puts("flightmodelhook: all passed");return fails?1:0;
}
