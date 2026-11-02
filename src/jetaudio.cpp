#include "jetaudio.h"
#include "airportjets.h"
#include "camera.h"
#include "game.h"
#include "hooks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>

using namespace game;
namespace jetaudio {
namespace {
void (*original)(uint8_t *,uint8_t *);
float (*distanceSquared)(uint8_t *,const CVector &);
void (*calculateDistance)(uint8_t *,uint8_t *,float);
uint8_t (*flySound)(uint8_t *,uint8_t),(*taxiSound)(uint8_t *,uint8_t);
uint8_t (*engineSound)(uint8_t *,uint8_t,uint32_t),(*whineSound)(uint8_t *,uint8_t,uint32_t);
int16_t (*accelerate)(uint8_t *);
bool (*onMission)();bool *cutscene;
int32_t *area;
struct VehicleParams {
 int32_t type=4;uint8_t calculated=0,padding[3]{};float square=0;uint32_t alignment=0;
 uint8_t *vehicle=nullptr,*transmission=nullptr;int32_t index=50;float velocity=0;
};
static_assert(sizeof(VehicleParams)==0x28&&offsetof(VehicleParams,vehicle)==0x10&&offsetof(VehicleParams,index)==0x20&&offsetof(VehicleParams,velocity)==0x24);
void (*oneShots)(uint8_t *,VehicleParams *),(*rain)(uint8_t *,VehicleParams *);
uint8_t (*flatTyre)(uint8_t *,VehicleParams *),(*siren)(uint8_t *,VehicleParams *),(*damage)(uint8_t *,VehicleParams *);
bool installed;
template<class T> T Read(const uint8_t *bytes,size_t offset){T value{};std::memcpy(&value,bytes+offset,sizeof value);return value;}
bool Finite(CVector p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool RegisteredModel(uint8_t *vehicle)
{
 if(!vehicle||!airportjets::UsesRegisteredModel(vehicle))return false;
 const int model=EntityModel(vehicle);return model>=240&&model<=249;
}
bool AudioContext(uint8_t *manager,uint8_t *vehicle)
{
 if(!manager||!manager[0]||manager[0x6ecd]||!RegisteredModel(vehicle))return false;
 const int id=Read<int32_t>(vehicle,0x78);
 if(!IsVehicle(vehicle)||Read<int32_t>(vehicle,0x348)!=4||id<0||id>=250||Read<int32_t>(manager,0x14)!=id)return false;
 // CVehicle's native constructor registers/enables this physical entity.
 // Never attach our loops to a reused or unrelated audio-entity identity.
 const uint8_t *entity=manager+0x3010+size_t(id)*0x30;
 return Read<int32_t>(entity,0)==0&&Read<uint8_t *>(entity,8)==vehicle&&entity[0x10]&&entity[0x11];
}
struct QueueScope {
 uint8_t *queue;std::array<uint8_t,0x60> saved{};
 explicit QueueScope(uint8_t *manager):queue(manager+0x14){std::memcpy(saved.data(),queue,saved.size());}
 ~QueueScope(){std::memcpy(queue,saved.data(),saved.size());}
};
void Process(uint8_t *manager,uint8_t *vehicle)
{
 // Preserve the full original route outside our registered native model.
 // Its optimized plane switch omits dynamic models, and its event profile
 // lookup has only110 entries. Never send our model248's index118 into it.
 if(!RegisteredModel(vehicle)){original(manager,vehicle);return;}
 // Model lifetime and free-play ownership are separate: a released, recycled
 // or SCM-taken actor can retain our registered model's unsafe stock index.
 // An inactive/mismatched context must not fall back to that native lookup.
 if(!AudioContext(manager,vehicle))return;
 uint8_t *ped=api.FindPlayerPed?api.FindPlayerPed():nullptr;
 const bool playerVehicle=ped&&PedInVehicle(ped)&&PedVehicle(ped)==vehicle;
 const int currentArea=*area;
 if(!playerVehicle&&(currentArea<0||currentArea>16||!(0x1e011u&(1u<<currentArea))))return;
 const float health=VehicleHealth(vehicle);
 const CVector velocity=Read<CVector>(vehicle,0x84),forward=Read<CVector>(vehicle,0x18),position=EntityPos(vehicle);
 if(!std::isfinite(health)||!Finite(velocity)||!Finite(forward)||!Finite(position))return;
 const float square=distanceSquared(manager,position);
 if(!std::isfinite(square)||square<0)return;
 QueueScope queue(manager);
 std::memcpy(manager+0x50,&position,sizeof position);
 VehicleParams params;params.square=square;params.vehicle=vehicle;
 uint8_t *handling=Read<uint8_t *>(vehicle,0x160);params.transmission=handling?handling+0x34:nullptr;
 params.velocity=(vehicle[0x60]>>3)==2?Read<float>(vehicle,0x1a4)*.02f:velocity.x*forward.x+velocity.y*forward.y+velocity.z*forward.z;
 if(!std::isfinite(params.velocity))return;
 // Exact native class4 events/tyre/rain consumers, using only the airtrain's
 // local audio profile. Actor identity/model/type and global settings stay real.
 oneShots(manager,&params);if(!AudioContext(manager,vehicle))return;
 if(!playerVehicle&&currentArea==4){siren(manager,&params);if(AudioContext(manager,vehicle))damage(manager,&params);return;}
 flatTyre(manager,&params);if(!AudioContext(manager,vehicle))return;
 rain(manager,&params);if(!AudioContext(manager,vehicle))return;
 if(!airportjets::Owns(vehicle)||*cutscene||onMission()||Wrecked(vehicle)||health<=0||!(vehicle[0x295]&16)||square>=440.0f*440.0f)return;
 ped=api.FindPlayerPed?api.FindPlayerPed():nullptr;
 const bool pilot=ped&&PedInVehicle(ped)&&PedVehicle(ped)==vehicle&&Read<uint8_t *>(vehicle,0x218)==ped&&(vehicle[0x60]>>3)==0;
 float throttle=Read<float>(vehicle,0x28c);
 if(pilot){uint8_t *pad=api.GetPad?api.GetPad(0):nullptr;if(!pad)return;throttle=accelerate(pad)/255.0f;}
 if(!std::isfinite(throttle))return;
 throttle=std::clamp(throttle,0.0f,1.0f);
 // Same native airtrain acceleration envelope, driven by physical velocity
 // instead of its ambient flight-path speed/planeId. Pilot throttle can spool
 // the turbines while stationary; coasting retains the native speed component.
 const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z);
 if(!std::isfinite(speed))return;
 const float motion=std::clamp((speed-.103344f)*1.6760077f,0.0f,1.0f);
 const float power=std::max(throttle,motion);
 const float cabin=camera::InSeat()&&camera::SeatVehicle()==vehicle?.5f:1.0f;
 const auto volume=[&](float v){return uint8_t(std::clamp(std::lround(v*cabin),0l,127l));};
 std::memcpy(manager+0x50,&position,sizeof position);
 uint8_t calculated=0;calculateDistance(manager,&calculated,square);
 // These are the actual airtrain bank0 consumers: native attenuation, loop
 // offsets, source identity, reverb, mixer volume and channel expiry remain.
 if(!flySound(manager,volume(20+107*motion)))return;
 taxiSound(manager,volume(75*(1-power)));
 engineSound(manager,volume(127*power),uint32_t(16000+6050*power));
 whineSound(manager,volume(18),uint32_t(29500+14600*power));
}
template<class T> bool Bind(T &value,const char *name){value=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,name));return value!=nullptr;}
}
bool Install()
{
 if(installed)return true;
 const bool bound=Bind(distanceSquared,"_ZN13cAudioManager18GetDistanceSquaredERK7CVector")&&Bind(calculateDistance,"_ZN13cAudioManager17CalculateDistanceERhf")&&
     Bind(flySound,"_ZN13cAudioManager18SetupJumboFlySoundEh")&&Bind(taxiSound,"_ZN13cAudioManager19SetupJumboTaxiSoundEh")&&
     Bind(engineSound,"_ZN13cAudioManager21SetupJumboEngineSoundEhj")&&Bind(whineSound,"_ZN13cAudioManager20SetupJumboWhineSoundEhj")&&
     Bind(oneShots,"_ZN13cAudioManager22ProcessVehicleOneShotsER14cVehicleParams")&&Bind(flatTyre,"_ZN13cAudioManager22ProcessVehicleFlatTyreER14cVehicleParams")&&
     Bind(rain,"_ZN13cAudioManager20ProcessRainOnVehicleER14cVehicleParams")&&Bind(siren,"_ZN13cAudioManager26ProcessVehicleSirenOrAlarmER14cVehicleParams")&&
     Bind(damage,"_ZN13cAudioManager19ProcessEngineDamageER14cVehicleParams")&&Bind(accelerate,"_ZN4CPad13GetAccelerateEv")&&
     Bind(onMission,"_ZN11CTheScripts18IsPlayerOnAMissionEv")&&Bind(cutscene,"_ZN12CCutsceneMgr10ms_runningE")&&Bind(area,"_ZN5CGame8currAreaE");
 if(!bound)return false;
 original=reinterpret_cast<decltype(original)>(hooks::Chain("_ZN13cAudioManager14ProcessVehicleEP8CVehicle",reinterpret_cast<void *>(Process)));
 installed=original!=nullptr;return installed;
}
}
