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
        std::vector<double> samples;int maximumRefinements=0;bool valid=true;
        for(int i=0;i<20;++i) {
            auto start=std::chrono::steady_clock::now();auto r=query();
            samples.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());
            maximumRefinements=std::max(maximumRefinements,r.refinements);valid&=r.hit==want;
        }
        std::sort(samples.begin(),samples.end());struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
        std::printf("query-benchmark %s calls=20 median_us=%.6f max_us=%.6f max_refinements=%d peak_process_rss_bytes=%ld\n",name,samples[10],samples.back(),maximumRefinements,usage.ru_maxrss);
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
    std::printf("query benchmark matrix: %d/%d passed\n",checks-failed,checks);
}

int main(int argc,char **argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1&&std::strcmp(argv[1],"--benchmark")==0){QueryBenchmark();return failed?1:0;}
    if(argc>1&&std::strcmp(argv[1],"--stress")==0) {NearGrazes();std::printf("near-graze matrix: %d/%d passed\n",checks-failed,checks);return failed?1:0;}
    if(argc>1){std::fprintf(stderr,"Unknown probe mode\n");return 2;}
    Paths();Shots();CrossedEdges();RotatingBlade();Witnesses();NearInitialContact();
    std::printf("continuous geometry matrix: %d/%d passed\n",checks-failed,checks);
    return failed?1:0;
}
