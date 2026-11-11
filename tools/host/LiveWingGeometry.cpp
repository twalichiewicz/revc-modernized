#include "LiveWingGeometry.h"
#include "Plane.h"
#include "Object.h"
#include "Pools.h"
#include "World.h"
#include "ModelIndices.h"
#include "Collision.h"
#include "WingMapping.h"
#include <map>
#include <set>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace {
CVector Vec(const double *v){return CVector(v[0],v[1],v[2]);}
CVector Local(const CMatrix &pose,CVector p){p-=pose.GetPosition();return CVector(DotProduct(p,pose.GetRight()),DotProduct(p,pose.GetForward()),DotProduct(p,pose.GetUp()));}
CMatrix Pose(const WpState &s,CVector anchor){CMatrix p;p.SetUnity();p.GetPosition()=Vec(s.bodyCenter)+anchor;p.GetRight()=CVector(s.bodyRotation[0],s.bodyRotation[3],s.bodyRotation[6]);p.GetForward()=CVector(s.bodyRotation[1],s.bodyRotation[4],s.bodyRotation[7]);p.GetUp()=CVector(s.bodyRotation[2],s.bodyRotation[5],s.bodyRotation[8]);return p;}
std::array<int,3> Key(int a,int b,int c){std::array<int,3> k{{a,b,c}};std::sort(k.begin(),k.end());return k;}
void DestroyClump(rw::Clump *c){if(c)c->destroy();}
struct Vertex {CVector point,normal;rw::RGBA colour;rw::TexCoords uv;};
struct Triangle {Vertex vertex[3];rw::Material *material;};
rw::Geometry *MakeMesh(const std::vector<Triangle> &triangles){
 if(triangles.empty())return nil;
 auto g=rw::Geometry::create(triangles.size()*3,triangles.size(),rw::Geometry::POSITIONS|rw::Geometry::NORMALS|rw::Geometry::PRELIT|rw::Geometry::TEXTURED|rw::Geometry::LIGHT|rw::Geometry::MODULATE);
 if(!g)return nil;
 for(size_t i=0;i<triangles.size();++i){const auto &t=triangles[i];int material=g->matList.findIndex(t.material);if(material<0)material=g->matList.appendMaterial(t.material);g->triangles[i].matId=material;
  CVector n=CrossProduct(t.vertex[1].point-t.vertex[0].point,t.vertex[2].point-t.vertex[0].point);if(n.MagnitudeSqr()>1e-12f)n.Normalise();
  for(int k=0;k<3;++k){auto v=t.vertex[k];size_t index=3*i+k;g->triangles[i].v[k]=index;g->morphTargets[0].vertices[index]=rw::makeV3d(v.point.x,v.point.y,v.point.z);auto normal=v.normal.MagnitudeSqr()>.5f?v.normal:n;g->morphTargets[0].normals[index]=rw::makeV3d(normal.x,normal.y,normal.z);g->colors[index]=v.colour;g->texCoords[0][index]=v.uv;}
 }
 g->calculateBoundingSphere();g->unlock();return g;
}
CColModel *MakeCol(const std::vector<std::array<CVector,3>> &triangles,const CColModel *body){
 auto col=new CColModel;if(body)*col=*body;
 if(col->vertices)RwFree(col->vertices);if(col->triangles)RwFree(col->triangles);col->RemoveTrianglePlanes();col->vertices=nil;col->triangles=nil;col->numTriangles=triangles.size();
 if(!triangles.empty()){col->vertices=(CompressedVector*)RwMalloc(triangles.size()*3*sizeof(CompressedVector));col->triangles=(CColTriangle*)RwMalloc(triangles.size()*sizeof(CColTriangle));}
 CVector lo(1e10f,1e10f,1e10f),hi(-1e10f,-1e10f,-1e10f);auto include=[&](CVector p){lo.x=Min(lo.x,p.x);lo.y=Min(lo.y,p.y);lo.z=Min(lo.z,p.z);hi.x=Max(hi.x,p.x);hi.y=Max(hi.y,p.y);hi.z=Max(hi.z,p.z);};
 if(body){for(int i=0;i<col->numSpheres;++i){auto s=col->spheres[i];include(s.center-CVector(s.radius,s.radius,s.radius));include(s.center+CVector(s.radius,s.radius,s.radius));}for(int i=0;i<col->numBoxes;++i){include(col->boxes[i].min);include(col->boxes[i].max);}}
 for(size_t i=0;i<triangles.size();++i){for(int j=0;j<3;++j){auto p=triangles[i][j];col->vertices[3*i+j].Set(p.x,p.y,p.z);include(p);}col->triangles[i].Set(3*i,3*i+1,3*i+2,0);}
 if(lo.x>hi.x)lo=hi=CVector(0,0,0);col->boundingBox.Set(lo,hi);col->boundingSphere.Set((hi-lo).Magnitude()*.5f,(hi+lo)*.5f);return col;
}
}
struct LiveWingGeometry::Impl {
 struct Binding {rw::Atomic *atomic=nil;rw::Geometry *original=nil,*current=nil;CMatrix local;std::vector<int> nodes;std::vector<std::array<int,3>> wing;std::vector<std::array<std::array<double,3>,3>> weights;uint32 flags=0;};
 CPlane *plane=nil;int reference=-1;rw::Clump *clump=nil;std::unique_ptr<CColModel> baseline,body;std::vector<Binding> bindings;std::vector<std::array<CVector,3>> bodyTriangles;std::map<int,CObject*> debris;uint64 session=0,revision=0;
 ~Impl(){for(auto &b:bindings)if(b.original)b.original->destroy();}
 bool Current()const{return plane&&CPools::GetVehicle(reference)==plane&&plane->GetModelIndex()==181&&plane->m_rwObject&&plane->GetClump()==clump&&std::all_of(bindings.begin(),bindings.end(),[](const Binding &b){return b.atomic->geometry==b.current;});}
 static rw::Atomic *CaptureAtomic(rw::Atomic *a,void *data){auto self=(Impl*)data;if(!(a->getFlags()&4)||!a->geometry||!a->getFrame())return a;Binding b;b.atomic=a;b.original=b.current=a->geometry;b.flags=a->getFlags();b.original->addRef();b.local.SetUnity();
  // Compose model-local frames. Subtracting two world LTMs at kilometre
  // coordinates quantizes small hinge translations and can lose wing faces.
  for(auto frame=a->getFrame();frame&&frame!=self->clump->getFrame();frame=RwFrameGetParent(frame)){CMatrix local(RwFrameGetMatrix(frame),false);b.local=local*b.local;}
  self->bindings.push_back(std::move(b));return a;}
};
LiveWingGeometry::LiveWingGeometry():impl(new Impl){}
LiveWingGeometry::~LiveWingGeometry(){Restore();}
bool LiveWingGeometry::Current()const{return impl->Current();}
bool LiveWingGeometry::Owns(const CEntity *e)const{if(!e)return false;if(e==impl->plane)return true;for(auto &p:impl->debris)if(p.second==e)return true;return false;}
int LiveWingGeometry::Fragment(const CEntity *e)const{if(e==impl->plane)return 0;for(auto &p:impl->debris)if(p.second==e)return p.first;return -1;}
std::vector<CEntity*> LiveWingGeometry::Participants()const{std::vector<CEntity*> result;if(impl->plane)result.push_back(impl->plane);for(auto &p:impl->debris)result.push_back(p.second);return result;}
bool LiveWingGeometry::Capture(CPlane *plane,int reference,const WpState &state,CVector anchor,std::string &error){
 if(impl->plane||!plane||CPools::GetVehicle(reference)!=plane||!plane->GetClump()){error="invalid aircraft generation";return false;}
 impl->plane=plane;impl->reference=reference;impl->clump=plane->GetClump();impl->baseline.reset(new CColModel);*impl->baseline=*plane->GetColModel();impl->body.reset(new CColModel);*impl->body=*impl->baseline;
 // Native body volumes stop at the supported outer-wing seam. The wing is
 // represented by its real triangles in both the coordinator and native COL.
 const float cut=2.625f;int spheres=0;for(int i=0;i<impl->body->numSpheres;++i){auto s=impl->body->spheres[i];s.radius=Min(s.radius,cut-s.center.x);if(s.radius>0)impl->body->spheres[spheres++]=s;}impl->body->numSpheres=spheres;
 int boxes=0;for(int i=0;i<impl->body->numBoxes;++i){auto b=impl->body->boxes[i];b.max.x=Min(b.max.x,cut);if(b.min.x<b.max.x)impl->body->boxes[boxes++]=b;}impl->body->numBoxes=boxes;
 for(int i=0;i<impl->baseline->numTriangles;++i){const auto t=impl->baseline->triangles[i];std::vector<CVector> poly{impl->baseline->vertices[t.a].Get(),impl->baseline->vertices[t.b].Get(),impl->baseline->vertices[t.c].Get()},clipped;
 for(size_t j=0;j<poly.size();++j){auto a=poly[j],b=poly[(j+1)%poly.size()];bool inside=a.x<=cut,next=b.x<=cut;if(inside)clipped.push_back(a);if(inside!=next)clipped.push_back(a+(b-a)*((cut-a.x)/(b.x-a.x)));}
 for(size_t j=1;j+1<clipped.size();++j)impl->bodyTriangles.push_back({{clipped[0],clipped[j],clipped[j+1]}});}
 plane->GetMatrix().UpdateRW();plane->UpdateRwFrame();RpClumpForAllAtomics(impl->clump,Impl::CaptureAtomic,impl.get());
 std::set<std::array<int,3>> physical,represented;for(int i=0;i<state.faces;++i)if(state.face[i].wing)physical.insert(Key(state.face[i].node[0],state.face[i].node[1],state.face[i].node[2]));
 std::vector<std::array<double,3>> rest(state.nodes);const CVector axes[]={plane->GetRight(),plane->GetForward(),plane->GetUp()};auto origin=plane->GetPosition();
 for(int n=0;n<state.nodes;++n){double delta[3]={state.node[n].x[0]+double(anchor.x)-origin.x,state.node[n].x[1]+double(anchor.y)-origin.y,state.node[n].x[2]+double(anchor.z)-origin.z};for(int k=0;k<3;++k)rest[n][k]=delta[0]*axes[k].x+delta[1]*axes[k].y+delta[2]*axes[k].z;}
 for(auto &b:impl->bindings){auto g=b.original;if(g->numMorphTargets!=1||!g->morphTargets[0].vertices){error="unsupported native morph geometry";return false;}b.nodes.assign(g->numVertices,-1);b.wing.assign(g->numTriangles,{{-1,-1,-1}});
  for(int i=0;i<g->numVertices;++i){auto v=g->morphTargets[0].vertices[i];auto p=b.local*CVector(v.x,v.y,v.z);for(int n=8;n<state.nodes;++n){double dx=p.x-rest[n][0],dy=p.y-rest[n][1],dz=p.z-rest[n][2];if(dx*dx+dy*dy+dz*dz<1e-12){b.nodes[i]=n;break;}}}
  b.weights.resize(g->numTriangles);
  for(int i=0;i<g->numTriangles;++i){auto t=g->triangles[i];auto k=Key(b.nodes[t.v[0]],b.nodes[t.v[1]],b.nodes[t.v[2]]);if(physical.count(k)){b.wing[i]={{b.nodes[t.v[0]],b.nodes[t.v[1]],b.nodes[t.v[2]]}};for(int j=0;j<3;++j)b.weights[i][j][j]=1;represented.insert(k);continue;}
   CVector points[3];bool outboard=true;for(int j=0;j<3;++j){auto v=g->morphTargets[0].vertices[t.v[j]];points[j]=b.local*CVector(v.x,v.y,v.z);outboard&=points[j].x>=cut-.0001f&&std::fabs(points[j].y)<2;}if(!outboard)continue;
   bool mapped=false;for(int f=0;f<state.faces;++f){auto face=state.face[f];if(!face.wing)continue;bool inside=true;std::array<std::array<double,3>,3> weights{};for(int j=0;j<3;++j){double point[3]={points[j].x,points[j].y,points[j].z};inside&=WingBarycentric(point,rest[face.node[0]].data(),rest[face.node[1]].data(),rest[face.node[2]].data(),weights[j].data());}if(inside){b.wing[i]={{face.node[0],face.node[1],face.node[2]}};b.weights[i]=weights;represented.insert(Key(face.node[0],face.node[1],face.node[2]));mapped=true;break;}}
   if(!mapped){error="clipped outboard render triangle has no canonical material owner";return false;}
  }
 }
 if(represented!=physical){error="rendered wing does not match material asset";return false;}impl->session=state.session;return true;
}
void LiveWingGeometry::Restore(){
 bool current=impl->Current();if(current){for(auto &b:impl->bindings){b.atomic->setGeometry(b.original,0);b.atomic->setFlags(b.flags);b.current=b.original;}auto col=new CColModel;*col=*impl->baseline;impl->plane->ReplaceInstanceCollision(col);}
 for(auto &p:impl->debris){CWorld::Remove(p.second);delete p.second;}impl->debris.clear();impl->plane=nil;
}
std::vector<CEntity*> LiveWingGeometry::DetachDebris(){std::vector<CEntity*> objects;for(auto &p:impl->debris)objects.push_back(p.second);impl->debris.clear();return objects;}
void LiveWingGeometry::Abandon(CEntity *e){if(e==impl->plane)impl->plane=nil;for(auto it=impl->debris.begin();it!=impl->debris.end();)if(it->second==e)it=impl->debris.erase(it);else ++it;}
bool LiveWingGeometry::Publish(const WpState &state,CVector anchor,std::string &error){
 if(!Current()||state.session!=impl->session||state.revision<=impl->revision||state.nodes<8||state.nodes>WP_MAX_NODES||state.faces<0||state.faces>WP_MAX_FACES){error="stale geometry publication";return false;}
 for(double v:state.bodyCenter)if(!std::isfinite(v)||std::fabs(v)>1e6){error="invalid published body position";return false;}
 for(double v:state.bodyRotation)if(!std::isfinite(v)){error="invalid published body basis";return false;}
 for(int i=0;i<state.nodes;++i){for(double v:state.node[i].x)if(!std::isfinite(v)||std::fabs(v)>1e6){error="nonfinite material vertex";return false;}if(i<8&&state.node[i].fragment!=0){error="chassis breakup exceeds supported island";return false;}}
 std::map<std::array<int,3>,int> surviving;
 for(int i=0;i<state.faces;++i){auto f=state.face[i];for(int k=0;k<3;++k)if(f.node[k]<0||f.node[k]>=state.nodes||state.node[f.node[k]].fragment!=f.fragment){error="invalid material face ownership";return false;}if(f.wing)surviving[Key(f.node[0],f.node[1],f.node[2])]=f.fragment;}
 const CMatrix parentPose=Pose(state,anchor);
 std::map<int,CMatrix> poses;poses[0]=parentPose;
 for(int i=8;i<state.nodes;++i){int fragment=state.node[i].fragment;if(fragment&&!poses.count(fragment)){CVector center(0,0,0);int count=0;for(int j=8;j<state.nodes;++j)if(state.node[j].fragment==fragment){center+=Vec(state.node[j].x)+anchor;++count;}CMatrix pose;pose.SetUnity();pose.GetPosition()=center/float(count);poses[fragment]=pose;}}
 std::map<int,std::vector<std::array<CVector,3>>> collision;collision[0]=impl->bodyTriangles;
 for(int i=0;i<state.faces;++i){auto f=state.face[i];if(!f.wing)continue;std::array<CVector,3> tri;for(int k=0;k<3;++k)tri[k]=Local(poses[f.fragment],Vec(state.node[f.node[k]].x)+anchor);collision[f.fragment].push_back(tri);}
 struct Prepared {Impl::Binding *binding;rw::Geometry *geometry;};std::vector<Prepared> parents;std::map<int,rw::Clump*> clumps;std::map<int,CColModel*> colliders;std::map<int,CObject*> created;
 auto discard=[&]{for(auto &p:parents)if(p.geometry)p.geometry->destroy();for(auto &p:clumps)DestroyClump(p.second);for(auto &p:colliders)delete p.second;for(auto &p:created)delete p.second;};
 try{
  for(auto &b:impl->bindings){auto g=b.original;std::map<int,std::vector<Triangle>> meshes;
   for(int i=0;i<g->numTriangles;++i){auto source=g->triangles[i];int fragment=0;const bool wing=b.wing[i][0]>=0;
    if(wing){auto found=surviving.find(Key(b.wing[i][0],b.wing[i][1],b.wing[i][2]));if(found==surviving.end())continue;fragment=found->second;}
    if(source.matId>=g->matList.numMaterials){discard();error="invalid native wing material";return false;}
    Triangle tri{};tri.material=g->matList.materials[source.matId];
    for(int k=0;k<3;++k){int index=source.v[k];auto &out=tri.vertex[k];auto original=g->morphTargets[0].vertices[index];out.point=CVector(original.x,original.y,original.z);out.colour=g->colors?g->colors[index]:rw::makeRGBA(255,255,255,255);out.uv=g->texCoords[0]?g->texCoords[0][index]:rw::TexCoords{0,0};out.normal=CVector(0,0,0);
     if(wing){CVector world(0,0,0);for(int j=0;j<3;++j)world+=Vec(state.node[b.wing[i][j]].x)*b.weights[i][k][j];world+=anchor;out.point=Local(poses[fragment],world);if(!fragment)out.point=Invert(b.local)*out.point;}
     else if(g->morphTargets[0].normals){auto n=g->morphTargets[0].normals[index];out.normal=CVector(n.x,n.y,n.z);}
    }meshes[fragment].push_back(tri);
   }
   auto main=MakeMesh(meshes[0]);if(!meshes[0].empty()&&!main)throw std::bad_alloc();parents.push_back({&b,main});
   for(auto &mesh:meshes)if(mesh.first&&!mesh.second.empty()){
    auto geo=MakeMesh(mesh.second);if(!geo)throw std::bad_alloc();auto atomic=rw::Atomic::create();if(!atomic){geo->destroy();throw std::bad_alloc();}atomic->setGeometry(geo,0);geo->destroy();auto frame=rw::Frame::create();atomic->setFrame(frame);
    auto &clump=clumps[mesh.first];if(!clump){clump=rw::Clump::create();clump->setFrame(rw::Frame::create());}clump->getFrame()->addChild(frame);clump->addAtomic(atomic);
   }
  }
  for(auto &mesh:collision){colliders[mesh.first]=MakeCol(mesh.second,mesh.first==0?impl->body.get():nil);if(mesh.first&&!impl->debris.count(mesh.first)){auto object=new CObject(MI_CAR_PANEL,false);if(!object)throw std::bad_alloc();object->ObjectCreatedBy=MISSION_OBJECT;object->SetIsStatic(true);object->bUsesCollision=true;object->bIsBIGBuilding=false;created[mesh.first]=object;}}
 }catch(...){discard();error="geometry preparation failed";return false;}
 // Validate all receivers before changing a resident actor. The source
 // replacement routine repeats these same nonallocating checks in commit.
 if(!impl->plane->CanReplaceInstanceCollision(colliders.at(0))){discard();error="parent rejected prepared collider";return false;}
 for(auto &p:colliders)if(p.first){auto found=impl->debris.find(p.first);auto object=found==impl->debris.end()?created.at(p.first):found->second;if(!clumps.count(p.first)||!object->CanReplaceInstanceCollision(p.second)){discard();error="fragment rejected prepared collider";return false;}}
 // Commit between normal world updates. Every allocation and topology check is
 // finished before touching a resident actor. No native query runs mid-commit.
 CWorld::Remove(impl->plane);impl->plane->GetMatrix()=parentPose;
 for(auto &p:parents){if(p.geometry){p.binding->atomic->setGeometry(p.geometry,0);p.binding->current=p.geometry;p.geometry->destroy();p.geometry=nil;p.binding->atomic->setFlags(p.binding->flags);}else p.binding->atomic->setFlags(0);}
 auto col=colliders[0];colliders.erase(0);if(!impl->plane->ReplaceInstanceCollision(col)){delete col;error="native parent collision rejected prepared geometry";discard();CWorld::Add(impl->plane);return false;}
 impl->plane->GetMatrix().UpdateRW();impl->plane->UpdateRwFrame();CWorld::Add(impl->plane);
 for(auto &p:created)impl->debris[p.first]=p.second;created.clear();
 for(auto &p:clumps){auto object=impl->debris[p.first];CWorld::Remove(object);if(object->m_rwObject){auto old=object->GetClump();object->DetachFromRwObject();DestroyClump(old);}object->AttachToRwObject((RwObject*)p.second);p.second=nil;object->GetMatrix()=poses[p.first];auto geometry=colliders[p.first];colliders.erase(p.first);if(!object->ReplaceInstanceCollision(geometry)){delete geometry;error="native fragment collision rejected prepared geometry";discard();return false;}object->GetMatrix().UpdateRW();object->UpdateRwFrame();CWorld::Add(object);}
 for(auto it=impl->debris.begin();it!=impl->debris.end();)if(!collision.count(it->first)){auto object=it->second;it=impl->debris.erase(it);CWorld::Remove(object);delete object;}else ++it;
 impl->revision=state.revision;discard();return true;
}

