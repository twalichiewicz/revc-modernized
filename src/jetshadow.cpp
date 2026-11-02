#include "jetshadow.h"
#include "airportjets.h"
#include "hooks.h"
#include <cstdint>
#include <dlfcn.h>

namespace jetshadow { namespace {
void (*originalPreRender)(uint8_t *);
void (*storeVehicleShadow)(uint8_t *,int);
const int32_t *area;
bool ready;

void PreRender(uint8_t *plane)
{
 originalPreRender(plane);
 if(!ready||!airportjets::Owns(plane)||*area!=0)return;
 // CPlane::PreRender5217 has no raster shadow caller. VEH_SHD_TYPE0 is the
 // base vehicle path: collision bounds, matrix and speed, all below+0x90.
 // Type1 is the bike path and reads+0x54c, beyond the0x388 CPlane allocation.
 // Keep the native helper's strength, camera-distance, motion/static-cache
 // and48-record capacity decisions. Native RT suppresses this dark sprite
 // later, using the current frame's result (Active here is the prior frame).
 storeVehicleShadow(plane,0);
}
}
bool Install()
{
 if(ready)return true;
 storeVehicleShadow=reinterpret_cast<decltype(storeVehicleShadow)>(dlsym(RTLD_MAIN_ONLY,"_ZN8CShadows21StoreShadowForVehicleEP8CVehicle12VEH_SHD_TYPE"));
 area=reinterpret_cast<const int32_t *>(dlsym(RTLD_MAIN_ONLY,"_ZN5CGame8currAreaE"));
 if(!storeVehicleShadow||!area)return false;
 if(!originalPreRender)originalPreRender=reinterpret_cast<decltype(originalPreRender)>(hooks::Chain("_ZN6CPlane9PreRenderEv",reinterpret_cast<void *>(PreRender)));
 return ready=originalPreRender!=nullptr;
}
}
