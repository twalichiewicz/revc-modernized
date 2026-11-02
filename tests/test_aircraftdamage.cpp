// Real outer ProcessControl hook -> native-shaped prior-contact damage reader.
#include "../src/game.h"
#include "../src/config.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
[[maybe_unused]] static void *DamageSymbol(void *,const char *);
#ifndef AIRCRAFTDAMAGE_BASELINE
#define dlsym DamageSymbol
#include "../src/aircraftdamage.cpp"
#undef dlsym
#else
namespace aircraftdamage {bool Install(){return true;}}
#endif
namespace game {Api api{};}
Config gConfig;
void Log(const char *,...){}
namespace {
using Control=void (*)(uint8_t *);
using DamageControl=void (*)(uint8_t *,float,uint16_t);
alignas(16) uint8_t vehicles[2][0x6f0]{},player[0x900]{},pool[0x20]{},handling[2][0xdc]{},replacement[0xdc]{};
uint8_t *poolPointer=pool,flags[2]{1,1};uint32_t clockMs=1000;
bool mission=false,cutscene=false,nested=false,replaceHandling=false,recycle=false,throwNative=false;
bool expectInner=false,rejectBoatOnce=false;int nestedScopes=0;
int fails=0,calls=0;std::array<uint8_t,0xdc> original[2];
Control automobile,boat;DamageControl vehicleDamage;
const uint8_t *retainedTransmission[2]{};
float &F(uint8_t *p,int at){return *reinterpret_cast<float *>(p+at);}
uint8_t *Player(){return player;}
bool OnMission(){return mission;}
uint8_t *GetVehicle(int ref){const int i=ref>>8;return i>=0&&i<2&&!(flags[i]&0x80)&&uint8_t(ref)==flags[i]?vehicles[i]:nullptr;}
uint8_t *Handling(uint8_t *v){return *reinterpret_cast<uint8_t **>(v+0x160);}
void Check(bool ok,const char *what){if(!ok){++fails;printf("FAIL: %s\n",what);}}
void NativeDamage(uint8_t *v,float,uint16_t)
{
 const int i=v==vehicles[0]?0:1;const uint8_t *h=Handling(v);
 for(int j=0;j<0xdc;++j)if(j<0xc8||j>=0xcc)Check(h[j]==original[i][j],"all native handling configuration except collision multiplier is exact");
 // CBoat267768: scaled prior contact >25, health>=250; player health
 // loses .5*(scaledContact-25). Automobile24f3c4: (contact-25)*c8*.6,
 // then player .5 factor. Both native consumers clamp lethal contact to1.
 const float contact=F(v,0x13c),scale=F(Handling(v),0xc8);float damage=0;
 if(*reinterpret_cast<int32_t *>(v+0x348)==1){if(contact*scale>25&&F(v,0x2a0)>=250)damage=(contact*scale-25)*.5f;}
 else if(contact>25)damage=(contact-25)*scale*.6f*.5f;
 if((v[0x297]&0x40)&&!(v[0x63]&8)&&!game::Wrecked(v)&&damage>0)F(v,0x2a0)=std::max(1.0f,F(v,0x2a0)-damage);
 if(expectInner){Check(scale>0&&Handling(v)!=handling[i],"inner admitted boat executes a genuine nonzero handling-copy scope");++nestedScopes;}
 if(nested&&i==0){nested=false;expectInner=true;boat(vehicles[1]);expectInner=false;Check(Handling(v)==h,"nested different-aircraft scope retains outer native handling lifetime");}
 if(replaceHandling){replaceHandling=false;*reinterpret_cast<uint8_t **>(v+0x160)=replacement;}
 if(recycle){recycle=false;++flags[i];*reinterpret_cast<uint8_t **>(v+0x160)=replacement;F(v,0x2a0)=777;}
 if(throwNative){throwNative=false;throw std::runtime_error("native callback fixture");}
}
void Native(uint8_t *v)
{
 ++calls;const int i=v==vehicles[0]?0:1;const uint8_t *h=Handling(v);
 if(*reinterpret_cast<int32_t *>(v+0x348)==0){
  Check(h==handling[i],"whole Automobile native controller retains original handling/transmission identity");
  vehicleDamage(v,0,0);
  // Actual native CalculateDriveAcceleration279aa0 writes transmission+5c;
  // 279b08 retains a gear pointer inside this same handling allocation.
  F(Handling(v),0x90)=3.25f;std::memcpy(original[i].data()+0x90,Handling(v)+0x90,4);retainedTransmission[i]=Handling(v)+0x34;
 }else NativeDamage(v,0,0);
 F(v,0x13c)=0; // CPhysical155dc0 consumes the previous collision witness
}
void Reset(int model=217,int kind=0)
{
 std::memset(vehicles,0,sizeof vehicles);std::memset(player,0,sizeof player);++flags[0];++flags[1];mission=cutscene=false;
 calls=nestedScopes=0;expectInner=false;nested=replaceHandling=recycle=throwNative=false;clockMs+=10;retainedTransmission[0]=retainedTransmission[1]=nullptr;
 for(int i=0;i<2;++i){
  auto *v=vehicles[i];*reinterpret_cast<int32_t *>(v+0x60)=2;*reinterpret_cast<int16_t *>(v+0x6a)=i?190:model;*reinterpret_cast<int32_t *>(v+0x348)=i?1:kind;
  *reinterpret_cast<void **>(v)=reinterpret_cast<void *>(uintptr_t(0x1000+i));v[0x294]=3;v[0x297]=0x40;
  *reinterpret_cast<uint8_t **>(v+0x160)=handling[i];*reinterpret_cast<uint8_t **>(v+0x218)=player;F(v,0xcc)=5000;F(v,0x2a0)=1000;F(v,0x13c)=5000*40/50;
  for(int j=0;j<0xdc;++j)handling[i][j]=uint8_t(j*7+i);F(handling[i],4)=5000;F(handling[i],0xc8)=0;std::memcpy(original[i].data(),handling[i],0xdc);
 }
 player[0x4c0]=1;*reinterpret_cast<uint8_t **>(player+0x4b8)=vehicles[0];
}
float Run(int model,int kind){Reset(model,kind);(kind==1?boat:automobile)(vehicles[0]);return F(vehicles[0],0x2a0);}
void Assets()
{
 const std::string root="/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData/data/";
 std::ifstream f(root+"handling.cfg");Check(bool(f),"actual native handling.cfg available");
 std::string line;int zero=0;
 while(std::getline(f,line)){
  std::istringstream in(line);std::vector<std::string> fields;std::string s;while(in>>s)fields.push_back(s);
  if(fields.empty())continue;const auto &name=fields[0];
  if(name=="SEAPLANE"||name=="MAVERICK"||name=="COASTMAV"||name=="POLMAV"||name=="SPARROW"||name=="SEASPAR"){
   Check(fields.size()>24&&std::stof(fields[24])==0,"actual aircraft asset has zero collision multiplier at native token24");++zero;
  }
  if(name=="HUNTER")Check(fields.size()>24&&std::stof(fields[24])==.5f,"actual Hunter provides raw.50 baseline");
 }
 Check(zero==6,"all six zero-multiplier native aircraft assets checked");
 const std::string exe="/Users/waliwalu/Library/Application Support/macVC/vcmodernhud/macVC.vanilla";
 std::ifstream native(exe,std::ios::binary);Check(bool(native),"exact read-only native image available");
 auto word=[&](uint64_t va){uint32_t value=0;native.seekg(va-0x100000000);native.read(reinterpret_cast<char *>(&value),4);return value;};
 Check(word(0x10027283c)==0xbd00c900&&word(0x100272c98)==0xbd40c826&&word(0x100272ca0)==0xbd00c825,"native loader stores+c8 and converts collision multiplier in place");
 Check(word(0x10026776c)==0xbd40c900&&word(0x100267770)==0xbd413e61&&word(0x10024f3cc)==0xbd40c901,"actual Boat/Automobile consumers read native handling+c8 and prior collision+13c");
 Check(word(0x100155dc0)==0xb9013e7f&&word(0x100276190)==0x52801b89,"native physical consumes contact and handling type stride is0xdc");
}
}
namespace hooks {void *Chain(const char *name,void *replacementFn){
 if(std::strstr(name,"CAutomobile14ProcessControl")){auto old=automobile;if(reinterpret_cast<void *>(old)==replacementFn)return nullptr;automobile=reinterpret_cast<Control>(replacementFn);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"CBoat14ProcessControl")){if(rejectBoatOnce){rejectBoatOnce=false;return nullptr;}auto old=boat;if(reinterpret_cast<void *>(old)==replacementFn)return nullptr;boat=reinterpret_cast<Control>(replacementFn);return reinterpret_cast<void *>(old);}
 if(std::strstr(name,"CAutomobile13VehicleDamage")){auto old=vehicleDamage;if(reinterpret_cast<void *>(old)==replacementFn)return nullptr;vehicleDamage=reinterpret_cast<DamageControl>(replacementFn);return reinterpret_cast<void *>(old);}
 return nullptr;
}}
static void *DamageSymbol(void *,const char *name){
 if(std::strstr(name,"IsPlayerOnAMission"))return reinterpret_cast<void *>(OnMission);
 if(std::strstr(name,"CutsceneMgr10ms_running"))return &cutscene;
 if(std::strstr(name,"ms_pVehiclePool"))return &poolPointer;return nullptr;
}
int main()
{
 automobile=boat=Native;vehicleDamage=NativeDamage;game::api.FindPlayerPed=Player;game::api.PoolsGetVehicle=GetVehicle;game::api.timeMs=&clockMs;
 *reinterpret_cast<uint8_t **>(pool)=vehicles[0];*reinterpret_cast<uint8_t **>(pool+8)=flags;*reinterpret_cast<int *>(pool+0x10)=2;
#ifndef AIRCRAFTDAMAGE_BASELINE
 rejectBoatOnce=true;Check(!aircraftdamage::Install(),"later hook installation failure is reported");
#endif
 const bool installed=aircraftdamage::Install();Check(installed,"retry retains earlier installed native predecessors");if(!installed)return 1;
 for(int model:{155,177,199,217,218,227})Check(Run(model,0)<999,"accepted full-heli contact reaches native body damage despite original zero multiplier");
 Check(std::fabs(Run(217,0)-761.5f)<.01f,"Hunter-derived converted baseline retains exact native full-heli collision curve");
 Check(std::fabs(Run(190,1)-612.5f)<.01f,"Hunter-derived converted baseline retains exact native Skimmer collision curve");
 Check(Handling(vehicles[0])==handling[0]&&!std::memcmp(handling[0],original[0].data(),0xdc),"native handling pointer restored and shared table immutable");
 Reset();automobile(vehicles[0]);Check(retainedTransmission[0]==handling[0]+0x34&&F(handling[0],0x90)==3.25f,"actual native transmission scratch write/pointer survives return in original allocation");
 Reset();F(handling[0],0xc8)=.1f;std::memcpy(original[0].data(),handling[0],0xdc);automobile(vehicles[0]);Check(F(handling[0],0xc8)==.1f&&F(vehicles[0],0x2a0)>850,"existing nonzero Hunter/custom damage remains native");
 Reset();F(handling[0],4)=3000;std::memcpy(original[0].data(),handling[0],0xdc);F(vehicles[0],0xcc)=3000;F(vehicles[0],0x13c)=3000*40/50;automobile(vehicles[0]);Check(std::fabs(F(vehicles[0],0x2a0)-762.5f)<.01f,"native coefficient conversion uses original handling mass rather than a guessed vehicle class constant");
 Reset();vehicles[0][0x63]|=8;automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000,"native collision-proof flag still owns accepted contact policy");
 for(int gate=0;gate<8;++gate){Reset();if(gate==0)mission=true;if(gate==1)cutscene=true;if(gate==2){player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=vehicles[1];vehicles[0][0x60]=2|(3<<3);}if(gate==3)gConfig.heliDamage=false;if(gate==4)*reinterpret_cast<int32_t *>(vehicles[0]+0x348)=4;if(gate==5){F(handling[0],4)=NAN;std::memcpy(original[0].data(),handling[0],0xdc);}if(gate==6)*reinterpret_cast<int16_t *>(vehicles[0]+0x6a)=130;if(gate==7)vehicles[0][0x60]=2|(5<<3);automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000&&Handling(vehicles[0])==handling[0],"mission/cutscene/foreign/config/class/invalidmass/othermodel/wreck retains native owner");gConfig.heliDamage=true;}
 Reset(190,1);gConfig.planes=false;boat(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000,"disabled planes preserves Skimmer stock damage");gConfig.planes=true;
 Reset();automobile(vehicles[0]);player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);F(vehicles[0],0x13c)=4000;const float before=F(vehicles[0],0x2a0);automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)<before,"same admitted abandoned idle airframe retains fragile impact after player exit");
 Reset();player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000,"unknown parked airframe never obtains player admission");
 Reset();automobile(vehicles[0]);mission=true;automobile(vehicles[0]);mission=false;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);F(vehicles[0],0x13c)=4000;F(vehicles[0],0x2a0)=1000;automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000,"mission takeover relinquishes former abandoned admission");
 Reset();automobile(vehicles[0]);clockMs=1;player[0x4c0]=0;*reinterpret_cast<uint8_t **>(vehicles[0]+0x218)=nullptr;vehicles[0][0x60]=2|(4<<3);F(vehicles[0],0x13c)=4000;F(vehicles[0],0x2a0)=1000;automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==1000,"rewind before next control discards all prior ownership leases");
 Reset();replaceHandling=true;automobile(vehicles[0]);Check(Handling(vehicles[0])==replacement,"native replacement handling pointer cannot be overwritten by scope cleanup");
 Reset();recycle=true;automobile(vehicles[0]);Check(Handling(vehicles[0])==replacement&&F(vehicles[0],0x2a0)==777,"recycled generation is never touched during scope cleanup");
 Reset();throwNative=true;try{automobile(vehicles[0]);}catch(const std::runtime_error &){}Check(Handling(vehicles[0])==handling[0],"native callback exception restores scoped handling lifetime");
 F(vehicles[0],0x13c)=4000;F(vehicles[0],0x2a0)=1000;vehicleDamage(vehicles[0],0,0);Check(F(vehicles[0],0x2a0)==1000,"exception also releases outer owner context before later unrelated VehicleDamage");
 Reset();*reinterpret_cast<uint8_t **>(player+0x4b8)=vehicles[1];boat(vehicles[1]);*reinterpret_cast<uint8_t **>(player+0x4b8)=vehicles[0];*reinterpret_cast<uint8_t **>(vehicles[1]+0x218)=nullptr;vehicles[1][0x60]=2|(4<<3);F(vehicles[1],0x13c)=4000;nested=true;automobile(vehicles[0]);Check(nestedScopes==1&&Handling(vehicles[0])==handling[0]&&Handling(vehicles[1])==handling[1]&&F(vehicles[1],0x2a0)<500,"nested admitted abandoned boat and outer helicopter both consume nonzero scoped damage and restore own pointers");
 Reset();automobile(vehicles[0]);const float once=F(vehicles[0],0x2a0);automobile(vehicles[0]);Check(F(vehicles[0],0x2a0)==once,"native consumed collision witness cannot damage again next frame");
 Assets();if(!fails)puts("aircraft damage native: all passed");return fails?1:0;
}
