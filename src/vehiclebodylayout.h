#pragma once
#include "airframemodel.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <strings.h>
namespace aircraftbodymodel {
// Native vehicle type, not a model-ID approximation. Bounds and wheel anchors
// are measured in this asset's local coordinates, in metres.
enum class Kind {Automobile,Bike,Boat,Plane,Helicopter};
struct Definition {
 Kind kind=Kind::Plane;
 int model=0;
 mechanicalmodel::Box bounds{};
 std::array<Vec3,4> wheels{}; // LF, LR, RF, RR; a bike may repeat its centreline.
 bool hasWheels=false;
 float armor=1; // Effective sheet resistance/thickness multiplier, >=1.
 bool legacyDodo=false;
};
// Effective material calibration; not material certification or shell FEM.
struct MaterialParameters {
 float young=70e9f,yield=100e6f,thickness=.0008f,surfaceDensity=2.7f;
 float fractureEnergy=30000,skinResistance=1;
};
struct BeamLayout {Vec3 axis{};float root=0,span=0;};
struct VehicleLayout {
 mechanicalmodel::Layout mechanical{};
 std::array<mechanicalmodel::Box,airframemodel::Count> airframe{};
 std::array<BeamLayout,3> beams{};
 bool longitudinalBody=false;
 std::array<bool,mechanicalmodel::kModules> enabled{};
 std::array<bool,airframemodel::Count> airframeEnabled{};
};
enum class GlassPane : uint8_t {None,Front,Rear,LeftFront,RightFront,LeftRear,RightRear};
// Automobile cabin glazing from a translucent face. Centre and face normal are
// in the car's frame. Only translucent cabin faces on known high-detail body
// components qualify. The measured 20 cm beltline floor and glass texture list
// exclude lamps/decals; LODs stay native. glassmodel::Classify is this rule.
inline GlassPane CabinGlass(const char *name,Vec3 p,Vec3 n,uint8_t alpha,const char *texture){
 if(!name||!alpha||alpha==255||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||
    !std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z)||p.z<.2f)return GlassPane::None;
 // The shipped cars also use alpha on badges, stripes and emergency-service
 // decals. Cabin glazing is untextured or uses these measured glass textures.
 // Some real tinted panes have alpha242/250, so an alpha ceiling loses glass.
 if(texture&&*texture){
  const char *known[]={"white128a","white128b","white128c","black64a","taximiami86speca","patriot86speca"};
  bool glass=false;for(auto t:known)glass|=!strncasecmp(texture,t,32);if(!glass)return GlassPane::None;
 }
 if(std::strstr(name,"_lo")||std::strstr(name,"_vlo"))return GlassPane::None;
 const float x=std::fabs(n.x),y=std::fabs(n.y),z=std::fabs(n.z);
 // The Deluxo's raked rear glass is only 11–13 degrees above horizontal.
 // Retain it while excluding horizontal roofs and degenerate faces.
 if(x+y<1e-8f||x+y<.15f*z)return GlassPane::None;
 if(std::strstr(name,"windscreen"))return GlassPane::Front;
 if(std::strstr(name,"door_lf"))return GlassPane::LeftFront;
 if(std::strstr(name,"door_rf"))return GlassPane::RightFront;
 if(std::strstr(name,"door_lr"))return GlassPane::LeftRear;
 if(std::strstr(name,"door_rr"))return GlassPane::RightRear;
 if(std::strstr(name,"boot_hi"))return GlassPane::Rear;
 if(std::strcmp(name,"chassis_hi"))return GlassPane::None;
 if(x>y*1.2f)return p.x<0?(p.y>0?GlassPane::LeftFront:GlassPane::LeftRear):(p.y>0?GlassPane::RightFront:GlassPane::RightRear);
 return p.y>0?GlassPane::Front:GlassPane::Rear;
}
}
