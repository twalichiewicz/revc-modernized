#pragma once
// Research rigid asset: a box surface and eight authored corner masses. This is
// a lumped mass distribution, not the inertia of a uniform solid box. Bullet
// registers the one actual body; the reduced solver owns its advancement.
#include <Eigen/Geometry>
namespace persistent {
struct RigidBox {
    using Vec=Eigen::Vector3d;
    btRigidBody *body=nullptr;
    const Vec half;
    const double mass;
    const bool anchored;
    const Vec inertia;
    const std::array<Vec,8> local;
    const std::array<std::array<int,3>,12> faces{{{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},{2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}}};
    btCollisionShape *shape=nullptr;int flags=0;
    static Vec Point(btVector3 x){return {x.x(),x.y(),x.z()};}
    static Eigen::Matrix3d Rotation(const btTransform &t){Eigen::Matrix3d r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r(i,j)=t.getBasis()[i][j];return r;}
    static Vec Inertia(Vec h,double m) {
        if(!h.allFinite()||(h.array()<=0).any()||!std::isfinite(m)||m<=0||!std::isfinite(1/m)||m/8<=0)throw std::invalid_argument("Invalid rigid box authoring");
        Vec result=m*Vec(h.y()*h.y()+h.z()*h.z(),h.x()*h.x()+h.z()*h.z(),h.x()*h.x()+h.y()*h.y());
        if(!result.allFinite()||(result.array()<=0).any()||!result.cwiseInverse().allFinite())throw std::invalid_argument("Unrepresentable rigid inertia");
        return result;
    }
    static std::array<Vec,8> Corners(Vec h){std::array<Vec,8> points;for(int i=0;i<8;++i)points[i]=Vec(i&1?h.x():-h.x(),i&2?h.y():-h.y(),i&4?h.z():-h.z());return points;}
    RigidBox(World &w,Vec h,double m,Vec position,Vec velocity=Vec::Zero(),Vec spin=Vec::Zero(),Eigen::Quaterniond orientation=Eigen::Quaterniond::Identity(),bool supported=false)
        :half(h),mass(m),anchored(supported),inertia(Inertia(h,m)),local(Corners(h)) {
        if(!position.allFinite()||!velocity.allFinite()||!spin.allFinite()||!orientation.coeffs().allFinite()||std::abs(orientation.norm()-1)>1e-12||
           (anchored&&(velocity.norm()!=0||spin.norm()!=0)))throw std::invalid_argument("Invalid rigid initial state");
        auto *box=new btBoxShape({h.x(),h.y(),h.z()});box->setMargin(0);shape=box;
        body=w.Body(box,anchored?0:m,{position.x(),position.y(),position.z()},{velocity.x(),velocity.y(),velocity.z()});
        if(!anchored)body->setMassProps(m,{inertia.x(),inertia.y(),inertia.z()});
        btTransform pose;pose.setIdentity();pose.setOrigin({position.x(),position.y(),position.z()});pose.setRotation({orientation.x(),orientation.y(),orientation.z(),orientation.w()});
        body->setCenterOfMassTransform(pose);body->updateInertiaTensor();body->setAngularVelocity({spin.x(),spin.y(),spin.z()});flags=body->getCollisionFlags();
        w.world->updateSingleAabb(body);
    }
    bool Valid()const {
        if(!body||body->getCollisionShape()!=shape||body->getCollisionFlags()!=flags||shape->getShapeType()!=BOX_SHAPE_PROXYTYPE||shape->getMargin()!=0)return false;
        auto *box=static_cast<const btBoxShape*>(shape);if((Point(box->getHalfExtentsWithMargin())-half).norm()>1e-12)return false;
        auto r=Rotation(body->getCenterOfMassTransform());
        if(!r.allFinite()||!Point(body->getCenterOfMassPosition()).allFinite()||!Point(body->getLinearVelocity()).allFinite()||!Point(body->getAngularVelocity()).allFinite()||
           (r.transpose()*r-Eigen::Matrix3d::Identity()).norm()>1e-10||std::abs(r.determinant()-1)>1e-10)return false;
        double expected=anchored?0:1/mass;
        if(!std::isfinite(body->getInvMass())||std::abs(body->getInvMass()-expected)>1e-12*std::abs(expected))return false;
        Vec inv=Point(body->getInvInertiaDiagLocal()),want=anchored?Vec::Zero().eval():inertia.cwiseInverse().eval();
        if(!inv.allFinite()||(inv-want).norm()>1e-12*want.norm())return false;
        return true;
    }
};
}
