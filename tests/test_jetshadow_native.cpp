// Pristine CPlane caller -> production owned hook -> pristine CShadows
// consumer, with a guard immediately after the actual0x388 actor allocation.
#include "nativejetshadowarm.h"
#include "../src/game.h"
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#if __has_include("../src/jetshadow.cpp") && !defined(JETSHADOW_NATIVE_BASELINE)
#include <dlfcn.h>
static void *ShadowSymbol(void *,const char *);
#define dlsym ShadowSymbol
#include "../src/jetshadow.cpp"
#undef dlsym
#define HAVE_JETSHADOW 1
#endif
namespace game {Api api;}
namespace {
using namespace nativejetshadowtest;
int failures=0,nativeCalls=0,staticCalls=0,helperCalls=0;bool owned=true,loseOwner=false;
#define CHECK(x) do{if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
uint8_t *plane;void (*entry)(uint8_t *),(*nativePreRender)(uint8_t *),(*nativeStore)(uint8_t *,int);
[[maybe_unused]]int area=0;bool active=false;
void Nothing(){}
void Assert(const char *,const char *,unsigned,const char *){Require(false,"unexpected native assertion");}
void *NoPlayer(){return nullptr;}
void Matrix(void *p){std::memset(p,0,0x48);}
CVector Transform(const float *m,const CVector &v){return {m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14]};}
void Static(uint32_t,uint8_t,void *,const CVector *,float,float,float,float,int16_t,uint8_t,uint8_t,uint8_t,float,float,float,bool,float){++staticCalls;}
void Before(uint8_t *p){++nativeCalls;nativePreRender(p);if(loseOwner)owned=false;}
[[maybe_unused]]void StoreShadow(uint8_t *p,int type){CHECK(type==0);++helperCalls;nativeStore(p,type);}
bool RtActive(){return active;}
}
namespace airportjets {bool Owns(const uint8_t *p){return p==plane&&owned;}}
namespace hooks {void *Chain(const char *name,void *hook){CHECK(std::strstr(name,"CPlane9PreRender"));void *before=reinterpret_cast<void *>(entry?entry:Before);entry=reinterpret_cast<decltype(entry)>(hook);return before;}}
#ifdef HAVE_JETSHADOW
static void *ShadowSymbol(void *,const char *name){
 if(std::strstr(name,"StoreShadowForVehicle"))return reinterpret_cast<void *>(StoreShadow);
 if(std::strstr(name,"currArea"))return &area;
 return nullptr;
}
#endif
int main(){
 Code code;
 CHECK(code.image.Word(0x100277640)==0x7980d408); // real caller MI index
 CHECK(code.image.Word(0x100212280)==0xbd454e60); // unsafe BIKE branch+54c
 code.Add(0x10027761c,0x100277978);code.Add(0x100211fbc,0x100212578);
 const auto suppression=code.Add(0x10021341c,0x100213480);Store(code.code+suppression+0x64,uint32_t(0xd61f02c0));
 code.bindings={{0x100172f48,reinterpret_cast<void *>(Transform)}, {0x1001e1cfc,reinterpret_cast<void *>(Nothing)},
 {0x1001715e4,reinterpret_cast<void *>(Matrix)},{0x100171828,reinterpret_cast<void *>(Nothing)},
 {0x10014a6b4,reinterpret_cast<void *>(Assert)},{0x10010c0b4,reinterpret_cast<void *>(NoPlayer)},
 {0x100210fec,reinterpret_cast<void *>(Static)},{0x100169818,reinterpret_cast<void *>(RtActive)}};
 alignas(16)uint8_t info[0x90]{},collision[0x68]{};
 Store(info+0x20,collision);Store(collision+0x10,-8.f);Store(collision+0x14,-9.f);Store(collision+0x1c,8.f);Store(collision+0x20,9.f);
 Store(code.Page(0x1006b2050)+248*8,info);Store(code.Page(0x100799650),int32_t(180));
 Store(code.Page(0x100784380),reinterpret_cast<void *>(0x1230));Store(code.Page(0x10036bd80),1.f);
 code.Finish();nativePreRender=reinterpret_cast<decltype(nativePreRender)>(code.Address(0x10027761c));nativeStore=reinterpret_cast<decltype(nativeStore)>(code.Address(0x100211fbc));
 GuardedPlane storage;plane=storage.entity;Store(plane+8,1.f);Store(plane+0x1c,1.f);Store(plane+0x30,1.f);Store(plane+0x6a,int16_t(248));Store(plane+0x84,1.f);plane[0x60]=2;
 entry=Before;
#ifdef HAVE_JETSHADOW
 CHECK(jetshadow::Install());CHECK(jetshadow::Install());
#endif
 entry(plane);CHECK(nativeCalls==1);CHECK(Read<int16_t>(code.Page(0x100784400))==1); // RED on unbridged native caller
 CHECK(helperCalls==1);CHECK(Read<int16_t>(code.Page(0x100784440))==180);CHECK(Read<void *>(code.Page(0x100784430))==reinterpret_cast<void *>(0x1230));
 CHECK(Read<float>(code.Page(0x100784418))!=0&&Read<float>(code.Page(0x10078441c))!=0);
#ifdef HAVE_JETSHADOW
 const CVector camera{};auto raster=reinterpret_cast<void (*)()>(code.Address(0x10021341c));
 VehiclePool(raster,nullptr,&camera);CHECK(!(Read<uint8_t>(code.Page(0x100784446))&2));
 active=true;VehiclePool(raster,nullptr,&camera);CHECK(Read<uint8_t>(code.Page(0x100784446))&2);active=false;
 auto reset=[&]{Store(code.Page(0x100784400),int16_t(0));staticCalls=helperCalls=0;owned=true;loseOwner=false;area=0;};
 reset();Store(plane+0x84,0.f);entry(plane);CHECK(staticCalls==1&&helperCalls==1);CHECK(!Read<int16_t>(code.Page(0x100784400)));
 reset();Store(code.Page(0x100799650),int32_t(0));entry(plane);CHECK(!staticCalls&&!Read<int16_t>(code.Page(0x100784400)));Store(code.Page(0x100799650),int32_t(180));
 reset();Store(plane+0x38,1000.f);entry(plane);CHECK(!staticCalls&&!Read<int16_t>(code.Page(0x100784400)));Store(plane+0x38,0.f);
 reset();area=1;entry(plane);CHECK(!helperCalls&&!staticCalls);
 reset();owned=false;const int ordinaryCalls=nativeCalls;entry(plane);CHECK(nativeCalls==ordinaryCalls+1&&!helperCalls&&!staticCalls);
 reset();loseOwner=true;entry(plane);CHECK(!helperCalls&&!staticCalls);
 reset();active=true;entry(plane);CHECK(helperCalls==1&&staticCalls==1);active=false; // consumer owns current-frame RT suppression
 reset();Store(plane+0x84,1.f);Store(code.Page(0x100784400),int16_t(48));entry(plane);CHECK(Read<int16_t>(code.Page(0x100784400))==48); // native capacity
#endif
 if(!failures)std::puts("jetshadow native: exact CPlane caller, guarded native type0 raster consumer and native gates passed");return failures?1:0;
}
