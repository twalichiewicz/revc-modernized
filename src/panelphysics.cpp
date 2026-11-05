#include "panelphysics.h"
#include "config.h"
#include "game.h"
#include "hooks.h"
#include "dentmodel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
namespace panelphysics {namespace {
struct Triangle {uint16_t v[3],material;};
struct Native {
 void **vehicles=nullptr,**objects=nullptr;
 uint8_t *(*getObject)(int)=nullptr,*(*spawn)(uint8_t *,int,unsigned)=nullptr;
 void *(*each)(void *,void *(*)(void *,void *),void *),*(*root)(const void *),*(*frame)(const void *),*(*parent)(const void *),*(*geometry)(const void *),*(*morph)(const void *,int);
 float *(*matrix)(void *);const Vec3 *(*vertices)(const void *);const Triangle *(*triangles)(const void *);
 int (*type)(const void *),(*nv)(const void *),(*nt)(const void *),(*doorStatus)(void *,int);unsigned (*flags)(const void *);
 void *(*material)(const void *,int);const uint8_t *(*colour)(const void *);
 void (*move)(uint8_t *,float,float,float),(*turn)(uint8_t *,float,float,float,float,float,float);
} n;bool ready;
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
int Ref(const uint8_t *v,void **binding,size_t stride){
 if(!v||!binding||!*binding)return -1;auto p=static_cast<const uint8_t *>(*binding);auto entries=*reinterpret_cast<uint8_t *const *>(p),flags=*reinterpret_cast<uint8_t *const *>(p+8);const int count=*reinterpret_cast<const int *>(p+16);
 const auto a=reinterpret_cast<uintptr_t>(v),base=reinterpret_cast<uintptr_t>(entries);if(!entries||!flags||count<1||count>100000||a<base||(a-base)%stride)return -1;const auto i=(a-base)/stride;
 return i<size_t(count)&&!(flags[i]&128)?int(i*256)|flags[i]:-1;
}
struct Actor {
 uint8_t *v=nullptr;int ref=-1,model=-1;void *clump=nullptr,*table=nullptr;std::array<float,16> pose{};
 explicit Actor(uint8_t *car){
  const int r=Ref(car,n.vehicles,0x6f0);if(r<0||!game::api.PoolsGetVehicle||game::api.PoolsGetVehicle(r)!=car||(game::EntityFlags(car)&7)!=2||*reinterpret_cast<const int *>(car+0x348)!=0||game::IsAircraft(car))return;
  v=car;ref=r;model=game::EntityModel(car);clump=game::EntityClump(car);table=*reinterpret_cast<void **>(car);std::memcpy(pose.data(),car+8,sizeof pose);
  for(int row:{0,4,8,12})if(!Finite({pose[row],pose[row+1],pose[row+2]}))v=nullptr;
 }
 bool Current()const{return v&&Ref(v,n.vehicles,0x6f0)==ref&&game::api.PoolsGetVehicle(ref)==v&&game::EntityModel(v)==model&&*reinterpret_cast<const int *>(v+0x348)==0&&!game::IsAircraft(v)&&game::EntityClump(v)==clump&&*reinterpret_cast<void **>(v)==table&&!std::memcmp(pose.data(),v+8,sizeof pose);}
};
bool InRoot(void *frame,void *root,Vec3 &p){
 for(int depth=0;frame&&frame!=root&&depth<32;depth++,frame=n.parent(frame)){
  const auto m=n.matrix(frame);if(!m)return false;p={m[12]+m[0]*p.x+m[4]*p.y+m[8]*p.z,m[13]+m[1]*p.x+m[5]*p.y+m[9]*p.z,m[14]+m[2]*p.x+m[6]*p.y+m[10]*p.z};if(!Finite(p))return false;
 }return frame==root;
}
struct Properties {
 double mass=0;double first[3]{},second[3]{};Vec3 centre{};float inertia[3]{};void *root=nullptr,*branch=nullptr;size_t count=0,faces=0;bool valid=true;
};
void *Measure(void *atomic,void *data){
 auto &p=*static_cast<Properties *>(data);if(!p.valid||++p.count>512){p.valid=false;return nullptr;}if(!(n.flags(atomic)&4))return atomic;
 auto frame=n.frame(atomic);if(p.branch){auto f=frame;int depth=0;while(f&&f!=p.branch&&depth++<32)f=n.parent(f);if(f!=p.branch)return atomic;}
 auto g=n.geometry(atomic);if(!g){p.valid=false;return nullptr;}const int nv=n.nv(g),nt=n.nt(g),nm=*reinterpret_cast<const int *>(static_cast<const uint8_t *>(g)+0x88);
 if(*reinterpret_cast<const int *>(static_cast<const uint8_t *>(g)+0x20)!=1){p.valid=false;return nullptr;}
 auto morph=n.morph(g,0);const auto vertices=morph?n.vertices(morph):nullptr;const auto triangles=n.triangles(g);
 if(nv<3||nv>65535||nt<0||nt>32768||nm<1||nm>256||!vertices||!triangles||p.faces+size_t(nt)>131072){p.valid=false;return nullptr;}p.faces+=size_t(nt);
 for(int i=0;i<nt;i++){
  const auto t=triangles[i];if(t.material>=nm){p.valid=false;break;}auto material=n.material(g,t.material);auto colour=material?n.colour(material):nullptr;if(!colour){p.valid=false;break;}if(!colour[3])continue;
  Vec3 a[3];for(int j=0;j<3;j++){if(t.v[j]>=nv){p.valid=false;break;}a[j]=vertices[t.v[j]];if(!Finite(a[j])||!InRoot(frame,p.root,a[j])){p.valid=false;break;}}if(!p.valid)break;
  const auto cross=Cross(Sub(a[1],a[0]),Sub(a[2],a[0]));const double area=.5*std::sqrt(Dot(cross,cross));if(!std::isfinite(area)){p.valid=false;break;}if(area<1e-9)continue;
  // Sheet density from the actual remaining opaque/glazed surface. These
  // reference sheets are .8mm steel and 3mm glass, including each modeled skin.
  const double dm=area*(colour[3]==255?6.3:7.5);p.mass+=dm;
  const float coordinates[3][3]={{a[0].x,a[1].x,a[2].x},{a[0].y,a[1].y,a[2].y},{a[0].z,a[1].z,a[2].z}};
  for(int axis=0;axis<3;axis++){const auto c=coordinates[axis];p.first[axis]+=dm*(c[0]+c[1]+c[2])/3;p.second[axis]+=dm*(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]+c[0]*c[1]+c[1]*c[2]+c[2]*c[0])/6;}
 }return p.valid?atomic:nullptr;
}
Properties Shape(void *object,void *branch=nullptr){
 Properties p;p.branch=branch;const int type=object?n.type(object):0;if(type!=1&&type!=2){p.valid=false;return p;}p.root=type==2?n.root(object):n.frame(object);if(!p.root){p.valid=false;return p;}
 if(type==2)n.each(object,Measure,&p);else Measure(object,&p);
 if(!p.valid||p.mass<.01||p.mass>100000){p.valid=false;return p;}
 p.centre={float(p.first[0]/p.mass),float(p.first[1]/p.mass),float(p.first[2]/p.mass)};const float c[]={p.centre.x,p.centre.y,p.centre.z};
 if(!Finite(p.centre)){p.valid=false;return p;}
 for(int axis=0;axis<3;axis++){const int b=(axis+1)%3,d=(axis+2)%3;p.inertia[axis]=float(std::max(.02,p.second[b]+p.second[d]-p.mass*(c[b]*c[b]+c[d]*c[d])));if(!std::isfinite(p.inertia[axis]))p.valid=false;}
 return p;
}
struct Context;thread_local Context *current;
struct Context {
 Context *previous=current;Load load;Actor actor;Properties shape;void *branch=nullptr,*objectClump=nullptr,*objectTable=nullptr;uint8_t *object=nullptr;int objectRef=-1,objectModel=-1;std::array<float,16> objectPose{};bool valid=false,separating=false;
 explicit Context(const Load &input):load(input),actor(input.vehicle){
  current=this;if(!ready||!gConfig.carParts||!actor.Current()||load.node<0||load.node>=24||load.door< -1||load.door>=6||!Finite(load.point)||!Finite(load.inward)||!std::isfinite(load.work)||load.work<=0||!carparts::CurrentBullet(load.vehicle,load.lease))return;
  branch=*reinterpret_cast<void **>(load.vehicle+0x440+load.node*8);if(!branch||!actor.clump)return;shape=Shape(actor.clump,branch);valid=shape.valid&&actor.Current();
 }
 ~Context(){current=previous;}
 void Deliver(){
  if(!valid||!actor.Current()||!carparts::CurrentBullet(load.vehicle,load.lease))return;
  const float length=std::sqrt(Dot(load.inward,load.inward));if(!std::isfinite(length)||length<1e-7f)return;
  // Adapter calibration: one native work unit is 25J for point impulses, with
  // an 8g reference round. Convert SI momentum to native 50Hz velocity units.
  const float momentum=std::sqrt(2*.008f*25*std::min(load.work,1000.f))/length;
  const Vec3 impulse{load.inward.x*momentum,load.inward.y*momentum,load.inward.z*momentum};
  if(object){if(Ref(object,n.objects,0x1e8)!=objectRef||n.getObject(objectRef)!=object||game::EntityClump(object)!=objectClump||game::EntityModel(object)!=objectModel||*reinterpret_cast<void **>(object)!=objectTable||std::memcmp(object+8,objectPose.data(),sizeof objectPose))return;Kick(object,impulse);return;}
  if(separating)return;
  if(load.door<0){Kick(load.vehicle,impulse);return;}
  const int state=n.doorStatus(load.vehicle+0x34c,load.door);if(state>=3)return;
  if(state!=2){Kick(load.vehicle,impulse);return;}
  Vec3 point,direction,hinge{};if(!dentmodel::Local(reinterpret_cast<float *>(load.vehicle+8),load.point,false,point)||!dentmodel::Local(reinterpret_cast<float *>(load.vehicle+8),impulse,true,direction)||!InRoot(branch,shape.root,hinge))return;
  auto door=load.vehicle+0x368+load.door*0x24;const unsigned axis=door[9];if(axis>2)return;const auto r=Sub(shape.centre,hinge),lever=Sub(point,hinge),torque=Cross(lever,direction);const float c[]={r.x,r.y,r.z},t[]={torque.x,torque.y,torque.z};
  const float inertia=shape.inertia[axis]+float(shape.mass)*(c[(axis+1)%3]*c[(axis+1)%3]+c[(axis+2)%3]*c[(axis+2)%3]);auto &velocity=*reinterpret_cast<float *>(door+0x14);
  if(std::isfinite(velocity)&&std::isfinite(inertia)&&inertia>.02f)velocity=std::clamp(velocity+t[axis]/inertia/50,-.3f,.3f);
 }
 void Kick(uint8_t *v,Vec3 impulse){
  const float mass=*reinterpret_cast<const float *>(v+0xcc),inertia=*reinterpret_cast<const float *>(v+0xd0);if(!std::isfinite(mass)||mass<=0||!std::isfinite(inertia)||inertia<=0)return;
  const auto com=*reinterpret_cast<const Vec3 *>(v+0xe4);const auto m=reinterpret_cast<const float *>(v+8);if(!Finite(com))return;
  const Vec3 at{load.point.x-m[12],load.point.y-m[13],load.point.z-m[14]};if(!Finite(at))return;
  n.move(v,impulse.x/50,impulse.y/50,impulse.z/50);n.turn(v,impulse.x/50,impulse.y/50,impulse.z/50,at.x,at.y,at.z);
 }
};
uint8_t *Spawn(uint8_t *v,int node,unsigned type){
 for(auto scope=current;scope;scope=scope->previous)if(scope->valid&&scope->actor.v==v&&scope->load.node==node&&scope->actor.Current()){scope->separating=true;break;}
 const Actor actor(v);auto object=n.spawn(v,node,type);if(!object||!gConfig.carParts||!actor.Current()||(game::EntityFlags(object)&7)!=4)return object;
 const int ref=Ref(object,n.objects,0x1e8);if(ref<0||n.getObject(ref)!=object)return object;
 const auto shape=Shape(game::EntityClump(object));if(shape.valid&&actor.Current()){
  const float mass=std::clamp(float(shape.mass),1.f,200.f),scale=mass/float(shape.mass);
  *reinterpret_cast<float *>(object+0xcc)=mass;*reinterpret_cast<float *>(object+0xd0)=std::max(.02f,(shape.inertia[0]+shape.inertia[1]+shape.inertia[2])*scale/3);
  *reinterpret_cast<Vec3 *>(object+0xe4)=shape.centre;auto &flags=*reinterpret_cast<unsigned *>(object+0x60);flags=(flags|game::kEntityUsesCollision)&~0x400u;
 }
 for(auto scope=current;scope;scope=scope->previous)if(scope->valid&&scope->actor.v==v&&scope->load.node==node&&scope->actor.Current()){scope->object=object;scope->objectRef=ref;scope->objectClump=game::EntityClump(object);scope->objectModel=game::EntityModel(object);scope->objectTable=*reinterpret_cast<void **>(object);std::memcpy(scope->objectPose.data(),object+8,sizeof scope->objectPose);break;}
 return object;
}
}
void Apply(const Load &load,void (*progress)(void *),void *context){Context receipt(load);progress(context);receipt.Deliver();}
bool Install(){if(ready||!gConfig.carParts)return true;
#define B(v,s) v=reinterpret_cast<decltype(v)>(dlsym(RTLD_MAIN_ONLY,s));if(!v)return false
 B(n.vehicles,"_ZN6CPools15ms_pVehiclePoolE");B(n.objects,"_ZN6CPools14ms_pObjectPoolE");B(n.getObject,"_ZN6CPools9GetObjectEi");B(n.doorStatus,"_ZN14CDamageManager13GetDoorStatusEi");B(n.type,"_Z15RwObjectGetTypePKN2rw6ObjectE");
 B(n.each,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_");B(n.root,"_Z15RpClumpGetFramePKN2rw5ClumpE");B(n.frame,"_Z16RpAtomicGetFramePKN2rw6AtomicE");B(n.parent,"_Z16RwFrameGetParentPKN2rw5FrameE");B(n.geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE");B(n.morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi");B(n.matrix,"_Z16RwFrameGetMatrixPN2rw5FrameE");B(n.vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE");B(n.triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE");B(n.nv,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE");B(n.nt,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE");B(n.flags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE");B(n.material,"_Z21RpGeometryGetMaterialPKN2rw8GeometryEi");B(n.colour,"_Z18RpMaterialGetColorPKN2rw8MaterialE");B(n.move,"_ZN9CPhysical14ApplyMoveForceEfff");B(n.turn,"_ZN9CPhysical14ApplyTurnForceEffffff");
#undef B
 if(!game::api.PoolsGetVehicle)return false;n.spawn=reinterpret_cast<decltype(n.spawn)>(hooks::Chain("_ZN11CAutomobile20SpawnFlyingComponentEij",reinterpret_cast<void *>(Spawn)));return ready=n.spawn!=nullptr;
}
}
