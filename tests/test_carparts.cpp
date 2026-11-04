#include "game.h"
#include "config.h"
#include "hooks.h"
#include "partsmodel.h"
#include "nativepartdamagearm.h"
#include <array>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <memory>
static void *Lookup(void *,const char *);
#define dlsym Lookup
#include "../src/carparts.cpp"
#undef dlsym
namespace game {Api api{};}
Config gConfig;
static std::vector<std::string> logs;
void Log(const char *format,...){char s[256];va_list ap;va_start(ap,format);vsnprintf(s,sizeof s,format,ap);va_end(ap);logs.emplace_back(s);}
namespace fixture {
using Control=void (*)(uint8_t *);using Damage=void (*)(uint8_t *,float,uint16_t);using Apply=nativeparttest::Apply;
Control control;Damage damage;Apply apply,actual;
void (*fix)(uint8_t *);
alignas(16) uint8_t vehicles[2][0x6f0]{},pool[24]{},handling[2][0xdc]{};
uint8_t poolFlags[2]={1,1};void *poolPointer=pool;uint32_t now=1000;
int failed=0,nativeControls=0,nativeDamages=0,nativeFixes=0,spawns=0,bodyCallbacks=0;
bool protection=false,nested=false,recycle=false,replaceClump=false,scripted=false,throwNative=false,nestedScript=false,afterScript=false,repairThenScript=false;
struct Hit {int component;float force;};std::vector<Hit> hits;
struct Call {uint8_t *manager;int component;float force,multiplier;};std::vector<Call> calls;
void Check(bool yes,const char *why){if(!yes){printf("FAIL: %s\n",why);++failed;}}
float &F(uint8_t *v,size_t offset){return *reinterpret_cast<float *>(v+offset);}
uint8_t *Car(int ref){const int i=ref>>8;return i>=0&&i<2&&!(poolFlags[i]&0x80)&&ref==((i<<8)|poolFlags[i])?vehicles[i]:nullptr;}
int Door(void *m,int part){return static_cast<uint8_t *>(m)[9+part];}
int Panel(void *m,int part){return (*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(m)+0x14)>>(4*part))&15;}
int State(uint8_t *v,int c){int slot=partsmodel::Slot(c);if(slot<0)return -1;const auto &p=partsmodel::Describe(slot);return p.panel?Panel(v+0x34c,p.index):Door(v+0x34c,p.index);}
void Set(uint8_t *v,int c,int state){int s=partsmodel::Slot(c);const auto &p=partsmodel::Describe(s);auto m=v+0x34c;if(p.panel){auto &w=*reinterpret_cast<uint32_t *>(m+0x14);w=(w&~(15u<<(4*p.index)))|(unsigned(state)<<(4*p.index));}else m[9+p.index]=uint8_t(state);}
bool ReferenceApply(uint8_t *m,int c,float force,float)
{
 const float mult[]={2.5f,1.25f,3.2f,1.4f,2.5f,2.8f,.5f};
 int group,index;if(c>=11){group=c>15?0:5;index=c-11;}else if(c>=5){group=c==5?3:c==6?4:2;index=c-5;}else if(c>=1){group=1;index=c-1;}else{group=6;index=0;}
 float scaled=force*mult[group];if(c==15)scaled*=.6f;if(!(scaled>150))return false;
 if(group==2||group==3||group==4){if(m[9+index]!=3)++m[9+index];}
 else if(group==0||group==5){auto &w=*reinterpret_cast<uint32_t *>(m+0x14);unsigned old=(w>>(4*index))&15;if(old!=3)w=(w&~(15u<<(4*index)))|((old+1)<<(4*index));}
 return true;
}
bool NativeApply(uint8_t *m,int c,float force,float multiplier)
{
 calls.push_back({m,c,force,multiplier});bool result=actual(m,c,force,multiplier);
 if(recycle){recycle=false;++poolFlags[0];memset(vehicles[0]+0x34c,0,24);}
 if(replaceClump){replaceClump=false;*reinterpret_cast<void **>(vehicles[0]+0x58)=reinterpret_cast<void *>(0x9000);}
 return result;
}
void NativeDamage(uint8_t *v,float amount,uint16_t)
{
 ++nativeDamages;
 if(throwNative){throwNative=false;throw 42;}
 // Native health remains separate from the tuned part force.
 F(v,0x2a0)-=10;
 if(!(v[0x297]&0x40)||(v[0x63]&8))return;
 if(nestedScript&&amount==0){nestedScript=false;damage(v,200,0);return;}
 for(auto h:hits){
  if(apply(v+0x34c,h.component,h.force,.731f)){
   ++bodyCallbacks;int state=State(v,h.component);
   if(protection&&h.component>=7&&h.component<=10&&state>=2){Set(v,h.component,1);state=1;}
   if(state==3)++spawns;
   if(afterScript&&amount==0){afterScript=false;protection=false;damage(v,200,0);}
   if(repairThenScript&&amount==0){repairThenScript=false;fix(v);damage(v,200,0);}
  }
 }
 (void)amount;
}
void NativeControl(uint8_t *v)
{
 ++nativeControls;int i=v==vehicles[0]?0:1;
 Check(*reinterpret_cast<void **>(v+0x160)==handling[i],"controller retains real handling/transmission allocation");
 if(nested&&i==0){nested=false;control(vehicles[1]);}
 damage(v,scripted?200.0f:0.0f,0);
 Check(*reinterpret_cast<void **>(v+0x160)==handling[i],"damage callbacks retain real handling allocation");
}
void NativeFix(uint8_t *v){++nativeFixes;memset(v+0x34c,0,24);}
void Reset()
{
 for(auto &v:vehicles)fix(v);
 memset(vehicles,0,sizeof vehicles);calls.clear();hits={{7,60}};logs.clear();spawns=bodyCallbacks=0;
 protection=nested=recycle=replaceClump=scripted=throwNative=nestedScript=afterScript=repairThenScript=false;now+=1000;
 for(int i=0;i<2;i++){
  poolFlags[i]=uint8_t((poolFlags[i]+1)&0x7f);auto v=vehicles[i];*reinterpret_cast<unsigned *>(v+0x60)=2;v[0x297]=0x40;
  *reinterpret_cast<int16_t *>(v+0x6a)=211;*reinterpret_cast<void **>(v+0x58)=reinterpret_cast<void *>(uintptr_t(0x1000+i*0x100));
  *reinterpret_cast<void **>(v+0x160)=handling[i];F(v,0x2a0)=1000;F(v,0x13c)=100;
 }
}
void Run(uint32_t gap=0){now+=gap;control(vehicles[0]);}
void Cases()
{
 Reset();hits={{7,60},{7,60},{7,60}};Run();Check(State(vehicles[0],7)==1&&bodyCallbacks==1,"one native collision callback burst advances only one state");
 for(int i=0;i<100;i++)Run(20);Check(State(vehicles[0],7)==1,"sustained scrape across frames keeps damaged stage");
 hits={{7,60}};Run(350);Check(State(vehicles[0],7)==2,"separate moderate hit frees hinge into native swinging stage");
 Run(350);Check(State(vehicles[0],7)==2,"previous moderate hit cannot detach swinging door");
 hits={{7,100}};Run(20);Check(State(vehicles[0],7)==3&&spawns==1,"stronger hit in unconsumed episode detaches once");Run(350);Check(spawns==1,"already missing component cannot respawn");
 Reset();hits={{7,60},{8,60}};Run();Check(State(vehicles[0],7)==1&&State(vehicles[0],8)==1,"different parts advance independently on one impact");
 Reset();Set(vehicles[0],7,1);protection=true;Run();Check(State(vehicles[0],7)==1&&logs.empty(),"protected native door downgrade is not logged or committed");
 protection=false;Run(20);Check(State(vehicles[0],7)==2,"native veto did not consume the episode");
 for(int c:{5,6,16,17}){Reset();hits={{c,c==5?130.0f:80.0f}};Run();Run(350);Check(State(vehicles[0],c)==2,"bonnet boot and bumpers retain intermediate stage");Run(350);Check(State(vehicles[0],c)==2,"moderate hit cannot detach loose bonnet boot bumper");hits={{c,c==5?200.0f:120.0f}};Run(20);Check(State(vehicles[0],c)==3,"harder hit detaches native bonnet boot bumper");}
 Reset();Set(vehicles[0],7,1);nested=true;Set(vehicles[1],7,1);Run();Check(State(vehicles[0],7)==2&&State(vehicles[1],7)==2&&calls.size()==2&&std::fabs(calls[0].force-51)<.001f&&std::fabs(calls[1].force-51)<.001f,"nested cars each receive their own scoped tuning");Check(F(vehicles[0],0x2a0)==990&&F(vehicles[1],0x2a0)==990,"native health result unchanged for nested cars");
 Reset();Run();fix(vehicles[0]);Run(20);Check(State(vehicles[0],7)==1,"repair clears component contact history immediately");
 Reset();Run();now=0;Run();Check(State(vehicles[0],7)==2,"backward clock starts fresh contact episode");
 Reset();Set(vehicles[0],7,1);recycle=true;Run();Check(logs.empty(),"recycled pool generation cannot commit or log old part state");Run(20);Check(State(vehicles[0],7)==1,"new generation owns fresh contact history");
 Reset();Run();*reinterpret_cast<void **>(vehicles[0]+0x58)=reinterpret_cast<void *>(0x2222);Run(20);Check(State(vehicles[0],7)==2,"new clump has fresh contact history");
 Reset();Set(vehicles[0],7,1);replaceClump=true;Run();Check(logs.empty(),"clump replacement during native damage is not attributed to old owner");
 for(auto type: {std::pair<int,int>{217,0},{190,1},{194,4},{191,5},{171,0},{162,0}}){Reset();Set(vehicles[0],7,1);*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=int16_t(type.first);*reinterpret_cast<int *>(vehicles[0]+0x348)=type.second;Run();Check(calls.size()==1&&calls[0].force==60,"aircraft boats bikes RC Bandit and tank retain native force");}
 Reset();Set(vehicles[0],7,1);gConfig.carParts=false;Run();Check(calls.size()==1&&calls[0].force==60,"disabled is native pass-through");gConfig.carParts=true;
 Reset();Set(vehicles[0],7,1);scripted=true;Run();Check(calls[0].force==60,"explicit/scripted damage untouched inside controller");
 Reset();nestedScript=true;Run();Check(State(vehicles[0],7)==1&&calls[0].force==60&&logs.empty(),"nested script damage is not attributed to collision tuning");
 Reset();afterScript=true;Run();Check(State(vehicles[0],7)==2&&logs.empty(),"script state after a real tuned hit is not logged as a tuned jump");hits={{7,100}};Run(20);Check(State(vehicles[0],7)==2,"nested script preserves the legitimately consumed collision episode");
 Reset();Set(vehicles[0],7,1);protection=afterScript=true;Run();Check(State(vehicles[0],7)==2&&logs.empty(),"script after a native veto is not attributed to the rejected collision");hits={{7,100}};Run(20);Check(State(vehicles[0],7)==3,"script after native veto does not consume the next physical progression");
 Reset();repairThenScript=true;Run();Check(State(vehicles[0],7)==1&&logs.empty(),"repair during damage invalidates old collision attribution before script progression");Run(20);Check(State(vehicles[0],7)==2,"new collision after nested repair owns a fresh episode");
 Reset();Set(vehicles[0],7,1);damage(vehicles[0],0,0);Check(calls[0].force==60,"unscoped zero-argument damage untouched");
 Reset();hits={{11,100},{11,100},{11,100},{15,100},{15,100},{15,100}};Run();Check(calls.size()==6&&std::all_of(calls.begin(),calls.end(),[](const Call &c){return c.force==100;}),"wings and windscreen retain exact native component progression");
 Reset();Set(vehicles[0],7,1);poolFlags[0]|=0x80;Run();Check(calls[0].force==60,"unallocated vehicle cannot obtain part ownership");
 Reset();throwNative=true;try{Run();}catch(int){}calls.clear();Set(vehicles[0],7,1);damage(vehicles[0],0,0);Check(calls[0].force==60,"exception restores control and damage contexts");
 Check(std::all_of(calls.begin(),calls.end(),[](const Call &c){return c.multiplier==.731f;}),"native collision-coefficient argument preserved");
}
}
namespace hooks {
void *Chain(const char *s,void *replacement){using namespace fixture;
#define CHAIN(n,v,T) if(!strcmp(s,n)){auto old=v;v=reinterpret_cast<T>(replacement);return reinterpret_cast<void *>(old);}
 CHAIN("_ZN11CAutomobile14ProcessControlEv",control,Control);
 CHAIN("_ZN11CAutomobile13VehicleDamageEft",damage,Damage);
 CHAIN("_ZN14CDamageManager11ApplyDamageE10tComponentff",apply,Apply);
 CHAIN("_ZN11CAutomobile3FixEv",fix,Control);
#undef CHAIN
 return nullptr;
}
}
static void *Lookup(void *,const char *s){using namespace fixture;
 if(!strcmp(s,"_ZN6CPools15ms_pVehiclePoolE"))return &poolPointer;
 if(!strcmp(s,"_ZN14CDamageManager13GetDoorStatusEi"))return reinterpret_cast<void *>(Door);
 if(!strcmp(s,"_ZN14CDamageManager14GetPanelStatusEi"))return reinterpret_cast<void *>(Panel);
 return nullptr;
}
int main(int argc,char **argv)
{
 using namespace fixture;std::unique_ptr<nativeparttest::Fixture> native;
 if(argc>1&&!strcmp(argv[1],"--native")){native=std::make_unique<nativeparttest::Fixture>();actual=native->apply;}else actual=ReferenceApply;
 control=NativeControl;damage=NativeDamage;apply=NativeApply;fix=NativeFix;
 game::api.PoolsGetVehicle=Car;game::api.timeMs=&now;
 *reinterpret_cast<void **>(pool)=vehicles;*reinterpret_cast<void **>(pool+8)=poolFlags;*reinterpret_cast<int *>(pool+16)=2;
 gConfig.carParts=false;Check(carparts::Install()&&control==NativeControl,"startup off installs no hook");gConfig.carParts=true;
 Check(carparts::Install(),"native part hooks installed");Cases();
 printf("carparts%s: %s\n",native?" native":"",failed?"FAILED":"all passed");return failed?1:0;
}
