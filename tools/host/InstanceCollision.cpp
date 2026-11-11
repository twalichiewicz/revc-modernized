// Source-host per-instance geometry without changing native object layouts.
#include "common.h"
#include "Entity.h"
#include "World.h"
#include <unordered_map>
#include <cmath>
namespace {
struct Record {CColModel *col=nil;uint64 revision=0;bool resident=false;int model=-1;};
// Process-lifetime registry avoids static destruction order with engine globals.
std::unordered_map<const CEntity*,Record> &Records(){static auto *store=new std::unordered_map<const CEntity*,Record>;return *store;}
}
CColModel *SourceInstanceCollision(const CEntity *entity){auto it=Records().find(entity);return it!=Records().end()&&it->second.model==entity->GetModelIndex()?it->second.col:nil;}
void SourceCollisionConstructed(CEntity *entity){auto it=Records().find(entity);if(it!=Records().end()){assert(!it->second.col);Records().erase(it);}Records().emplace(entity,Record{});}
void SourceCollisionDestroyed(CEntity *entity){auto it=Records().find(entity);if(it==Records().end())return;assert(!it->second.col||!it->second.resident);delete it->second.col;Records().erase(it);}
bool SourceCollisionIsResident(const CEntity *entity){auto it=Records().find(entity);return it!=Records().end()&&it->second.resident;}
void SourceCollisionResident(CEntity *entity,bool resident){Records()[entity].resident=resident;}
bool CEntity::CanReplaceInstanceCollision(CColModel *model) const{
 auto &state=Records()[this];
 if(m_modelIndex<0||m_modelIndex>=MODELINFOSIZE||!CModelInfo::GetModelInfo(m_modelIndex)||!model||!model->ownsCollisionVolumes||model==const_cast<CEntity*>(this)->GetColModel()||state.revision==~uint64(0))return false;
 const auto lo=model->boundingBox.min,hi=model->boundingBox.max;
 if(!std::isfinite(lo.x)||!std::isfinite(lo.y)||!std::isfinite(lo.z)||!std::isfinite(hi.x)||!std::isfinite(hi.y)||!std::isfinite(hi.z)||lo.x>hi.x||lo.y>hi.y||lo.z>hi.z)return false;
 return true;
}
bool CEntity::ReplaceInstanceCollision(CColModel *model){
 if(!CanReplaceInstanceCollision(model))return false;auto &state=Records()[this];
 bool resident=state.resident;if(resident)CWorld::Remove(this);
 auto old=state.col;state.col=model;state.model=m_modelIndex;++state.revision;
 if(resident)CWorld::Add(this);delete old;return true;
}
uint64 CEntity::InstanceCollisionRevision()const{auto it=Records().find(this);return it==Records().end()?0:it->second.revision;}

void CEntity::ClearInstanceCollision(){auto it=Records().find(this);if(it==Records().end()||!it->second.col)return;bool resident=it->second.resident;if(resident)CWorld::Remove(this);auto old=it->second.col;it->second.col=nil;it->second.model=-1;if(it->second.revision!=~uint64(0))++it->second.revision;if(resident)CWorld::Add(this);delete old;}
