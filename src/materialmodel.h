#pragma once
#include "mechanicalmodel.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
namespace materialmodel {
// Accepted impulse times average signed closing velocity. A rebounding
// contact may return energy; only nonnegative dissipated work damages material.
inline float ImpulseWork(float impulse,float closingBefore,float outwardAfter){
 if(!std::isfinite(impulse)||impulse<=0||!std::isfinite(closingBefore)||closingBefore<0||!std::isfinite(outwardAfter))return 0;
 return std::max(0.f,.5f*impulse*(closingBefore-outwardAfter));
}
constexpr size_t kLayers=64;
enum class Material {Body,Glass,Rubber,Armor,ArmoredGlass,Skin};
inline bool Armored(int model){return model==157||model==158;}
inline Material Surface(int model,Material m){return Armored(model)?m==Material::Body?Material::Armor:m==Material::Glass?Material::ArmoredGlass:m:m;}
inline float Cost(Material m,float incidence){const float costs[]={12,4,6,100,65,3};const int i=int(m);return i>=0&&i<6?costs[i]/std::max(.05f,incidence):1000.f;}
struct Layer {float fraction=0,incidence=1;Material material=Material::Body;float resistanceScale=1;};
// Fractions refer to the original segment. Entry is the real accepted surface;
// limit is nearer external cover. Budgets use calibrated native damage units.
struct Ray {Vec3 from{},to{};float entry=0,limit=1,budget=0;const Layer *layers=nullptr;size_t count=0;float areaScale=1;};
struct Structure {std::array<mechanicalmodel::Box,3> volumes{};std::array<float,3> resistance{},capacity{};};
struct Result {std::array<float,3> structuralDoses{};std::array<float,mechanicalmodel::kModules> doses{};std::array<float,kLayers> layerRemaining{},layerLoss{};float remaining=0,deposited=0,surfaceLoss=0;bool valid=false;};
namespace detail {
inline bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool BoxHit(const mechanicalmodel::Box &b,Vec3 p,Vec3 d,float &near,float &far)
{
 near=0;far=1;const float a[]={p.x,p.y,p.z},v[]={d.x,d.y,d.z},lo[]={b.lo.x,b.lo.y,b.lo.z},hi[]={b.hi.x,b.hi.y,b.hi.z};
 for(int i=0;i<3;i++){
  if(std::fabs(v[i])<1e-7f){if(a[i]<lo[i]||a[i]>hi[i])return false;continue;}
  float l=(lo[i]-a[i])/v[i],h=(hi[i]-a[i])/v[i];if(l>h)std::swap(l,h);near=std::max(near,l);far=std::min(far,h);if(near>=far)return false;
 }
 return true;
}
inline bool ValidBox(const mechanicalmodel::Box &b){return Finite(b.lo)&&Finite(b.hi)&&b.lo.x<b.hi.x&&b.lo.y<b.hi.y&&b.lo.z<b.hi.z;}
}
using detail::Finite;using detail::BoxHit;using detail::ValidBox;
inline Result Absorb(const mechanicalmodel::Box &body,const std::array<mechanicalmodel::Box,mechanicalmodel::kModules> &volumes,const float *resistance,const float *capacity,const Ray &ray,const Structure *structure=nullptr)
{
 Result out;
 constexpr size_t systems=mechanicalmodel::kModules,maximum=systems+3;const size_t modules=systems+(structure?3:0);
 const auto volume=[&](size_t i)->const mechanicalmodel::Box &{return i<systems?volumes[i]:structure->volumes[i-systems];};
 const auto resistanceAt=[&](size_t i){return (i<systems?resistance[i]:structure->resistance[i-systems])*ray.areaScale;};
 const auto capacityAt=[&](size_t i){return i<systems?capacity[i]:structure->capacity[i-systems];};
 if(!Finite(ray.from)||!Finite(ray.to)||!std::isfinite(ray.entry)||!std::isfinite(ray.limit)||ray.entry<0||ray.limit>1||ray.limit<ray.entry||
    !std::isfinite(ray.areaScale)||ray.areaScale<=0||ray.areaScale>256||!std::isfinite(ray.budget)||ray.budget<=0||ray.count>kLayers||(ray.count&&!ray.layers)||!ValidBox(body))return out;
 const Vec3 d{ray.to.x-ray.from.x,ray.to.y-ray.from.y,ray.to.z-ray.from.z};const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
 if(!std::isfinite(length)||length<1e-6f)return out;
 std::array<Layer,kLayers> layers;size_t n=0;
 for(size_t i=0;i<ray.count;i++){const auto l=ray.layers[i];if(!std::isfinite(l.fraction)||l.fraction<0||l.fraction>1||!std::isfinite(l.incidence)||l.incidence<0||l.incidence>1||!std::isfinite(l.resistanceScale)||l.resistanceScale<=0||l.resistanceScale>1000||int(l.material)<0||int(l.material)>5)return out;layers[n++]=l;}
 std::sort(layers.begin(),layers.begin()+n,[](const Layer&a,const Layer&b){return a.fraction<b.fraction;});
 // Shared edges/double-sided faces can return the same sheet more than once.
 size_t unique=0;for(size_t i=0;i<n;i++){
  bool same=false;for(size_t j=unique;j>0&&std::fabs(layers[i].fraction-layers[j-1].fraction)*length<=.001f;j--)if(layers[i].material==layers[j-1].material){layers[j-1].incidence=std::min(layers[j-1].incidence,layers[i].incidence);layers[j-1].resistanceScale=std::max(layers[j-1].resistanceScale,layers[i].resistanceScale);same=true;break;}
  if(!same)layers[unique++]=layers[i];
 }n=unique;
 float near=ray.entry,far=ray.limit;out.valid=true;out.remaining=std::min(ray.budget,1000.f);if(near>=far)return out;
 // Visible bumpers/panels can extend beyond the original COL body bounds.
 // Charge those entry surfaces before reaching any internal module volume.
 std::array<float,3+2*maximum+kLayers> cuts{};size_t count=0;cuts[count++]=ray.entry;cuts[count++]=near;cuts[count++]=far;
 std::array<float,maximum> enter{},exit{};
 for(size_t i=0;i<modules;i++){
  if(!ValidBox(volume(i))||!std::isfinite(resistanceAt(i))||resistanceAt(i)<0||!std::isfinite(capacityAt(i))||capacityAt(i)<=0)return {};
  float a,b;if(BoxHit(volume(i),ray.from,d,a,b)){enter[i]=std::max(near,a);exit[i]=std::min(far,b);if(enter[i]<exit[i]){cuts[count++]=enter[i];cuts[count++]=exit[i];}}
 }
 for(size_t i=0;i<n;i++)if(layers[i].fraction>=ray.entry-1e-6f&&layers[i].fraction<=far)cuts[count++]=std::max(ray.entry,layers[i].fraction);
 std::sort(cuts.begin(),cuts.begin()+count);size_t layer=0;while(layer<n&&layers[layer].fraction<ray.entry-1e-6f)++layer;
 for(size_t k=0;k+1<count&&out.remaining>0;k++){
  const float t=cuts[k];while(layer<n&&layers[layer].fraction<=t+1e-6f){const auto &l=layers[layer++];
   const float loss=std::min(out.remaining,Cost(l.material,l.incidence)*l.resistanceScale);out.surfaceLoss+=loss;out.remaining-=loss;bool charged=false;for(size_t j=0;j<ray.count;j++)if(ray.layers[j].material==l.material&&std::fabs(ray.layers[j].fraction-l.fraction)*length<=.001f){out.layerRemaining[j]=out.remaining;if(!charged){out.layerLoss[j]+=loss;charged=true;}}}
  const float next=cuts[k+1],middle=(t+next)*.5f;float rate=0;
  for(size_t i=0;i<modules;i++)if(enter[i]<exit[i]&&middle>=enter[i]&&middle<exit[i])rate+=resistanceAt(i);
  if(!std::isfinite(rate))return {};
  if(rate<=0||next<=t||out.remaining<=0)continue;
  const float absorbed=std::min(out.remaining,(next-t)*length*rate);out.remaining-=absorbed;out.deposited+=absorbed;
  for(size_t i=0;i<modules;i++)if(enter[i]<exit[i]&&middle>=enter[i]&&middle<exit[i]){auto &dose=i<systems?out.doses[i]:out.structuralDoses[i-systems];dose=std::min(1.f,dose+absorbed*(resistanceAt(i)/rate)/capacityAt(i));}
 }
 return out;
}
inline Result Trace(const mechanicalmodel::Layout &layout,const Ray &ray){
 static constexpr float resistance[]={80,8,16,16,16,16,12,12,12,12,20,70,6,10};
 static constexpr float capacity[]={120,10,40,40,40,40,12,12,12,12,35,100,12,15};
 return Absorb(layout.body,layout.volumes,resistance,capacity,ray,nullptr);
}
// Native impulse/mass gives contact speed. Work grows with its square; broad
// contacts distribute that work into the nearby material instead of making
// independent injury doses for every intersected system. Units retain the
// existing calibrated projectile budget; these are not claimed to be SI joules.
// A contact patch loads four times the reference projectile cross-section.
// Both mechanisms still spend the same bounded work budget in the solver.
inline Ray ContactRay(Vec3 point,Vec3 inward,float severity){
 const float length=std::sqrt(inward.x*inward.x+inward.y*inward.y+inward.z*inward.z);
 if(!Finite(point)||!Finite(inward)||!std::isfinite(severity)||severity<=4.5f||!std::isfinite(length)||length<1e-6f)return {};
 const float reach=std::min(1.f,.08f+severity*.035f),work=std::min(1000.f,.3f*(severity-4.5f)*(severity-4.5f));
 return {point,{point.x+inward.x/length*reach,point.y+inward.y/length*reach,point.z+inward.z/length*reach},0,1,work,nullptr,0,4};
}
inline Result Contact(const mechanicalmodel::Layout &layout,Vec3 point,Vec3 inward,float severity){
 auto distributed=layout;
 for(auto &b:distributed.volumes){b.lo={b.lo.x-.25f,b.lo.y-.25f,b.lo.z-.25f};b.hi={b.hi.x+.25f,b.hi.y+.25f,b.hi.z+.25f};}
 // Contact patch can start just outside the broad body bounds.
 distributed.body.lo={std::min(layout.body.lo.x,point.x-.001f),std::min(layout.body.lo.y,point.y-.001f),std::min(layout.body.lo.z,point.z-.001f)};
 distributed.body.hi={std::max(layout.body.hi.x,point.x+.001f),std::max(layout.body.hi.y,point.y+.001f),std::max(layout.body.hi.z,point.z+.001f)};
 return Trace(distributed,ContactRay(point,inward,severity));
}
}
