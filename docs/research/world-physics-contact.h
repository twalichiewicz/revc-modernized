#pragma once
// Research-only inelastic, frictionless normal contact. Inputs reference the
// actual coupled-world participants at a validated contact event. No pose
// integration, health/damage budget, friction or contact-cache owner lives here.
#include <BulletSoftBody/btSoftBody.h>
#include <btBulletDynamicsCommon.h>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>
namespace continuous {
struct WeightedNode {btSoftBody::Node *node;double weight;};
struct Point {
    btRigidBody *body=nullptr;btVector3 location{0,0,0};std::vector<WeightedNode> nodes;
    static Point Rigid(btRigidBody &body,btVector3 at) {return {&body,at,{}};}
    static Point Nodes(std::initializer_list<WeightedNode> list) {return {nullptr,{0,0,0},list};}
};
struct Receipt {double impulse=0,dissipated=0;btVector3 fixedLinear{0,0,0},fixedAngular{0,0,0};};
inline bool FiniteVector(btVector3 v){return std::isfinite(double(v.length2()));}
inline btVector3 Position(const Point &p) {
    if(p.body) {
        auto &b=*p.body;
        if(!p.nodes.empty()||!FiniteVector(p.location)||!FiniteVector(b.getLinearVelocity())||!FiniteVector(b.getAngularVelocity())||
           !FiniteVector(b.getWorldTransform().getOrigin())||!std::isfinite(double(b.getInvMass()))||b.getInvMass()<0)
            throw std::invalid_argument("Invalid rigid contact state");
        if((b.getLinearFactor()-btVector3(1,1,1)).length2()!=0||(b.getAngularFactor()-btVector3(1,1,1)).length2()!=0)
            throw std::invalid_argument("Constrained rigid factors require a different contact metric");
        for(int i=0;i<3;++i)if(!FiniteVector(b.getInvInertiaTensorWorld()[i]))throw std::invalid_argument("Invalid rigid inertia");
        if(b.getInvMass()==0&&(b.getLinearVelocity().length2()!=0||b.getAngularVelocity().length2()!=0))
            throw std::invalid_argument("Driven support work is outside this contact policy");
        return p.location;
    }
    if(p.nodes.empty())throw std::invalid_argument("Empty contact point");
    double sum=0;btVector3 result(0,0,0);
    for(auto w:p.nodes) {
        if(!w.node||!std::isfinite(w.weight)||w.weight<0||!FiniteVector(w.node->m_x)||!FiniteVector(w.node->m_v)||
           !std::isfinite(double(w.node->m_im))||w.node->m_im<0)throw std::invalid_argument("Invalid weighted contact state");
        if(w.node->m_im==0&&w.node->m_v.length2()!=0)throw std::invalid_argument("Driven node support work is unsupported");
        sum+=w.weight;result+=w.node->m_x*w.weight;
    }
    if(std::abs(sum-1)>1e-10||!FiniteVector(result))throw std::invalid_argument("Invalid contact weights");
    return result;
}
inline btVector3 Velocity(const Point &p) {
    if(p.body)return p.body->getVelocityInLocalPoint(p.location-p.body->getWorldTransform().getOrigin());
    btVector3 result(0,0,0);for(auto w:p.nodes)result+=w.node->m_v*w.weight;return result;
}
inline Receipt SolveNormal(const Point &a,const Point &b,btVector3 normal) {
    if(!FiniteVector(normal)||std::abs(double(normal.length2())-1)>1e-10)throw std::invalid_argument("Normal must be finite and unit length");
    auto pa=Position(a),pb=Position(b),gap=pa-pb;
    if(gap.cross(normal).length()>1e-7*(1+gap.length()))throw std::invalid_argument("Contact points/normal would introduce a spurious couple");
    if(a.body&&a.body==b.body)throw std::invalid_argument("Rigid self-contact is not a dynamic constraint");
    // Combine repeated participants before J M^-1 J^T, including nodes shared
    // by both stencils. Squaring each occurrence separately is incorrect.
    std::map<btSoftBody::Node*,double> stencil;
    for(auto w:a.nodes)stencil[w.node]+=w.weight;
    for(auto w:b.nodes)stencil[w.node]-=w.weight;
    double inverse=0;
    for(auto w:stencil)inverse+=w.second*w.second*w.first->m_im;
    for(const auto *p:{&a,&b})if(p->body) {
        auto lever=p->location-p->body->getWorldTransform().getOrigin();auto angular=lever.cross(normal);
        inverse+=p->body->getInvMass()+angular.dot(p->body->getInvInertiaTensorWorld()*angular);
    }
    const double vn=(Velocity(a)-Velocity(b)).dot(normal);
    if(!std::isfinite(inverse)||inverse<0||!std::isfinite(vn))throw std::invalid_argument("Invalid effective mass or relative speed");
    Receipt result;
    if(vn>=0||inverse==0)return result;
    result.impulse=-vn/inverse;result.dissipated=-.5*vn*result.impulse;
    if(!std::isfinite(result.impulse)||!std::isfinite(result.dissipated))throw std::invalid_argument("Contact impulse overflow");
    const auto impulse=normal*result.impulse;
    // Validate every prospective output before mutating any participant.
    for(auto w:stencil)if(!FiniteVector(w.first->m_v+impulse*(w.second*w.first->m_im)))throw std::invalid_argument("Contact velocity overflow");
    for(int i=0;i<2;++i) {
        const auto &p=i?b:a;const auto j=i?-impulse:impulse;
        if(p.body) {
            auto r=p.location-p.body->getWorldTransform().getOrigin();
            if(!FiniteVector(p.body->getLinearVelocity()+j*p.body->getInvMass())||
               !FiniteVector(p.body->getAngularVelocity()+p.body->getInvInertiaTensorWorld()*r.cross(j)))throw std::invalid_argument("Rigid contact overflow");
        }
    }
    for(auto w:stencil) {
        auto *node=w.first;auto j=impulse*w.second;
        if(node->m_im>0)node->m_v+=j*node->m_im;
        else {result.fixedLinear+=j;result.fixedAngular+=node->m_x.cross(j);}
    }
    for(int i=0;i<2;++i) {
        const auto &p=i?b:a;const auto j=i?-impulse:impulse;
        if(p.body) {
            if(p.body->getInvMass()>0) {p.body->applyImpulse(j,p.location-p.body->getWorldTransform().getOrigin());p.body->activate(true);}
            else {result.fixedLinear+=j;result.fixedAngular+=p.location.cross(j);}
        }
    }
    return result;
}
}
