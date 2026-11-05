// Actual Bullet participants; one frictionless normal-contact operator for
// rigid/weighted-node, weighted-edge/edge and rigid/rigid events.
#define main BaselineProbeMain
#include "world-physics-probe.cpp"
#undef main
#include "world-physics-structure.h"
#include "world-physics-contact.h"
#include "world-physics-ccd.h"
#include "world-physics-witness.h"
#include <ipc/tangent/closest_point.hpp>
#include <limits>
int checks=0,failed=0;
void Expect(const char *name,bool pass){++checks;failed+=!pass;std::printf("contact-event %s %s\n",name,pass?"PASS":"FAIL");}
Settings Options(){Settings s;s.explicitForces=true;return s;}
structure::Material Elastic(){return {1000,1e12,0,1,1e12,.5};}
struct Totals {btVector3 p{0,0,0},l{0,0,0};double energy=0;};
Totals Measure(const World &w,const structure::Asset *asset=nullptr) {
    Totals t;
    for(auto &b:w.rigid)if(b->getInvMass()>0) {
        auto p=b->getLinearVelocity()/b->getInvMass(),spin=b->getInvInertiaTensorWorld().inverse()*b->getAngularVelocity();
        t.p+=p;t.l+=b->getWorldTransform().getOrigin().cross(p)+spin;
        t.energy+=.5*(p.dot(b->getLinearVelocity())+spin.dot(b->getAngularVelocity()));
    }
    if(asset)for(auto n:asset->binding)if(n->m_im>0) {
        auto p=n->m_v/n->m_im;t.p+=p;t.l+=n->m_x.cross(p);t.energy+=.5*p.dot(n->m_v);
    }
    return t;
}
void Balance(const Totals &before,const Totals &after,const continuous::Receipt &r) {
    Expect("linear_impulse_including_fixed_reaction",(after.p-before.p+r.fixedLinear).length()<1e-8);
    Expect("angular_impulse_including_fixed_reaction",(after.l-before.l+r.fixedAngular).length()<1e-8);
    // The moving-skin control carries 1e9J of bulk kinetic energy; its
    // subtraction has a double-precision rounding floor above 1e-8J.
    double error=std::abs(before.energy-after.energy-r.dissipated);
    double tolerance=1e-8+32*std::numeric_limits<double>::epsilon()*(std::abs(before.energy)+std::abs(after.energy));
    if(error>1e-8)std::printf("energy_roundoff residual_J=%.12g tolerance_J=%.12g bulk_J=%.12g\n",error,tolerance,before.energy);
    Expect("lost_kinetic_energy_matches_normal_work",error<tolerance&&r.dissipated>=0);
}
void FaceEvent(bool reversed,bool rotated,double scale) {
    World w(Options());structure::Force force;structure::Asset a(w,force);
    btMatrix3x3 rotation;rotation.setIdentity();if(rotated)rotation=btMatrix3x3(btQuaternion(btVector3(.3,.5,.8).normalized(),.8));
    const btVector3 shift(3,-2,1),drift(.7,-.2,.8);
    a.Node(shift+rotation*btVector3(-1,-1,0),scale,drift+rotation*btVector3(.2,.1,.5));
    a.Node(shift+rotation*btVector3(1,-1,0),2*scale,drift+rotation*btVector3(-.3,.2,-.4));
    a.Node(shift+rotation*btVector3(0,1,0),4*scale,drift+rotation*btVector3(.5,-.1,.2));
    for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)a.Member(i,j,.001,Elastic(),"skin");a.Face(0,1,2);a.Build();
    const auto at=shift+rotation*btVector3(.1,0,0),n=rotation*btVector3(0,0,1);
    auto ball=w.Body(new btSphereShape(.01),.5*scale,at+n*.01,drift+rotation*btVector3(0,0,-6));
    auto face=continuous::Point::Nodes({{a.binding[0],.2},{a.binding[1],.3},{a.binding[2],.5}}),rigid=continuous::Point::Rigid(*ball,at);
    auto before=Measure(w,&a);auto va=a.binding[0]->m_v,vb=a.binding[1]->m_v,vc=a.binding[2]->m_v,vr=ball->getLinearVelocity();
    auto r=reversed?continuous::SolveNormal(face,rigid,-n):continuous::SolveNormal(rigid,face,n);
    const double want=2432./859*scale;
    Expect("asymmetric_rigid_face_literal_impulse",std::abs(r.impulse-want)<1e-9);
    Expect("each_mass_receives_barycentric_impulse",(a.binding[0]->m_v-va+n*(want*.2/scale)).length()<1e-9&&
        (a.binding[1]->m_v-vb+n*(want*.3/(2*scale))).length()<1e-9&&(a.binding[2]->m_v-vc+n*(want*.5/(4*scale))).length()<1e-9&&
        (ball->getLinearVelocity()-vr-n*(want/(.5*scale))).length()<1e-9);
    Balance(before,Measure(w,&a),r);
    auto again=reversed?continuous::SolveNormal(face,rigid,-n):continuous::SolveNormal(rigid,face,n);
    Expect("repeat_contact_cannot_spend_work_twice",again.impulse<1e-12&&again.dissipated<1e-12);
}
void EdgeEvent() {
    World w(Options());structure::Force force;structure::Asset a(w,force);
    a.Node({-1,0,.001},2,{.1,.2,-2});a.Node({3,0,.001},3,{-.3,.1,-4});
    a.Node({0,-3,0},5,{.2,-.2,.5});a.Node({0,2,0},7,{.3,.1,1});
    a.Member(0,1,.001,Elastic(),"edge");a.Member(2,3,.001,Elastic(),"edge");a.Member(0,2,.001,Elastic(),"bridge");a.Build();
    auto one=continuous::Point::Nodes({{a.binding[0],.75},{a.binding[1],.25}}),two=continuous::Point::Nodes({{a.binding[2],.4},{a.binding[3],.6}});
    auto before=Measure(w,&a);auto r=continuous::SolveNormal(one,two,{0,0,1});
    Expect("self_edge_edge_literal_impulse",std::abs(r.impulse-277200./32383)<1e-9);
    Balance(before,Measure(w,&a),r);
}
void RigidEvent(bool fixed,bool rotated) {
    World w(Options());
    btMatrix3x3 rotation;rotation.setIdentity();if(rotated)rotation=btMatrix3x3(btQuaternion(btVector3(.7,.1,.4).normalized(),1.1));
    const btVector3 shift(4,-3,2),normal=rotation*btVector3(0,0,1),at=shift+rotation*btVector3(1,.5,.5);
    auto box=w.Body(new btBoxShape({1,2,.5}),fixed?0:2,shift,fixed?btVector3(0,0,0):rotation*btVector3(0,0,2));
    auto transform=box->getWorldTransform();transform.setBasis(rotation);box->setCenterOfMassTransform(transform);
    if(!fixed)box->setAngularVelocity(rotation*btVector3(0,.2,0));
    auto ball=w.Body(new btSphereShape(.2),1,shift+rotation*btVector3(1,.5,.7),rotation*btVector3(0,0,-1));
    auto one=continuous::Point::Rigid(*ball,at),two=continuous::Point::Rigid(*box,at);
    auto before=Measure(w);auto r=continuous::SolveNormal(one,two,normal);
    Expect(fixed?"fixed_support_receives_explicit_reaction":"eccentric_rigid_contact_uses_spin_inertia",std::abs(r.impulse-(fixed?1.:238./237))<1e-9);
    Balance(before,Measure(w),r);
    ball->setLinearVelocity(normal*5);auto separating=continuous::SolveNormal(one,two,normal);
    Expect("separating_contact_has_no_impulse",separating.impulse==0&&separating.dissipated==0);
    bool rejected=false;try{continuous::SolveNormal(one,two,{0,0,0});}catch(const std::invalid_argument&){rejected=true;}
    Expect("reject_invalid_normal_before_mutation",rejected);
}

