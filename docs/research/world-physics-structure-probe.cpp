// Isolated physical-structure feasibility assertions; never linked into macVC.
#define main InitialWorldProbeMain
#include "world-physics-probe.cpp"
#undef main
#include <chrono>
#include <sys/resource.h>
#include <limits>
#include "world-physics-structure.h"

int failed=0, checked=0;
void Expect(const char *name, bool pass) {
    ++checked; failed += !pass;
    std::printf("structure %s %s\n", name, pass ? "PASS" : "FAIL");
}
Settings Options(double dt=1./19200) { Settings s; s.explicitForces=true; s.dt=dt; return s; }
structure::Material Elastic(double modulus=1000) { return {modulus, 1e12, 0, 1, 1e12, .5}; }
void Heterogeneous() {
    World w(Options(.001)); structure::Force force;
    structure::Asset a(w,force), b(w,force);
    a.Node({0,0,0},1); a.Node({1,0,0},1);
    b.Node({0,3,0},1); b.Node({1,3,0},1);
    a.Member(0,1,.1,Elastic(1000),"spar"); b.Member(0,1,.1,Elastic(2000),"skin");
    a.nodes[1].x.setX(1.1); b.nodes[1].x.setX(1.1);
    a.Build(); b.Build(); a.Prepare(); b.Prepare(); w.Step(); a.Pull(); b.Pull();
    Expect("different_materials_keep_distinct_forces", std::abs(a.nodes[0].v.x()-.01)<1e-9 && std::abs(b.nodes[0].v.x()-.02)<1e-9);
    Expect("central_internal_forces_conserve_momentum", (a.Momentum()+b.Momentum()).length()<1e-10 && (a.Angular()+b.Angular()).length()<1e-10);
}
void Topology() {
    World w(Options()); structure::Force force; structure::Asset a(w,force);
    // Two triangles, a weak 1m mount, unequal point masses. All values literal.
    a.Node({0,0,0},1,{1,2,3}); a.Node({1,0,0},2,{-2,1,0}); a.Node({0,1,0},3,{0,-1,2});
    a.Node({2,0,0},4,{3,0,-1}); a.Node({3,0,0},5,{0,2,1}); a.Node({2,1,0},6,{-1,0,0});
    for(auto e: {std::pair<int,int>{0,1},{1,2},{2,0},{3,4},{4,5},{5,3}}) a.Member(e.first,e.second,.1,Elastic(),"skin");
    a.Member(1,3,.1,{1000,10,0,.05,.1,.5},"mount"); a.Face(0,1,2); a.Face(3,4,5);
    a.Build();
    const auto p=a.Momentum(), l=a.Angular(); const auto energy=a.Kinetic();
    Expect("intact_actual_collision_face", a.Ray({.2,.2,1},{.2,.2,-1}));
    for(int i=3;i<6;++i) a.nodes[i].x+=btVector3(.2,0,0);
    // This boundary update is prescribed loading, not a conservation step.
    const auto loadedP=a.Momentum(), loadedL=a.Angular();
    a.Prepare();
    Expect("fractured_mount_splits_graph", a.FragmentCount()==2 && a.members.back().bar.broken);
    Expect("split_keeps_mass_momentum_and_energy", std::abs(a.Mass()-21)<1e-12 && (a.Momentum()-loadedP).length()<1e-12 && (a.Angular()-loadedL).length()<1e-12 && std::abs(a.Kinetic()-energy)<1e-12);
    Expect("body_rebuild_matches_state", a.Synchronized() && std::abs(a.BulletMass()-21)<1e-12);
    double fragmentMass[2]={0,0}, izz=0;
    if(a.fragments.size()==2) {
        for(int j=0;j<2;++j)for(int i=0;i<a.fragments[j].body->m_nodes.size();++i)fragmentMass[j]+=1/a.fragments[j].body->m_nodes[i].m_im;
        btVector3 com(0,0,0);auto &body=*a.fragments[0].body;
        for(int i=0;i<body.m_nodes.size();++i)com+=body.m_nodes[i].m_x/body.m_nodes[i].m_im;
        com/=6;
        for(int i=0;i<body.m_nodes.size();++i){auto r=body.m_nodes[i].m_x-com;izz+=(r.x()*r.x()+r.y()*r.y())/body.m_nodes[i].m_im;}
    }
    Expect("unequal_fragment_mass_and_inertia", std::abs(fragmentMass[0]-6)<1e-12&&std::abs(fragmentMass[1]-15)<1e-12&&std::abs(izz-17./6)<1e-12);

    Expect("no_phantom_hull_across_gap", !a.Ray({1.6,.2,1},{1.6,.2,-1}));
    Expect("surviving_fragments_have_collision", a.Ray({.2,.2,1},{.2,.2,-1}) && a.Ray({2.4,.2,1},{2.4,.2,-1}));
    // Further real strain breaks all three edges of the first triangle.
    for(int i=0;i<3;++i) { auto &bar=a.members[i].bar; bar.material={1000,10,0,.05,.1,.5}; }
    a.nodes[1].x.setX(1.5); a.nodes[2].x.setY(1.5); a.Prepare();
    Expect("repeated_tear_removes_actual_faces", a.SurfaceCount()==1 && !a.Ray({.2,.2,1},{.2,.2,-1}) && a.Ray({2.4,.2,1},{2.4,.2,-1}));
    Expect("collision_render_aero_share_current_surface", a.Synchronized() && std::abs(a.Area()-.5)<1e-12 && a.revision>=3);
    Expect("fixture_initial_momentum_is_literal", (p-btVector3(3,11,10)).length()<1e-12 && std::isfinite(l.length()));
}

