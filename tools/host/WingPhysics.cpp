// Only this translation unit sees the existing Eigen/IPC/Bullet coordinator.
#include "WingPhysics.h"
#define main WingUnusedResearchMain
#include "../../docs/research/world-physics-probe.cpp"
#undef main
#include "../../docs/research/world-physics-structure.h"
#include "../../docs/research/world-physics-persistent.h"
#include <Eigen/SVD>
#include <fstream>
#include <chrono>
#include <queue>
#include <tbb/global_control.h>

namespace {
using Vec=Eigen::Vector3d;
Settings PhysicsSettings(){Settings s;s.explicitForces=true;return s;}
Vec ToVec(btVector3 p){return {p.x(),p.y(),p.z()};}
btVector3 ToBullet(Vec p){return {p.x(),p.y(),p.z()};}
struct Skin {std::vector<Vec> points;std::vector<std::array<int,3>> faces;};
Skin ReadSkin(const char *path,bool largestOnly=true){
    std::ifstream in(path);int count=0;if(!(in>>count)||count<1||count>10000)throw std::invalid_argument("Missing/invalid aircraft mesh");
    Skin all;std::set<std::array<int,3>> unique;
    for(int f=0;f<count;++f){int wing;Vec p[3];if(!(in>>wing))throw std::invalid_argument("Invalid mesh face");for(auto &v:p)if(!(in>>v.x()>>v.y()>>v.z())||!v.allFinite())throw std::invalid_argument("Invalid mesh vertex");if(!wing)continue;
        std::array<int,3> ids;for(int j=0;j<3;++j){int id=0;while(id<int(all.points.size())&&(all.points[id]-p[j]).norm()>1e-7)++id;if(id==int(all.points.size()))all.points.push_back(p[j]);ids[j]=id;}
        auto key=ids;std::sort(key.begin(),key.end());if(unique.insert(key).second)all.faces.push_back(ids);
    }
    if(!largestOnly){
        if(all.points.size()+8>WP_MAX_NODES||all.faces.size()+12>WP_MAX_FACES)
            throw std::invalid_argument("Aircraft skin exceeds bounded adapter capacity");
        return all;
    }
    // The diagnostic export combines duplicate atomics and a separate flap.
    // Use the largest connected exterior component as one physical skin.
    std::vector<int> component(all.points.size(),-1);int groups=0;
    for(int i=0;i<int(component.size());++i)if(component[i]<0){std::queue<int> q;q.push(i);component[i]=groups;while(!q.empty()){int n=q.front();q.pop();for(auto f:all.faces)if(std::find(f.begin(),f.end(),n)!=f.end())for(int v:f)if(component[v]<0){component[v]=groups;q.push(v);}}++groups;}
    std::vector<int> sizes(groups);for(auto f:all.faces)++sizes[component[f[0]]];int largest=std::max_element(sizes.begin(),sizes.end())-sizes.begin();
    Skin result;std::vector<int> remap(all.points.size(),-1);for(int i=0;i<int(all.points.size());++i)if(component[i]==largest){remap[i]=result.points.size();result.points.push_back(all.points[i]);}
    for(auto f:all.faces)if(component[f[0]]==largest)result.faces.push_back({remap[f[0]],remap[f[1]],remap[f[2]]});
    if(result.points.size()!=12||result.faces.size()!=20)throw std::invalid_argument("Expected pinned Dodo exterior skin (12 vertices/20 faces)");return result;
}
struct Session {
    // This island has only dozens of vertices; task fan-out cost dominates.
    tbb::global_control workers{tbb::global_control::max_allowed_parallelism,1};
    World world{PhysicsSettings()};structure::Force force;structure::Asset plane{world,force};
    std::unique_ptr<persistent::RigidBox> building;std::unique_ptr<persistent::Stepper> stepper;
    std::unique_ptr<structure::Asset> environment;
    Vec gravity{0,0,-9.81};persistent::Matrix planeContactReference;
    uint64_t id;Vec support=Vec::Zero(),external=Vec::Zero();double numericalLoss=0,stepMs=0,contactEnergy=0;int contacts=0,newton=0,substeps=0,retries=0;
    const Vec offset{0,-3,4};std::vector<Vec> rest;std::vector<int> mountIds;
    explicit Session(uint64_t identity,const char *path,double speed,bool miss,bool oblique,bool completeSkin=false):id(identity){
        if(!std::isfinite(speed)||speed<=0||speed>30)throw std::invalid_argument("Strike speed must be in (0,30]m/s");auto skin=ReadSkin(path,!completeSkin);
        auto chassis=structure::Material{70e9,1e12,7e8,1,1e12,.2};
        auto wing=structure::Material{70e9,250e6,7e8,.08,25000,.2};
        // Synthetic weak attachment for this bounded strike fixture. These
        // are authored connector inputs, not a calibrated Dodo/alloy claim.
        auto mount=structure::Material{70e9,20e6,7e8,.003,25000,.2};
        const auto rootSkin=completeSkin?ReadSkin(path):skin;
        for(int i=0;i<8;++i)plane.Node(ToBullet(Vec(i&1?.4:-.4,i&2?.6:-.6,1+(i&4?.2:-.2))+offset),25,{0,speed,0});
        for(auto p:skin.points)plane.Node(ToBullet(p+offset),28./skin.points.size(),{0,speed,0});
        for(int i=0;i<8;++i)for(int j=i+1;j<8;++j)plane.Member(i,j,.001,chassis,"chassis");
        for(int i=8;i<int(plane.nodes.size());++i)for(int j=i+1;j<int(plane.nodes.size());++j)plane.Member(i,j,.00001,wing,"wing spar/skin");
        // Control-surface vertices attach to the wing graph, not to the
        // fuselage. Overlapping flap geometry must not create extra mounts.
        for(int i=8;i<int(plane.nodes.size());++i)if(skin.points[i-8].x()<3&&
            std::any_of(rootSkin.points.begin(),rootSkin.points.end(),[&](Vec p){return (p-skin.points[i-8]).norm()<1e-7;})){
            Vec p=skin.points[i-8];int corner=(p.y()>0?2:0)|(p.z()>1?4:0);
            for(int j:{corner,corner+1})mountIds.push_back(plane.Member(j,i,.000005,mount,"wing mount"));
        }
        const std::array<std::array<int,3>,12> boxfaces{{{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}}};
        for(auto f:boxfaces)plane.Face(f[0],f[1],f[2]);for(auto f:skin.faces)plane.Face(f[0]+8,f[1]+8,f[2]+8);
        for(auto n:plane.nodes)rest.push_back(ToVec(n.x));plane.Build();
        building=std::make_unique<persistent::RigidBox>(world,Vec(.45,.2,3.5),10000,Vec(miss?7.5:4.4,0,3.5),Vec::Zero(),Vec::Zero(),Eigen::Quaterniond(Eigen::AngleAxisd(oblique?.35:0,Vec::UnitZ())),true);
        persistent::Options settings;settings.velocityTolerance=1e-6;
        stepper=std::make_unique<persistent::Stepper>(world,std::vector<structure::Asset*>{&plane},std::vector<persistent::Pin>{},settings,std::vector<persistent::RigidBox*>{building.get()});if(!completeSkin)contactEnergy=stepper->ContactEnergy();
    }
    Session(uint64_t identity,const char *path,const WpInitial &initial,const WpStaticMesh &geometry)
        :Session(identity,path,6,true,false,true){
        if(geometry.vertices<3||geometry.vertices>WP_MAX_WORLD_POINTS||geometry.faces<1||geometry.faces>WP_MAX_WORLD_FACES)
            throw std::invalid_argument("Unsupported static mesh capacity");
        Eigen::Matrix3d rotation;for(int i=0;i<3;++i)for(int j=0;j<3;++j)rotation(i,j)=initial.rotation[3*i+j];
        const Vec position(initial.position),velocity(initial.velocity),spin(initial.spin);
        gravity=Vec(initial.gravity);
        if(!position.allFinite()||!velocity.allFinite()||!spin.allFinite()||!gravity.allFinite()||!rotation.allFinite()||
           (rotation.transpose()*rotation-Eigen::Matrix3d::Identity()).norm()>1e-5||std::abs(rotation.determinant()-1)>1e-5||
           !std::isfinite(initial.bodyMass)||!std::isfinite(initial.wingMass)||initial.bodyMass<=0||initial.wingMass<=0)
            throw std::invalid_argument("Invalid aircraft initial state");
        stepper.reset();plane.Clear();
        world.world->removeRigidBody(building->body);world.rigid.clear();building.reset();
        const Vec half(initial.chassisHalf),chassisCenter(initial.chassisCenter);
        if(!half.allFinite()||!chassisCenter.allFinite()||(half.norm()!=0&&half.minCoeff()<=0))throw std::invalid_argument("Invalid chassis bounds");
        if(half.norm()!=0)for(int i=0;i<8;++i)rest[i]=offset+chassisCenter+Vec(i&1?half.x():-half.x(),i&2?half.y():-half.y(),i&4?half.z():-half.z());
        if(half.norm()!=0){
            // Native fuselage bounds are much longer than the small fixture.
            // Triangulate each wing-root attachment to both upper/lower and
            // fore/aft chassis corners; a near-horizontal strut alone cannot
            // carry the normal vertical/turning load of this larger body.
            std::set<int> roots;for(int id:mountIds)roots.insert(plane.members[id].b);
            const auto connector=plane.members[mountIds.front()].bar.material;
            for(int node:roots)for(int corner=0;corner<8;++corner)if(plane.Edge(corner,node)<0)mountIds.push_back(plane.Member(corner,node,.000005,connector,"wing mount"));
        }
        for(int i=0;i<int(plane.nodes.size());++i){
            const Vec arm=rotation*(rest[i]-offset);auto &node=plane.nodes[i];
            node.x=ToBullet(position+arm);node.v=ToBullet(velocity+spin.cross(arm));
            node.mass=i<8?initial.bodyMass/8:initial.wingMass/(plane.nodes.size()-8);
        }
        // The low-mass diagnostic has12 authored100N-yield connectors.
        // Preserve the axial-force/wing-weight ratio in native game gravity;
        // directional capacity also depends on the authored attachment geometry.
        // This changes joint section area, not material yield or damage HP.
        const double jointScale=std::max(1.,initial.wingMass/28*gravity.norm()/9.81);
        for(auto &member:plane.members){if(std::string(member.role)=="wing spar/skin")member.bar.area*=std::max(1.,initial.wingMass/28);else if(std::string(member.role)=="chassis")member.bar.area*=std::max(1.,initial.bodyMass/200);}
        for(int id:mountIds)plane.members[id].bar.area*=jointScale;
        for(auto &member:plane.members)member.bar.length=(plane.nodes[member.a].x-plane.nodes[member.b].x).length();
        plane.Build();planeContactReference.resize(plane.nodes.size(),3);for(int i=0;i<int(plane.nodes.size());++i)planeContactReference.row(i)=ToVec(plane.nodes[i].x);environment=MakeEnvironment(geometry);environment->Build();
        stepper=NewStepper(environment.get());contactEnergy=stepper->ContactEnergy();
    }
    std::unique_ptr<structure::Asset> MakeEnvironment(const WpStaticMesh &geometry){
        if(geometry.vertices==0&&geometry.faces==0)return {};
        if(geometry.vertices<3||geometry.vertices>WP_MAX_WORLD_POINTS||geometry.faces<1||geometry.faces>WP_MAX_WORLD_FACES)
            throw std::invalid_argument("Unsupported static mesh capacity");
        auto candidate=std::make_unique<structure::Asset>(world,force);
        const auto material=structure::Material{70e9,1e12,7e8,1,1e12,.2};
        for(int i=0;i<geometry.vertices;++i)candidate->Node(ToBullet(Vec(geometry.point[i])),1);
        for(int i=0;i<geometry.faces;++i){
            auto f=geometry.face[i];
            for(int k=0;k<3;++k)if(f[k]<0||f[k]>=geometry.vertices)throw std::invalid_argument("Invalid world triangle index");
            Vec a(geometry.point[f[0]]),b(geometry.point[f[1]]),c(geometry.point[f[2]]);
            if((b-a).cross(c-a).norm()<1e-8)throw std::invalid_argument("Degenerate world triangle");
            for(int k=0;k<3;++k)if(candidate->Edge(f[k],f[(k+1)%3])<0)candidate->Member(f[k],f[(k+1)%3],.001,material,"static world edge");
            candidate->Face(f[0],f[1],f[2]);
        }
        return candidate;
    }
    std::unique_ptr<persistent::Stepper> NewStepper(structure::Asset *env,double releasedEnergy=0){
        std::vector<structure::Asset*> participants{&plane};std::vector<persistent::Pin> pins;
        if(env){participants.push_back(env);for(int i=0;i<int(env->nodes.size());++i)pins.push_back({env,i});}
        // Keep high-speed contact above the finite CCD tolerance band. This
        // numerical barrier is separate from authored material strength.
        persistent::Options settings;settings.velocityTolerance=1e-7;settings.linearAabbCulling=true;settings.excludeConnectedSelfContact=true;settings.contactStiffness=1e9;
        persistent::Matrix reference(plane.nodes.size()+(env?env->nodes.size():0),3);reference.topRows(plane.nodes.size())=planeContactReference;
        if(env)for(int i=0;i<int(env->nodes.size());++i)reference.row(plane.nodes.size()+i)=ToVec(env->nodes[i].x);
        return std::make_unique<persistent::Stepper>(world,participants,pins,settings,std::vector<persistent::RigidBox*>{},reference,releasedEnergy);
    }
    void ReplaceWorld(const WpStaticMesh &geometry){
        if(building)throw std::invalid_argument("Diagnostic box ownership cannot change");
        auto candidate=MakeEnvironment(geometry);double time=stepper->time,release=stepper->ReleasedContactEnergy(),oldContact=stepper->ContactEnergy();
        auto old=std::move(environment);stepper.reset();if(old)old->Clear();
        try{if(candidate)candidate->Build();auto next=NewStepper(candidate.get(),release);next->time=time;double contact=next->ContactEnergy();if(contact>oldContact+1e-8*(1+oldContact))throw std::runtime_error("World replacement would introduce active contact energy");next->AccountReleasedContact(std::max(0.,oldContact-contact));contactEnergy=contact;environment=std::move(candidate);stepper=std::move(next);}
        catch(...){if(candidate)candidate->Clear();if(old)old->Build();environment=std::move(old);stepper=NewStepper(environment.get(),release);stepper->time=time;throw;}
    }
    void Advance(double dt,const WpLoad *load=nullptr){
        Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(plane.nodes.size()+(environment?environment->nodes.size():(building?8:0)),3);for(int i=0;i<int(plane.nodes.size());++i)loads.row(i)=gravity.transpose()*plane.nodes[i].mass;
        if(load){
            const Vec net(load->force),torque(load->torque);if(!net.allFinite()||!torque.allFinite())throw std::invalid_argument("Nonfinite aircraft force");
            std::vector<int> attached;for(auto &fragment:plane.fragments)if(std::find(fragment.ids.begin(),fragment.ids.end(),0)!=fragment.ids.end())attached=fragment.ids;
            if(attached.empty())throw std::invalid_argument("Missing parent fragment");
            // The existing flight controller supplies a net wrench, not a
            // spanwise pressure field. Distribute it over attached mass only;
            // separated skin retains gravity/contact, without engine lift.
            Vec center=Vec::Zero();double mass=0;for(int i:attached){mass+=plane.nodes[i].mass;center+=ToVec(plane.nodes[i].x)*plane.nodes[i].mass;}center/=mass;
            Eigen::Matrix3d inertia=Eigen::Matrix3d::Zero();for(int i:attached){Vec r=ToVec(plane.nodes[i].x)-center;inertia+=plane.nodes[i].mass*(r.squaredNorm()*Eigen::Matrix3d::Identity()-r*r.transpose());}
            Vec spin=inertia.ldlt().solve(torque);if(!spin.allFinite()||(inertia*spin-torque).norm()>1e-7*std::max(1.,torque.norm()))throw std::invalid_argument("Unrepresentable aircraft torque");
            for(int i:attached){Vec r=ToVec(plane.nodes[i].x)-center;loads.row(i)+=(net*(plane.nodes[i].mass/mass)+plane.nodes[i].mass*spin.cross(r)).transpose();}
        }
        auto start=std::chrono::steady_clock::now();auto stats=stepper->Advance(dt,loads);stepMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        support+=stats.supportLinear;external+=stats.externalImpulse;numericalLoss+=stats.numericalLoss;contactEnergy=stats.contactEnergy;contacts=stats.peakContacts;newton=stats.newton;substeps=stats.substeps;retries=stats.retries;
    }
    void Snapshot(WpState &s)const{
        s={};s.session=id;s.revision=plane.revision;s.time=stepper->time;s.nodes=plane.nodes.size();s.faces=0;s.fragments=plane.FragmentCount();s.mass=plane.Mass();s.stepMs=stepMs;s.contacts=contacts;s.newton=newton;s.substeps=substeps;s.retries=retries;s.numericalLoss=numericalLoss;s.contactEnergy=contactEnergy;s.releasedContactEnergy=stepper->ReleasedContactEnergy();
        std::vector<int> owner(plane.nodes.size());for(auto &f:plane.fragments){int identity=*std::min_element(f.ids.begin(),f.ids.end());for(int n:f.ids)owner[n]=identity;}
        Vec p=ToVec(plane.Momentum()),center=Vec::Zero(),restCenter=Vec::Zero();for(int k=0;k<3;++k){s.momentum[k]=p[k];s.support[k]=support[k];s.externalImpulse[k]=external[k];}
        s.forwardSpeed=p.y()/s.mass;for(int i=0;i<8;++i){center+=ToVec(plane.nodes[i].x)/8;restCenter+=rest[i]/8;s.parentSpeed+=plane.nodes[i].v.y()/8;}
        Eigen::Matrix3d covariance=Eigen::Matrix3d::Zero();for(int i=0;i<8;++i)covariance+=(ToVec(plane.nodes[i].x)-center)*(rest[i]-restCenter).transpose();
        Eigen::JacobiSVD<Eigen::Matrix3d> fit(covariance,Eigen::ComputeFullU|Eigen::ComputeFullV);Eigen::Matrix3d sign=Eigen::Matrix3d::Identity();sign(2,2)=(fit.matrixU()*fit.matrixV().transpose()).determinant();Eigen::Matrix3d rotation=fit.matrixU()*sign*fit.matrixV().transpose();
        Vec modelOrigin=center-rotation*(restCenter-offset);for(int k=0;k<3;++k){s.bodyCenter[k]=modelOrigin[k];for(int j=0;j<3;++j)s.bodyRotation[3*k+j]=rotation(k,j);}
        for(int i=0;i<s.nodes;++i){auto &n=plane.nodes[i];for(int k=0;k<3;++k){s.node[i].x[k]=n.x[k];s.node[i].v[k]=n.v[k];}s.node[i].mass=n.mass;s.node[i].fragment=owner[i];s.deformation=std::max(s.deformation,(ToVec(n.x)-(rotation*(rest[i]-offset)+modelOrigin)).norm());}
        for(int i=0;i<int(plane.surfaces.size());++i)if(plane.surfaces[i].alive){auto &source=plane.surfaces[i];auto &f=s.face[s.faces++];for(int k=0;k<3;++k)f.node[k]=source.nodes[k];f.fragment=owner[source.nodes[0]];f.wing=i>=12;}
        for(int m:mountIds)s.brokenMounts+=plane.members[m].bar.broken;
        for(auto &m:plane.members){auto &b=m.bar;s.plastic=std::max(s.plastic,b.accumulated);s.fractureWork+=b.fractureWork;s.materialEnergy+=b.heat+b.fractureWork+b.reserve;if(!b.broken){double e=(plane.nodes[m.a].x-plane.nodes[m.b].x).length()/b.length-1-b.plasticStrain;s.materialEnergy+=.5*b.material.modulus*e*e*b.area*b.length+.5*b.material.hardening*b.accumulated*b.accumulated*b.area*b.length;}}
        if(building){auto pose=building->body->getCenterOfMassTransform();auto r=persistent::RigidBox::Rotation(pose);for(int i=0;i<8;++i){Vec q=ToVec(pose.getOrigin())+r*building->local[i];for(int k=0;k<3;++k)s.building[i][k]=q[k];}}
    }
};
std::map<uint64_t,std::unique_ptr<Session>> sessions;uint64_t next=0;
void Error(char *out,int capacity,const char *message){if(out&&capacity>0)std::snprintf(out,capacity,"%s",message);}
}
extern "C" {
int wp_create(const char *mesh,double speed,int miss,int oblique,uint64_t *id,char *error,int capacity){try{if(!mesh||!id||next==UINT64_MAX)throw std::invalid_argument("Invalid session creation");uint64_t candidate=++next;auto p=std::make_unique<Session>(candidate,mesh,speed,miss,oblique);sessions.emplace(candidate,std::move(p));*id=candidate;return 0;}catch(const std::exception &e){Error(error,capacity,e.what());return 1;}}
int wp_create_world(const char *mesh,const WpInitial *initial,const WpStaticMesh *geometry,uint64_t *id,char *error,int capacity){try{if(!mesh||!initial||!geometry||!id||next==UINT64_MAX)throw std::invalid_argument("Invalid world session creation");uint64_t candidate=++next;auto p=std::make_unique<Session>(candidate,mesh,*initial,*geometry);sessions.emplace(candidate,std::move(p));*id=candidate;return 0;}catch(const std::exception &e){Error(error,capacity,e.what());return 1;}}
int wp_advance(uint64_t id,double dt,char *error,int capacity){try{auto it=sessions.find(id);if(it==sessions.end())throw std::invalid_argument("Revoked physics session");it->second->Advance(dt);return 0;}catch(const std::exception &e){Error(error,capacity,e.what());return 1;}}
int wp_advance_forces(uint64_t id,double dt,const WpLoad *load,char *error,int capacity){try{auto it=sessions.find(id);if(it==sessions.end()||!load)throw std::invalid_argument("Invalid owned force submission");it->second->Advance(dt,load);return 0;}catch(const std::exception &e){Error(error,capacity,e.what());return 1;}}
int wp_replace_world(uint64_t id,const WpStaticMesh *geometry,char *error,int capacity){try{auto it=sessions.find(id);if(it==sessions.end()||!geometry)throw std::invalid_argument("Invalid world geometry submission");it->second->ReplaceWorld(*geometry);return 0;}catch(const std::exception &e){Error(error,capacity,e.what());return 1;}}
int wp_snapshot(uint64_t id,WpState *out){auto it=sessions.find(id);if(it==sessions.end()||!out)return 0;it->second->Snapshot(*out);return 1;}
int wp_destroy(uint64_t id){return sessions.erase(id)!=0;}
int wp_live_sessions(){return sessions.size();}
}
