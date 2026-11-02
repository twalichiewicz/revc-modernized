// Native registered physical entity -> actual ProcessVehicle hook -> native
// Jumbo queue consumers backed by the installed SDT/RAW bank. No game input.
#include <dlfcn.h>
static void *AudioSymbol(void *,const char *);
#define dlsym AudioSymbol
#include "../src/jetaudio.cpp"
#undef dlsym
#include "../src/config.h"
#include "../src/game.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <stdexcept>
#include <string>
#include <vector>
Config gConfig;
namespace game {Api api{};}
static int failures;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;}}while(0)
template<class T> T Read(const uint8_t *p,size_t at){T v{};std::memcpy(&v,p+at,sizeof v);return v;}
template<class T> void Write(uint8_t *p,size_t at,T v){std::memcpy(p+at,&v,sizeof v);}
struct Image {
 std::vector<uint8_t> bytes;
 uint32_t Word(uint64_t address) const {
  if(bytes.size()<sizeof(mach_header_64))return 0;
  const auto h=Read<mach_header_64>(bytes.data(),0);if(h.magic!=MH_MAGIC_64)return 0;
  size_t at=sizeof h;
  for(uint32_t i=0;i<h.ncmds&&i<4096&&at+sizeof(load_command)<=bytes.size();++i){
   const auto c=Read<load_command>(bytes.data(),at);if(c.cmdsize<8||c.cmdsize>bytes.size()-at)break;
   if(c.cmd==LC_SEGMENT_64&&c.cmdsize>=sizeof(segment_command_64)){
    const auto s=Read<segment_command_64>(bytes.data(),at);
    if(address>=s.vmaddr&&address-s.vmaddr+4<=s.filesize&&s.fileoff+address-s.vmaddr+4<=bytes.size())return Read<uint32_t>(bytes.data(),size_t(s.fileoff+address-s.vmaddr));
   }at+=c.cmdsize;
  }return 0;
 }
 uint64_t Symbol(const char *name) const {
  if(bytes.size()<sizeof(mach_header_64))return 0;
  const auto h=Read<mach_header_64>(bytes.data(),0);size_t at=sizeof h;
  if(h.magic!=MH_MAGIC_64)return 0;
  for(uint32_t i=0;i<h.ncmds&&i<4096&&at+sizeof(load_command)<=bytes.size();++i){
   const auto c=Read<load_command>(bytes.data(),at);if(c.cmdsize<8||c.cmdsize>bytes.size()-at)break;
   if(c.cmd==LC_SYMTAB&&c.cmdsize>=sizeof(symtab_command)){
    const auto s=Read<symtab_command>(bytes.data(),at);
    if(size_t(s.stroff)+s.strsize>bytes.size()||size_t(s.symoff)+size_t(s.nsyms)*sizeof(nlist_64)>bytes.size())return 0;
    for(uint32_t j=0;j<s.nsyms;++j){const auto n=Read<nlist_64>(bytes.data(),s.symoff+size_t(j)*sizeof(nlist_64));
     if(n.n_un.n_strx>=s.strsize)continue;
     const char *entry=reinterpret_cast<const char *>(bytes.data()+s.stroff+n.n_un.n_strx);
     const size_t available=s.strsize-n.n_un.n_strx;if(strnlen(entry,available)<available&&!std::strcmp(entry,name))return n.n_value;
    }
   }at+=c.cmdsize;
  }return 0;
 }
};
static size_t nativeProfiles;
static std::vector<uint8_t> File(const std::string &path){
 std::ifstream f(path,std::ios::binary|std::ios::ate);CHECK(bool(f));if(!f)return {};
 const auto n=f.tellg();CHECK(n>0&&n<512*1024*1024);if(n<=0||n>=512*1024*1024)return {};
 std::vector<uint8_t> out(size_t(n),uint8_t{});f.seekg(0);f.read(reinterpret_cast<char *>(out.data()),n);CHECK(bool(f));return out;
}
static void NativeContract(){
 const char *native=std::getenv("MACVC_NATIVE");Image image{File(native?native:"/Applications/Grand Theft Auto Vice City.app/Contents/MacOS/macVC")};
 // Actual caller is optimized: ProcessVehicle compares models180/181 and
 // directly calls ProcessJumbo; nominal ProcessPlane is not its hook seam.
 CHECK(image.Word(0x100010504)==0x710012ff);
 CHECK(image.Word(0x100010630)==0x12003ec8);
 CHECK(image.Word(0x100010634)==0x7102d51f);
 CHECK(image.Word(0x10001063c)==0x7102d11f);
 CHECK(image.Word(0x10001064c)==0x9400199a);
 CHECK(image.Word(0x10027a1fc)==0xb9007a61); // native vehicle audio ID+0x78
 CHECK(image.Word(0x10000e37c)==0xb9001401); // native queue entity index
 CHECK(image.Word(0x10000e4d0)==0xf9580d01); // physical entry pointer+0x3018
 CHECK(image.Word(0x100030fcc)==0x9100c14a&&image.Word(0x100030fd0)==0xf103e91f); //250 entries of48bytes
 CHECK(image.Word(0x100017ae4)==0x528026e1); // actual engine sample311
 CHECK(image.Word(0x1000178dc)==0x528026c1); // whine310
 CHECK(image.Word(0x100017800)==0x528026a1); // taxi309
 CHECK(image.Word(0x100017690)==0x52802681); // distant fly308
 CHECK(image.Word(0x100017b24)==0x94006c51); // native AddSampleToRequestedQueue
 CHECK(image.Word(0x100032d9c)==0x3cc14000&&image.Word(0x100032ce4)==0x52800c0b); //96-byte queue scratch+0x14
 const auto table=image.Symbol("__ZL16aVehicleSettings"),end=image.Symbol("__ZZN13cAudioManager27ProcessPlayersVehicleEngineER14cVehicleParamsP8CVehicleE11GearFreqAdj");
 CHECK(table==0x10031fecc&&end>table&&(end-table)%28==0);nativeProfiles=size_t((end-table)/28);CHECK(nativeProfiles==110);
 CHECK(image.Word(0x100010a24)==0x52800389&&image.Word(0x100010a30)==0x9ba92d08); // unchecked profile stride28
 CHECK(image.Word(0x1000103dc)==0xa901a7f4&&image.Word(0x1000103e8)==0xb9002be9); // native params pointer/index
}
struct BankSample {uint32_t offset=0,length=0,rate=0,start=0,end=0;float rms=0;};
static BankSample bank[4];
static void Bank(){
 const char *data=std::getenv("GAMEDATA");const std::string path=data?data:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";
 const auto sdt=File(path+"/audio/sfx.SDT");std::ifstream raw(path+"/audio/sfx.RAW",std::ios::binary|std::ios::ate);CHECK(bool(raw));if(!raw||sdt.size()<312*20)return;
 const auto size=raw.tellg();
 for(int id=308;id<=311;++id){
  auto &b=bank[id-308];std::memcpy(&b,sdt.data()+id*20,20);
  CHECK(b.length>1000&&b.length<1024*1024&&b.offset+b.length<=size&&b.rate>=6000&&b.rate<=44100&&b.end>b.start&&b.end<b.length);
  std::vector<int16_t> pcm(b.length/2);raw.seekg(b.offset);raw.read(reinterpret_cast<char *>(pcm.data()),pcm.size()*2);CHECK(bool(raw));
  double sum=0;for(int16_t v:pcm)sum+=double(v)*v;b.rms=float(std::sqrt(sum/pcm.size()));CHECK(b.rms>500);
 }
}
alignas(16) static uint8_t manager[0x7000],vehicles[2][0x6f0],player[0x900],pad[0x160];
static bool mission,cutscene,cabin,owned=true,registeredModelValid=true,poolValid=true,throwSample,recycleEvents,doorEvent;
static int registeredModel=248;
static uint8_t generation=17,registeredGeneration=17;
static int originalCalls,ambientCalls,oneCalls,tyreCalls,rainCalls,sirenCalls,damageCalls,badProfiles,lastProfile,area,expectedModel,hookCalls;static uint8_t effects=127,master=127;
struct TestParams {int32_t type;uint8_t calculated,padding[3];float square;uint32_t align;uint8_t *vehicle,*transmission;int32_t index;float velocity;};
static_assert(sizeof(TestParams)==0x28);
using Process=void (*)(uint8_t *,uint8_t *);static void NativeVehicle(uint8_t *,uint8_t *);static Process process=NativeVehicle;
struct Requested {int entity,counter,sample;uint32_t frequency,loopStart,loopEnd;int volume;float distance,maxDistance;bool twoD,reverb,reflection,audible;CVector pos;};
static std::vector<Requested> requested;
namespace airportjets {
bool UsesRegisteredModel(const uint8_t *v){return registeredModelValid&&poolValid&&v==vehicles[0]&&game::EntityModel(v)==registeredModel&&Read<int32_t>(v,0x348)==4;}
bool Owns(const uint8_t *v){return owned&&UsesRegisteredModel(v)&&generation==registeredGeneration;}
}
namespace camera {bool InSeat(){return cabin;}uint8_t *SeatVehicle(){return cabin?vehicles[0]:nullptr;}}
static uint8_t *Player(){return player;}static uint8_t *Pad(int){return pad;}
static bool OnMission(){return mission;}
static int16_t Throttle(uint8_t *p){return game::PadControlsDisabled(p)?0:Read<int16_t>(p,0x20);}
static float Distance(uint8_t *,const CVector &p){return p.x*p.x+p.y*p.y+p.z*p.z*.04f;}
static void Calculate(uint8_t *m,uint8_t *done,float square){if(!*done){Write(m,0x30,std::sqrt(square));*done=1;}}
static uint8_t Compute(uint8_t volume,float maximum,float distance){if(maximum<=0)return 0;if(distance<maximum/5)return volume;const float gain=(maximum-distance)/(maximum*.8f);return uint8_t(std::clamp(int(volume*gain*gain),0,127));}
static bool Setup(uint8_t *m,int id,int counter,uint8_t volume,uint32_t frequency,float range){
 const float distance=Read<float>(m,0x30);if(distance>=range)return false;
 const auto actual=Compute(volume,range,distance);if(!actual)return true;
 const auto &asset=bank[id-308];
 Write(m,0x18,counter);Write(m,0x1c,id);Write<uint16_t>(m,0x20,0);Write(m,0x24,1);Write(m,0x28,frequency);m[0x2c]=actual;
 Write(m,0x34,0);Write(m,0x38,asset.start);Write(m,0x3c,asset.end);m[0x40]=volume;Write(m,0x44,4.0f);Write(m,0x48,range);
 m[0x4c]=0;m[0x5c]=1;m[0x5e]=0;Write(m,0x64,id==308?5:4);
 requested.push_back({Read<int>(m,0x14),counter,id,frequency,asset.start,asset.end,actual,distance,range,false,true,false,effects&&master,Read<CVector>(m,0x50)});
 if(throwSample)throw std::runtime_error("native queue seam");return true;
}
static uint8_t Fly(uint8_t *m,uint8_t v){return Setup(m,308,0,v,bank[0].rate,440);}
static uint8_t Taxi(uint8_t *m,uint8_t v){const float d=Read<float>(m,0x30);const uint8_t vol=uint8_t((v>>1)+(v>>1)*d/180);return Setup(m,309,1,vol,uint32_t(22050+d*10950/180),180);}
static uint8_t Whine(uint8_t *m,uint8_t v,uint32_t f){return Setup(m,310,2,v,f,170);}
static uint8_t Engine(uint8_t *m,uint8_t v,uint32_t f){return Setup(m,311,3,v,f,180);}
static void OneShots(uint8_t *m,TestParams *p){
 ++oneCalls;lastProfile=p->index;
 if(doorEvent){if(p->index<0||size_t(p->index)>=nativeProfiles)++badProfiles;else Write(m,0x18,68);}
 CHECK(p->type==Read<int32_t>(p->vehicle,0x348));
 CHECK(game::EntityModel(p->vehicle)==expectedModel);
 const auto *handling=Read<uint8_t *>(p->vehicle,0x160);CHECK(p->transmission==(handling?handling+0x34:nullptr));
 if(recycleEvents){++generation;recycleEvents=false;}
}
static uint8_t FlatTyre(uint8_t *,TestParams *){++tyreCalls;return 1;}
static void Rain(uint8_t *,TestParams *){++rainCalls;}
static uint8_t Siren(uint8_t *,TestParams *){++sirenCalls;return 0;}
static uint8_t Damage(uint8_t *,TestParams *){++damageCalls;return 0;}
static void NativeVehicle(uint8_t *m,uint8_t *v){
 ++originalCalls;Write(m,0x50,game::EntityPos(v));Write(m,0x30,77.0f); // original leaves native scratch intact for following entities
 if(Read<int>(v,0x348)==4&&game::EntityModel(v)==180){++ambientCalls;uint8_t done=0;Calculate(m,&done,Distance(m,game::EntityPos(v)));Fly(m,127);Engine(m,63,22050);}
 TestParams p{};p.type=Read<int32_t>(v,0x348);p.vehicle=v;p.index=game::EntityModel(v)-130;p.square=Distance(m,game::EntityPos(v));const auto *handling=Read<uint8_t *>(v,0x160);p.transmission=const_cast<uint8_t *>(handling?handling+0x34:nullptr);
 OneShots(m,&p);FlatTyre(m,&p);Rain(m,&p);
}
namespace hooks {void *Chain(const char *name,void *replacement){++hookCalls;CHECK(!std::strcmp(name,"_ZN13cAudioManager14ProcessVehicleEP8CVehicle"));auto old=process;process=reinterpret_cast<Process>(replacement);return reinterpret_cast<void *>(old);}}
static void *AudioSymbol(void *,const char *name){
#define B(n,f) if(!std::strcmp(name,n))return reinterpret_cast<void *>(f)
 B("_ZN13cAudioManager18GetDistanceSquaredERK7CVector",Distance);B("_ZN13cAudioManager17CalculateDistanceERhf",Calculate);
 B("_ZN13cAudioManager18SetupJumboFlySoundEh",Fly);B("_ZN13cAudioManager19SetupJumboTaxiSoundEh",Taxi);
 B("_ZN13cAudioManager20SetupJumboWhineSoundEhj",Whine);B("_ZN13cAudioManager21SetupJumboEngineSoundEhj",Engine);
 B("_ZN4CPad13GetAccelerateEv",Throttle);B("_ZN11CTheScripts18IsPlayerOnAMissionEv",OnMission);
 B("_ZN13cAudioManager22ProcessVehicleOneShotsER14cVehicleParams",OneShots);B("_ZN13cAudioManager22ProcessVehicleFlatTyreER14cVehicleParams",FlatTyre);
 B("_ZN13cAudioManager20ProcessRainOnVehicleER14cVehicleParams",Rain);B("_ZN13cAudioManager26ProcessVehicleSirenOrAlarmER14cVehicleParams",Siren);B("_ZN13cAudioManager19ProcessEngineDamageER14cVehicleParams",Damage);
#undef B
 if(!std::strcmp(name,"_ZN12CCutsceneMgr10ms_runningE"))return &cutscene;if(!std::strcmp(name,"_ZN5CGame8currAreaE"))return &area;return nullptr;
}
static void Prepare(int model=248){
 std::memset(manager,0,sizeof manager);std::memset(vehicles,0,sizeof vehicles);std::memset(player,0,sizeof player);std::memset(pad,0,sizeof pad);
 mission=cutscene=cabin=throwSample=recycleEvents=doorEvent=false;owned=registeredModelValid=poolValid=true;registeredModel=model>=240&&model<=249?model:248;generation=registeredGeneration=17;originalCalls=ambientCalls=oneCalls=tyreCalls=rainCalls=sirenCalls=damageCalls=badProfiles=0;lastProfile=-1;area=0;requested.clear();effects=master=127;
 expectedModel=model;
 manager[0]=1;Write(manager,0x14,3);
 auto *v=vehicles[0];Write(v,0x60,uint32_t(2));Write(v,0x6a,int16_t(model));Write(v,0x348,4);Write(v,0x78,3);v[0x295]=16;Write(v,0x2a0,1000.0f);
 Write(v,0x18,CVector{0,1,0});Write(v,0x38,CVector{0,0,0});Write(v,0x84,CVector{});Write(v,0x28c,0.0f);
 Write(v,0x218,player);player[0x4c0]=1;Write(player,0x4b8,v);Write(pad,0x20,int16_t(255));
 auto *entry=manager+0x3010+3*0x30;Write(entry,0,0);Write(entry,8,v);entry[0x10]=entry[0x11]=1;
 game::api.FindPlayerPed=Player;game::api.GetPad=Pad;
}
static const Requested *Sample(int id){for(const auto &s:requested)if(s.sample==id)return &s;return nullptr;}
int main(){
 NativeContract();Bank();if(failures)return 1;CHECK(jetaudio::Install());CHECK(jetaudio::Install()&&hookCalls==1);
 Prepare();doorEvent=true;process(manager,vehicles[0]);CHECK(originalCalls==0&&oneCalls==1&&tyreCalls==1&&rainCalls==1&&lastProfile==50&&badProfiles==0);CHECK(Sample(311)&&Sample(311)->volume>0&&Sample(311)->frequency>=22000);
 if(Sample(311))CHECK(Sample(311)->entity==3&&Sample(311)->loopEnd==bank[3].end&&!Sample(311)->twoD&&Sample(311)->reverb&&!Sample(311)->reflection);
 Prepare();Write(pad,0x20,int16_t(0));process(manager,vehicles[0]);CHECK(Sample(309)&&Sample(309)->volume>0);CHECK(!Sample(311));
 Prepare();Write(pad,0x20,int16_t(0));Write(vehicles[0],0x84,CVector{0,.6f,0});process(manager,vehicles[0]);CHECK(Sample(311)&&Sample(311)->volume>0);
 Prepare();cabin=true;process(manager,vehicles[0]);const int interior=Sample(311)?Sample(311)->volume:0;
 Prepare();process(manager,vehicles[0]);CHECK(interior>0&&Sample(311)&&interior<Sample(311)->volume);
 Prepare();Write(vehicles[0],0x38,CVector{200,0,0});process(manager,vehicles[0]);CHECK(Sample(308)&&!Sample(311)&&!Sample(309)&&!Sample(310));
 Prepare();Write(vehicles[0],0x38,CVector{500,0,0});process(manager,vehicles[0]);CHECK(requested.empty());
 Prepare();effects=0;process(manager,vehicles[0]);CHECK(!requested.empty());for(auto &s:requested)CHECK(!s.audible);CHECK(effects==0&&master==127);
 Prepare();master=0;process(manager,vehicles[0]);for(auto &s:requested)CHECK(!s.audible);CHECK(effects==127&&master==0);
 Prepare();player[0x4c0]=0;Write(vehicles[0],0x218,static_cast<uint8_t *>(nullptr));Write(vehicles[0],0x28c,.5f);process(manager,vehicles[0]);CHECK(Sample(311)&&Sample(311)->frequency>16000&&Sample(311)->frequency<22050);
 Prepare();gConfig.planes=false;process(manager,vehicles[0]);CHECK(Sample(311));gConfig.planes=true; // still-owned actor retains native audio during safe landing
 Prepare();player[0x4c0]=0;area=2;process(manager,vehicles[0]);CHECK(requested.empty()&&oneCalls==0&&originalCalls==0);
 Prepare();player[0x4c0]=0;area=4;process(manager,vehicles[0]);CHECK(oneCalls==1&&sirenCalls==1&&damageCalls==1&&tyreCalls==0&&rainCalls==0&&requested.empty());
 Prepare();area=2;process(manager,vehicles[0]);CHECK(Sample(311)); // native player vehicle exception to area suppression
 for(int gate:{1,2}){
  Prepare();doorEvent=true;if(gate==1)mission=true;else cutscene=true;
  std::array<uint8_t,0x60> before{};std::memcpy(before.data(),manager+0x14,before.size());
  process(manager,vehicles[0]);
  CHECK(originalCalls==0&&oneCalls==1&&lastProfile==50&&badProfiles==0&&requested.empty());
  CHECK(!std::memcmp(before.data(),manager+0x14,before.size()));
 }
 for(int refusal=0;refusal<19;++refusal){
  Prepare();doorEvent=true;switch(refusal){
  case 0:owned=false;break;case 1:mission=true;break;case 2:cutscene=true;break;case 3:vehicles[0][0x295]=0;break;
  case 4:Write(vehicles[0],0x2a0,0.0f);break;case 5:Write(vehicles[0],0x60,uint32_t(2|(5<<3)));break;
  case 6:++generation;break;case 7:Write(vehicles[0],0x348,0);Write(vehicles[0],0x6a,int16_t(237));expectedModel=237;break;case 8:Write(vehicles[0],0x78,-1);break;
  case 9:Write(manager,0x14,4);break;case 10:Write(manager+0x3010+3*0x30,8,vehicles[1]);break;
  case 11:manager[0x6ecd]=1;break;case 12:recycleEvents=true;break;case 13:Write(vehicles[0],0x84,CVector{NAN,0,0});break;
  case 14:manager[0]=0;break;case 15:manager[0x3010+3*0x30+0x10]=0;break;case 16:manager[0x3010+3*0x30+0x11]=0;break;
  case 17:Write(manager+0x3010+3*0x30,0,1);break;case 18:Write(vehicles[0],0x78,250);break;
  }process(manager,vehicles[0]);CHECK(requested.empty());
  // Released/SCM-taken and recycled actors can still use our registered MI.
  // Profile safety follows that model lifetime, custom loops follow Owns.
  CHECK(originalCalls==(refusal==7?1:0));
  CHECK(badProfiles==0);
  if(refusal==0||refusal==6||refusal==12)CHECK(oneCalls==1&&lastProfile==50&&tyreCalls==1&&rainCalls==1);
 }
 Prepare(180);process(manager,vehicles[0]);CHECK(ambientCalls==1&&requested.size()==2&&Sample(308)&&Sample(311));
 Prepare(181);process(manager,vehicles[0]);CHECK(originalCalls==1&&requested.empty());
 for(int model:{237,239}){Prepare(model);owned=false;doorEvent=true;process(manager,vehicles[0]);CHECK(originalCalls==1&&badProfiles==0);}
 for(int release:{0,1,2}){
  Prepare();doorEvent=true;if(release==0)owned=false;else if(release==1)++generation;else recycleEvents=true;
  std::array<uint8_t,0x60> before{};std::memcpy(before.data(),manager+0x14,before.size());
  process(manager,vehicles[0]);
  CHECK(originalCalls==0&&oneCalls==1&&tyreCalls==1&&rainCalls==1&&lastProfile==50&&badProfiles==0&&requested.empty());
  CHECK(!std::memcmp(before.data(),manager+0x14,before.size()));
 }
 for(int invalid:{0,1}){Prepare();owned=false;doorEvent=true;if(invalid==0)manager[0x6ecd]=1;else Write(manager,0x14,4);process(manager,vehicles[0]);CHECK(originalCalls==0&&oneCalls==0&&badProfiles==0&&requested.empty());}
 // Foreign model metadata and invalid pool identity do not acquire our route.
 // Use a valid stock profile here: the original remains responsible for it.
 for(int invalid:{0,1,2}){Prepare(237);doorEvent=true;if(invalid==0)registeredModelValid=false;else if(invalid==1)poolValid=false;else registeredModel=249;process(manager,vehicles[0]);CHECK(originalCalls==1&&oneCalls==1&&lastProfile==107&&badProfiles==0&&requested.empty());}
 for(int model=240;model<=249;++model){Prepare(model);doorEvent=true;process(manager,vehicles[0]);CHECK(originalCalls==0&&lastProfile==50&&badProfiles==0);}
 Prepare();doorEvent=true;std::array<uint8_t,0x60> queue{};std::memcpy(queue.data(),manager+0x14,queue.size());
 std::array<uint8_t,sizeof pad> input{};std::memcpy(input.data(),pad,input.size());process(manager,vehicles[0]);
 CHECK(!std::memcmp(queue.data(),manager+0x14,queue.size())&&!std::memcmp(input.data(),pad,input.size()));
 Prepare();throwSample=true;try{process(manager,vehicles[0]);CHECK(false);}catch(const std::runtime_error &){}
 CHECK(Read<int>(manager,0x14)==3&&Read<float>(manager,0x30)==0.0f);
 if(!failures)std::puts("jetaudio native queue/assets: all passed");return failures?1:0;
}