// Synthetic engineering-scale constants, NOT calibrated aircraft/alloy data.
// Explicit point masses (200kg chassis, 28kg wing), 1mm skin metadata, lattice
// depth .24m. Bars carry axial loads; geometry provides coarse bending.
void WingAsset(structure::Asset &a, double speed=30, double modulus=70e9,btVector3 offset={0,0,0}) {
    const double mass[]={25,25,4,2,1};
    const double y[]={-.4,.4,.4,-.4}, z[]={-.12,-.12,.12,.12};
    for(int station=0;station<5;++station)for(int j=0;j<4;++j)
        a.Node(btVector3(double(station-1),y[j],z[j])+offset,mass[station],{0,speed,0});
    auto material=[&](const char *role) {
        const bool mount=std::strcmp(role,"mount")==0;
        return structure::Material{modulus,mount?120e6:250e6,modulus*.01,mount?.025:.08,25000,.2};
    };
    for(int station=0;station<5;++station)for(int j=0;j<4;++j)for(int k=j+1;k<4;++k)
        a.Member(station*4+j,station*4+k,.0001,material(station<2?"chassis":"spar"),station<2?"chassis":"spar");
    for(int station=0;station<4;++station) {
        for(int j=0;j<4;++j)for(int k=0;k<4;++k) {
            const char *role=station==0?"chassis":station==1?"mount":j==k?"spar":"skin";
            a.Member(station*4+j,(station+1)*4+k,std::strcmp(role,"mount")==0?.00002:.0001,material(role),role);
        }
        for(int j=0;j<4;++j) {
            int k=(j+1)%4;
            a.Face(station*4+j,(station+1)*4+j,(station+1)*4+k);
            a.Face(station*4+j,(station+1)*4+k,station*4+k);
        }
    }
    a.Face(0,1,2);a.Face(0,2,3);a.Face(16,18,17);a.Face(16,19,18);a.Build();
}
struct Totals { btVector3 p{0,0,0},l{0,0,0}; double energy=0,mass=0; };
Totals Measure(const World &w,const structure::Asset &a) {
    Totals t;t.p=a.Momentum();t.l=a.Angular();t.energy=a.Kinetic();t.mass=a.Mass();
    for(auto &m:a.members) {
        const auto &b=m.bar;
        if(!b.broken) {
            double e=(a.nodes[m.b].x-a.nodes[m.a].x).length()/b.length-1-b.plasticStrain;
            t.energy+=.5*b.material.modulus*e*e*b.area*b.length+b.hardeningEnergy+b.reserve;
        }
        t.energy+=b.heat+b.fractureWork;
    }
    for(auto &r:w.rigid)if(r->getInvMass()>0) {
        double mass=1/r->getInvMass();auto p=r->getLinearVelocity()*mass;
        auto basis=r->getWorldTransform().getBasis();auto omega=basis.transpose()*r->getAngularVelocity();btVector3 spin(0,0,0);
        for(int i=0;i<3;++i)if(r->getInvInertiaDiagLocal()[i]>0)spin[i]=omega[i]/r->getInvInertiaDiagLocal()[i];
        t.mass+=mass;t.p+=p;t.l+=r->getWorldTransform().getOrigin().cross(p)+basis*spin;
        t.energy+=.5*(r->getLinearVelocity().dot(p)+omega.dot(spin));
    }
    return t;
}
void WallClip(bool hit,bool oblique,double rate) {
    World w(Options(1/rate));structure::Force force;structure::Asset a(w,force);WingAsset(a,120);
    auto wall=w.Body(new btBoxShape({.22,.2,2}),10000,{hit?2.7:4.5,2,0});
    auto anchor=w.Body(new btEmptyShape(),0,{hit?2.7:4.5,2,0});
    if(oblique) { auto t=wall->getWorldTransform();t.setRotation(btQuaternion({0,0,1},.4));wall->setCenterOfMassTransform(t);anchor->setCenterOfMassTransform(t); }
    btTransform local;local.setIdentity();btFixedConstraint fixed(*wall,*anchor,local,local);
    btJointFeedback feedback{};fixed.enableFeedback(true);fixed.setJointFeedback(&feedback);w.world->addConstraint(&fixed,true);
    auto initial=Measure(w,a);btVector3 impulse(0,0,0),angularImpulse(0,0,0),mountImpulse(0,0,0),mountAngular(0,0,0);
    auto chassis=[&]() { Totals t;for(int i=0;i<8;++i){auto &n=a.nodes[i];t.p+=n.v*n.mass;t.l+=n.x.cross(n.v*n.mass);}return t; };
    const auto initialChassis=chassis();
    double pError=0,lError=0,gain=0,maxMountForce=0,maxPlastic=0,maxDeflection=0;int brokenMounts=0,peakContacts=0;
    const auto start=std::chrono::steady_clock::now();
    int count=w.Steps(.16);bool finite=true;
    for(int step=0;step<count;++step) {
        a.Prepare();
        for(auto &m:a.members)if(std::strcmp(m.role,"mount")==0) {
            maxMountForce=std::max(maxMountForce,std::abs(m.bar.force));maxPlastic=std::max(maxPlastic,m.bar.accumulated);
            if(!m.bar.broken) { auto j=(a.nodes[m.b].x-a.nodes[m.a].x).normalized()*(m.bar.force*w.settings.dt);mountImpulse+=j;mountAngular+=a.nodes[m.a].x.cross(j); }
        }
        auto at=wall->getWorldTransform().getOrigin();w.Step();a.Pull();
        int contacts=0;for(auto &f:a.fragments)contacts+=f.body->m_faceRigidContacts.size()+f.body->m_nodeRigidContacts.size()+f.body->m_faceNodeContacts.size();
        peakContacts=std::max(peakContacts,contacts);
        auto j=feedback.m_appliedForceBodyA*w.settings.dt;
        impulse+=j;angularImpulse+=feedback.m_appliedTorqueBodyA*w.settings.dt+at.cross(j);
        auto t=Measure(w,a);double dp=(t.p-initial.p-impulse).length(),dl=(t.l-initial.l-angularImpulse).length(),de=t.energy-initial.energy;
        finite&=std::isfinite(dp+dl+de);pError=std::max(pError,dp);lError=std::max(lError,dl);gain=std::max(gain,de);
        maxDeflection=std::max(maxDeflection,std::abs(double(a.nodes[16].x.y()-a.nodes[4].x.y())));
    }
    double cpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    for(auto &m:a.members)if(std::strcmp(m.role,"mount")==0)brokenMounts+=m.bar.broken;
    double chassisVy=0;for(int i=0;i<8;++i)chassisVy+=a.nodes[i].v.y()/8;
    double chassisSlowdown=120-chassisVy;
    double slowdown=120-a.Momentum().y()/a.Mass();
    std::printf("wall %s %s rate=%.0f steps=%d nodes=%zu members=%zu faces=%d fragments=%d assembly_slowdown=%.9g chassis_slowdown=%.9g mountF=%.9g plastic=%.9g brokenMounts=%d deflection=%.9g reaction=%.9g dP=%.9g dL=%.9g energyGain=%.9g peak_contacts=%d cpu_s=%.6f rebuilds=%d rebuild_s=%.6f\n",hit?"clip":"miss",oblique?"oblique":"normal",rate,count,a.nodes.size(),a.members.size(),a.SurfaceCount(),a.FragmentCount(),slowdown,chassisSlowdown,maxMountForce,maxPlastic,brokenMounts,maxDeflection,double(impulse.length()),pError,lError,gain,peakContacts,cpu,a.rebuilds,a.rebuildSeconds);
    Expect("wall_work_and_reactions",finite&&pError<.01&&lError<.1&&gain<initial.energy*.02+.01&&std::abs(a.Mass()-228)<1e-9);
    Expect(hit?"wall_load_deforms_and_severs":"clear_miss_keeps_structure",hit?(slowdown>.01&&maxMountForce>1&&maxPlastic>.025&&brokenMounts>0&&a.FragmentCount()>1&&maxDeflection>.01):(std::abs(slowdown)<1e-7&&maxMountForce<1e-4&&brokenMounts==0&&a.FragmentCount()==1));
    const auto finalChassis=chassis();
    double rootP=(finalChassis.p-initialChassis.p-mountImpulse).length(),rootL=(finalChassis.l-initialChassis.l-mountAngular).length();
    double mass0=0,mass1=0;for(int id:a.fragments[0].ids)mass0+=a.nodes[id].mass;
    if(a.fragments.size()==2)for(int id:a.fragments[1].ids)mass1+=a.nodes[id].mass;
    std::printf("mount_receipts root_dP=%.9g root_dL=%.9g impulse=%.9g angular_impulse=%.9g parent_mass=%.9g detached_mass=%.9g\n",rootP,rootL,double(mountImpulse.length()),double(mountAngular.length()),mass0,mass1);
    Expect("mount_transmits_force_and_torque_to_chassis",rootP<1e-6&&rootL<.01&&(!hit||(mountImpulse.length()>.001&&mountAngular.length()>.001)));
    Expect("split_preserves_parent_and_wing_mass",hit?(std::abs(mass0-200)<1e-9&&std::abs(mass1-28)<1e-9):std::abs(mass0-228)<1e-9);
    double inertia[2]={0,0};
    for(int f=0;f<int(a.fragments.size())&&f<2;++f) {
        auto &body=*a.fragments[f].body;btVector3 com(0,0,0);double mass=0;
        for(int j=0;j<body.m_nodes.size();++j){auto &n=body.m_nodes[j];mass+=1/n.m_im;com+=n.m_x/n.m_im;}
        com/=mass;
        for(int j=0;j<body.m_nodes.size();++j){auto &n=body.m_nodes[j];auto r=n.m_x-com;inertia[f]+=(r.x()*r.x()+r.y()*r.y())/n.m_im;}
    }
    std::printf("fragment_inertia parent_Izz=%.9g detached_Izz=%.9g kg_m2\n",inertia[0],inertia[1]);
    // The eight unyielded chassis points have nominal Izz=82kg*m². Its
    // geometric inertia should remain near that value under elastic strain.
    Expect("separated_fragments_retain_geometric_inertia",!hit||(std::abs(inertia[0]-82)<.1&&std::isfinite(inertia[1])&&inertia[1]>1));

    Expect("current_collision_surface_after_crash",a.Synchronized());
    w.world->removeConstraint(&fixed);
}




