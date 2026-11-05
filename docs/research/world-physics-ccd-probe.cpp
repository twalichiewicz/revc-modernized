// Isolated continuous-contact comparison, not installed game code.
#include "world-physics-ccd.h"
#include "world-physics-witness.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/resource.h>
int checks=0,failed=0;
void Expect(const char *name,bool ok) {++checks;failed+=!ok;std::printf("ccd %s %s\n",name,ok?"PASS":"FAIL");}
using continuous::Vec;
void Paths() {
    const double pi=std::acos(-1.);
    continuous::RigidPath p({2,0,0},{0,0,0},{0,1,0},{0,-1,0},pi/2);
    Expect("rotating_path_actual_midpoint",(p(.5)-Vec(std::sqrt(2.),.5,std::sqrt(2.))).norm()<1e-12);
    // Circular arc/chord midpoint deviation for a 90deg, radius2 arc.
    Expect("rotation_envelope_bounds_known_deviation",p.max_distance_from_linear(0,1)>=2-std::sqrt(2.));
    continuous::RigidPath full({2,0,0},{0,0,0},{0,0,0},{0,-1,0},2*pi);
    Expect("full_turn_envelope_is_not_zero",full.max_distance_from_linear(0,1)>=4-1e-12);
    Vec target(1.5*std::cos(.3),0,1.5*std::sin(.3));continuous::LinearPath point(target,target);
    continuous::RigidPath left({-2,0,0},{0,0,0},{0,0,0},{0,-1,0},.6),right({2,0,0},{0,0,0},{0,0,0},{0,-1,0},.6);
    bool rejected=false;
    try{continuous::Sweep(continuous::Primitive::PointEdge,{&point,&left,&right,nullptr},.03,1);}
    catch(const std::runtime_error&){rejected=true;}
    Expect("exhausted_budget_rejects_unresolved_motion",rejected);

}
void Shots() {
    for(double speed:{1000.,10000.})for(bool moving:{false,true})for(bool miss:{false,true}) {
        const double distance=speed/480.;
        continuous::LinearPath point({miss?3.:.5,-.2,moving?0.:.5},{miss?3.:.5,-.2,moving?0.:.5-distance});
        Vec shift(0,0,moving?-distance:0);
        continuous::LinearPath a({-1,-1,moving?.5:0.},Vec(-1,-1,moving?.5:0.)+shift);
        continuous::LinearPath b({1,-1,moving?.5:0.},Vec(1,-1,moving?.5:0.)+shift);
        continuous::LinearPath c({1,1,moving?.5:0.},Vec(1,1,moving?.5:0.)+shift);
        auto r=continuous::Sweep(continuous::Primitive::PointTriangle,{&point,&a,&b,&c},.01);
        std::printf("shot speed=%.0f moving=%d miss=%d hit=%d time=%.12g gap=%.12g refinements=%d\n",speed,moving,miss,r.hit,r.time,r.gap,r.refinements);
        Expect("fast_shot_matches_actual_surface",r.hit==!miss);
        if(!miss) {
            // Relative plane/sphere approach is exactly .5m over speed/480m.
            double want=.49/distance;
            Expect("first_contact_time_matches_literal_geometry",std::abs(r.time-want)<1e-6&&r.gap>=.01-1e-6&&r.gap<=.01+1e-7);
        }
    }
}
void CrossedEdges() {
    continuous::LinearPath a({-1,0,0},{-1,0,0}),b({1,0,0},{1,0,0});
    for(bool miss:{false,true}) {
        continuous::LinearPath c({miss?3.:0.,-1,1},{miss?3.:0.,-1,-1}),d({miss?3.:0.,1,1},{miss?3.:0.,1,-1});
        auto r=continuous::Sweep(continuous::Primitive::EdgeEdge,{&a,&b,&c,&d},.001);
        std::printf("edge miss=%d hit=%d time=%.12g gap=%.12g\n",miss,r.hit,r.time,r.gap);
        Expect("edge_crossing_between_nodes_is_detected",r.hit==!miss);
        if(!miss)Expect("edge_contact_time_and_gap",std::abs(r.time-.4995)<1e-6&&r.gap>=.001-1e-6&&r.gap<=.001+1e-7);
    }
}
void RotatingBlade() {
    double pi=std::acos(-1.);
    for(double angle:{.6,2*pi,5*pi,-.6})for(bool miss:{false,true}) {
        double sign=angle<0?-1:1;
        Vec target((miss?3.:1.5)*std::cos(.3),0,(miss?3.:1.5)*std::sin(.3)*sign);
        continuous::LinearPath point(target,target);
        continuous::RigidPath a({-2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle),b({2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle);
        auto r=continuous::Sweep(continuous::Primitive::PointEdge,{&point,&a,&b,nullptr},.03);
        std::printf("rotor angle=%.12g miss=%d hit=%d time=%.12g gap=%.12g refinements=%d\n",angle,miss,r.hit,r.time,r.gap,r.refinements);
        Expect("curved_rotor_sweep_matches_material_extent",r.hit==!miss);
        if(!miss) {
            double want=(.3-std::asin(.03/1.5))/std::abs(angle);
            Expect("curved_rotor_first_contact_time",std::abs(r.time-want)<1e-5&&r.gap>=.03-1e-6&&r.gap<=.03+1e-7);
        }
    }
}

void Witnesses() {
    Vec a(0,0,0),b(2,0,0),c(0,1,0);
    struct Case {Vec point;std::array<double,3> weights;};
    for(auto value:{Case{{.5,.25,.1},{.5,.25,.25}},Case{{1,-1,.1},{.5,.5,0}},Case{{2,1,.1},{0,.8,.2}},
                    Case{{-1,.5,.1},{.5,0,.5}},Case{{-1,-1,.1},{1,0,0}},Case{{3,0,.1},{0,1,0}},Case{{0,2,.1},{0,0,1}}}) {
        auto w=continuous::TriangleWeights(value.point,a,b,c);bool ok=true;
        for(int i=0;i<3;++i)ok&=std::abs(w[i]-value.weights[i])<1e-12;
        Expect("closest_witness_covers_face_edges_and_vertices",ok);
    }
}


void NearGrazes() {
    for(double radius:{2.031,2.03001,2.02999}) {
        Vec target(radius*std::cos(.3),0,radius*std::sin(.3));
        continuous::LinearPath point(target,target);
        continuous::RigidPath a({-2,0,0},{0,0,0},{0,0,0},{0,-1,0},.6),b({2,0,0},{0,0,0},{0,0,0},{0,-1,0},.6);
        bool resolved=true;continuous::Result r{false,1,0,0};
        try{r=continuous::Sweep(continuous::Primitive::PointEdge,{&point,&a,&b,nullptr},.03);}
        catch(const std::runtime_error &e){resolved=false;std::printf("near-graze radius=%.8g unresolved=%s\n",radius,e.what());}
        if(resolved)std::printf("near-graze radius=%.8g hit=%d gap=%.12g refinement=%d\n",radius,r.hit,r.gap,r.refinements);
        Expect("near_graze_resolves_actual_contact_or_miss",resolved&&r.hit==(radius<2.03));
    }
}




void PointPairs() {
    continuous::LinearPath a({0,0,.5},{0,0,-1.5}),b({0,0,0},{0,0,0}),miss({1,0,.5},{1,0,-1.5});
    auto r=continuous::Sweep(continuous::Primitive::PointPoint,{&a,&b,nullptr,nullptr},.01);
    Expect("codimensional_point_contact_time",r.hit&&std::abs(r.time-.245)<1e-6&&r.gap>=.01-1e-7&&r.gap<=.01+1e-7);
    r=continuous::Sweep(continuous::Primitive::PointPoint,{&miss,&b,nullptr,nullptr},.01);
    Expect("codimensional_points_preserve_empty_space",!r.hit);
}

void NearInitialContact() {
    constexpr double separation=.03,radius=1.5;
    for(double margin:{.00001,.00002,.0001,.001,.01})for(double angle:{.05,.6,2*std::acos(-1.),5*std::acos(-1.)}) {
        double theta=std::asin((separation+margin)/radius);
        Vec target(radius*std::cos(theta),0,radius*std::sin(theta));continuous::LinearPath point(target,target);
        continuous::RigidPath a({-2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle),b({2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle);
        auto r=continuous::Sweep(continuous::Primitive::PointEdge,{&point,&a,&b,nullptr},separation);
        double want=(theta-std::asin(separation/radius))/angle;
        double timeTolerance=2e-7/(radius*angle)+1e-12;
        std::printf("near-initial margin=%.8g angle=%.12g actual_time=%.12g want_time=%.12g gap=%.12g refinements=%d\n",margin,angle,r.time,want,r.gap,r.refinements);
        Expect("near_initial_contact_keeps_earliest_time_and_both_gap_bounds",r.hit&&std::abs(r.time-want)<timeTolerance&&r.gap>=separation-1e-7&&r.gap<=separation+1e-7);
    }
    continuous::LinearPath point({1,0,.02},{1,0,.02}),a({-2,0,0},{-2,0,0}),b({2,0,0},{2,0,0});
    bool rejected=false;try{continuous::Sweep(continuous::Primitive::PointEdge,{&point,&a,&b,nullptr},.03);}
    catch(const std::runtime_error&){rejected=true;}
    Expect("deep_initial_overlap_requires_recovery_policy",rejected);
}

void QueryBenchmark() {
    auto run=[&](const char *name,auto query,bool want) {
        std::vector<double> samples;int maximumRefinements=0,maximumCertificates=0;bool valid=true;
        for(int i=0;i<20;++i) {
            auto start=std::chrono::steady_clock::now();auto r=query();
            samples.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
            maximumRefinements=std::max(maximumRefinements,r.refinements);maximumCertificates=std::max(maximumCertificates,r.certificates);valid&=r.hit==want;
        }
        std::sort(samples.begin(),samples.end());struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
        std::printf("query-benchmark %s calls=20 median_us=%.6f max_us=%.6f max_refinements=%d max_certificates=%d peak_process_rss_bytes=%ld\n",name,samples[10],samples.back(),maximumRefinements,maximumCertificates,usage.ru_maxrss);
        Expect("benchmark_retains_actual_hit_or_miss",valid);
    };
    continuous::LinearPath shot({.5,-.2,.5},{.5,-.2,.5-1000./480}),a({-1,-1,0},{-1,-1,0}),b({1,-1,0},{1,-1,0}),c({1,1,0},{1,1,0});
    run("shot",[&](){return continuous::Sweep(continuous::Primitive::PointTriangle,{&shot,&a,&b,&c},.01);},true);
    for(double angle:{.6,2*std::acos(-1.)})for(double radius:{1.5,3.}) {
        Vec target(radius*std::cos(.3),0,radius*std::sin(.3));continuous::LinearPath point(target,target);
        continuous::RigidPath left({-2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle),right({2,0,0},{0,0,0},{0,0,0},{0,-1,0},angle);
        char label[80];std::snprintf(label,sizeof(label),"rotor_angle_%.3f_radius_%.3f",angle,radius);
        run(label,[&](){return continuous::Sweep(continuous::Primitive::PointEdge,{&point,&left,&right,nullptr},.03);},radius<2);
    }
    continuous::LinearPath tangent({0,0,.002000001},{1,0,.002000001}),inbound({-.2,0,.002000001},{.8,0,.001999999}),wa({-10,-10,0},{-10,-10,0}),wb({10,-10,0},{10,-10,0}),wc({0,10,0},{0,10,0});
    run("near_tangent_plane",[&](){return continuous::Sweep(continuous::Primitive::PointTriangle,{&tangent,&wa,&wb,&wc},.002,1024,1e-10);},false);
    run("near_inbound_plane",[&](){return continuous::Sweep(continuous::Primitive::PointTriangle,{&inbound,&wa,&wb,&wc},.002,1024,1e-10);},true);
    continuous::LinearPath still({0,0,.002000001},{0,0,.002000001});double turn=2*std::acos(-1.);
    continuous::RigidPath ra({-10,-10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),turn),rb({10,-10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),turn),rc({0,10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),turn);
    run("near_rotating_plane",[&](){return continuous::Sweep(continuous::Primitive::PointTriangle,{&still,&ra,&rb,&rc},.002,1024,1e-10);},false);
    std::printf("query benchmark matrix: %d/%d passed\n",checks-failed,checks);
}

void ExplicitAccuracy(){
    continuous::LinearPath near({0,0,.01000001},{0,0,.01000001}),origin(Vec::Zero(),Vec::Zero()),inbound({0,0,.01000001},{0,0,.009999});
    auto still=continuous::Sweep(continuous::Primitive::PointPoint,{&near,&origin,nullptr,nullptr},.01,1024,1e-10);
    auto strike=continuous::Sweep(continuous::Primitive::PointPoint,{&inbound,&origin,nullptr,nullptr},.01,1024,1e-10);
    Expect("requested_precision_distinguishes_safe_near_contact",!still.hit);
    Expect("requested_precision_resolves_near_contact_prefix",strike.hit&&strike.time>0&&std::abs(strike.gap-.01)<=1e-10&&std::abs(strike.time-1./101)<1e-4);
}
void NearTangentialMotion(){
    using namespace continuous;
    LinearPath point({0,0,.002000001},{1,0,.002000001}),inbound({-.2,0,.002000001},{.8,0,.001999999});
    LinearPath a({-10,-10,0},{-10,-10,0}),b({10,-10,0},{10,-10,0}),c({0,10,0},{0,10,0});
    bool clear=false,hit=false;
    try{auto r=Sweep(Primitive::PointTriangle,{&point,&a,&b,&c},.002,1024,1e-10);clear=!r.hit;}catch(const std::runtime_error &e){std::printf("near tangent: %s\n",e.what());}
    try{auto r=Sweep(Primitive::PointTriangle,{&inbound,&a,&b,&c},.002,1024,1e-10);hit=r.hit&&r.time>=.45-1e-6&&r.time<=.5+1e-6&&std::abs(r.gap-.002)<=1e-10;}catch(const std::runtime_error &e){std::printf("near inbound: %s\n",e.what());}
    Expect("near_tangent_slide_is_clear_within_budget",clear);Expect("near_tangent_inbound_keeps_first_contact",hit);
    for(double angle:{.6,-.6,2*std::acos(-1.)}) {
        LinearPath still({0,0,.002000001},{0,0,.002000001});
        RigidPath ra({-10,-10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),angle),rb({10,-10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),angle),rc({0,10,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),angle);
        clear=false;try{auto r=Sweep(Primitive::PointTriangle,{&still,&ra,&rb,&rc},.002,1024,1e-10);clear=!r.hit;}catch(const std::runtime_error &e){std::printf("near rotating plane: %s\n",e.what());}
        Expect("near_rotating_tangent_plane_stays_clear",clear);
    }
}
void DirectedCases(){
    using namespace continuous;
    auto clear=[&](const char *name,Primitive type,std::array<const Path*,4> paths,double separation){bool okay=false;try{auto r=Sweep(type,paths,separation,1024,1e-10);okay=!r.hit&&r.refinements<64;}catch(const std::runtime_error&e){std::printf("directed query: %s\n",e.what());}Expect(name,okay);};
    LinearPath slide({0,.002000001,0},{1,.002000001,0}),left({-10,0,0},{-10,0,0}),right({10,0,0},{10,0,0});
    clear("near_tangent_point_edge_resolves",Primitive::PointEdge,{&slide,&left,&right,nullptr},.002);
    LinearPath edge0({-1,.002000001,0},{0,.002000001,0}),edge1({1,.002000001,0},{2,.002000001,0});
    clear("near_parallel_sliding_edges_resolve",Primitive::EdgeEdge,{&edge0,&edge1,&left,&right},.002);
    LinearPath boosted0({0,0,.002001},{100000,0,.002001}),boosted1(Vec::Zero(),{100000,0,0});
    clear("common_translation_does_not_exhaust_point_query",Primitive::PointPoint,{&boosted0,&boosted1,nullptr,nullptr},.002);
    RigidPath a({-1,0,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),2*std::acos(-1.)),b({1,0,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),2*std::acos(-1.));
    RigidPath c({0,0,-.5},{0,.75,0},Vec::Zero(),Vec::UnitZ(),-.3),d({0,0,.5},{0,.75,0},Vec::Zero(),Vec::UnitZ(),-.3);
    auto contact=Sweep(Primitive::EdgeEdge,{&a,&b,&c,&d},.01,1024,1e-10);double want=std::acos(.01/.75)/(2*std::acos(-1.));
    Expect("different_rigid_frames_keep_first_edge_contact",contact.hit&&std::abs(contact.time-want)<1e-7&&std::abs(contact.gap-.01)<=1e-10);
    struct Opaque : Path {ipc::VectorMax3d operator()(double t)const override{return Vec(t,0,.002000001);}double max_speed()const override{return 1;}double max_distance_from_linear(double,double)const override{return 0;}} opaque;
    LinearPath ta({-10,-10,0},{-10,-10,0}),tb({10,-10,0},{10,-10,0}),tc({0,10,0},{0,10,0});
    bool rejected=false;try{(void)Sweep(Primitive::PointTriangle,{&opaque,&ta,&tb,&tc},.002,8,1e-10);}catch(const std::runtime_error&){rejected=true;}
    Expect("unknown_trajectory_retains_bounded_fallback",rejected);
    RigidPath frame(Vec::Zero(),{1,0,0},{.5,-.2,.1},Vec::UnitZ(),2);LinearPath moving({3,.2,.1},{4,.6,.4});directed::Motion bound;
    bool valid=directed::Bound(moving,&frame,Vec::UnitX(),0,bound);
    for(int i=0;i<=200;++i){double t=i/200.,acc=-4*std::cos(2*t)*(2+.5*t)-4*std::sin(2*t)*(.2+.6*t)-2*std::sin(2*t)+2.4*std::cos(2*t);valid&=std::abs(acc)<=bound.acceleration+1e-12;}
    Expect("rotating_frame_derivative_includes_transport_terms",valid&&(bound.point-Vec(2,.2,.1)).norm()<1e-12&&(bound.velocity-Vec(.9,-3.4,.2)).norm()<1e-12);
}
void RecordedNewtonMotion(){
    using namespace continuous;
    Vec center(-7.7641129883319619e-08,-4.236754926808537e-06,-2.7056173016606153e-07),shift(-1.0264193128449975e-06,-6.4240554111507541e-05,-6.5114567091525903e-10),axis(3.4379654401238104e-05,4.3746338961320124e-06,.99999999939945095);double angle=8.5733776774583133e-05;
    LinearPath target({.75,0,0},{.75,0,0});
    RigidPath a({1.0000320199486157,-.0059958894794850549,-.010000419333245511},center,shift,axis,angle),b({-.99971213207066767,.025993331104806668,.0099999864397842531},center,shift,axis,angle),c({1.0000320309297419,-.0059954567063005421,.0099995806620691539},center,shift,axis,angle);
    constexpr int samples=10000;double minimum=1e100,prefix=1e100,firstCrossing=2;
    for(int i=0;i<=samples;++i){double t=double(i)/samples,distance=std::sqrt(ipc::point_triangle_distance(target(t),a(t),b(t),c(t)));minimum=std::min(minimum,distance);if(i<=samples/5)prefix=std::min(prefix,distance);if(distance<=.002&&firstCrossing>1)firstCrossing=t;}
    double lower=prefix-std::max({a.max_speed(),b.max_speed(),c.max_speed()})/(2*samples);
    Expect("recorded_newton_motion_has_independent_safe_prefix_and_later_hit",lower>.002+1e-12&&minimum<.002&&firstCrossing<=1);
    bool resolved=false;try{auto r=Sweep(Primitive::PointTriangle,{&target,&a,&b,&c},.002,1024,1e-10);resolved=r.hit&&r.time>.2&&r.time<=firstCrossing&&std::abs(r.gap-.002)<=1e-10&&r.refinements<64;std::printf("recorded Newton query sampled_min=%.12g prefix_lower=%.12g sampled_crossing=%.9g toi=%.9g refinements=%d certificates=%d\n",minimum,lower,firstCrossing,r.time,r.refinements,r.certificates);}catch(const std::runtime_error&e){std::printf("recorded Newton query: %s\n",e.what());}
    Expect("recorded_newton_motion_resolves_within_original_budget",resolved);
}
void DerivedTrajectories(){
    using namespace continuous;
    struct WavePoint : LinearPath {
        WavePoint():LinearPath({-1,0,.01},{1,0,.01}){}
        ipc::VectorMax3d operator()(double t)const override{return Vec(-1+2*t,0,.01-.02*std::sin(std::acos(-1.)*t));}
        double max_speed()const override{return 2.1;}
        double max_distance_from_linear(double,double)const override{return .02;}
    } point;
    LinearPath a({-2,-2,0},{-2,-2,0}),b({2,-2,0},{2,-2,0}),c({0,2,0},{0,2,0});
    auto r=Sweep(Primitive::PointTriangle,{&point,&a,&b,&c},.001);
    Expect("derived_linear_trajectory_cannot_use_base_formula",r.hit&&r.time<.2);
    struct WaveFace : RigidPath {
        explicit WaveFace(Vec p):RigidPath(p,Vec::Zero(),Vec::Zero(),Vec::UnitZ(),0){}
        ipc::VectorMax3d operator()(double t)const override{return local+Vec(0,0,.02*std::sin(std::acos(-1.)*t));}
        double max_speed()const override{return .02*std::acos(-1.);}
        double max_distance_from_linear(double,double)const override{return .02;}
    } wa({-2,-2,0}),wb({2,-2,0}),wc({0,2,0});
    LinearPath target({0,0,.01},{0,0,.01});auto q=Sweep(Primitive::PointTriangle,{&target,&wa,&wb,&wc},.001);
    Expect("derived_rigid_trajectory_cannot_use_base_formula",q.hit&&q.time<.2);
    RigidPath invalid({1,0,0},Vec::Zero(),Vec::Zero(),Vec::UnitZ(),.6);invalid.axis*=2;
    LinearPath far({10,0,0},{10,0,0});bool rejected=false;
    try{(void)Sweep(Primitive::PointPoint,{&invalid,&far,nullptr,nullptr},.01);}catch(const std::invalid_argument&){rejected=true;}
    Expect("changed_nonunit_rigid_axis_is_rejected",rejected);
}
int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1&&std::strcmp(argv[1],"--benchmark")==0){QueryBenchmark();return failed?1:0;}
    if(argc>1&&std::strcmp(argv[1],"--stress")==0) {NearGrazes();std::printf("near-graze matrix: %d/%d passed\n",checks-failed,checks);return failed?1:0;}
    if(argc>1){std::fprintf(stderr,"Unknown probe mode\n");return 2;}
    Paths();Shots();CrossedEdges();RotatingBlade();Witnesses();NearInitialContact();PointPairs();ExplicitAccuracy();NearTangentialMotion();DirectedCases();RecordedNewtonMotion();DerivedTrajectories();
    std::printf("continuous geometry matrix: %d/%d passed\n",checks-failed,checks);
    return failed?1:0;
}
