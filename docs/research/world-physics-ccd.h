#pragma once
// Research adapter for prescribed trajectories using IPC primitive distance.
// Advance only from a known-safe time with a bounded relative point speed;
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
struct Result {bool hit;double time,gap;int refinements;};
inline Result Sweep(Primitive type,std::array<const Path*,4> p,double separation,int maxRefinements=1024,double requestedTolerance=1e-7) {
    if(!std::isfinite(requestedTolerance)||requestedTolerance<=0)throw std::invalid_argument("Invalid geometry tolerance");
    if(maxRefinements<=0)throw std::invalid_argument("Invalid refinement budget");
    if(!std::isfinite(separation)||separation<0)throw std::invalid_argument("Invalid separation");
    const int count=type==Primitive::PointPoint?2:type==Primitive::PointEdge?3:4;
    for(int i=0;i<count;++i) {
        if(!p[i])throw std::invalid_argument("Missing trajectory");
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
    double start=0,initial=distance(0);
    if(initial<separation-contactTolerance)throw std::runtime_error("Initial overlap needs a recovery policy");
    if(initial<=separation+contactTolerance)return {true,0,initial,0};
    if(relativeSpeed==0)return {false,1,distance(1),0};
    for(int iteration=0;iteration<maxRefinements;++iteration) {
        double gap=distance(start);
        if(gap<separation-contactTolerance)throw std::runtime_error("Contact refinement entered overlap");
        if(gap<=separation+contactTolerance)return {true,start,gap,iteration+1};
        // Distance between moving convex primitives is Lipschitz with the sum
        // of their maximum point speeds. This advance cannot skip contact in
        // exact arithmetic. Floating-point tolerance remains explicit.
        double next=start+.8*(gap-separation)/relativeSpeed;
        if(next>1)return {false,1,distance(1),iteration+1};
        if(next<=start)throw std::runtime_error("CCD refinement made no progress");
        start=next;
    }
    throw std::runtime_error("CCD refinement budget exhausted; motion is unresolved");
}
}