// Independent local intersection test, not a mean-plane separation proxy.
bool SegmentTriangle(btVector3 from,btVector3 to,btVector3 a,btVector3 b,btVector3 c) {
    auto d=to-from,e1=b-a,e2=c-a,h=d.cross(e2);double det=e1.dot(h);
    if(std::abs(det)<1e-12)return false;
    double u=(from-a).dot(h)/det;if(u<1e-8||u>1-1e-8)return false;
    auto q=(from-a).cross(e1);double v=d.dot(q)/det;if(v<1e-8||u+v>1-1e-8)return false;
    double t=e2.dot(q)/det;return t>1e-8&&t<1-1e-8;
}
bool Intersects(const structure::Asset &a,int sa,int sb) {
    const auto &f=a.surfaces[sa],&g=a.surfaces[sb];
    if(!f.alive||!g.alive)return false;
    for(int j=0;j<3;++j) {
        if(SegmentTriangle(a.nodes[f.nodes[j]].x,a.nodes[f.nodes[(j+1)%3]].x,a.nodes[g.nodes[0]].x,a.nodes[g.nodes[1]].x,a.nodes[g.nodes[2]].x))return true;
        if(SegmentTriangle(a.nodes[g.nodes[j]].x,a.nodes[g.nodes[(j+1)%3]].x,a.nodes[f.nodes[0]].x,a.nodes[f.nodes[1]].x,a.nodes[f.nodes[2]].x))return true;
    }
    return false;
}

