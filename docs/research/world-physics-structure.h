#pragma once
// Research-only axial lattice. One physical graph owns bars and surface faces;
// Bullet fragments are rebuilt from it at safe boundaries, without new mass.
#include "world-physics-material.h"
#include <array>
#include <chrono>
#include <map>
#include <numeric>
namespace structure {
struct Asset;
struct Force : btDeformableMassSpringForce {
    std::vector<Asset*> assets;
    Force():btDeformableMassSpringForce(0,0){}
    void addScaledExplicitForce(btScalar scale,TVStack &out) override;
};
struct Particle { btVector3 x,v; double mass; };
struct MemberState { int a,b; Bar bar; const char *role; };
struct Surface { std::array<int,3> nodes,edges; double thickness; bool alive=true; };
struct Fragment { std::unique_ptr<btSoftBody> body; std::vector<int> ids,faces; };
struct Asset {
    World &w; Force &force;
    std::vector<Particle> nodes; std::vector<MemberState> members; std::vector<Surface> surfaces;
    std::vector<Fragment> fragments;
    std::vector<btSoftBody::Node*> binding;
    int revision=0;
    double rebuildSeconds=0; int rebuilds=0;
    bool selfCollision=false;
    explicit Asset(World &world,Force &f):w(world),force(f) { force.assets.push_back(this); }
    Asset(const Asset&)=delete; Asset &operator=(const Asset&)=delete;
    ~Asset() { Clear(); force.assets.erase(std::remove(force.assets.begin(),force.assets.end(),this),force.assets.end()); }
    void Clear() {
        for(auto &f:fragments) { w.world->removeForce(f.body.get(),&force); w.world->removeSoftBody(f.body.get()); }
        fragments.clear();
    }
    int Node(btVector3 x,double mass,btVector3 v={0,0,0}) {
        if(!std::isfinite(mass) || mass<=0 || !std::isfinite(x.length2()+v.length2())) throw std::invalid_argument("Invalid particle");
        nodes.push_back({x,v,mass}); return int(nodes.size())-1;
    }
    int Edge(int a,int b) const {
        for(int i=0;i<int(members.size());++i) { const auto &m=members[i]; if((m.a==a&&m.b==b)||(m.a==b&&m.b==a)) return i; }
        return -1;
    }
    int Member(int a,int b,double area,Material m,const char *role) {
        if(a<0||b<0||a>=int(nodes.size())||b>=int(nodes.size())||a==b||Edge(a,b)>=0) throw std::invalid_argument("Invalid member endpoints");
        members.push_back({a,b,{double((nodes[b].x-nodes[a].x).length()),area,m},role}); return int(members.size())-1;
    }
    void Face(int a,int b,int c,double thickness=.001) {
        if(!std::isfinite(thickness)||thickness<=0)throw std::invalid_argument("Invalid surface thickness");
        const int ab=Edge(a,b),bc=Edge(b,c),ca=Edge(c,a);
        if(ab<0||bc<0||ca<0||a==b||b==c||c==a) throw std::invalid_argument("Surface must follow physical edges");
        surfaces.push_back({{a,b,c},{ab,bc,ca},thickness,true});
    }
    void Build() {
        const auto started=std::chrono::steady_clock::now();
        Clear();
        std::vector<int> component(nodes.size()); std::iota(component.begin(),component.end(),0);
        auto root=[&](int i) { while(component[i]!=i)i=component[i];return i; };
        for(const auto &m:members) if(!m.bar.broken) component[root(m.a)]=root(m.b);
        std::map<int,int> groups;
        for(int i=0;i<int(nodes.size());++i) {
            int r=root(i); auto it=groups.find(r);
            if(it==groups.end()) { int j=int(fragments.size()); groups.emplace(r,j); fragments.emplace_back(); it=groups.find(r); }
            fragments[it->second].ids.push_back(i);
        }
        binding.resize(nodes.size());
        for(auto &f:fragments) {
            std::vector<btVector3> positions; std::vector<btScalar> masses; std::vector<int> local(nodes.size(),-1);
            for(int id:f.ids) { local[id]=int(positions.size()); positions.push_back(nodes[id].x); masses.push_back(nodes[id].mass); }
            f.body=std::make_unique<btSoftBody>(&w.world->getWorldInfo(),int(positions.size()),positions.data(),masses.data());
            auto &body=*f.body;
            body.getCollisionShape()->setMargin(.01); body.m_cfg.kDF=0;
            body.m_cfg.collisions=btSoftBody::fCollision::SDF_RD|btSoftBody::fCollision::SDF_RDF|btSoftBody::fCollision::VF_DD;
            body.m_useSelfCollision=selfCollision; body.setActivationState(DISABLE_DEACTIVATION);
            for(int i=0;i<int(f.ids.size());++i) { int id=f.ids[i]; body.m_nodes[i].m_v=nodes[id].v; binding[id]=&body.m_nodes[i]; }
            for(auto &m:members) if(!m.bar.broken&&local[m.a]>=0&&local[m.b]>=0) body.appendLink(local[m.a],local[m.b]);
            for(int i=0;i<int(surfaces.size());++i) {
                const auto &s=surfaces[i];
                if(s.alive&&local[s.nodes[0]]>=0&&local[s.nodes[1]]>=0&&local[s.nodes[2]]>=0) {
                    body.appendFace(local[s.nodes[0]],local[s.nodes[1]],local[s.nodes[2]]); f.faces.push_back(i);
                }
            }
            body.updateNormals(); body.updateBounds(); body.initializeFaceTree();
            w.world->addSoftBody(&body); w.world->addForce(&body,&force);
        }
        ++revision; ++rebuilds;
        rebuildSeconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    }
    void Prepare() {
        // Call once per physical step, outside the solver callbacks. Material
        // state must not be consumed multiple times by iterative force queries.
        bool changed=false;
        for(auto &m:members) {
            const bool before=m.bar.broken;
            m.bar.Sample(double((nodes[m.b].x-nodes[m.a].x).length())); changed|=before!=m.bar.broken;
        }
        for(auto &s:surfaces) for(int e:s.edges) if(members[e].bar.broken)s.alive=false;
        if(changed) Build();
    }
    void Pull() {
        for(int i=0;i<int(nodes.size());++i) { nodes[i].x=binding[i]->m_x; nodes[i].v=binding[i]->m_v; }
        ++revision;
    }
    btVector3 Momentum()const { btVector3 p(0,0,0); for(auto &n:nodes)p+=n.v*n.mass;return p; }
    btVector3 Angular()const { btVector3 l(0,0,0);for(auto &n:nodes)l+=n.x.cross(n.v*n.mass);return l; }
    double Kinetic()const { double e=0;for(auto &n:nodes)e+=.5*n.mass*n.v.length2();return e; }
    double Mass()const { double m=0;for(auto &n:nodes)m+=n.mass;return m; }
    double BulletMass()const { double m=0;for(auto &f:fragments)for(int i=0;i<f.body->m_nodes.size();++i)m+=1/f.body->m_nodes[i].m_im;return m; }
    int FragmentCount()const {return int(fragments.size());}
    int SurfaceCount()const {int n=0;for(auto &s:surfaces)n+=s.alive;return n;}
    bool Synchronized()const {
        if(binding.size()!=nodes.size())return false;
        for(size_t i=0;i<nodes.size();++i) {
            const auto *solved=binding[i];
            if(!solved||!std::isfinite(nodes[i].x.length2())||!std::isfinite(nodes[i].v.length2())||
               !std::isfinite(solved->m_x.length2())||!std::isfinite(solved->m_v.length2()))return false;
        }
        int count=0;
        for(const auto &f:fragments) {
            if(f.faces.size()!=size_t(f.body->m_faces.size()))return false;
            for(int i=0;i<int(f.faces.size());++i) {
                const auto &s=surfaces[f.faces[i]];
                if(!s.alive)return false;
                for(int j=0;j<3;++j) if((f.body->m_faces[i].m_n[j]->m_x-nodes[s.nodes[j]].x).length()>1e-12)return false;
                ++count;
            }
            for(int id:f.ids) if((binding[id]->m_x-nodes[id].x).length()>1e-12 || (binding[id]->m_v-nodes[id].v).length()>1e-12)return false;
        }
        return count==SurfaceCount();
    }
    // These current triangles are the only exported visual/aerodynamic surface.
    // No game renderer or aero model is installed by this headless prototype.
    double Area()const {
        double area=0;
        for(auto &s:surfaces) if(s.alive) area+=.5*(nodes[s.nodes[1]].x-nodes[s.nodes[0]].x).cross(nodes[s.nodes[2]].x-nodes[s.nodes[0]].x).length();
        return area;
    }
    bool Ray(btVector3 from,btVector3 to)const {
        for(auto &f:fragments) { btSoftBody::sRayCast r; if(f.body->rayFaceTest(from,to,r))return true; }
        return false;
    }
};
inline void Force::addScaledExplicitForce(btScalar scale,TVStack &out) {
    for(auto *a:assets) for(const auto &m:a->members) {
        if(m.bar.broken)continue;
        auto *n0=a->binding[m.a],*n1=a->binding[m.b];
        const auto delta=n1->m_x-n0->m_x;
        if(delta.length()<=SIMD_EPSILON)throw std::runtime_error("Collapsed structural member");
        const auto impulse=delta.normalized()*(scale*m.bar.force);
        out[n0->index]+=impulse;out[n1->index]-=impulse;
    }
}
}
