#pragma once
#include "game.h"
#include <algorithm>
#include <cmath>
#include <iterator>
namespace jetboarding {
inline CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline CVector Mul(CVector a,float s){return {a.x*s,a.y*s,a.z*s};}
struct Key {float time;CVector at;};
template<size_t N>inline CVector Interpolate(const Key (&keys)[N],float progress){
 for(size_t i=1;i<N;++i)if(progress<=keys[i].time){const auto &a=keys[i-1],&b=keys[i];return Add(a.at,Mul(Sub(b.at,a.at),std::clamp((progress-a.time)/(b.time-a.time),0.0f,1.0f)));}
 return keys[N-1].at;
}
// Original ped.ifp CAR_getin_LHS Root track. Native flags0x18 do not
// extract XY; the bounded route replaces that motion in the actor frame.
// The real-asset fixture validates every key and native extraction flags.
inline CVector Root(float progress){
 const Key keys[]={
 {0,{.002034493f,-.002878077f,-.020203412f}}, {.05f,{.071763240f,.124189228f,-.055188656f}},
 {.15f,{.158313155f,.203599945f,-.117916882f}}, {.20f,{.208559468f,.250946611f,-.122911811f}},
 {.25f,{.267438978f,.307135105f,-.068158448f}}, {.30f,{.328235954f,.367444545f,.016804097f}},
 {.35f,{.389292926f,.410691202f,.048148274f}}, {.40f,{.492592663f,.452809751f,.015310643f}},
 {.45f,{.605708003f,.496199608f,-.033284489f}}, {.50f,{.717967570f,.538653314f,-.092966795f}},
 {.55f,{.818716466f,.577969968f,-.158788264f}}, {.60f,{.897284329f,.611942351f,-.225740850f}},
 {.65f,{.925874889f,.639465928f,-.288771749f}}, {.70f,{.912607670f,.656229794f,-.343141794f}},
 {.75f,{.900702178f,.655225813f,-.383848727f}}, {.80f,{.904720366f,.634929240f,-.375690579f}},
 {.95f,{.919717431f,.535658360f,-.330572367f}}, {1,{.922743499f,.514147997f,-.321537256f}}};
 return Interpolate(keys,progress);
}
inline float Yaw(float progress){
 const float in=std::clamp(progress/.15f,0.0f,1.0f),out=std::clamp((progress-.8f)/.2f,0.0f,1.0f);
 return .7853981634f*in*(1-out);
}
inline CVector Rotate(CVector p,float yaw){const float c=std::cos(yaw),s=std::sin(yaw);return {p.x*c+p.y*s,-p.x*s+p.y*c,p.z};}
inline CVector Route(float progress,CVector outside){
 // Logical body roots, measured against the opened door's three integrated
 // tread surfaces and low jamb. The original lean/leg movement is retained.
 const Key route[]={{0,outside},{.10f,{-1.60f,3.80f,-.75f}},{.15f,{-1.29f,3.85f,-.797f}},
  {.18f,{-1.29f,3.78f,-.60f}},{.21f,{-1.1123f,3.4938f,-.6587f}},
  {.27f,{-1.14f,3.55f,-.40f}},{.30f,{-1.10f,3.50f,-.4648f}},
  {.33f,{-.96f,3.48f,-.27f}},{.40f,{-.76f,3.44f,-.24f}},{.43f,{-.68f,3.43f,-.27f}},
  {.45f,{-.455f,3.416f,-.285f}},{.60f,{-.24f,3.303f,-.20f}},{.70f,{-.10f,3.342f,-.15f}},
  {.80f,{-.10f,3.4f,-.30f}},{.90f,{-.18f,4.35f,-.32f}},{1,{-.32f,5,-.33174094f}}};
 CVector at=Interpolate(route,progress),root=Rotate(Root(progress),Yaw(progress));at.x-=root.x;at.y-=root.y;return at;
}
inline CVector SitOffset(){const auto root=Root(1);return {-root.x,-root.y,-.003278195858f-root.z};}
}