void Recontact() {
    World w(Options(1./1920));structure::Force force;structure::Asset a(w,force);
    for(auto p:{btVector3(-1,-1,-.4),btVector3(1,-1,-.4),btVector3(0,1,-.4)})a.Node(p,1,{0,0,1});
    for(auto p:{btVector3(-.8,-.8,.4),btVector3(1.2,-.8,.4),btVector3(.2,1.2,.4)})a.Node(p,3,{0,0,-1});
    for(int base:{0,3}) {
        for(int i=base;i<base+3;++i)for(int j=i+1;j<base+3;++j)a.Member(i,j,.001,Elastic(1e6),"skin");
        a.Face(base,base+1,base+2);
    }
    a.Member(0,3,.001,{1e6,1000,0,.05,1,.5},"mount");a.Build();
    for(int i=3;i<6;++i)a.nodes[i].x+=btVector3(0,0,.2);a.Prepare();
    const auto initial=Measure(w,a);const auto p0=a.nodes[0].v;double gap=1e9,pError=0,lError=0,gain=0;bool synchronized=true;int intersectionFrames=0;
    for(int i=0;i<w.Steps(.9);++i) {
        a.Prepare();w.Step();a.Pull();auto t=Measure(w,a);double za=0,zb=0;
        for(int k=0;k<3;++k){za+=a.nodes[k].x.z()/3;zb+=a.nodes[k+3].x.z()/3;}
        gap=std::min(gap,zb-za);pError=std::max(pError,double((t.p-initial.p).length()));lError=std::max(lError,double((t.l-initial.l).length()));gain=std::max(gain,t.energy-initial.energy);
        synchronized&=a.Synchronized();intersectionFrames+=Intersects(a,0,1);
    }
    std::printf("fragment_recontact fragments=%d intersection_frames=%d minimum_mean_gap=%.9g velocity_change=%.9g dP=%.9g dL=%.9g energyGain=%.9g\n",a.FragmentCount(),intersectionFrames,gap,double((a.nodes[0].v-p0).length()),pError,lError,gain);
    Expect("severed_fragments_recontact",a.FragmentCount()==2&&intersectionFrames==0&&(a.nodes[0].v-p0).length()>.01);
    Expect("recontact_conservation_and_current_surfaces",pError<1e-6&&lError<.02&&gain<.02*initial.energy+.01&&synchronized);
}

