#include "glassmodel.h"
#include <cmath>
#include <cstring>
#include <strings.h>

namespace glassmodel {
Pane Classify(const char *name,Vec3 p,Vec3 n,uint8_t alpha,const char *texture)
{
 if(!name||!alpha||alpha==255||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
    !std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z)||p.z<.2f)return Pane::None;
 // The shipped cars also use alpha on badges, stripes and emergency-service
 // decals. Cabin glazing is untextured or uses these measured glass textures.
 // Some real tinted panes have alpha242/250, so an alpha ceiling loses glass.
 if(texture&&*texture){
  const char *known[]={"white128a","white128b","white128c","black64a","taximiami86speca","patriot86speca"};
  bool glass=false;for(auto t:known)glass|=!strncasecmp(texture,t,32);if(!glass)return Pane::None;
 }
 if(std::strstr(name,"_lo")||std::strstr(name,"_vlo"))return Pane::None;
 const float x=std::fabs(n.x),y=std::fabs(n.y),z=std::fabs(n.z);
 // The Deluxo's raked rear glass is only 11–13 degrees above horizontal.
 // Retain it while excluding horizontal roofs and degenerate faces.
 if(x+y<1e-8f||x+y<.15f*z)return Pane::None;
 if(std::strstr(name,"windscreen"))return Pane::Front;
 if(std::strstr(name,"door_lf"))return Pane::LeftFront;
 if(std::strstr(name,"door_rf"))return Pane::RightFront;
 if(std::strstr(name,"door_lr"))return Pane::LeftRear;
 if(std::strstr(name,"door_rr"))return Pane::RightRear;
 if(std::strstr(name,"boot_hi"))return Pane::Rear;
 if(std::strcmp(name,"chassis_hi"))return Pane::None;
 if(x>y*1.2f)return p.x<0?(p.y>0?Pane::LeftFront:Pane::LeftRear):(p.y>0?Pane::RightFront:Pane::RightRear);
 return p.y>0?Pane::Front:Pane::Rear;
}
Pane DoorPane(int door)
{
 switch(door){case 1:return Pane::Rear;case 2:return Pane::LeftFront;case 3:return Pane::RightFront;
 case 4:return Pane::LeftRear;case 5:return Pane::RightRear;default:return Pane::None;}
}
}
