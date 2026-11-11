// Exercise the actual internal section integrator, not a duplicated test model.
#include "../src/aircraftbodymodel.cpp"
#include <cstdio>

namespace {
using namespace aircraftbodymodel;
int failures=0;
void Check(bool value,const char*message){if(!value){++failures;std::printf("FAIL: %s\n",message);}}
void Quad(std::vector<Surface>&s,Vec3 a,Vec3 b,Vec3 c,Vec3 d,uint32_t group=0){s.push_back({{a,b,c},group,true,false,Role::WingRight});s.push_back({{a,c,d},group,true,false,Role::WingRight});}
void TransitiveIntervals(){
 std::vector<Surface> surfaces;for(auto interval:std::array<std::array<float,2>,3>{{{0,.2f},{.4f,.6f},{.1f,.5f}}}){Surface face;face.points={Vec3{2,interval[0],1},Vec3{4,interval[0],1},Vec3{3,interval[1],1}};face.role=Role::WingRight;surfaces.push_back(face);}
 std::array<float,2> lengths{},inertias{};int run=0;for(auto order:std::array<std::array<int,3>,2>{{{0,1,2},{2,0,1}}}){std::vector<aircraftconnectivity::Fragment> fragments;for(int source:order){const auto&q=surfaces[source];fragments.push_back({size_t(source),q.group,{{q.points[0],{1,0,0}},{q.points[1],{0,1,0}},{q.points[2],{0,0,1}}},0,true});}for(const auto&line:SectionLines(fragments,surfaces,1,2.15f))lengths[run]+=Length(Sub(line.b,line.a));inertias[run]=MeasureSection(fragments,surfaces,1,2.15f,false).inertia[1];++run;}
 // A single .6 m strip, .8 mm thick: I=.6*.0008^3/12.
 Check(std::fabs(lengths[0]-.6f)<1e-6f&&std::fabs(lengths[1]-.6f)<1e-6f,"transitive interval union has no triangulation-order extra material");Check(std::fabs(inertias[0]-2.56e-11f)<1e-15f&&std::fabs(inertias[1]-2.56e-11f)<1e-15f,"transitive interval union gives order-independent physical sheet inertia");
}
void CrackGroupProvenance(){
 std::vector<Surface> surfaces;Quad(surfaces,{2,0,1},{4,0,1},{4,.1f,1},{2,.1f,1},0);Quad(surfaces,{2,.1f,1},{4,.1f,1},{4,.6f,1},{2,.6f,1},1);std::vector<aircraftconnectivity::Face> faces;for(const auto&s:surfaces)faces.push_back({s.points,s.group});aircraftconnectivity::Mesh mesh;Check(mesh.Build(faces,{{0,{3,.025f,1}}}),"multi-group material builds");Check(std::fabs(MeasureSection(mesh.Fragments(),surfaces,1,2.15f,false).area-.00048f)<1e-9f,"material group seams do not add structural area");const auto lines=SectionLines(mesh.Fragments(),surfaces,1,2.15f);const Ligament*target=nullptr;float longest=0;for(const auto&line:lines){const float length=Length(Sub(line.b,line.a));if(length>longest){target=&line;longest=length;}}Check(target!=nullptr,"multi-group section has crack material");if(!target)return;const auto revision=mesh.Revision();Check(mesh.ApplyHole({Mul(Add(target->a,target->b),.5f),target->normal,.12f},target->group)&&mesh.Revision()!=revision,"crack midpoint belongs to its retained material group");
 const auto geometry=MeasureSection(mesh.Fragments(),surfaces,1,2.15f,false);Check(geometry.area<.0008f*.5f,"multi-group local crack removes actual section material");
}
void AxisCriticalRetention(){
 std::vector<Surface> surfaces;const std::array<std::array<float,3>,6> stations{{{.7f,1.2f,.2f},{2.8f,1.2f,.2f},{3.f,1.2f,.02f},{3.05f,.2f,.2f},{3.4f,1.2f,.2f},{5.3f,1.2f,.2f}}};
 for(size_t i=1;i<stations.size();++i){const auto a=stations[i-1],b=stations[i];const float ax=a[0],ay=a[1]*.5f,az=a[2]*.5f,bx=b[0],by=b[1]*.5f,bz=b[2]*.5f;Quad(surfaces,{ax,-ay,1+az},{bx,-by,1+bz},{bx,by,1+bz},{ax,ay,1+az});Quad(surfaces,{ax,-ay,1-az},{ax,ay,1-az},{bx,by,1-bz},{bx,-by,1-bz});Quad(surfaces,{ax,-ay,1-az},{bx,-by,1-bz},{bx,-by,1+bz},{ax,-ay,1+az});Quad(surfaces,{ax,ay,1-az},{ax,ay,1+az},{bx,by,1+bz},{bx,by,1-bz});}
 Model first;Check(first.Initialize(surfaces,1400),"two-axis variable-depth beam initializes");Check(first.Bullet({3,0,1.3f},{3,0,.7f},1).valid,"first local cut registers vertically weak station");Model second=first;Check(second.Bullet({3.05f,0,1.3f},{3.05f,0,.7f},1).valid,"second local cut registers horizontally weak station");for(int i=0;i<300;++i){first.Tick(.01f,false);second.Tick(.01f,false);}const float sagA=1-first.Deform({5,.2f,1},0).z,sagB=1-second.Deform({5,.2f,1},0).z;std::printf("Per-axis critical retention sag %.8f -> %.8fm\n",sagA,sagB);Check(sagA>.005f&&sagB>=sagA*.95f,"later orthogonal damage cannot erase an existing vertical weak station");for(int i=0;i<300;++i){first.Tick(.01f,false,{0,20,-20});second.Tick(.01f,false,{0,20,-20});}const float sideA=first.Deform({5,.2f,1},0).y-.2f,sideB=second.Deform({5,.2f,1},0).y-.2f;Check(sideB>sideA*1.25f,"second cut still weakens its own horizontal bending axis");
}
}
int main(){TransitiveIntervals();CrackGroupProvenance();AxisCriticalRetention();if(!failures)std::puts("Aircraft section integration and critical stations: all passed");return failures?1:0;}
