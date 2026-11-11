#pragma once
#include "vec3.h"
#include <cmath>
namespace bodyloads {
class State {
 Vec3 previous{},clean{0,0,-20};float previousDt=0;
public:
 // World m/s and physics seconds. A contact that already spent material work
 // holds the previous clean load, but refreshes velocity history immediately.
 Vec3 Sample(Vec3 velocity,float dt,bool spentContactWork){
  if(!std::isfinite(dt)||dt<=0||dt>.1f||!std::isfinite(velocity.x)||!std::isfinite(velocity.y)||!std::isfinite(velocity.z))return clean;
  Vec3 load{0,0,-20};if(previousDt>0)load={-(velocity.x-previous.x)/previousDt,-(velocity.y-previous.y)/previousDt,-20-(velocity.z-previous.z)/previousDt};previous=velocity;previousDt=dt;
  if(spentContactWork)return clean;float length=std::sqrt(load.x*load.x+load.y*load.y+load.z*load.z);if(!std::isfinite(length))return clean;if(length>1000){load.x*=1000/length;load.y*=1000/length;load.z*=1000/length;}clean=load;return load;
 }
};
}