namespace jetboarding {
constexpr float kEntryEnd=.8f,kFloor=-.93f,kAnkle=.095f,kHipDrop=.46f;
constexpr float kCrouchSeconds=.85f,kSitSeconds=.75f,kWalkSpeed=.55f,kStep=.30f;
constexpr CVector kStandRoot{-.002666f,-.000728f,-.022486f};
constexpr CVector kWalkStart{0,3.83f,.11f},kWalkEnd{-.32f,5,.11f},kSeatRoot{-.32f,5,-.65f};
inline float Smooth(float x){x=std::clamp(x,0.0f,1.0f);return x*x*(3-2*x);}
inline CVector Lerp(CVector a,CVector b,float t){return Add(a,Mul(Sub(b,a),t));}
inline float WalkLength(){return std::hypot(kWalkEnd.x-kWalkStart.x,kWalkEnd.y-kWalkStart.y);}
inline CVector WalkWay(){return Mul(Sub(kWalkEnd,kWalkStart),1/WalkLength());}
inline float WalkYaw(){const auto way=WalkWay();return std::atan2(way.x,way.y);}
inline CVector CrouchRoot(){return Add(kStandRoot,{0,0,-kHipDrop});}
// Measured original player.dff CAR_getin_LHS at progress.8. Both ankles
// have cleared the jamb here; the original animation stops at this point.
inline CVector EntryFoot(int leg){const CVector raw=leg==0?CVector{.3829190305f,.8705940738f,-.5064060448f}:CVector{1.0857947598f,1.2654108450f,-.3926754388f};return Add(Route(kEntryEnd,{-2.25f,3.7f,-1.234784f}),Rotate(raw,Yaw(kEntryEnd)));}
inline CVector EntryRoot(){const auto raw=Root(kEntryEnd);return Add(Route(kEntryEnd,{-2.25f,3.7f,-1.234784f}),Rotate(raw,Yaw(kEntryEnd)));}
inline CVector WalkRoot(float distance){return Add(kWalkStart,Mul(WalkWay(),std::clamp(distance,0.0f,WalkLength())));}
struct Foot {CVector at;bool planted;};
inline Foot WalkFoot(float distance,int leg){
 const auto way=WalkWay();const CVector right{way.y,-way.x,0};const float offset=leg==0?0:.5f,cycle=distance/(2*kStep)+offset;
 const float turn=std::floor(cycle),u=cycle-turn,start=(turn-offset)*2*kStep+kStep*.5f;
 float along=start,lift=0;
 if(u>=.5f){const float swing=(u-.5f)*2;along+=2*kStep*Smooth(swing);lift=.07f*std::sin(3.141592653589793f*swing);}
 auto at=Add(Add(kWalkStart,Mul(way,along)),Mul(right,leg==0?-.12f:.12f));at.z=kFloor+kAnkle+lift;return {at,u<.5f};
}
inline CVector CrouchFoot(float blend,int leg){
 // The initially raised right foot lands while the left stays planted;
 // then the left takes its first short step into the walking stance.
 const float u=leg==0?std::clamp((blend-.5f)*2,0.0f,1.0f):std::clamp(blend*2,0.0f,1.0f);
 auto at=Lerp(EntryFoot(leg),WalkFoot(0,leg).at,Smooth(u));at.z=std::max(kFloor+kAnkle,at.z)+.055f*std::sin(3.141592653589793f*u);return at;
}
inline CVector CrouchKey(float blend){return Lerp(Root(kEntryEnd),CrouchRoot(),blend);}
inline float CrouchYaw(float blend){return Yaw(kEntryEnd)+(WalkYaw()-Yaw(kEntryEnd))*blend;}
inline CVector CrouchActor(float blend){
 const auto hip=Lerp(EntryRoot(),Add(kWalkStart,Rotate(CrouchRoot(),WalkYaw())),blend);return Sub(hip,Rotate(CrouchKey(blend),CrouchYaw(blend)));
}
inline CVector SeatFoot(int leg){return Add(kSeatRoot,leg==0?CVector{-.035126678f,.805600244f,-.144632562f}:CVector{.173667423f,.797437403f,-.126424963f});}
inline CVector SitFoot(float blend,int leg){auto at=Lerp(WalkFoot(WalkLength(),leg).at,SeatFoot(leg),blend);at.z+=.045f*std::sin(3.141592653589793f*blend);return at;}
inline CVector SitActor(float blend){return Lerp(kWalkEnd,kSeatRoot,blend);}
inline CVector SitKey(float blend){return Lerp(CrouchRoot(),{0,0,-.003278195858f},blend);}
struct CabinPose {
 enum class Mode {Crouch,Walk,Sit} mode=Mode::Crouch;
 CVector root{},feet[2]{};float blend=0,distance=0;
 int pedRef=-1;uint64_t serial=0;
};
}
