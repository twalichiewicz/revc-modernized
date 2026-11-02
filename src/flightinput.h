#pragma once
#include "bindings.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace flightinput {
struct Axes {float pitch=0,yaw=0;};
namespace detail {
inline bool Key(bindings::Action action)
{
 const auto &api=game::api;
 if(!api.controlsManager||!api.GetIsKeyboardKeyDown)return false;
 // Native GetControllerKeyAssociatedWithAction0xdb6cc; keyboard and extra
 // keyboard slots only. The native PCTemp* states have already been cleared
 // by CPad::Update0x108458 before FlyingControl consumes the merged pad.
 for(int type=0;type<2;++type){
  int32_t key=0;std::memcpy(&key,api.controlsManager+0xec0+32*int(action)+8*type,sizeof key);
  if(((key>0&&key<=254)||(key>=1000&&key<=1055))&&api.GetIsKeyboardKeyDown(api.controlsManager,key))return true;
 }
 return false;
}
}
// Native steering and look-button getters supply the primary flight axes.
// GetCarGun* reads the physical right LOOK stick in modes0-2; never use that
// merged value as a flight fallback. Only actual controller Mode3 maps these
// native getters to Dpad flight keys (right Y remains its native collective).
inline Axes Read(uint8_t *pad,int16_t primaryPitch,float primaryYaw,int16_t nativeTurretPitch,int16_t nativeTurretYaw=0)
{
 if(!pad||game::PadControlsDisabled(pad))return {};
 uint16_t mode=0;
 if(game::api.padAffectedByController&&*game::api.padAffectedByController==1)std::memcpy(&mode,pad+0xec,sizeof mode);
 if(mode>3)return {};
 const float keyboardPitch=mode==3?-nativeTurretPitch/128.0f:
     float(detail::Key(bindings::Action::kVehicleTurretdown))-float(detail::Key(bindings::Action::kVehicleTurretup));
 const float keyboardYaw=mode==3?nativeTurretYaw/128.0f:
     float(detail::Key(bindings::Action::kVehicleTurretright))-float(detail::Key(bindings::Action::kVehicleTurretleft));
 const float pitch=primaryPitch?primaryPitch/128.0f:keyboardPitch;
 const float yaw=std::fabs(primaryYaw)>0?primaryYaw:keyboardYaw;
 return {std::clamp(pitch,-1.0f,1.0f),std::clamp(yaw,-1.0f,1.0f)};
}
}