continuous::Vec EigenVector(btVector3 x){return {x.x(),x.y(),x.z()};}
btVector3 BulletVector(continuous::Vec x){return {x.x(),x.y(),x.z()};}
void Panel(structure::Asset &a,double z,btVector3 velocity) {
    for(auto p:{btVector3(-1,-1,z),btVector3(1,-1,z),btVector3(1,1,z),btVector3(-1,1,z)})a.Node(p,5,velocity);
    for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)a.Member(i,j,.001,Elastic(),"skin");
    a.Face(0,1,2);a.Face(0,2,3);a.Build();
}
// First-event replay only: prescribed force-free motion advances to the
// detected event, then the common operator acts on the registered bodies.
// This does not implement a persistent-contact or force-driven timestepper.
void SweptShot(double speed,bool moving,bool hole,bool empty) {
    World w(Options());structure::Force force;structure::Asset a(w,force);
    Panel(a,moving?.5:0,moving?btVector3(0,0,-speed):btVector3(0,0,0));
    if(hole){a.members[a.Edge(0,1)].bar.material={1000,10,0,.05,.01,.5};a.nodes[0].x.setX(-1.5);a.Prepare();}
    auto ball=w.Body(new btSphereShape(.01),.01,{empty?3.:.5,-.2,moving?0.:.5},moving?btVector3(0,0,0):btVector3(0,0,-speed));
    constexpr double dt=1./480;
    auto from=EigenVector(ball->getWorldTransform().getOrigin());
    continuous::LinearPath point(from,from+EigenVector(ball->getLinearVelocity())*dt);
    std::vector<continuous::LinearPath> paths;for(auto n:a.binding)paths.emplace_back(EigenVector(n->m_x),EigenVector(n->m_x+n->m_v*dt));
    continuous::Result closest{false,1,0,0};int face=-1;
    for(int i=0;i<int(a.surfaces.size());++i)if(a.surfaces[i].alive) {
        auto ids=a.surfaces[i].nodes;
        auto r=continuous::Sweep(continuous::Primitive::PointTriangle,{&point,&paths[ids[0]],&paths[ids[1]],&paths[ids[2]]},.01);
        if(r.hit&&(!closest.hit||r.time<closest.time)){closest=r;face=i;}
    }
    auto before=Measure(w,&a);
    double at=closest.hit?closest.time:1;
    for(int i=0;i<int(paths.size());++i){a.binding[i]->m_x=BulletVector(paths[i](at));a.binding[i]->m_q=a.binding[i]->m_x;}
    auto transform=ball->getWorldTransform();transform.setOrigin(BulletVector(point(at)));ball->setCenterOfMassTransform(transform);a.Pull();
    continuous::Receipt receipt;
    if(face>=0) {
        auto ids=a.surfaces[face].nodes;auto x=EigenVector(ball->getWorldTransform().getOrigin());
        auto bary=continuous::TriangleWeights(x,paths[ids[0]](at),paths[ids[1]](at),paths[ids[2]](at));
        double b0=bary[0],b1=bary[1],b2=bary[2];
        auto surface=a.binding[ids[0]]->m_x*b0+a.binding[ids[1]]->m_x*b1+a.binding[ids[2]]->m_x*b2;
        auto normal=(ball->getWorldTransform().getOrigin()-surface).normalized();
        auto rigid=continuous::Point::Rigid(*ball,ball->getWorldTransform().getOrigin()-normal*.01);
        auto soft=continuous::Point::Nodes({{a.binding[ids[0]],b0},{a.binding[ids[1]],b1},{a.binding[ids[2]],b2}});
        receipt=continuous::SolveNormal(rigid,soft,normal);
        Expect("swept_contact_stops_normal_approach",(continuous::Velocity(rigid)-continuous::Velocity(soft)).dot(normal)>-1e-6);
    }
    std::printf("swept-shot speed=%.0f moving=%d hole=%d empty=%d hit=%d time=%.12g impulse=%.12g dissipation=%.12g\n",speed,moving,hole,empty,closest.hit,at,receipt.impulse,receipt.dissipated);
    bool solid=!hole&&!empty;
    Expect("current_topology_controls_swept_impact",closest.hit==solid);
    Expect("swept_shot_literal_material_impulse",std::abs(receipt.impulse-(solid?speed*1000/100069.:0))<1e-7);
    Balance(before,Measure(w,&a),receipt);
}
void SweptRotor(double angle) {
    World w(Options());
    auto blade=w.Body(new btCapsuleShapeX(.005,3.99),2,{0,0,0});
    double sign=angle<0?-1:1;blade->setAngularVelocity({0,-144*sign,0});
    auto target=w.Body(new btSphereShape(.03),1,{1.5*std::cos(.3),0,sign*1.5*std::sin(.3)});
    auto targetAt=EigenVector(target->getWorldTransform().getOrigin());
    continuous::LinearPath point(targetAt,targetAt);
    continuous::RigidPath a({-1.995,0,0},{0,0,0},{0,0,0},{0,-1,0},angle),b({1.995,0,0},{0,0,0},{0,0,0},{0,-1,0},angle);
    auto event=continuous::Sweep(continuous::Primitive::PointEdge,{&point,&a,&b,nullptr},.035);
    Expect("rotor_event_detected_before_pose_is_clear_again",event.hit&&event.time>0&&event.time<1);
    auto before=Measure(w);
    btTransform pose;pose.setIdentity();pose.setRotation(btQuaternion({0,-1,0},angle*event.time));blade->setCenterOfMassTransform(pose);
    auto e0=a(event.time),e1=b(event.time);double weight=ipc::point_edge_closest_point(targetAt,e0,e1);
    auto axisPoint=BulletVector(e0+(e1-e0)*weight),normal=(target->getWorldTransform().getOrigin()-axisPoint).normalized();
    auto one=continuous::Point::Rigid(*target,target->getWorldTransform().getOrigin()-normal*.03);
    auto two=continuous::Point::Rigid(*blade,axisPoint+normal*.005);
    auto receipt=continuous::SolveNormal(one,two,normal);
    std::printf("swept-rotor angle=%.12g time=%.12g impulse=%.12g omega_after=%.12g\n",angle,event.time,receipt.impulse,double(blade->getAngularVelocity().length()));
    Expect("rotor_contact_transfers_torque_and_motion",receipt.impulse>0&&target->getLinearVelocity().length()>0&&blade->getAngularVelocity().length()<144);
    Expect("rotor_event_stops_normal_approach",(continuous::Velocity(one)-continuous::Velocity(two)).dot(normal)>-1e-6);
    Balance(before,Measure(w),receipt);
}
void SharedStencil() {
    World w(Options());structure::Force force;structure::Asset a(w,force);
    a.Node({0,0,.002},1,{0,0,-3});a.Node({0,0,0},2,{0,0,1});a.Node({0,0,-.002},4,{0,0,2});
    a.Member(0,1,.001,Elastic(),"edge");a.Member(1,2,.001,Elastic(),"edge");a.Build();
    auto one=continuous::Point::Nodes({{a.binding[0],.5},{a.binding[1],.5}}),two=continuous::Point::Nodes({{a.binding[1],.5},{a.binding[2],.5}});
    auto before=Measure(w,&a);auto receipt=continuous::SolveNormal(one,two,{0,0,1});
    Expect("shared_participant_cancels_in_contact_jacobian",std::abs(receipt.impulse-8)<1e-12&&std::abs(a.binding[1]->m_v.z()-1)<1e-12);
    Balance(before,Measure(w,&a),receipt);
}


