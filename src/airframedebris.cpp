#include "airframedebris.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace airframedebris {namespace {
struct Native {
 void *(*allocate)(size_t);void (*construct)(uint8_t *),(*destroy)(uint8_t *),(*setModel)(uint8_t *,unsigned),(*referenceModel)(uint8_t *,int),(*attach)(uint8_t *,void *),(*add)(uint8_t *);
 int (*reference)(uint8_t *);int16_t *temporary;
 void *(*createClump)(),*(*createFrame)(),*(*createAtomic)(),*(*setClumpFrame)(void *,void *),*(*addChild)(void *,void *),*(*addAtomic)(void *,void *),*(*setAtomicFrame)(void *,void *),*(*setGeometry)(void *,void *,unsigned),*(*setFlags)(void *,unsigned),*(*update)(void *);
 int (*destroyClump)(void *),(*destroyFrame)(void *),(*destroyAtomic)(void *),(*verticesCount)(const void *);
 void *(*morph)(const void *,int);const Vec3 *(*vertices)(const void *);float *(*matrix)(void *);void (*renderCallback)(void *,void *);
} n;bool gReady;
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
Vec3 Point(const float *m,Vec3 p){return {m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};}
struct Clump {void *value=nullptr;~Clump(){if(value)n.destroyClump(value);}};
}
bool Emit(uint8_t *v,const Piece *pieces,size_t count)
{
 if(!gReady||!v||!pieces||!count||count>64||!game::api.PoolsGetVehicle||!game::api.timeMs||!game::IsAircraft(v)||!game::api.modelInfoPtrs||*n.temporary<0||*n.temporary>=40||!game::api.modelInfoPtrs[245])return false;
 const int ref=n.reference(v),model=game::EntityModel(v);const auto clump=game::EntityClump(v);const auto table=*reinterpret_cast<void **>(v);std::array<float,16> pose{};std::memcpy(pose.data(),v+8,sizeof pose);
 for(int row=0;row<4;row++)if(!Finite({pose[size_t(row)*4],pose[size_t(row)*4+1],pose[size_t(row)*4+2]}))return false;
 if(!Finite(*reinterpret_cast<Vec3 *>(v+0x84))||!Finite(*reinterpret_cast<Vec3 *>(v+0x90)))return false;
 if(ref<0||game::api.PoolsGetVehicle(ref)!=v||model<0||model>=6500||!game::api.modelInfoPtrs[model])return false;
 auto current=[&](){return game::api.PoolsGetVehicle(ref)==v&&game::EntityModel(v)==model&&game::EntityClump(v)==clump&&*reinterpret_cast<void **>(v)==table&&!std::memcmp(pose.data(),v+8,sizeof pose);};
 Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};size_t total=0;
 for(size_t i=0;i<count;i++){
  const auto &part=pieces[i];if(!part.geometry)return false;const int nv=n.verticesCount(part.geometry);auto m=n.morph(part.geometry,0);const auto vertices=m?n.vertices(m):nullptr;if(nv<3||nv>65535||!vertices||total+size_t(nv)>262144)return false;total+=size_t(nv);
  for(int k=0;k<nv;k++){const auto p=Point(part.transform.data(),vertices[k]);if(!Finite(p))return false;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
 }
 const Vec3 centre{(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f,(lo.z+hi.z)*.5f};Clump assembly{n.createClump()};if(!assembly.value)return false;void *root=n.createFrame();if(!root)return false;n.setClumpFrame(assembly.value,root);
 float *world=n.matrix(root);std::memcpy(world,pose.data(),sizeof pose);const auto at=Point(pose.data(),centre);world[12]=at.x;world[13]=at.y;world[14]=at.z;
 for(size_t i=0;i<count;i++){
  void *frame=n.createFrame();if(!frame)return false;void *atomic=n.createAtomic();if(!atomic){n.destroyFrame(frame);return false;}
  auto m=n.matrix(frame);std::memcpy(m,pieces[i].transform.data(),sizeof pieces[i].transform);m[12]-=centre.x;m[13]-=centre.y;m[14]-=centre.z;
  n.addChild(root,frame);n.setAtomicFrame(atomic,frame);n.setGeometry(atomic,pieces[i].geometry,0);n.setFlags(atomic,4);n.renderCallback(atomic,nullptr);n.addAtomic(assembly.value,atomic);
 }
 n.update(root);if(!current())return false;auto object=static_cast<uint8_t *>(n.allocate(0x1e8));if(!object)return false;n.construct(object);n.setModel(object,245);n.referenceModel(object,model);
 n.attach(object,assembly.value);assembly.value=nullptr;
 if(!current()){n.destroy(object);return false;}
 // Same temporary-object ownership/lifetime as native flying body parts.
 // Model245 supplies the native body-part collision proxy; the actual clipped
 // skin is the object's multi-atomic geometry, centred at its own mass origin.
 *reinterpret_cast<uint16_t *>(object+0x64)|=0x1000;*reinterpret_cast<uint32_t *>(object+0x60)&=~0x400u;
 const float size=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z,.1f}),mass=std::clamp(15.f+size*8.f,15.f,250.f);
 *reinterpret_cast<float *>(object+0xcc)=mass;*reinterpret_cast<float *>(object+0xd0)=mass*size*size*.25f;*reinterpret_cast<float *>(object+0xd8)=.99f;*reinterpret_cast<float *>(object+0xe0)=.1f;
 *reinterpret_cast<uint16_t *>(object+0x1b5)=(*reinterpret_cast<uint16_t *>(object+0x1b5)&~1u)|0x80u;
 *reinterpret_cast<uint16_t *>(object+0x1e0)=*reinterpret_cast<uint16_t *>(v+0x210);
 *reinterpret_cast<Vec3 *>(object+0x84)=*reinterpret_cast<Vec3 *>(v+0x84);auto &velocity=*reinterpret_cast<Vec3 *>(object+0x84);velocity.z+=.035f;
 *reinterpret_cast<Vec3 *>(object+0x90)=*reinterpret_cast<Vec3 *>(v+0x90);auto &turn=*reinterpret_cast<Vec3 *>(object+0x90);turn.x+=.02f;turn.y+=.015f;
 *reinterpret_cast<uint8_t **>(object+0x1d8)=v;*reinterpret_cast<uint32_t *>(object+0x1c8)=*game::api.timeMs+12000;
 object[0x1b4]=3;++*n.temporary;n.add(object);return true;
}
bool Install(){if(gReady)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(n.allocate,"_ZN7CObjectnwEm");B(n.construct,"_ZN7CObjectC1Ev");B(n.destroy,"_ZN7CObjectD0Ev");B(n.setModel,"_ZN7CEntity21SetModelIndexNoCreateEj");B(n.referenceModel,"_ZN7CObject12RefModelInfoEi");B(n.attach,"_ZN7CEntity16AttachToRwObjectEPN2rw6ObjectE");B(n.add,"_ZN6CWorld3AddEP7CEntity");B(n.reference,"_ZN6CPools13GetVehicleRefEP8CVehicle");B(n.temporary,"_ZN7CObject14nNoTempObjectsE");
 B(n.createClump,"_Z13RpClumpCreatev");B(n.destroyClump,"_Z14RpClumpDestroyPN2rw5ClumpE");B(n.createFrame,"_Z13RwFrameCreatev");B(n.destroyFrame,"_Z14RwFrameDestroyPN2rw5FrameE");B(n.setClumpFrame,"_Z15RpClumpSetFramePN2rw5ClumpEPNS_5FrameE");B(n.addChild,"_Z15RwFrameAddChildPN2rw5FrameES1_");B(n.createAtomic,"_Z14RpAtomicCreatev");B(n.destroyAtomic,"_Z15RpAtomicDestroyPN2rw6AtomicE");B(n.addAtomic,"_Z16RpClumpAddAtomicPN2rw5ClumpEPNS_6AtomicE");B(n.setAtomicFrame,"_Z16RpAtomicSetFramePN2rw6AtomicEPNS_5FrameE");B(n.setGeometry,"_Z19RpAtomicSetGeometryPN2rw6AtomicEPNS_8GeometryEj");B(n.setFlags,"_Z16RpAtomicSetFlagsPN2rw6AtomicEj");B(n.update,"_Z20RwFrameUpdateObjectsPN2rw5FrameE");B(n.matrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");B(n.verticesCount,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE");B(n.morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi");B(n.vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE");B(n.renderCallback,"_ZN18CVisibilityPlugins23SetAtomicRenderCallbackEPN2rw6AtomicEPFS2_S2_E");
#undef B
 gReady=true;game::api.AircraftDebris=Emit;return true;
}
}