void Panel(structure::Asset &a,double z,btVector3 velocity={0,0,0}) {
    for(auto p:{btVector3(-1,-1,z),btVector3(1,-1,z),btVector3(1,1,z),btVector3(-1,1,z)})a.Node(p,5,velocity);
    for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)a.Member(i,j,.001,Elastic(1e6),"skin");
    a.Face(0,1,2,.001);a.Face(0,2,3,.001);a.Build();
}
void Projectile(double rate,bool hole,bool empty,bool moving) {
    World w(Options(1/rate));structure::Force force;structure::Asset a(w,force);
    Panel(a,moving?.5:0,moving?btVector3(0,0,-1000):btVector3(0,0,0));
    if(hole) {
        a.members[a.Edge(0,1)].bar.material={1e6,1e3,0,.05,.01,.5};
        a.nodes[0].x.setX(-1.5);a.Prepare(); // prescribed local strain; removes one actual triangle
    }
    const btVector3 shotAt(empty?3:.5,-.2,moving?0:.5);
    bool ray=a.Ray({shotAt.x(),shotAt.y(),2},{shotAt.x(),shotAt.y(),-2});
    auto ball=w.Body(new btSphereShape(.01),.01,shotAt,moving?btVector3(0,0,0):btVector3(0,0,-1000));
    ball->setCcdMotionThreshold(.001);ball->setCcdSweptSphereRadius(.008);
    const auto initial=Measure(w,a);const auto p0=a.Momentum();
    double minClearance=1e9,pError=0,lError=0,gain=0;int steps=std::max(2,w.Steps(.003));
    for(int i=0;i<steps;++i) {
        a.Prepare();w.Step();a.Pull();auto t=Measure(w,a);
        pError=std::max(pError,double((t.p-initial.p).length()));lError=std::max(lError,double((t.l-initial.l).length()));gain=std::max(gain,t.energy-initial.energy);
        double meanZ=0;for(auto &n:a.nodes)meanZ+=n.x.z()/4;
        double relativeZ=double(ball->getWorldTransform().getOrigin().z())-meanZ;
        minClearance=std::min(minClearance,moving?-relativeZ:relativeZ);
    }
    double transfer=(a.Momentum()-p0).length();
    bool struck=transfer>.001;
    bool solid=!hole&&!empty;
    std::printf("projectile rate=%.0f hole=%d empty=%d moving_surface=%d ray=%d impulse=%.9g min_clearance=%.9g dP=%.9g dL=%.9g energyGain=%.9g\n",rate,hole,empty,moving,ray,transfer,minClearance,pError,lError,gain);
    Expect("projectile_actual_material_query",ray==solid&&a.Synchronized());
    Expect(solid?"fast_projectile_stops_at_intact_material":"projectile_passes_empty_or_torn_surface",solid?(struck&&minClearance>=-.02):(!struck));
    Expect("projectile_reactions_are_conservative",pError<1e-6&&lError<.02&&gain<initial.energy*.02+.01);
}
void AngularSweep(int substeps) {
    World w(Options(1./240/substeps));structure::Force force;structure::Asset empty(w,force);
    auto blade=w.Body(new btBoxShape({2,.02,.01}),2,{0,0,0});blade->getCollisionShape()->setMargin(.001);
    blade->setAngularVelocity({0,-144,0});blade->setCcdMotionThreshold(.001);blade->setCcdSweptSphereRadius(.005);
    auto target=w.Body(new btSphereShape(.03),1,{1.5*std::cos(.3),0,1.5*std::sin(.3)});
    auto initial=Measure(w,empty);
    for(int i=0;i<substeps;++i)w.Step();
    auto final=Measure(w,empty);double received=target->getLinearVelocity().length();
    auto axis=blade->getWorldTransform().getBasis()*btVector3(1,0,0);
    std::printf("angular_pose x_axis=(%.9g,%.9g,%.9g) angle=%.9g manifolds=%d\n",double(axis.x()),double(axis.y()),double(axis.z()),std::atan2(double(axis.z()),double(axis.x())),w.dispatcher.getNumManifolds());
    std::printf("angular_sweep substeps=%d omega=144rad/s sweep=.6rad target_impulse=%.9g dP=%.9g dL=%.9g energyGain=%.9g\n",substeps,received,double((final.p-initial.p).length()),double((final.l-initial.l).length()),final.energy-initial.energy);
    Expect("rotating_thin_blade_contacts_between_endposes",received>.001);
    Expect("angular_contact_conservation",(final.p-initial.p).length()<1e-6&&(final.l-initial.l).length()<.02&&final.energy<=initial.energy*1.02+.01);
}
void EdgeCross(bool self) {
    World w(Options(1./1920));structure::Force force;structure::Asset a(w,force),b(w,force);
    auto *second=self?&a:&b;
    a.selfCollision=self;
    for(auto p:{btVector3(-1,-.01,0),btVector3(1,-.01,0),btVector3(1,.01,0)})a.Node(p,1);
    int offset=self?3:0;
    for(auto p:{btVector3(-.01,-1,.1),btVector3(.01,1,.1),btVector3(-.01,1,.1)})second->Node(p,1,{0,0,-1});
    auto triangle=[&](structure::Asset &asset,int base) {
        for(int i=base;i<base+3;++i)for(int j=i+1;j<base+3;++j)asset.Member(i,j,.001,Elastic(),"skin");
        asset.Face(base,base+1,base+2);
    };
    triangle(a,0);triangle(*second,offset);
    if(self)a.Member(0,3,.00001,Elastic(1),"flexible_mount");
    Expect("edge_fixture_has_two_physical_triangles",a.SurfaceCount()+b.SurfaceCount()==2);

    a.Build();if(!self)b.Build();
    auto initialP=a.Momentum()+b.Momentum();double initialGap=.1,lowest=initialGap;
    for(int i=0;i<w.Steps(.2);++i) {
        a.Prepare();if(!self)b.Prepare();w.Step();a.Pull();if(!self)b.Pull();
        double za=0,zb=0;for(int j=0;j<3;++j){za+=a.nodes[j].x.z()/3;zb+=second->nodes[offset+j].x.z()/3;}
        lowest=std::min(lowest,zb-za);
    }
    std::printf("edge_cross self=%d minimum_mean_gap=%.9g dP=%.9g\n",self,lowest,double((a.Momentum()+b.Momentum()-initialP).length()));
    Expect(self?"self_contact_stops_between_node_edge_crossing":"two_deformables_stop_between_node_edge_crossing",lowest>=-.02);
    Expect("edge_contact_preserves_momentum",(a.Momentum()+b.Momentum()-initialP).length()<1e-6);
}



