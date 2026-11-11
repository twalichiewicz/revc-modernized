#include "common.h"
#include "DodoRig.h"
#include "GameplayWing.h"
#include "Plane.h"
#include "Pools.h"
#include "Streaming.h"
#include "ModelInfo.h"
#include <dlfcn.h>
#include <cstdio>
#include <algorithm>
#include <vector>
namespace {
struct Rig {CPlane *plane=nil;int reference=-1;RpClump *clump=nil;};std::vector<Rig> rigs;
CBaseModelInfo *donor=nil;bool modelPin=false,texturePin=false;int txd=-1;
bool Live(const Rig &rig){return rig.plane&&CPools::GetVehicle(rig.reference)==rig.plane&&rig.plane->GetModelIndex()==181&&rig.plane->m_rwObject&&rig.plane->GetClump()==rig.clump;}
void ReleasePins(){if(modelPin)CStreaming::SetModelIsDeletable(227);if(texturePin&&txd>=0)CStreaming::SetModelIsDeletable(6500+txd);modelPin=texturePin=false;}
}
bool DodoRigReady(CPlane *plane){return std::any_of(rigs.begin(),rigs.end(),[&](const Rig&r){return r.plane==plane&&Live(r);});}
void DodoRigDeleting(const CEntity *entity){rigs.erase(std::remove_if(rigs.begin(),rigs.end(),[&](const Rig&r){return r.plane==entity;}),rigs.end());}
void DodoRigNeutralWing(CPlane *plane){static auto neutral=reinterpret_cast<void(*)(void*)>(dlsym(RTLD_DEFAULT,"VCSourceNeutralDodoWing"));if(neutral&&DodoRigReady(plane))neutral(plane->GetClump());}
void DodoRigShutdown(){rigs.clear();ReleasePins();if(donor&&CModelInfo::GetModelInfo(227)==donor)donor->RemoveRef();donor=nil;txd=-1;}
void DodoRigFrame(){
 static auto eligible=reinterpret_cast<bool(*)(const void*)>(dlsym(RTLD_DEFAULT,"VCSourceAircraftEligible"));
 static auto prepare=reinterpret_cast<bool(*)(void*,void*)>(dlsym(RTLD_DEFAULT,"VCSourcePrepareDodo"));
 if(!eligible||!prepare)return;
 std::vector<CPlane*> pending;auto pool=CPools::GetVehiclePool();if(!pool)return;
 rigs.erase(std::remove_if(rigs.begin(),rigs.end(),[](const Rig&r){return !Live(r);}),rigs.end());
 for(int i=0;i<pool->GetSize();++i){auto v=pool->GetSlot(i);if(v&&v->GetModelIndex()==181&&v->IsPlane()&&v->m_rwObject&&eligible(v)&&!DodoRigReady((CPlane*)v))pending.push_back((CPlane*)v);}
 if(pending.empty()){if(rigs.empty())DodoRigShutdown();return;}
 auto model=CModelInfo::GetModelInfo(227);if(!model||model->GetModelType()!=MITYPE_VEHICLE)return;
 txd=model->GetTxdSlot();if(txd<0)return;
 if(!model->GetRwObject()){
  auto &texture=CStreaming::ms_aInfoForModel[6500+txd],&info=CStreaming::ms_aInfoForModel[227];
  bool oldModel=info.m_flags&STREAMFLAGS_DONT_REMOVE,oldTexture=texture.m_flags&STREAMFLAGS_DONT_REMOVE;
  CStreaming::RequestModel(6500+txd,texture.m_flags|STREAMFLAGS_DONT_REMOVE);CStreaming::RequestModel(227,info.m_flags|STREAMFLAGS_DONT_REMOVE);
  modelPin|=!oldModel&&(info.m_flags&STREAMFLAGS_DONT_REMOVE);texturePin|=!oldTexture&&(texture.m_flags&STREAMFLAGS_DONT_REMOVE);return;
 }
 if(!donor){model->AddRef();donor=model;}ReleasePins();
 for(auto plane:pending){plane->GetMatrix().UpdateRW();plane->UpdateRwFrame();
 if(!GameplayWingOwns(plane)&&prepare(plane->GetClump(),model->GetRwObject())){Rig rig;rig.plane=plane;rig.reference=CPools::GetVehicleRef(plane);rig.clump=plane->GetClump();rigs.push_back(rig);std::fprintf(stderr,"[dodo-rig] prepared ref=%d private_clump=%p\n",CPools::GetVehicleRef(plane),(void*)plane->GetClump());}}
}
