#include "jetlights.h"
#include "airportjets.h"
#include "game.h"
#include "hooks.h"
#include <dlfcn.h>
#include <type_traits>

namespace jetlights { namespace {
using Corona=void (*)(uint32_t,uint8_t,uint8_t,uint8_t,uint8_t,const CVector &,float,float,uint8_t,int8_t,uint8_t,uint8_t,uint8_t,float,bool,float);
Corona originalCorona;
void (*originalPreRender)(uint8_t *);
void (*pointLight)(uint8_t,CVector,CVector,float,float,float,float,uint8_t,bool);
const uint8_t *activePlane;
bool ready;

void CoronaHook(uint32_t id,uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha,const CVector &at,float size,float reach,uint8_t type,int8_t flare,uint8_t reflection,uint8_t los,uint8_t streak,float angle,bool near,float nearDistance)
{
	// Native CPlane::PreRender emits these three coronas from MI+6c/+78.
	// The stock dummy positions and1-2m sprites are inappropriate for our
	// independently registered Learjet. Preserve unrelated/nested producers.
	if(activePlane&&airportjets::Owns(activePlane)) {
		const uint32_t base=uint32_t(uintptr_t(activePlane));
		if(id==base+10||id==base+11||id==base+12)return;
	}
	originalCorona(id,red,green,blue,alpha,at,size,reach,type,flare,reflection,los,streak,angle,near,nearDistance);
}
CVector At(const uint8_t *plane,Vec3 local)
{
	const auto &r=*reinterpret_cast<const CVector *>(plane+8),&f=*reinterpret_cast<const CVector *>(plane+0x18),&u=*reinterpret_cast<const CVector *>(plane+0x28),&p=*reinterpret_cast<const CVector *>(plane+0x38);
	return {p.x+r.x*local.x+f.x*local.y+u.x*local.z,p.y+r.y*local.x+f.y*local.y+u.y*local.z,p.z+r.z*local.x+f.z*local.y+u.z*local.z};
}
void Emit(uint8_t *plane)
{
	if(!airportjets::Owns(plane)||game::Wrecked(plane)||game::VehicleHealth(plane)<=0||!(plane[0x295]&16)||!game::api.timeMs)return;
	const uint32_t phase=*game::api.timeMs%1200;
	const bool beacon=phase<140||(phase>=230&&phase<310);
	for(size_t i=0;i<kLenses.size();++i) {
		if(i>=3&&!beacon)continue;
		const auto &lens=kLenses[i];const CVector at=At(plane,lens.position);
		originalCorona(uint32_t(uintptr_t(plane))+768+uint32_t(i),lens.red,lens.green,lens.blue,255,at,lens.radius*2,600,0,0,0,1,0,0,false,.5f);
		pointLight(0,at,{0,0,0},2.0f,lens.red/255.0f*.8f,lens.green/255.0f*.8f,lens.blue/255.0f*.8f,0,false);
	}
}
void PreRender(uint8_t *plane)
{
	if(!ready)return originalPreRender(plane);
	{
		struct Scope {
			const uint8_t *previous;
			explicit Scope(const uint8_t *p):previous(activePlane){activePlane=airportjets::Owns(p)?p:nullptr;}
			~Scope(){activePlane=previous;}
		} scope(plane);
		originalPreRender(plane);
	}
	Emit(plane);
}
}
bool Install()
{
	if(ready)return true;
	pointLight=reinterpret_cast<decltype(pointLight)>(dlsym(RTLD_MAIN_ONLY,"_ZN12CPointLights8AddLightEh7CVectorS0_ffffhb"));
	if(!pointLight||!game::api.timeMs)return false;
	auto chain=[](const char *name,auto hook,auto &original){
		if(original)return true;
		original=reinterpret_cast<std::decay_t<decltype(original)>>(hooks::Chain(name,reinterpret_cast<void *>(hook)));
		return original!=nullptr;
	};
	return ready=chain("_ZN6CPlane9PreRenderEv",PreRender,originalPreRender)&&
		chain("_ZN8CCoronas14RegisterCoronaEjhhhhRK7CVectorffhahhhfbf",CoronaHook,originalCorona);
}
}