void InvalidGeometry() {
    World w(Options());structure::Force force;structure::Asset a(w,force);Panel(a,0);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    auto saved=a.nodes[0];
    a.nodes[0].x.setX(nan);
    Expect("nonfinite_canonical_position_is_not_synchronized",!a.Synchronized());
    a.nodes[0]=saved;a.binding[0]->m_x.setX(nan);
    Expect("nonfinite_solver_position_is_not_synchronized",!a.Synchronized());
    a.binding[0]->m_x=saved.x;a.nodes[0].v.setY(nan);
    Expect("nonfinite_canonical_velocity_is_not_synchronized",!a.Synchronized());
    a.nodes[0]=saved;a.binding[0]->m_v.setZ(nan);
    Expect("nonfinite_solver_velocity_is_not_synchronized",!a.Synchronized());
    a.binding[0]->m_v=saved.v;
    Expect("restored_geometry_is_synchronized",a.Synchronized());
}

void Benchmark(int number,double rate) {
    World w(Options(1/rate));structure::Force force;std::vector<std::unique_ptr<structure::Asset>> assets;
    for(int i=0;i<number;++i) {
        assets.push_back(std::make_unique<structure::Asset>(w,force));WingAsset(*assets.back(),120,70e9,{10.*i,0,0});
        w.Body(new btBoxShape({.22,.2,2}),0,{10.*i+2.7,2,0});
    }
    int steps=w.Steps(.16),peakContacts=0,peakFragments=0;bool finite=true;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<steps;++i) {
        for(auto &a:assets)a->Prepare();w.Step();
        int contacts=0,fragments=0;
        for(auto &a:assets) {
            a->Pull();finite&=a->Synchronized()&&std::isfinite(a->Kinetic());fragments+=a->FragmentCount();
            for(auto &f:a->fragments)contacts+=f.body->m_nodeRigidContacts.size()+f.body->m_faceRigidContacts.size()+f.body->m_faceNodeContacts.size();
        }
        peakContacts=std::max(peakContacts,contacts);peakFragments=std::max(peakFragments,fragments);
    }
    double cpu=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),rebuildTime=0;int rebuilds=0;
    for(auto &a:assets){rebuildTime+=a->rebuildSeconds;rebuilds+=a->rebuilds;}
    struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::printf("benchmark structures=%d points=%d axial_members=%d initial_triangles=%d static_walls=%d native_constraints=0 rate=%.0f steps=%d simulated_s=.16 wall_s=%.6f us_per_step=%.6f seconds_per_simulated_second=%.6f peak_contact_records=%d peak_fragments=%d rebuilds=%d rebuild_s=%.6f peak_process_rss_bytes=%ld\n",number,20*number,94*number,36*number,number,rate,steps,cpu,cpu/steps*1e6,cpu/.16,peakContacts,peakFragments,rebuilds,rebuildTime,usage.ru_maxrss);
    Expect("benchmark_retains_finite_current_geometry",finite);
    Expect("benchmark_exercises_contact_and_fracture",peakContacts>0&&rebuilds>number&&peakFragments>number);
}

int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1&&std::strcmp(argv[1],"--benchmark")==0) { Benchmark(1,38400);Benchmark(32,38400);return failed?1:0; }
    if(argc>1&&std::strcmp(argv[1],"--contact-gate")==0) {
        for(double rate:{480.,19200.,192000.}) { Projectile(rate,false,false,false);Projectile(rate,true,false,false);Projectile(rate,false,true,false);Projectile(rate,false,false,true); }
        AngularSweep(1);AngularSweep(20);AngularSweep(100);EdgeCross(false);EdgeCross(true);Recontact();
        std::printf("geometric contact gate: %d/%d passed\n",checked-failed,checked);return failed?1:0;
    }
    Heterogeneous(); Topology();InvalidGeometry();
    for(double rate:{19200.,38400.,76800.}) { WallClip(true,false,rate);WallClip(true,true,rate);WallClip(false,false,rate); }
    std::printf("structural matrix: %d/%d passed\n", checked-failed,checked);
    return failed ? 1 : 0;
}
