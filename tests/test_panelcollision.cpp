#define PANELPHYSICS_NO_MAIN
#include "test_panelphysics.cpp"
#include "nativejetshadowarm.h"
static void *CollisionSymbol(void *,const char *);
#if __has_include("../src/panelcollision.cpp")
#define dlsym CollisionSymbol
#include "../src/panelcollision.cpp"
#undef dlsym
#else
namespace panelcollision {bool Install(){return true;}}
#endif
namespace contacttest {
using Pair=int (*)(uint8_t *,uint8_t *,uint8_t *);
using Models=int (*)(const uint8_t *,uint8_t *,const uint8_t *,uint8_t *,uint8_t *,uint8_t *,float *);
alignas(16) uint8_t ped[0x8d0]{},pedPool[24]{},carCol[0x68]{},pedCol[0x68]{},carInfo[0x80]{},pedInfo[0x80]{},points[64*40]{};
uint8_t pedFlag=1;void *pp=pedPool;uint8_t *infos[6500]{};bool recycle=false,wrong=false,poisonRecycled=false,repairExtra=false,retireReceiver=false;int original=0,supplements=0,consumer=0,planes=0;
using SphereTriangle=bool (*)(const uint8_t *,const Vec3 *,const panelphysics::Face &,const void *,uint8_t *,float &);SphereTriangle actualTriangle=nullptr;void (*actualPlane)(void *,const Vec3 &,const Vec3 &,const Vec3 &)=nullptr;
uint8_t *GetPed(int r){return r==pedFlag?ped:nullptr;}
int ModelsNative(const uint8_t *ma,uint8_t *a,const uint8_t *mb,uint8_t *b,uint8_t *out,uint8_t *,float *){
 if(b==carCol||b==pedCol){if(recycle){++fixture::carFlags[0];
#if __has_feature(address_sanitizer)
 if(poisonRecycled)__asan_poison_memory_region(fixture::cars.data(),0x6f0);
#endif
 }if(original){std::memset(out,0,40);*reinterpret_cast<float *>(out)=777;}return original;}
 if(actualTriangle){++supplements;int count=0;const auto vertices=*reinterpret_cast<Vec3 **>(b+0x50);const auto faces=*reinterpret_cast<panelphysics::Face **>(b+0x58);const int nt=*reinterpret_cast<int16_t *>(b+0x2c);const auto spheres=*reinterpret_cast<uint8_t **>(a+0x38);
  for(int i=0;i<*reinterpret_cast<int16_t *>(a+0x28);i++){alignas(16) uint8_t sphere[20]{};memcpy(sphere,spheres+i*20,20);auto &p=*reinterpret_cast<Vec3 *>(sphere);const auto m=reinterpret_cast<const float *>(ma);Vec3 world{m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};dentmodel::Local(reinterpret_cast<const float *>(mb),world,false,p);float nearest=1e9f;bool hit=false;
   for(int j=0;j<nt;j++){const auto t=faces[j];auto normal=panelphysics::Cross(panelphysics::Sub(vertices[t.v[1]],vertices[t.v[0]]),panelphysics::Sub(vertices[t.v[2]],vertices[t.v[0]]));float length=std::sqrt(panelphysics::Dot(normal,normal));if(length<1e-6f)continue;normal={normal.x/length,normal.y/length,normal.z/length};alignas(16) float plane[8]={normal.x,normal.y,normal.z,panelphysics::Dot(normal,vertices[t.v[0]])};actualPlane(plane,vertices[t.v[0]],vertices[t.v[1]],vertices[t.v[2]]);hit=actualTriangle(sphere,vertices,t,plane,out+count*40,nearest)||hit;}
   if(hit)++count;
  }return count;
 }
 ++supplements;fixture::Check(a[0x2e]==0,"supplement cannot consume or overwrite native wheel lines");const int nt=*reinterpret_cast<int16_t *>(b+0x2c);fixture::Check(nt>0&&nt<600&&*reinterpret_cast<void **>(b+0x50)&&*reinterpret_cast<void **>(b+0x58),"native solver receives bounded live panel triangles");
 static int cache;*reinterpret_cast<void **>(b+0x60)=&cache;std::memset(out,0,40);*reinterpret_cast<float *>(out)=2;*reinterpret_cast<float *>(out+0x10)=1;*reinterpret_cast<float *>(out+0x24)=.1f;if(retireReceiver){++fixture::carFlags[1];
#if __has_feature(address_sanitizer)
 __asan_poison_memory_region(fixture::cars.data()+0x6f0,0x6f0);
#endif
 }if(repairExtra)*reinterpret_cast<float *>(fixture::cars.data()+0x368+2*0x24+0xc)=0;return 1;
}
int PairNative(uint8_t *a,uint8_t *b,uint8_t *out){const auto &map=fixture::entries;const auto solve=reinterpret_cast<Models>(map.at("_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf"));auto ac=a==ped?pedCol:carCol,bc=b==ped?pedCol:carCol;const int result=solve(a+8,ac,b+8,bc,out+(wrong?40:0),nullptr,nullptr);if(result)++consumer;return result;}
void RemovePlanes(uint8_t *c){*reinterpret_cast<void **>(c+0x60)=nullptr;++planes;}
void Fresh(){paneltest::Fresh();fixture::cars[0x61]|=1;infos[145]=carInfo;infos[0]=pedInfo;game::api.modelInfoPtrs=infos;game::api.PoolsGetPed=GetPed;*reinterpret_cast<void **>(carInfo+0x20)=carCol;*reinterpret_cast<void **>(pedInfo+0x20)=pedCol;std::memset(ped,0,sizeof ped);fixture::Identity(reinterpret_cast<float *>(ped+8));*reinterpret_cast<Vec3 *>(ped+0x38)={-.7f,.5f,-.1f};*reinterpret_cast<unsigned *>(ped+0x60)=3|0x100;*reinterpret_cast<int16_t *>(pedCol+0x28)=1;*reinterpret_cast<int16_t *>(carCol+0x28)=1;static uint8_t sphere[20]{};*reinterpret_cast<float *>(sphere+12)=.25f;*reinterpret_cast<void **>(carCol+0x38)=sphere;*reinterpret_cast<void **>(pedCol+0x38)=sphere;*reinterpret_cast<float *>(fixture::cars.data()+0x368+2*0x24+0xc)=-.8f;original=supplements=consumer=0;recycle=wrong=repairExtra=retireReceiver=false;}
constexpr auto modelsKey="_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf";
constexpr auto pedKey="_ZN4CPed22ProcessEntityCollisionEP7CEntityP9CColPoint";
constexpr auto carKey="_ZN11CAutomobile22ProcessEntityCollisionEP7CEntityP9CColPoint";
uintptr_t Zero(...){return 0;}int nativeRecords=0;void Record(uint8_t *,uint8_t *){++nativeRecords;}
int Hit(bool reversed=false){return reinterpret_cast<Pair>(fixture::entries.at(pedKey))(reversed?fixture::cars.data():ped,reversed?ped:fixture::cars.data(),points);}
}
static void *CollisionSymbol(void *h,const char *s){using namespace contacttest;if(std::strstr(s,"ms_pPedPool"))return &pp;if(std::strstr(s,"RemoveTrianglePlanes"))return reinterpret_cast<void *>(RemovePlanes);return PanelSymbol(h,s);}
int main(int argc,char **){if(PanelPhysicsChecks())return 1;using namespace fixture;using namespace contacttest;*reinterpret_cast<void **>(pedPool)=ped;*reinterpret_cast<void **>(pedPool+8)=&pedFlag;*reinterpret_cast<int *>(pedPool+16)=1;
 entries[modelsKey]=reinterpret_cast<void *>(ModelsNative);entries[pedKey]=entries[carKey]=entries["_ZN9CPhysical22ProcessEntityCollisionEP7CEntityP9CColPoint"]=reinterpret_cast<void *>(PairNative);Fresh();Check(panelcollision::Install(),"panel native contact owner installs");
 Check(Hit()>0&&consumer==1&&supplements==1,"walking into an open door reaches the original native ped contact consumer");
 Fresh();Check(Hit(true)>0&&consumer==1&&supplements==1&&*reinterpret_cast<float *>(points+0x10)<0,"moving car with open door uses reversed native contact orientation");
 Fresh();const auto face=base->triangles[2];base->triangles.assign(700,face);Check(Hit()==1&&supplements==2,"complex perforated door remains collidable across native batches without duplicate impulses");
 Fresh();original=1;Check(Hit()>=1&&*reinterpret_cast<float *>(points)==777,"native original body contacts remain first and unchanged");
 Fresh();*reinterpret_cast<float *>(cars.data()+0x368+2*0x24+0xc)=0;Check(Hit()==0&&supplements==0,"closed door retains original hull collision");
 Fresh();cars[0x34c+9+2]=3;Check(Hit()==0,"missing door cannot leave an invisible collider");
 Fresh();repairExtra=true;Check(Hit()==0,"repair or closing the source hinge during native solving retires the pending panel contact");
 Fresh();recycle=true;Check(Hit()==0&&supplements==0,"native callback recycling source rejects the old panel receipt");
 if(argc>1){nativejetshadowtest::Code code;code.Add(0x10004b6d8,0x10004bc00);code.Add(0x100172e50,0x100172ec0);code.Add(0x100048ef8,0x100049008);code.Add(0x10019f0dc,0x10019f9b8);code.Add(0x100258750,0x100258b04);
 for(const auto &r:code.ranges)for(auto pc=r.first;pc<r.last;pc+=4){auto word=code.image.Word(pc);if((word&0xfc000000)==0x94000000){auto target=uint64_t(int64_t(pc)+(int64_t(int32_t(word<<6))>>4));if(!code.Address(target))code.bindings[target]=reinterpret_cast<void *>(Zero);}}
 code.bindings[0x10004c40c]=entries[modelsKey];code.bindings[0x1001557f4]=reinterpret_cast<void *>(Record);code.Finish();nativejetshadowtest::Store(code.Page(0x10058d674),1.f);nativejetshadowtest::Store(code.Page(0x1006b2050),pedInfo);nativejetshadowtest::Store(code.Page(0x1006b2050+145*8),carInfo);
actualTriangle=reinterpret_cast<SphereTriangle>(code.Address(0x10004b6d8));actualPlane=reinterpret_cast<decltype(actualPlane)>(code.Address(0x100048ef8));Fresh();Check(Hit()>0&&consumer==1,"actual native sphere/triangle solver blocks a walking body on the current door sheet");Fresh();*reinterpret_cast<Vec3 *>(ped+0x38)={3,.5f,-.1f};Check(Hit()==0,"actual native solver leaves space beyond the open panel clear");auto oldPed=panelcollision::ped;panelcollision::ped=reinterpret_cast<Pair>(code.Address(0x10019f0dc));Fresh();*reinterpret_cast<uint64_t *>(ped+0x188)|=uint64_t(1)<<48;nativeRecords=0;Check(Hit()>0&&nativeRecords==2,"actual full native CPed caller accepts panel contact and records both physical actors");panelcollision::ped=oldPed;
 auto oldCar=panelcollision::automobile;panelcollision::automobile=reinterpret_cast<Pair>(code.Address(0x100258750));Fresh();*reinterpret_cast<Vec3 *>(cars.data()+0x6f0+0x38)={-.7f,.5f,-.1f};nativeRecords=0;Check(reinterpret_cast<Pair>(entries[carKey])(cars.data()+0x6f0,cars.data(),points)>0&&nativeRecords==2,"actual full native CAutomobile caller accepts open-door contact from a second car");panelcollision::automobile=oldCar;
 actualTriangle=nullptr;}

#if __has_feature(address_sanitizer)
 Fresh();retireReceiver=true;Check(reinterpret_cast<Pair>(entries[carKey])(cars.data()+0x6f0,cars.data(),points)==0,"retired receiving car cannot become the second panel source");__asan_unpoison_memory_region(cars.data()+0x6f0,0x6f0);retireReceiver=false;
 Fresh();recycle=poisonRecycled=true;Check(Hit()==0,"retired source allocation is not dereferenced after native callback");__asan_unpoison_memory_region(cars.data(),0x6f0);poisonRecycled=false;
#endif
 Fresh();gConfig.carParts=false;Check(Hit()==0,"runtime off preserves original contacts");gConfig.carParts=true;
 Check(planes>0,"each temporary native triangle-plane cache is unlinked before its storage expires");Reset();printf("panel collision: %d failures\n",failed);return failed?1:0;
}