// Verification runs only under ordinary-world automation. It exercises the
// compiled native queries after publication, without mutating gameplay actors.
bool LiveWingGeometry::VerifyQueries(const WpState &state,CVector anchor,int &checks,std::string &error)const{
 checks=0;if(!Current()){error="query receiver is stale";return false;}
 CColModel probe;probe.numSpheres=1;probe.spheres=(CColSphere*)RwMalloc(sizeof(CColSphere));probe.spheres[0].Set(.03f,CVector(0,0,0),0,0);probe.boundingBox.Set(CVector(-.03f,-.03f,-.03f),CVector(.03f,.03f,.03f));probe.boundingSphere.Set(.03f,CVector(0,0,0));
 for(int i=0;i<state.faces;++i){auto face=state.face[i];if(!face.wing)continue;CVector points[3];for(int k=0;k<3;++k)points[k]=Vec(state.node[face.node[k]].x)+anchor;auto normal=CrossProduct(points[1]-points[0],points[2]-points[0]);if(normal.MagnitudeSqr()<.001f||normal.z<.3f*normal.Magnitude())continue;
  auto point=(points[0]+points[1]+points[2])/3;auto start=point+CVector(0,0,.08f),end=point-CVector(0,0,.08f);float nearest=2;int nearestFragment=-1;CVector direction=end-start;
  // Authored main skin and flap overlap. The expected query receipt is the
  // nearest accepted surface, rather than the sampled (possibly hidden) face.
  for(int j=0;j<state.faces;++j){auto candidate=state.face[j];if(!candidate.wing)continue;CVector v[3];for(int k=0;k<3;++k)v[k]=Vec(state.node[candidate.node[k]].x)+anchor;auto n=CrossProduct(v[1]-v[0],v[2]-v[0]);float denominator=DotProduct(n,direction);if(denominator>=-1e-10f)continue;float t=DotProduct(n,v[0]-start)/denominator;if(t<0||t>1||t>=nearest)continue;auto hit=start+direction*t;bool inside=true;for(int k=0;k<3;++k)inside&=DotProduct(CrossProduct(v[(k+1)%3]-v[k],hit-v[k]),n)>=-1e-7f*n.MagnitudeSqr();if(inside){nearest=t;nearestFragment=candidate.fragment;}}
  if(nearestFragment<0)continue;point=start+direction*nearest;CEntity *owner=impl->plane;if(nearestFragment)owner=impl->debris.at(nearestFragment);CColPoint losHit,verticalHit;CEntity *losEntity=nil,*verticalEntity=nil;
  bool los=CWorld::ProcessLineOfSight(start,end,losHit,losEntity,false,true,false,true,false,false,false,false);bool vertical=CWorld::ProcessVerticalLine(start,end.z,verticalHit,verticalEntity,false,true,false,true,false,false,nil);
  CMatrix pose;pose.SetUnity();pose.GetPosition()=point+CVector(0,0,.005f);CColPoint contacts[MAX_COLLISION_POINTS];int count=CCollision::ProcessColModels(pose,probe,owner->GetMatrix(),*owner->GetColModel(),contacts,nil,nil);
  bool matchingContact=false;for(int c=0;c<count;++c){auto at=contacts[c].point;for(int j=0;j<state.faces;++j){auto accepted=state.face[j];if(!accepted.wing||accepted.fragment!=nearestFragment)continue;CVector v[3];for(int k=0;k<3;++k)v[k]=Vec(state.node[accepted.node[k]].x)+anchor;auto n=CrossProduct(v[1]-v[0],v[2]-v[0]);float square=n.MagnitudeSqr();if(square<1e-12f)continue;float distance=DotProduct(at-v[0],n);auto projected=at-n*(distance/square);bool inside=true;for(int k=0;k<3;++k)inside&=DotProduct(CrossProduct(v[(k+1)%3]-v[k],projected-v[k]),n)>=-1e-7f*square;if(inside&&distance*distance/square<.0005f*.0005f&&(at-pose.GetPosition()).Magnitude()<.0305f)matchingContact=true;}}

  checks+=3;if(!los||!vertical||losEntity!=owner||verticalEntity!=owner||(losHit.point-point).Magnitude()>.0005f||(verticalHit.point-point).Magnitude()>.0005f||!matchingContact){char message[512];std::snprintf(message,sizeof message,"native query disagreement face=%d owner=%p los=%d/%p vertical=%d/%p distance=%.8g/%.8g contact=%d expectedz=%.8g actualz=%.8g",i,(void*)owner,los,(void*)losEntity,vertical,(void*)verticalEntity,(losHit.point-point).Magnitude(),(verticalHit.point-point).Magnitude(),count,point.z,losHit.point.z);error=message;return false;}
 }
 if(checks==0){error="no surviving upward wing face available for world query proof";return false;}return true;
}
