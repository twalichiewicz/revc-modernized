// Isolated exact-rigid/axial-deformable coupling in the shared research solve.
#define main BaselineProbeMain
#include "world-physics-probe.cpp"
#undef main
#include "world-physics-structure.h"
#include "world-physics-persistent.h"
#include <sys/resource.h>
int checks=0,failures=0;
void Check(const char *name,bool okay){++checks;failures+=!okay;std::printf("coupled %s %s\n",name,okay?"PASS":"FAIL");}
Settings SettingsFor(){Settings s;s.explicitForces=true;return s;}
Eigen::Vector3d Vec(btVector3 x){return {x.x(),x.y(),x.z()};}
void FreeBox(){
    World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{1,2,3},{.5,0,0});
    bool advanced=false;try{
        persistent::Stepper stepper(w,{}, {}, {}, {&box});Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(8,3);for(int i=0;i<8;++i)loads(i,1)=2;
        auto stats=stepper.Advance(.1,loads);advanced=true;
        Check("rigid_translation_matches_literal_force_solution",(box.body->getCenterOfMassPosition()-btVector3(1.05,2.02,3)).length()<1e-9&&(box.body->getLinearVelocity()-btVector3(.5,.2,0)).length()<1e-9);
        Check("rigid_translation_work_and_loss",std::abs(stats.externalWork-.32)<1e-9&&std::abs(stats.numericalLoss-.16)<1e-9);
        Check("registered_rigid_has_one_authoritative_pose",w.world->getCollisionObjectArray().size()==1&&stepper.time==.1&&box.Valid());
    }catch(const std::exception &e){std::printf("coupled free-box rejection: %s\n",e.what());}
    Check("managed_rigid_box_can_advance",advanced);
}
void FreeSpin(){
    World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0},{0,0,0},{0,0,2});
    bool advanced=false;try{
        persistent::Stepper stepper(w,{}, {}, {}, {&box});auto stats=stepper.Advance(.1,Eigen::MatrixXd::Zero(8,3));advanced=true;
        auto basis=box.body->getCenterOfMassTransform().getBasis();double angle=std::atan2(basis[1][0],basis[0][0]);
        Check("free_spin_matches_constrained_implicit_solution",std::abs(angle-std::atan(.2))<1e-8&&std::abs(box.body->getAngularVelocity().z()-2/std::sqrt(1.04))<1e-8);
        Check("free_spin_keeps_exact_shape_and_tracks_projection",box.Valid()&&stats.rigidProjectionLoss>0&&stats.numericalLoss>stats.rigidProjectionLoss);
    }catch(const std::exception&e){std::printf("coupled spin rejection: %s\n",e.what());}
    Check("managed_rigid_rotation_can_advance",advanced);
}
Eigen::Vector3d Angular(const persistent::RigidBox &b){
    if(b.anchored)return Eigen::Vector3d::Zero();
    Eigen::Matrix3d inverse;auto &inv=b.body->getInvInertiaTensorWorld();for(int i=0;i<3;++i)for(int j=0;j<3;++j)inverse(i,j)=inv[i][j];
    return inverse.inverse()*Vec(b.body->getAngularVelocity())+Vec(b.body->getCenterOfMassPosition()).cross(b.mass*Vec(b.body->getLinearVelocity()));
}
double PointGap(const structure::Asset &point,const persistent::RigidBox &box){
    auto transform=box.body->getCenterOfMassTransform();double gap=1e100;
    for(auto f:box.faces){std::array<Eigen::Vector3d,3> corners;for(int i=0;i<3;++i){auto p=box.local[f[i]];corners[i]=Vec(transform*btVector3(p.x(),p.y(),p.z()));}
        gap=std::min(gap,std::sqrt(ipc::point_triangle_distance(Vec(point.nodes[0].x),corners[0],corners[1],corners[2])));}
    return gap;
}
void EccentricForce(){
    World w(SettingsFor());auto rotation=Eigen::Quaterniond(Eigen::AngleAxisd(.4,Eigen::Vector3d::UnitZ())*Eigen::AngleAxisd(.6,Eigen::Vector3d::UnitX()));
    persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0},{0,0,0},{0,0,0},rotation);
    Eigen::Matrix3d inverse;auto &inv=box.body->getInvInertiaTensorWorld();for(int i=0;i<3;++i)for(int j=0;j<3;++j)inverse(i,j)=inv[i][j];
    Eigen::Vector3d force(0,5,0),lever=rotation*box.local[0],expected=inverse*lever.cross(force)*1e-4;
    persistent::Stepper stepper(w,{}, {}, {}, {&box});Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(8,3);loads.row(0)=force;
    auto stats=stepper.Advance(1e-4,loads);
    Check("eccentric_load_uses_rotated_full_inertia",(Vec(box.body->getAngularVelocity())-expected).norm()<1e-8&&std::abs(inverse(0,1))>.01);
    Check("rigid_velocity_constraint_preserves_local_momentum",stats.rigidProjectionLinearError<1e-10&&stats.rigidProjectionAngularError<1e-10);
}
void PointContact(double offset,bool miss=false,bool supported=false){
    World w(SettingsFor());structure::Force force;structure::Asset point(w,force);point.Node({miss?1.2:offset,0,.25},1,{0,0,-1});point.Build();
    persistent::RigidBox box(w,{.5,.5,.05},8,{0,0,0},{0,0,0},{0,0,0},Eigen::Quaterniond::Identity(),supported);persistent::Stepper stepper(w,{&point}, {}, {}, {&box});
    auto initial=point.Momentum()+box.mass*box.body->getLinearVelocity();Eigen::Vector3d l0=Vec(point.Angular())+Angular(box);
    int contacts=0;double smallest=1e100,projectionError=0,torqueBalance=0;Eigen::Vector3d reaction=Eigen::Vector3d::Zero(),angularReaction=reaction;
    for(int tick=0;tick<120;++tick){auto s=stepper.Advance(.005,Eigen::MatrixXd::Zero(9,3));contacts=std::max(contacts,s.peakContacts);smallest=std::min(smallest,PointGap(point,box));projectionError=std::max(projectionError,s.rigidProjectionAngularError);torqueBalance=std::max(torqueBalance,s.torqueBalance);reaction+=s.supportLinear;angularReaction+=s.supportAngular;}
    double pError=(Vec(point.Momentum()+box.mass*box.body->getLinearVelocity()-initial)+reaction).norm(),lError=(Vec(point.Angular())+(supported?Eigen::Vector3d::Zero().eval():Angular(box))+angularReaction-l0).norm();
    std::printf("point-box offset=%.3g miss=%d gap_min=%.9g p_error=%.9g l_error=%.9g spin_y=%.9g\n",offset,miss,smallest,pError,lError,box.body->getAngularVelocity().y());
    Check(miss?"coupled_clear_miss_has_no_phantom_force":supported?"anchored_box_contact_has_explicit_reaction":"rigid_deformable_contact_has_two_dynamic_recipients",miss?contacts==0&&std::abs(point.nodes[0].x.z()+.35)<1e-8:contacts>0&&smallest>.002-1e-7&&(supported?reaction.norm()>.1:box.body->getLinearVelocity().z()<-.01));
    Check("coupled_momentum_and_internal_torque",pError<1e-6&&lError<.003&&projectionError<1e-10&&torqueBalance<1e-8);
    if(!miss&&offset>.1&&!supported)Check("off_center_contact_spins_actual_rigid_body",box.body->getAngularVelocity().y()>.01);
    if(supported)Check("anchored_box_stays_fixed",box.body->getCenterOfMassPosition().length()==0&&box.body->getLinearVelocity().length()==0&&box.body->getAngularVelocity().length()==0);
    auto center=box.body->getCenterOfMassPosition();btVector3 from=center+btVector3(0,0,2),to=center-btVector3(0,0,2);btCollisionWorld::ClosestRayResultCallback ray(from,to);w.world->rayTest(from,to,ray);
    Check("current_registered_box_geometry_matches_query",ray.hasHit()&&ray.m_collisionObject==box.body&&box.Valid());
}
bool SegmentTriangle(Eigen::Vector3d from,Eigen::Vector3d to,Eigen::Vector3d a,Eigen::Vector3d b,Eigen::Vector3d c){
    Eigen::Vector3d direction=to-from,e1=b-a,e2=c-a,h=direction.cross(e2);double det=e1.dot(h);if(std::abs(det)<1e-12)return false;
    double inverse=1/det;Eigen::Vector3d s=from-a;double u=inverse*s.dot(h);if(u<0||u>1)return false;
    Eigen::Vector3d q=s.cross(e1);double v=inverse*direction.dot(q);if(v<0||u+v>1)return false;double t=inverse*e2.dot(q);return t>=0&&t<=1;
}
using Triangle=std::array<Eigen::Vector3d,3>;
std::vector<Triangle> Triangles(const persistent::RigidBox &box){std::vector<Triangle> faces;auto t=box.body->getCenterOfMassTransform();for(auto f:box.faces){Triangle tri;for(int i=0;i<3;++i){auto p=box.local[f[i]];tri[i]=Vec(t*btVector3(p.x(),p.y(),p.z()));}faces.push_back(tri);}return faces;}
std::vector<Triangle> Triangles(const structure::Asset &a){std::vector<Triangle> faces;for(auto &f:a.surfaces)if(f.alive)faces.push_back({Vec(a.nodes[f.nodes[0]].x),Vec(a.nodes[f.nodes[1]].x),Vec(a.nodes[f.nodes[2]].x)});return faces;}
bool Intersects(const std::vector<Triangle> &a,const std::vector<Triangle> &b){for(auto f:a)for(auto g:b)for(int i=0;i<3;++i)if(SegmentTriangle(f[i],f[(i+1)%3],g[0],g[1],g[2])||SegmentTriangle(g[i],g[(i+1)%3],f[0],f[1],f[2]))return true;return false;}
void RotatingStrike(double omega,bool miss){
    World w(SettingsFor());structure::Force f;structure::Asset target(w,f);target.Node({miss?1.5:.75,0,0},1);target.Build();
    persistent::RigidBox blade(w,{1,.01,.01},4,{0,0,0},{0,0,0},{0,0,omega},Eigen::Quaterniond(Eigen::AngleAxisd(-.3,Eigen::Vector3d::UnitZ())));
    persistent::Stepper stepper(w,{&target},{{&target,0}}, {}, {&blade});bool ran=false;int revision=target.revision;auto original=blade.body->getCenterOfMassTransform();
    try{
        auto began=std::chrono::steady_clock::now();auto stats=stepper.Advance(.6/omega,Eigen::MatrixXd::Zero(9,3));double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();ran=true;
        auto local=blade.body->getCenterOfMassTransform().invXform(target.nodes[0].x);bool inside=std::abs(local.x())<1&&std::abs(local.y())<.01&&std::abs(local.z())<.01;
        std::printf("rotating box omega=%.6g miss=%d contacts=%d substeps=%d retries=%d gap=%.9g final_spin=%.9g newton=%d wall_s=%.6f\n",omega,miss,stats.peakContacts,stats.substeps,stats.retries,PointGap(target,blade),blade.body->getAngularVelocity().z(),stats.newton,elapsed);
        Check(miss?"curved_rigid_miss_stays_clear":"curved_rigid_strike_cannot_skip_fixed_target",!inside&&PointGap(target,blade)>.002-1e-7&&(miss?stats.peakContacts==0:stats.peakContacts>0));
        Check("angular_interval_is_resolved_before_publication",stats.substeps>=4&&blade.Valid());
        if(!miss)Check("rotor_strike_transmits_support_impulse",stats.supportLinear.norm()>.01&&blade.body->getAngularVelocity().z()<omega-.1);
    }catch(const std::exception&e){std::printf("rotating strike rejection: %s\n",e.what());Check("rejected_rotating_interval_retains_both_states",stepper.time==0&&target.revision==revision&&(persistent::RigidBox::Rotation(original)-persistent::RigidBox::Rotation(blade.body->getCenterOfMassTransform())).norm()<1e-12&&blade.body->getCenterOfMassPosition().length()==0&&blade.body->getAngularVelocity().z()==omega);}
    Check("force_driven_rotating_interval_resolves",ran);
}
void TwoBoxes(bool reverse){
    World w(SettingsFor());persistent::RigidBox a(w,{.2,.25,.05},2,{0,0,-.15},{0,0,1}),b(w,{.2,.25,.05},8,{0,0,.15},{0,0,-.4});
    std::vector<persistent::RigidBox*> boxes=reverse?std::vector<persistent::RigidBox*>{&b,&a}:std::vector<persistent::RigidBox*>{&a,&b};
    persistent::Stepper stepper(w,{}, {}, {}, boxes);Eigen::Vector3d p0=a.mass*Vec(a.body->getLinearVelocity())+b.mass*Vec(b.body->getLinearVelocity());int contacts=0,crossings=0;
    for(int i=0;i<90;++i){auto s=stepper.Advance(.005,Eigen::MatrixXd::Zero(16,3));contacts=std::max(contacts,s.peakContacts);crossings+=Intersects(Triangles(a),Triangles(b));}
    double error=(a.mass*Vec(a.body->getLinearVelocity())+b.mass*Vec(b.body->getLinearVelocity())-p0).norm();
    std::printf("two boxes reverse=%d contacts=%d intersection_frames=%d p_error=%.9g\n",reverse,contacts,crossings,error);
    Check("rigid_rigid_contact_keeps_local_surfaces_separate",contacts>0&&crossings==0&&a.body->getCenterOfMassPosition().z()<b.body->getCenterOfMassPosition().z());
    Check("rigid_rigid_ordering_retains_momentum",error<1e-6&&a.Valid()&&b.Valid());
}
void MountedPanel(bool tear=false){
    World w(SettingsFor());structure::Force f;structure::Asset panel(w,f);
    for(auto p:{btVector3(-.6,-.6,0),btVector3(.6,-.6,0),btVector3(.6,.6,0),btVector3(-.6,.6,0),btVector3(.6,.6,-.3)})panel.Node(p,1);
    for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)panel.Member(i,j,.001,{1e4,1e12,0,1,1e12,.5},"skin");
    int mount=panel.Member(2,4,.001,{1e4,1e12,0,1,1e12,.5},"mount");panel.Face(0,1,2);panel.Face(0,2,3);
    if(tear)panel.members[panel.Edge(1,2)].bar.material={1e4,5,0,.001,.0001,.5};panel.Build();
    persistent::RigidBox box(w,{.1,.1,.05},.5,{tear?.35:.4,tear?0:.4,.25},{0,0,-.5});persistent::Stepper stepper(w,{&panel},{{&panel,0},{&panel,1},{&panel,3},{&panel,4}}, {}, {&box});
    Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(13,3);for(int i=5;i<13;++i)loads(i,2)=-.25;
    Eigen::Vector3d p0=Vec(panel.Momentum())+box.mass*Vec(box.body->getLinearVelocity()),reaction=Eigen::Vector3d::Zero(),external=reaction;
    double force=0,deflection=0;int contacts=0,crossings=0,breaks=0;
    for(int i=0;i<(tear?240:160);++i){auto s=stepper.Advance(.005,loads);breaks+=s.topologyChanges;contacts=std::max(contacts,s.peakContacts);reaction+=s.supportLinear;external+=s.externalImpulse;force=std::max(force,std::abs(panel.members[mount].bar.force));deflection=std::max(deflection,-double(panel.nodes[2].x.z()));crossings+=Intersects(Triangles(panel),Triangles(box));}
    double error=(Vec(panel.Momentum())+box.mass*Vec(box.body->getLinearVelocity())+reaction-p0-external).norm();
    std::printf("mounted panel contacts=%d intersection_frames=%d mount_force=%.9g deflection=%.9g p_error=%.9g\n",contacts,crossings,force,deflection,error);
    Check("rigid_contact_loads_actual_deformable_mount",contacts>0&&force>.1&&deflection>.005&&crossings==0);
    Check("mounted_coupled_reactions_and_geometry_agree",error<1e-6&&panel.Synchronized()&&box.Valid());
    if(tear){std::printf("coupled tear breaks=%d faces=%d final_z=%.9g ray=%d plastic=%.9g\n",breaks,panel.SurfaceCount(),box.body->getCenterOfMassPosition().z(),panel.Ray({.35,0,1},{.35,0,-1}),panel.members[panel.Edge(1,2)].bar.accumulated);Check("coupled_tear_removes_face_and_releases_rigid_body",breaks==1&&panel.SurfaceCount()==1&&box.body->getCenterOfMassPosition().z()<-.1&&!panel.Ray({.35,0,1},{.35,0,-1}));}
}
void BoundaryChecks(){
    for(int scenario=0;scenario<5;++scenario){
        World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0});btPoint2PointConstraint constraint(*box.body,{0,0,0});
        persistent::Stepper stepper(w,{}, {}, {}, {&box});
        if(scenario==0)box.body->applyCentralForce({1,0,0});
        if(scenario==1)box.body->applyTorque({0,1,0});
        if(scenario==2)w.world->addConstraint(&constraint);
        if(scenario==3)box.body->setLinearVelocity({1,0,0});
        if(scenario==4)box.shape->setLocalScaling({2,1,1});
        bool rejected=false;try{stepper.Advance(.01,Eigen::MatrixXd::Zero(8,3));}catch(const std::invalid_argument&){rejected=true;}
        const char *names[]={"unsubmitted_sdk_force_rejected","unsubmitted_sdk_torque_rejected","unsupported_sdk_constraint_rejected","external_rigid_velocity_edit_rejected","external_rigid_shape_edit_rejected"};
        Check(names[scenario],rejected&&stepper.time==0&&box.body->getCenterOfMassPosition().length()==0);
        if(scenario==2)w.world->removeConstraint(&constraint);
    }
    World w(SettingsFor());structure::Force force;structure::Asset point(w,force);point.Node({2,0,0},1);point.Build();persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0});
    persistent::Options options;options.maxNewton=1;options.maxSubdivisions=0;persistent::Stepper stepper(w,{&point}, {}, options, {&box});
    Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(9,3);loads(0,1)=1;loads(1,0)=1;int revision=point.revision;bool rejected=false;
    try{stepper.Advance(.01,loads);}catch(const std::runtime_error&){rejected=true;}
    Check("coupled_failure_rolls_back_both_recipients",rejected&&stepper.time==0&&point.revision==revision&&point.nodes[0].x==btVector3(2,0,0)&&box.body->getCenterOfMassPosition().length()==0&&point.Synchronized());
}
void CurvedLoadReceipt(){
    World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0},{0,0,0},{0,0,2});
    auto before=box.body->getCenterOfMassTransform();persistent::Stepper stepper(w,{}, {}, {}, {&box});Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(8,3);Eigen::Vector3d force(0,.01,0);loads.row(0)=force;
    auto stats=stepper.Advance(.1,loads);auto after=box.body->getCenterOfMassTransform();Eigen::AngleAxisd rotation(persistent::RigidBox::Rotation(after)*persistent::RigidBox::Rotation(before).transpose());
    Eigen::Vector3d mean=Eigen::Vector3d::Zero(),local=persistent::RigidBox::Rotation(before)*box.local[0];
    for(int i=0;i<=200;++i){double t=i/200.;double weight=(i==0||i==200)?1:i%2?4:2;mean+=weight*(Vec(before.getOrigin())+t*(Vec(after.getOrigin())-Vec(before.getOrigin()))+Eigen::AngleAxisd(t*rotation.angle(),rotation.axis())*local);}
    mean/=600;Eigen::Vector3d expected=.1*mean.cross(force);
    Check("external_angular_receipt_follows_curved_material_point",(stats.externalAngular-expected).norm()<1e-11&&stats.substeps==1);
}
void InitialSolidGeometry(){
    for(int scenario=0;scenario<6;++scenario){
        World w(SettingsFor());structure::Force f;structure::Asset soft(w,f);std::vector<structure::Asset*> assets;
        std::vector<std::unique_ptr<persistent::RigidBox>> storage;std::vector<persistent::RigidBox*> boxes;
        auto add=[&](Eigen::Vector3d half,Eigen::Vector3d at){auto b=std::make_unique<persistent::RigidBox>(w,half,1,at);boxes.push_back(b.get());storage.push_back(std::move(b));};
        if(scenario==0){soft.Node({0,0,0},1);soft.Build();assets.push_back(&soft);add({.5,.5,.5},{0,0,0});}
        if(scenario==1){add({.5,.5,.5},{0,0,0});add({.1,.1,.1},{0,0,0});}
        if(scenario==2||scenario==5){add({1,.1,.1},{0,0,0});add({.1,1,.1},{0,0,scenario==2?.05:.4});}
        if(scenario==3||scenario==4){double z=scenario==3?0:.3;soft.Node({-1,-1,z},1);soft.Node({1,-1,z},1);soft.Node({0,1,z},1);for(int i=0;i<3;++i)soft.Member(i,(i+1)%3,.01,{1000,1e9,0,1,1e9,.5},"skin");soft.Face(0,1,2);soft.Build();assets.push_back(&soft);add({.1,.1,.1},{0,0,0});}
        bool rejected=false;int revision=soft.revision;try{persistent::Stepper stepper(w,assets, {}, {}, boxes);stepper.Advance(.01,Eigen::MatrixXd::Zero(soft.nodes.size()+8*boxes.size(),3));}catch(const std::invalid_argument&){rejected=true;}
        const char *names[]={"embedded_point_rejected_before_motion","nested_solid_boxes_rejected_before_motion","crossed_boxes_without_contained_corners_rejected","soft_face_crossing_solid_box_rejected","separate_soft_face_remains_clear","separate_crossed_axes_boxes_remain_clear"};
        Check(names[scenario],scenario<4?rejected&&soft.revision==revision:!rejected);
    }
}
void NonfiniteSdkGeometry(){
    for(bool late:{false,true}){
        World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0});bool rejected=false;double time=0;
        auto corrupt=[&](){box.shape->setLocalScaling({std::numeric_limits<double>::quiet_NaN(),1,1});};
        if(!late)corrupt();try{persistent::Stepper stepper(w,{}, {}, {}, {&box});if(late)corrupt();stepper.Advance(.01,Eigen::MatrixXd::Zero(8,3));time=stepper.time;}catch(const std::invalid_argument&){rejected=true;}
        Check(late?"nan_sdk_scaling_after_construction_rejected":"nan_sdk_scaling_before_construction_rejected",rejected&&!box.Valid()&&time==0&&box.body->getCenterOfMassPosition().length()==0&&box.body->getLinearVelocity().length()==0);
    }
}
void Benchmark(int pairs){
    World w(SettingsFor());structure::Force force;std::vector<std::unique_ptr<structure::Asset>> soft;std::vector<std::unique_ptr<persistent::RigidBox>> rigid;
    std::vector<structure::Asset*> assets;std::vector<persistent::RigidBox*> boxes;
    for(int i=0;i<pairs;++i){auto point=std::make_unique<structure::Asset>(w,force);point->Node({3.*i+.25,0,.25},1,{0,0,-1});point->Build();assets.push_back(point.get());soft.push_back(std::move(point));
        auto box=std::make_unique<persistent::RigidBox>(w,Eigen::Vector3d(.5,.5,.05),8,Eigen::Vector3d(3.*i,0,0));boxes.push_back(box.get());rigid.push_back(std::move(box));}
    persistent::Stepper stepper(w,assets, {}, {}, boxes);int newton=0,substeps=0,retries=0,peak=0;auto start=std::chrono::steady_clock::now();
    for(int i=0;i<120;++i){auto s=stepper.Advance(.005,Eigen::MatrixXd::Zero(9*pairs,3));newton+=s.newton;substeps+=s.substeps;retries+=s.retries;peak=std::max(peak,s.peakContacts);}
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::printf("coupled benchmark pairs=%d generalized_dof=%d quadrature_vertices=%d rigid_triangles=%d steps=120 simulated_s=.6 wall_s=%.6f us_per_step=%.6f newton=%d accepted_substeps=%d retries=%d peak_contacts=%d peak_rss_bytes=%ld\n",pairs,9*pairs,9*pairs,12*pairs,seconds,1e6*seconds/120,newton,substeps,retries,peak,usage.ru_maxrss);
}
double SpinError(double dt){
    World w(SettingsFor());persistent::RigidBox box(w,{.2,.3,.4},8,{0,0,0},{0,0,0},{.5,1,1.5});persistent::Stepper stepper(w,{}, {}, {}, {&box});
    auto l0=Angular(box);double projection=0,loss=0;for(int i=0;i<int(std::lround(.2/dt));++i){auto s=stepper.Advance(dt,Eigen::MatrixXd::Zero(8,3));projection+=s.rigidProjectionLoss;loss+=s.numericalLoss;}
    double error=(Angular(box)-l0).norm();std::printf("rigid free spin dt=%.6g angular_error=%.9g projection_loss=%.9g total_loss=%.9g\n",dt,error,projection,loss);return error;
}
double FastStrikeRefinement(int intervals){
    World w(SettingsFor());structure::Force f;structure::Asset target(w,f);target.Node({.75,0,0},1);target.Build();
    persistent::RigidBox blade(w,{1,.01,.01},4,{0,0,0},{0,0,0},{0,0,144},Eigen::Quaterniond(Eigen::AngleAxisd(-.3,Eigen::Vector3d::UnitZ())));
    auto kinetic=[&](){Eigen::Vector3d spin=Vec(blade.body->getAngularVelocity());auto rotation=persistent::RigidBox::Rotation(blade.body->getCenterOfMassTransform());return .5*blade.mass*blade.body->getLinearVelocity().length2()+.5*spin.dot(rotation*blade.inertia.asDiagonal()*rotation.transpose()*spin);};
    persistent::Stepper stepper(w,{&target},{{&target,0}}, {}, {&blade});Eigen::Vector3d l0=Angular(blade),reaction=Eigen::Vector3d::Zero(),angularReaction=reaction;
    double initial=kinetic(),loss=0,contact=0,gap=1e100;int substeps=0,retries=0;bool resolved=false;
    try{for(int i=0;i<intervals;++i){auto s=stepper.Advance(.6/(144*intervals),Eigen::MatrixXd::Zero(9,3));reaction+=s.supportLinear;angularReaction+=s.supportAngular;loss+=s.numericalLoss;contact=s.contactEnergy+s.contactRelease;gap=std::min(gap,PointGap(target,blade));substeps+=s.substeps;retries+=s.retries;}resolved=true;}
    catch(const std::exception&e){std::printf("fast refinement rejection intervals=%d %s\n",intervals,e.what());}
    double pError=(blade.mass*Vec(blade.body->getLinearVelocity())+reaction).norm(),lError=(Angular(blade)+angularReaction-l0).norm(),energyError=std::abs(initial-kinetic()-contact-loss);
    std::printf("fast strike intervals=%d substeps=%d retries=%d gap_min=%.12g p_error=%.9g angular_error=%.9g energy_error=%.9g numerical_loss=%.9g final_spin=%.9g\n",intervals,substeps,retries,gap,pError,lError,energyError,loss,blade.body->getAngularVelocity().z());
    Check("refined_fast_strike_resolves_with_balanced_reaction_and_work",resolved&&gap>.002-1e-10&&pError<1e-6&&energyError<1e-5&&std::abs(stepper.time-.6/144)<1e-12);
    return resolved?lError:std::numeric_limits<double>::infinity();
}
int main(int argc,char **argv){std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1&&std::string(argv[1])=="--limits"){RotatingStrike(144,false);std::printf("coupled fast-rotation gate: %d/%d passed\n",checks-failures,checks);return failures?1:0;}
    if(argc>1&&std::string(argv[1])=="--benchmark"){Benchmark(1);Benchmark(4);return 0;}
    if(argc>1)return 2;
    FreeBox();FreeSpin();EccentricForce();PointContact(0);PointContact(.25);PointContact(0,true);PointContact(.25,false,true);
    RotatingStrike(6,false);RotatingStrike(144,false);RotatingStrike(144,true);TwoBoxes(false);TwoBoxes(true);MountedPanel();MountedPanel(true);BoundaryChecks();CurvedLoadReceipt();
    double coarse=SpinError(.02),fine=SpinError(.01),finer=SpinError(.005);Check("asymmetric_rigid_spin_error_converges",fine<.7*coarse&&finer<.7*fine&&finer>0);
    InitialSolidGeometry();NonfiniteSdkGeometry();
    double fastCoarse=FastStrikeRefinement(4),fastFine=FastStrikeRefinement(8),fastFiner=FastStrikeRefinement(16);Check("fast_contact_angular_error_decreases_with_refinement",fastFine<fastCoarse&&fastFiner<fastFine);
    std::printf("coupled matrix: %d/%d passed\n",checks-failures,checks);return failures?1:0;}