void FixedNodes() {
    World w(Options());structure::Force force;structure::Asset a(w,force);
    a.Node({-1,-1,0},1);a.Node({1,-1,0},2);a.Node({0,1,0},4);
    for(int i=0;i<3;++i)for(int j=i+1;j<3;++j)a.Member(i,j,.001,Elastic(),"support");a.Face(0,1,2);a.Build();
    for(auto n:a.binding)n->m_im=0;
    auto ball=w.Body(new btSphereShape(.01),.5,{.1,0,.01},{0,0,-6});
    auto one=continuous::Point::Rigid(*ball,{.1,0,0}),two=continuous::Point::Nodes({{a.binding[0],.2},{a.binding[1],.3},{a.binding[2],.5}});
    auto before=Measure(w,&a);auto receipt=continuous::SolveNormal(one,two,{0,0,1});
    Expect("fixed_node_stencil_has_literal_support_impulse",std::abs(receipt.impulse-3)<1e-12&&(receipt.fixedLinear-btVector3(0,0,-3)).length()<1e-12);
    Balance(before,Measure(w,&a),receipt);
    auto bad=two;bad.nodes[0].weight=.4;bool rejected=false;auto velocity=ball->getLinearVelocity();
    try{continuous::SolveNormal(one,bad,{0,0,1});}catch(const std::invalid_argument&){rejected=true;}
    Expect("invalid_stencil_cannot_mutate_other_participant",rejected&&(ball->getLinearVelocity()-velocity).length()==0);
}

int main(){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    for(bool reverse:{false,true})for(bool rotate:{false,true})for(double scale:{1.,20.})FaceEvent(reverse,rotate,scale);
    EdgeEvent();for(bool rotated:{false,true}){RigidEvent(false,rotated);RigidEvent(true,rotated);}SharedStencil();FixedNodes();
    for(double speed:{1000.,10000.})for(bool moving:{false,true}) {SweptShot(speed,moving,false,false);SweptShot(speed,moving,true,false);SweptShot(speed,moving,false,true);}
    for(double angle:{.6,2*std::acos(-1.),-.6})SweptRotor(angle);
    std::printf("contact event matrix: %d/%d passed\n",checks-failed,checks);return failed?1:0;
}
