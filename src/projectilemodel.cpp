#include "projectilemodel.h"
#include <algorithm>
#include <cmath>
namespace projectilemodel {namespace {
bool Finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool BoxHit(const mechanicalmodel::Box &b,Vec3 p,Vec3 d,float &near,float &far)
{
 near=0;far=1;const float a[]={p.x,p.y,p.z},v[]={d.x,d.y,d.z},lo[]={b.lo.x,b.lo.y,b.lo.z},hi[]={b.hi.x,b.hi.y,b.hi.z};
 for(int i=0;i<3;i++){
  if(std::fabs(v[i])<1e-7f){if(a[i]<lo[i]||a[i]>hi[i])return false;continue;}
  float l=(lo[i]-a[i])/v[i],h=(hi[i]-a[i])/v[i];if(l>h)std::swap(l,h);near=std::max(near,l);far=std::min(far,h);if(near>=far)return false;
 }
 return true;
}
bool ValidBox(const mechanicalmodel::Box &b){return Finite(b.lo)&&Finite(b.hi)&&b.lo.x<b.hi.x&&b.lo.y<b.hi.y&&b.lo.z<b.hi.z;}
constexpr float resistance[]={80,8,16,16,16,16,12,12,12,12,20,70,6,10};
constexpr float capacity[]={120,10,40,40,40,40,12,12,12,12,35,100,12,15};
static_assert(sizeof(resistance)/sizeof(float)==mechanicalmodel::kModules);
}
Result Absorb(const mechanicalmodel::Box &body,const std::array<mechanicalmodel::Box,mechanicalmodel::kModules> &volumes,const float *resistance,const float *capacity,const Ray &ray,const Structure *structure)
{
 Result out;
 constexpr size_t systems=mechanicalmodel::kModules,maximum=systems+3;const size_t modules=systems+(structure?3:0);
 const auto volume=[&](size_t i)->const mechanicalmodel::Box &{return i<systems?volumes[i]:structure->volumes[i-systems];};
 const auto resistanceAt=[&](size_t i){return i<systems?resistance[i]:structure->resistance[i-systems];};
 const auto capacityAt=[&](size_t i){return i<systems?capacity[i]:structure->capacity[i-systems];};
 if(!Finite(ray.from)||!Finite(ray.to)||!std::isfinite(ray.entry)||!std::isfinite(ray.limit)||ray.entry<0||ray.limit>1||ray.limit<ray.entry||
    !std::isfinite(ray.budget)||ray.budget<=0||ray.count>kLayers||(ray.count&&!ray.layers)||!ValidBox(body))return out;
 const Vec3 d{ray.to.x-ray.from.x,ray.to.y-ray.from.y,ray.to.z-ray.from.z};const float length=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
 if(!std::isfinite(length)||length<1e-6f)return out;
 std::array<Layer,kLayers> layers;size_t n=0;
 for(size_t i=0;i<ray.count;i++){const auto l=ray.layers[i];if(!std::isfinite(l.fraction)||l.fraction<0||l.fraction>1||!std::isfinite(l.incidence)||l.incidence<0||l.incidence>1||int(l.material)<0||int(l.material)>5)return out;layers[n++]=l;}
 std::sort(layers.begin(),layers.begin()+n,[](const Layer&a,const Layer&b){return a.fraction<b.fraction;});
 // Shared edges/double-sided faces can return the same sheet more than once.
 size_t unique=0;for(size_t i=0;i<n;i++){
  bool same=false;for(size_t j=unique;j>0&&std::fabs(layers[i].fraction-layers[j-1].fraction)*length<=.001f;j--)if(layers[i].material==layers[j-1].material){layers[j-1].incidence=std::min(layers[j-1].incidence,layers[i].incidence);same=true;break;}
  if(!same)layers[unique++]=layers[i];
 }n=unique;
 float near,far;out.valid=true;out.remaining=std::min(ray.budget,1000.f);
 if(!BoxHit(body,ray.from,d,near,far))return out;near=std::max(near,ray.entry);far=std::min(far,ray.limit);if(near>=far)return out;
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
   const float loss=std::min(out.remaining,Cost(l.material,l.incidence));out.surfaceLoss+=loss;out.remaining-=loss;for(size_t j=0;j<ray.count;j++)if(ray.layers[j].material==l.material&&std::fabs(ray.layers[j].fraction-l.fraction)*length<=.001f)out.layerRemaining[j]=out.remaining;}
  const float next=cuts[k+1],middle=(t+next)*.5f;float rate=0;
  for(size_t i=0;i<modules;i++)if(enter[i]<exit[i]&&middle>=enter[i]&&middle<exit[i])rate+=resistanceAt(i);
  if(!std::isfinite(rate))return {};
  if(rate<=0||next<=t||out.remaining<=0)continue;
  const float absorbed=std::min(out.remaining,(next-t)*length*rate);out.remaining-=absorbed;out.deposited+=absorbed;
  for(size_t i=0;i<modules;i++)if(enter[i]<exit[i]&&middle>=enter[i]&&middle<exit[i]){auto &dose=i<systems?out.doses[i]:out.structuralDoses[i-systems];dose=std::min(1.f,dose+absorbed*(resistanceAt(i)/rate)/capacityAt(i));}
 }
 return out;
}
Result Doses(const mechanicalmodel::Layout &layout,const Ray &ray){return Absorb(layout.body,layout.volumes,resistance,capacity,ray);}

}
