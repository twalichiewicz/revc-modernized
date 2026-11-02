// Actual CPlane PreRender -> native corona call -> scoped replacement lights.
#include <dlfcn.h>
static void *LightSymbol(void *,const char *);
#define dlsym LightSymbol
#include "../src/jetlights.cpp"
#undef dlsym
#include "../src/game.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <vector>

namespace game { Api api; }
namespace {
int failures,preRenders;bool owned=true,nested=false,dropOwnership=false,failCorona=true,extraCorona=false,throwNative=false;
alignas(16) uint8_t jet[0x388]{},foreign[0x388]{};uint32_t now=50;
using Corona=void (*)(uint32_t,uint8_t,uint8_t,uint8_t,uint8_t,const CVector &,float,float,uint8_t,int8_t,uint8_t,uint8_t,uint8_t,float,bool,float);
void (*preRender)(uint8_t *);Corona corona;
struct Light {uint32_t id;uint8_t r,g,b,los;CVector at;float size;};
std::vector<Light> lights;int points;
#define CHECK(c) do { if(!(c)) {std::printf("FAIL %d: %s\n",__LINE__,#c);++failures;} } while(0)
void NativeCorona(uint32_t id,uint8_t r,uint8_t g,uint8_t b,uint8_t,const CVector &at,float size,float,uint8_t,int8_t,uint8_t,uint8_t los,uint8_t,float,bool,float)
{lights.push_back({id,r,g,b,los,at,size});}
void NativePoint(uint8_t,CVector,CVector,float range,float,float,float,uint8_t,bool) {CHECK(range>0&&range<4);++points;}
void NativePreRender(uint8_t *v)
{
	++preRenders;*reinterpret_cast<float *>(v+0x370)+=1;
	for(uint32_t i=10;i<=12;++i)corona(uint32_t(uintptr_t(v))+i,255,0,0,255,*reinterpret_cast<CVector *>(v+0x38),2,240,1,0,0,0,0,0,false,1.5f);
	if(extraCorona)corona(uint32_t(uintptr_t(v))+99,255,0,0,255,*reinterpret_cast<CVector *>(v+0x38),.7f,240,1,0,0,0,0,0,false,1.5f);
	if(throwNative)throw std::runtime_error("native render interrupted");
	if(v==jet&&nested)preRender(foreign);
	if(v==jet&&dropOwnership)owned=false;
}
void Reset()
{
	lights.clear();points=preRenders=0;owned=true;nested=dropOwnership=extraCorona=throwNative=false;now=50;
	for(auto *v:{jet,foreign}) {
		std::memset(v,0,0x388);*reinterpret_cast<CVector *>(v+8)={0,1,0};*reinterpret_cast<CVector *>(v+0x18)={-1,0,0};*reinterpret_cast<CVector *>(v+0x28)={0,0,1};*reinterpret_cast<CVector *>(v+0x38)={10,20,30};
		*reinterpret_cast<float *>(v+0x2a0)=1000;v[0x60]=2;v[0x295]=16;
	}
	game::api.timeMs=&now;
}
}
namespace airportjets { bool Owns(const uint8_t *v) {return v==jet&&owned;} }
namespace hooks {
void *Chain(const char *name,void *hook)
{
	if(std::strstr(name,"CPlane9PreRender")) {
		if(preRender==reinterpret_cast<decltype(preRender)>(hook))return nullptr;
		void *old=preRender?reinterpret_cast<void *>(preRender):reinterpret_cast<void *>(NativePreRender);preRender=reinterpret_cast<decltype(preRender)>(hook);return old;
	}
	if(std::strstr(name,"RegisterCorona")) {
		if(failCorona){failCorona=false;return nullptr;}
		if(corona==reinterpret_cast<Corona>(hook))return nullptr;
		void *old=corona?reinterpret_cast<void *>(corona):reinterpret_cast<void *>(NativeCorona);corona=reinterpret_cast<Corona>(hook);return old;
	}
	return nullptr;
}
}
static void *LightSymbol(void *,const char *name)
{return std::strstr(name,"CPointLights8AddLight")?reinterpret_cast<void *>(NativePoint):nullptr;}
int main()
{
	Reset();corona=NativeCorona;
	CHECK(!jetlights::Install());if(preRender){preRender(jet);CHECK(lights.size()==3&&points==0);}Reset();
	CHECK(jetlights::Install()); // recover a partial hook install
	if(!preRender)return 1;
	preRender(jet);CHECK(preRenders==1&&*reinterpret_cast<float *>(jet+0x370)==1);CHECK(lights.size()==5&&points==5);
	for(size_t i=0;i<lights.size()&&i<jetlights::kLenses.size();++i) {
		const auto &lens=jetlights::kLenses[i];const auto &light=lights[i];
		CHECK(light.id!=uint32_t(uintptr_t(jet))+10&&light.id!=uint32_t(uintptr_t(jet))+11&&light.id!=uint32_t(uintptr_t(jet))+12);
		CHECK(light.size>.05f&&light.size<=.18f&&light.los==1);
		CHECK(light.r==lens.red&&light.g==lens.green&&light.b==lens.blue);
		CHECK(std::fabs(light.at.x-(10-lens.position.y))<.001f&&std::fabs(light.at.y-(20+lens.position.x))<.001f&&std::fabs(light.at.z-(30+lens.position.z))<.001f);
	}
	Reset();now=600;preRender(jet);CHECK(lights.size()==3&&points==3); // beacon pulse is off
	Reset();jet[0x295]=0;preRender(jet);CHECK(lights.empty()&&points==0);
	Reset();jet[0x60]=2|(5<<3);preRender(jet);CHECK(lights.empty()&&points==0);
	Reset();preRender(foreign);CHECK(lights.size()==3&&lights[0].size==2&&points==0);
	Reset();nested=true;preRender(jet);int giant=0,small=0;for(const auto &light:lights){giant+=light.size==2;small+=light.size<=.18f;}CHECK(preRenders==2&&giant==3&&small==5);
	Reset();dropOwnership=true;preRender(jet);CHECK(lights.empty()&&points==0);
	Reset();extraCorona=true;preRender(jet);CHECK(lights.size()==6&&lights[0].size==.7f&&points==5);
	Reset();throwNative=true;try{preRender(jet);}catch(const std::runtime_error &){}CHECK(lights.empty()&&points==0);
	corona(uint32_t(uintptr_t(jet))+10,255,0,0,255,{0,0,0},2,240,1,0,0,0,0,0,false,1.5f);CHECK(lights.size()==1); // scope was restored on unwind
	if(!failures)std::puts("jet lights: all passed");return failures?1:0;
}
