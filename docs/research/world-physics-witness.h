#pragma once
// Reuse IPC's closest-feature classification. Projecting to the infinite
// triangle plane alone gives invalid weights outside an edge or corner.
#include <ipc/tangent/closest_point.hpp>
#include <ipc/distance/distance_type.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>
namespace continuous {
inline std::array<double,3> TriangleWeights(const Eigen::Vector3d &p,const Eigen::Vector3d &a,const Eigen::Vector3d &b,const Eigen::Vector3d &c) {
    if(!p.allFinite()||!a.allFinite()||!b.allFinite()||!c.allFinite()||
       !std::isfinite((b-a).cross(c-a).squaredNorm())||(b-a).cross(c-a).squaredNorm()==0)
        throw std::invalid_argument("Invalid/degenerate triangle witness");
    std::array<Eigen::Vector3d,3> points{a,b,c};
    auto edge=[&](int i,int j) {
        double t=std::clamp(double(ipc::point_edge_closest_point(p,points[i],points[j])),0.,1.);
        std::array<double,3> result{0,0,0};result[i]=1-t;result[j]=t;return result;
    };
    using Type=ipc::PointTriangleDistanceType;
    switch(ipc::point_triangle_distance_type(p,a,b,c)) {
      case Type::P_T0:return {1,0,0};case Type::P_T1:return {0,1,0};case Type::P_T2:return {0,0,1};
      case Type::P_E0:return edge(0,1);case Type::P_E1:return edge(1,2);case Type::P_E2:return edge(2,0);
      case Type::P_T:{auto q=ipc::point_triangle_closest_point(p,a,b,c);return {1-q[0]-q[1],q[0],q[1]};}
      default:throw std::runtime_error("Unresolved triangle feature");
    }
}
}
