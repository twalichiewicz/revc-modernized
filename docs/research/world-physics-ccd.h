#pragma once
// Research adapter for prescribed trajectories using IPC primitive distance.
// Advance from a known-safe time using directed separation certificates or
// the bounded relative point speed fallback;
// upstream CCD retries may change a nonzero separation, so their returned
// times/misses are not used to certify or skip any part of this trajectory.
// This does not advance a dynamics world or solve persistent contact.
#include <ipc/ccd/nonlinear_ccd.hpp>
#include <ipc/distance/point_triangle.hpp>
#include <ipc/distance/point_edge.hpp>
#include <ipc/distance/point_point.hpp>
#include <ipc/distance/edge_edge.hpp>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <iomanip>
#include <typeinfo>
namespace continuous {
using Vec=Eigen::Vector3d;
inline void Finite(Vec v) { if(!v.allFinite()||!std::isfinite(v.norm()))throw std::invalid_argument("Nonfinite trajectory"); }
struct Path : ipc::NonlinearTrajectory { virtual double max_speed()const=0; };
struct LinearPath : Path {
    Vec from,to;
    LinearPath(Vec a,Vec b):from(a),to(b) {Finite(a);Finite(b);Finite(b-a);}
    ipc::VectorMax3d operator()(double t)const override{return from+(to-from)*t;}
    double max_distance_from_linear(double,double)const override{return 0;}
    double max_speed()const override{return (to-from).norm();}
};
struct RigidPath : Path {
    Vec local,center,translation,axis;double angle;
    RigidPath(Vec point,Vec origin,Vec shift,Vec spinAxis,double sweep)
        :local(point),center(origin),translation(shift),axis(spinAxis),angle(sweep) {
        Finite(local);Finite(center);Finite(translation);Finite(axis);
        if(!std::isfinite(angle)||axis.norm()<1e-12)throw std::invalid_argument("Invalid rigid trajectory");
        axis.normalize();
    }
    ipc::VectorMax3d operator()(double t)const override {
        return center+translation*t+Eigen::AngleAxisd(angle*t,axis)*local;
    }
    double max_speed()const override{return translation.norm()+std::abs(angle)*local.norm();}
    double max_distance_from_linear(double t0,double t1)const override {
        // For a C² curve, chord error <= max|p''|*(t1-t0)²/8.
        // Translation cancels and |p''| <= radius*angle². The alternative 2r
        // bound holds for any number/sign of turns, including identical ends.
        double radius=local.norm(),sweep=std::abs(angle*(t1-t0));
        double bound=radius*std::min(2.,sweep*sweep/8.);
        return std::nextafter(bound,std::numeric_limits<double>::infinity());
    }
};
enum class Primitive {PointTriangle,PointEdge,EdgeEdge,PointPoint};
struct Result {bool hit;double time,gap;int refinements;int certificates=0;};
namespace directed {
inline const LinearPath *ExactLinear(const Path *p){return p&&typeid(*p)==typeid(LinearPath)?static_cast<const LinearPath*>(p):nullptr;}
inline const RigidPath *ExactRigid(const Path *p){return p&&typeid(*p)==typeid(RigidPath)?static_cast<const RigidPath*>(p):nullptr;}
struct Span {double length=0;bool complete=false;};
inline bool SameMotion(const RigidPath &a,const RigidPath &b) {
    return a.angle==b.angle&&(a.center.array()==b.center.array()).all()&&
        (a.translation.array()==b.translation.array()).all()&&(a.axis.array()==b.axis.array()).all();
}
inline const RigidPath *Shared(const std::array<const Path*,4> &paths,int begin,int count) {
    auto *first=ExactRigid(paths[begin]);if(!first)return nullptr;
    for(int i=begin+1;i<begin+count;++i){auto *next=ExactRigid(paths[i]);if(!next||!SameMotion(*first,*next))return nullptr;}
    return first;
}
struct Motion {Vec point,velocity;double acceleration=0;};
inline bool Bound(const Path &path,const RigidPath *frame,Vec normal,double time,Motion &out) {
    auto *linear=ExactLinear(&path);auto *rigid=ExactRigid(&path);
    if((!linear&&!rigid)||(frame&&!ExactRigid(frame)))return false; // Unknown trajectories retain the norm bound.
    if(frame&&rigid&&SameMotion(*frame,*rigid)){out={rigid->local,Vec::Zero(),0};return true;}
    Vec center=linear?linear->from:rigid->center,shift=linear?(linear->to-linear->from).eval():rigid->translation;
    Vec local=rigid?(Eigen::AngleAxisd(rigid->angle*time,rigid->axis)*rigid->local).eval():Vec::Zero().eval();
    Vec omega=rigid?(rigid->angle*rigid->axis).eval():Vec::Zero().eval();
    double radius=rigid?(rigid->local-rigid->axis*rigid->axis.dot(rigid->local)).norm():0;
    double acceleration=rigid?rigid->angle*rigid->angle*radius:0;
    if(!frame) {
        out.point=center+time*shift+local;out.velocity=shift+omega.cross(local);
        out.acceleration=rigid?rigid->angle*rigid->angle*normal.cross(rigid->axis).norm()*radius:0;
    } else {
        Vec relativeCenter=center-frame->center,relativeShift=shift-frame->translation;
        Eigen::Matrix3d inverse=Eigen::AngleAxisd(-frame->angle*time,frame->axis).toRotationMatrix();
        Vec frameOmega=frame->angle*frame->axis;
        out.point=inverse*(relativeCenter+time*relativeShift+local);
        out.velocity=inverse*(relativeShift+omega.cross(local))-frameOmega.cross(out.point);
        double positionBound=std::max((relativeCenter+time*relativeShift).norm(),(relativeCenter+relativeShift).norm())+(rigid?rigid->local.norm():0);
        double speedBound=relativeShift.norm()+(rigid?std::abs(rigid->angle)*radius:0);
        // u=R^T(p-c); u''=R^T p''-2 w x R^T(p'-c')+w x(w x u).
        // Project before taking norms, retaining zero rotation about the normal.
        out.acceleration=normal.norm()*acceleration+normal.cross(frameOmega).norm()*(2*speedBound+std::abs(frame->angle)*positionBound);
    }
    return out.point.allFinite()&&out.velocity.allFinite()&&std::isfinite(out.acceleration)&&out.acceleration>=0;
}
inline Span Certificate(Primitive type,const std::array<const Path*,4> &paths,double time,double separation) {
    int count=type==Primitive::PointPoint?2:type==Primitive::PointEdge?3:4,aCount=type==Primitive::EdgeEdge?2:1;
    std::array<Vec,4> positions;double scale=1+separation,angle=0;
    for(int i=0;i<count;++i) {
        positions[i]=(*paths[i])(time);
        if(auto *r=ExactRigid(paths[i])){scale+=r->center.norm()+r->translation.norm()+r->local.norm();angle=std::max(angle,std::abs(r->angle));}
        else if(auto *l=ExactLinear(paths[i]))scale+=l->from.norm()+l->to.norm();
        else return {};
    }
    Vec normal;
    if(type==Primitive::PointPoint)normal=positions[0]-positions[1];
    else if(type==Primitive::PointEdge)normal=ipc::point_edge_distance_gradient(positions[0],positions[1],positions[2]).head<3>();
    else if(type==Primitive::PointTriangle)normal=ipc::point_triangle_distance_gradient(positions[0],positions[1],positions[2],positions[3]).head<3>();
    else {auto gradient=ipc::edge_edge_distance_gradient(positions[0],positions[1],positions[2],positions[3]);normal=gradient.head<3>()+gradient.segment<3>(3);}
    double length=normal.norm();if(!normal.allFinite()||!std::isfinite(length)||length==0)return {};
    normal/=length;
    const RigidPath *frame=Shared(paths,aCount,count-aCount);if(!frame)frame=Shared(paths,0,aCount);
    if(frame)normal=Eigen::AngleAxisd(-frame->angle*time,frame->axis)*normal;
    double norm=normal.norm(),guard=128*std::numeric_limits<double>::epsilon()*scale*(1+angle);
    if(!std::isfinite(norm)||!std::isfinite(guard))return {};
    std::array<Motion,4> motion;
    for(int i=0;i<count;++i)if(!Bound(*paths[i],frame,normal,time,motion[i]))return {};
    const double remaining=1-time;double safe=remaining;bool complete=true;
    for(int i=0;i<aCount;++i)for(int j=aCount;j<count;++j) {
        double margin=normal.dot(motion[i].point-motion[j].point)-separation*norm-guard;
        double velocity=normal.dot(motion[i].velocity-motion[j].velocity)-guard*(1+angle);
        double acceleration=(motion[i].acceleration+motion[j].acceleration+guard*(1+angle)*(1+angle))*(1+64*std::numeric_limits<double>::epsilon());
        if(!std::isfinite(margin)||!std::isfinite(velocity)||!std::isfinite(acceleration)||margin<=0)return {};
        double end=margin+velocity*remaining-.5*acceleration*remaining*remaining;
        if(!std::isfinite(end))return {};
        if(end>0)continue; // Concave lower bound: both endpoints positive certify all times.
        complete=false;double root;
        if(acceleration==0)root=margin/(-velocity);
        else {
            double product=2*acceleration*margin;if(!std::isfinite(product))return {};
            double discriminant=std::hypot(velocity,std::sqrt(product));
            root=velocity<=0?2*margin/(discriminant-velocity):(velocity+discriminant)/acceleration;
        }
        if(!std::isfinite(root)||root<=0)return {};safe=std::min(safe,.8*root);
    }
    return {safe,complete};
}
} // namespace directed

inline Result Sweep(Primitive type,std::array<const Path*,4> p,double separation,int maxRefinements=1024,double requestedTolerance=1e-7) {
    if(!std::isfinite(requestedTolerance)||requestedTolerance<=0)throw std::invalid_argument("Invalid geometry tolerance");
    if(maxRefinements<=0)throw std::invalid_argument("Invalid refinement budget");
    if(!std::isfinite(separation)||separation<0)throw std::invalid_argument("Invalid separation");
    const int count=type==Primitive::PointPoint?2:type==Primitive::PointEdge?3:4;
    for(int i=0;i<count;++i) {
        if(!p[i])throw std::invalid_argument("Missing trajectory");
        if(auto *r=directed::ExactRigid(p[i]))if(!r->axis.allFinite()||!std::isfinite(r->angle)||std::abs(r->axis.norm()-1)>16*std::numeric_limits<double>::epsilon())
            throw std::invalid_argument("Rigid trajectory requires a finite unit axis");
        for(double t:{0.,1.}) {auto x=(*p[i])(t);if(x.size()!=3||!x.allFinite())throw std::invalid_argument("Invalid trajectory endpoint");}
    }
    auto distance=[&](double t) {
        double squared=type==Primitive::PointPoint?ipc::point_point_distance((*p[0])(t),(*p[1])(t)):
            type==Primitive::PointTriangle?ipc::point_triangle_distance((*p[0])(t),(*p[1])(t),(*p[2])(t),(*p[3])(t)):
            type==Primitive::PointEdge?ipc::point_edge_distance((*p[0])(t),(*p[1])(t),(*p[2])(t)):
            ipc::edge_edge_distance((*p[0])(t),(*p[1])(t),(*p[2])(t),(*p[3])(t));
        if(!std::isfinite(squared)||squared<0)throw std::runtime_error("Invalid primitive distance");
        return std::sqrt(squared);
    };
    const double contactTolerance=requestedTolerance;
    double speed[4]={0,0,0,0};
    for(int i=0;i<count;++i) {
        speed[i]=p[i]->max_speed();
        if(!std::isfinite(speed[i])||speed[i]<0)throw std::invalid_argument("Invalid trajectory speed bound");
    }
    const double relativeSpeed=type==Primitive::PointPoint?speed[0]+speed[1]:type==Primitive::PointEdge?speed[0]+std::max(speed[1],speed[2]):
        type==Primitive::PointTriangle?speed[0]+std::max({speed[1],speed[2],speed[3]}):
        std::max(speed[0],speed[1])+std::max(speed[2],speed[3]);
    if(!std::isfinite(relativeSpeed))throw std::invalid_argument("Relative speed bound overflow");
    double start=0,initial=distance(0);int certificates=0;
    if(initial<separation-contactTolerance)throw std::runtime_error("Initial overlap needs a recovery policy");
    if(initial<=separation+contactTolerance)return {true,0,initial,0};
    if(relativeSpeed==0)return {false,1,distance(1),0};
    for(int iteration=0;iteration<maxRefinements;++iteration) {
        double gap=distance(start);
        if(gap<separation-contactTolerance)throw std::runtime_error("Contact refinement entered overlap");
        if(gap<=separation+contactTolerance)return {true,start,gap,iteration+1,certificates};
        // Distance between moving convex primitives is Lipschitz with the sum
        // of their maximum point speeds. This advance cannot skip contact in
        // exact arithmetic. Floating-point tolerance remains explicit.
        double step=.8*(gap-separation)/relativeSpeed;
        if(start+step>1)return {false,1,distance(1),iteration+1,certificates};
        auto certificate=directed::Certificate(type,p,start,separation);
        if(certificate.complete) {
            double end=distance(1);if(end<separation-contactTolerance)throw std::runtime_error("Separation certificate contradicted endpoint distance");
            return {false,1,end,iteration+1,certificates+1};
        }
        if(certificate.length>step){step=certificate.length;++certificates;}
        double next=start+step;
        if(next<=start)throw std::runtime_error("CCD refinement made no progress");
        start=next;
    }
    std::ostringstream description;description<<std::setprecision(17)<<"CCD refinement budget exhausted; motion is unresolved primitive="<<int(type)<<" t="<<start<<" gap="<<distance(start)<<" speed="<<relativeSpeed;
    for(int i=0;i<count;++i) {
        if(auto *r=dynamic_cast<const RigidPath*>(p[i]))description<<" rigid["<<i<<"] local="<<r->local.transpose()<<" center="<<r->center.transpose()<<" translation="<<r->translation.transpose()<<" axis="<<r->axis.transpose()<<" angle="<<r->angle;
        else description<<" path["<<i<<"] from="<<(*p[i])(0).transpose()<<" to="<<(*p[i])(1).transpose();
    }
    throw std::runtime_error(description.str());
}
}
