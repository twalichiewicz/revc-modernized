// Force-driven contact investigation, isolated from native/game integration.
#define main BaselineProbeMain
#include "world-physics-probe.cpp"
#undef main
#include "world-physics-structure.h"
#include "world-physics-persistent.h"
#include <sys/resource.h>
int checked=0,failed=0;
void Check(const char *name,bool pass){++checked;failed+=!pass;std::printf("persistent %s %s\n",name,pass?"PASS":"FAIL");}
Settings Options(){Settings s;s.explicitForces=true;return s;}
structure::Material Elastic(double e=1000){return {e,1e12,0,1,1e12,.5};}
void AnalyticBar(){
    World w(Options());structure::Force f;structure::Asset a(w,f);
    a.Node({0,0,0},1);a.Node({1,0,0},1);a.Member(0,1,.1,Elastic(),"spar");a.nodes[1].x.setX(1.1);a.Build();
    persistent::Stepper stepper(w,{&a});Eigen::MatrixXd forces=Eigen::MatrixXd::Zero(2,3);
    auto result=stepper.Advance(.1,forces);
    Check("implicit_bar_matches_hand_solution",std::abs(a.nodes[0].x.x()-1./30)<1e-9&&std::abs(a.nodes[1].x.x()-16./15)<1e-9&&std::abs(a.nodes[0].v.x()-1./3)<1e-9&&std::abs(a.nodes[1].v.x()+1./3)<1e-9);
    Check("one_clock_and_balanced_internal_forces",std::abs(stepper.time-.1)<1e-12&&a.Momentum().length()<1e-10&&result.forceBalance<1e-8&&result.torqueBalance<1e-8);
    Check("bar_numerical_loss_is_measured",std::abs(result.numericalLoss-1./3)<1e-9);
}
void ExternalForce(){
    World w(Options());structure::Force f;structure::Asset a(w,f);a.Node({0,0,0},2,{1,.2,0});a.Build();
    persistent::Stepper stepper(w,{&a});Eigen::MatrixXd forces=Eigen::MatrixXd::Zero(1,3);forces(0,1)=4;
    auto result=stepper.Advance(.1,forces);
    Check("free_mass_has_force_driven_motion",(a.nodes[0].x-btVector3(.1,.04,0)).length()<1e-10&&(a.nodes[0].v-btVector3(1,.4,0)).length()<1e-10);
    Check("external_work_and_numerical_loss",std::abs(result.externalWork-.16)<1e-10&&std::abs(result.numericalLoss-.04)<1e-10);
    bool rejected=false;auto before=a.nodes[0];
    try{forces(0,0)=std::numeric_limits<double>::quiet_NaN();stepper.Advance(.1,forces);}catch(const std::invalid_argument&){rejected=true;}
    Check("nonfinite_load_cannot_publish_partial_state",rejected&&(a.nodes[0].x-before.x).length()==0&&(a.nodes[0].v-before.v).length()==0&&std::abs(stepper.time-.1)<1e-12);
}
void Triangle(structure::Asset &a,double z,double mass,btVector3 velocity={0,0,0}) {
    a.Node({-.6,-.4,z},mass,velocity);a.Node({.6,-.4,z},mass,velocity);a.Node({0,.6,z},mass,velocity);
    for(auto pair:{std::pair<int,int>{0,1},{1,2},{2,0}})a.Member(pair.first,pair.second,.01,Elastic(1e5),"skin");
    a.Face(0,1,2);a.Build();
}
double PanelGap(const structure::Asset &a,const structure::Asset &b) {
    double gap=1e100;
    for(auto *p:{&a,&b})for(auto &n:p->nodes){auto *other=p==&a?&b:&a;
        auto vec=[](btVector3 v){return Eigen::Vector3d(v.x(),v.y(),v.z());};
        gap=std::min(gap,std::sqrt(ipc::point_triangle_distance(vec(n.x),vec(other->nodes[0].x),vec(other->nodes[1].x),vec(other->nodes[2].x))));
    }
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){auto vec=[](btVector3 v){return Eigen::Vector3d(v.x(),v.y(),v.z());};
        gap=std::min(gap,std::sqrt(ipc::edge_edge_distance(vec(a.nodes[i].x),vec(a.nodes[(i+1)%3].x),vec(b.nodes[j].x),vec(b.nodes[(j+1)%3].x))));
    }
    return gap;
}
void LoadedPanels(bool supported) {
    World w(Options());structure::Force f;structure::Asset a(w,f),b(w,f);
    Triangle(a,0,2);Triangle(b,.06,1,{.1,0,-.5});
    std::vector<persistent::Pin> pins;if(supported)for(int i=0;i<3;++i)pins.push_back({&a,i});
    persistent::Stepper stepper(w,{&a,&b},pins);Eigen::MatrixXd forces=Eigen::MatrixXd::Zero(6,3);
    btVector3 p0=a.Momentum()+b.Momentum(),support(0,0,0),external(0,0,0);
    double smallest=1e100,loadedGap=0,releasedGap=0;int contacts=0,iterations=0,retries=0,recontacts=0;
    bool synchronized=true;
    for(int tick=0;tick<240;++tick){
        for(int i=0;i<3;++i){forces(i,2)=supported?0:3;forces(i+3,2)=tick<80||tick>=120?-3:3;}
        auto stats=stepper.Advance(.005,forces);smallest=std::min(smallest,PanelGap(a,b));contacts=std::max(contacts,stats.peakContacts);
        iterations+=stats.newton;retries+=stats.retries;
        support+=btVector3(stats.supportLinear.x(),stats.supportLinear.y(),stats.supportLinear.z());
        external+=btVector3(stats.externalImpulse.x(),stats.externalImpulse.y(),stats.externalImpulse.z());
        synchronized&=a.Synchronized()&&b.Synchronized();if(tick==79)loadedGap=PanelGap(a,b);if(tick==119)releasedGap=PanelGap(a,b);
        if(tick>=120)recontacts+=stats.peakContacts>0;
    }
    std::printf("loaded panels supported=%d gap_min=%.9g gap_loaded=%.9g gap_final=%.9g contacts=%d newton=%d retries=%d momentum_error=%.9g\n",supported,smallest,loadedGap,PanelGap(a,b),contacts,iterations,retries,(a.Momentum()+b.Momentum()+support-p0-external).length());
    Check(supported?"supported_panels_resist_and_release":"free_panels_resist_and_release",contacts>0&&smallest>.002-1e-7&&releasedGap>loadedGap+.01);
    Check(supported?"supported_panel_recontact":"free_panel_recontact",recontacts>10&&PanelGap(a,b)<releasedGap-.01);
    Check(supported?"fixed_support_receives_contact_reaction":"unequal_mass_contact_preserves_momentum",(a.Momentum()+b.Momentum()+support-p0-external).length()<1e-6&&(!supported||support.length()>.1));
    Check(supported?"support_geometry_stays_current":"free_geometry_stays_current",synchronized&&std::abs(a.BulletMass()+b.BulletMass()-9)<1e-10);
}
double AngularError(double dt) {
    World w(Options());structure::Force f;structure::Asset a(w,f);
    a.Node({-.5,0,0},1,{0,-.5,0});a.Node({.5,0,0},1,{0,.5,0});a.Member(0,1,.1,Elastic(1e4),"spar");a.Build();
    persistent::Stepper stepper(w,{&a});auto initial=a.Angular();double loss=0;
    for(int tick=0;tick<int(std::lround(.2/dt));++tick)loss+=stepper.Advance(dt,Eigen::MatrixXd::Zero(2,3)).numericalLoss;
    double error=(a.Angular()-initial).length();std::printf("rotating bar dt=%.8g angular_error=%.9g numerical_loss=%.9g\n",dt,error,loss);return error;
}
void SeveredMount() {
    World w(Options());structure::Force f;structure::Asset a(w,f);
    a.Node({0,0,0},2);a.Node({1,0,0},1);a.Member(0,1,.01,{1000,5,0,.02,.01,.5},"mount");a.Build();
    persistent::Stepper stepper(w,{&a},{{&a,0}});Eigen::MatrixXd forces=Eigen::MatrixXd::Zero(2,3);forces(1,0)=10;
    auto stats=stepper.Advance(.1,forces);
    Check("broken_mount_splits_registered_graph",stats.topologyChanges==1&&a.members[0].bar.broken&&a.FragmentCount()==2&&w.world->getSoftBodyArray().size()==2&&a.Synchronized());
    stepper.Advance(.1,forces);
    Check("support_identity_survives_fragment_rebuild",a.nodes[0].x.length()<1e-12&&a.nodes[0].v.length()<1e-12&&a.nodes[1].x.x()>1.2&&a.Synchronized()&&std::abs(a.BulletMass()-3)<1e-12);
}
void RejectWithoutPublication() {
    World w(Options());structure::Force f;structure::Asset a(w,f);
    a.Node({0,0,0},1);a.Node({1,0,0},1);a.Member(0,1,.1,Elastic(),"spar");a.Build();
    persistent::Options settings;settings.maxNewton=1;settings.maxSubdivisions=0;
    persistent::Stepper stepper(w,{&a},{},settings);Eigen::MatrixXd forces=Eigen::MatrixXd::Zero(2,3);forces(1,0)=5;
    auto before=a.nodes;auto bar=a.members[0].bar;int revision=a.revision;bool rejected=false;
    try{stepper.Advance(.1,forces);}catch(const std::runtime_error&){rejected=true;}
    Check("exhausted_solve_does_not_publish_trial_state",rejected&&stepper.time==0&&a.revision==revision&&a.members[0].bar.plasticStrain==bar.plasticStrain&&
        (a.nodes[0].x-before[0].x).length()==0&&(a.nodes[1].x-before[1].x).length()==0&&a.Synchronized());
    settings.contactStiffness=std::numeric_limits<double>::quiet_NaN();rejected=false;
    try{persistent::Stepper invalid(w,{&a},{},settings);}catch(const std::invalid_argument&){rejected=true;}
    Check("nonfinite_contact_settings_rejected",rejected);
}
void TornSurface() {
    World w(Options());structure::Force f;structure::Asset a(w,f);
    a.Node({0,0,0},1);a.Node({1,0,0},1);a.Node({0,1,0},1);
    a.Member(0,1,.001,{1000,1,0,.0001,.0001,.5},"skin seam");
    a.Member(1,2,.01,Elastic(1e5),"skin");a.Member(2,0,.01,Elastic(1e5),"skin");a.Face(0,1,2);a.Build();
    bool initial=a.Ray({.2,.2,1},{.2,.2,-1});int revision=a.revision;
    persistent::Stepper stepper(w,{&a},{{&a,0},{&a,2}});Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(3,3);loads(1,0)=10;
    int breaks=0;for(int i=0;i<10;++i)breaks+=stepper.Advance(.01,loads).topologyChanges;
    Check("force_driven_tear_removes_actual_query_face",initial&&breaks==1&&a.SurfaceCount()==0&&!a.Ray({.2,.2,1},{.2,.2,-1})&&a.revision>revision&&a.Synchronized());
    Check("tear_does_not_duplicate_mass_or_fracture_work",std::abs(a.BulletMass()-3)<1e-12&&std::abs(a.members[0].bar.fractureWork-1e-7)<1e-14);
}
void FragmentRecontact() {
    World w(Options());structure::Force f;structure::Asset a(w,f);
    for(int sheet=0;sheet<2;++sheet)for(btVector3 xy:{btVector3(-.6,-.4,0),btVector3(.6,-.4,0),btVector3(0,.6,0)})
        a.Node(xy+btVector3(0,0,.06*sheet),sheet?1:2,sheet?btVector3(.1,0,-.5):btVector3(0,0,0));
    for(int sheet=0;sheet<2;++sheet){int base=3*sheet;for(auto pair:{std::pair<int,int>{0,1},{1,2},{2,0}})
        a.Member(base+pair.first,base+pair.second,.01,Elastic(1e5),"skin");a.Face(base,base+1,base+2);}
    int bridge=a.Member(0,3,.001,{1000,1,0,.01,.00001,.5},"mount");a.Build();
    persistent::Stepper stepper(w,{&a},{{&a,0},{&a,1},{&a,2}});Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(6,3);
    double minGap=1e100,releasedGap=0;int contacts=0,breaks=0;
    for(int tick=0;tick<240;++tick) {
        for(int i=3;i<6;++i)loads(i,2)=tick<80||tick>=120?-3:3;
        auto stats=stepper.Advance(.005,loads);breaks+=stats.topologyChanges;if(tick>=160)contacts+=stats.peakContacts>0;
        auto vec=[](btVector3 v){return Eigen::Vector3d(v.x(),v.y(),v.z());};
        double gap=1e100;for(int i=3;i<6;++i)gap=std::min(gap,std::sqrt(ipc::point_triangle_distance(vec(a.nodes[i].x),vec(a.nodes[0].x),vec(a.nodes[1].x),vec(a.nodes[2].x))));
        minGap=std::min(minGap,gap);if(tick==119)releasedGap=gap;
    }
    std::printf("fragment recontact gap_min=%.9g released=%.9g contacts=%d fragments=%d\n",minGap,releasedGap,contacts,a.FragmentCount());
    Check("severed_fragments_recontact_in_same_island",breaks==1&&a.members[bridge].bar.broken&&a.FragmentCount()==2&&contacts>10&&minGap>.002-1e-7&&releasedGap>.04);
    Check("fragment_faces_and_registered_mass_stay_current",a.SurfaceCount()==2&&a.Synchronized()&&std::abs(a.BulletMass()-9)<1e-10);
}
void ChangedIsland() {
    World w(Options());structure::Force f;structure::Asset a(w,f);a.Node({0,0,0},1);a.Build();persistent::Stepper stepper(w,{&a});
    a.nodes[0].mass=2;bool rejected=false;try{stepper.Advance(.1,Eigen::MatrixXd::Zero(1,3));}catch(const std::invalid_argument&){rejected=true;}
    Check("changed_mass_rejected_before_motion",rejected&&stepper.time==0&&a.nodes[0].x.length()==0);
}
void FastPoint(bool miss) {
    World w(Options());structure::Force f;structure::Asset panel(w,f),point(w,f);Triangle(panel,0,2);
    point.Node({miss?1.5:.07,0,1},.02,{0,0,-200});point.Build();
    persistent::Stepper stepper(w,{&panel,&point},{{&panel,0},{&panel,1},{&panel,2}});
    auto result=stepper.Advance(.01,Eigen::MatrixXd::Zero(4,3));
    Check(miss?"fast_point_preserves_clear_miss":"fast_point_cannot_tunnel_through_intact_face",miss?std::abs(point.nodes[0].x.z()+1)<1e-8:point.nodes[0].x.z()>.002-1e-7&&result.peakContacts>0);
}
void Benchmark(int pairs) {
    World w(Options());structure::Force f;std::vector<std::unique_ptr<structure::Asset>> storage;std::vector<structure::Asset*> assets;std::vector<persistent::Pin> pins;
    for(int pair=0;pair<pairs;++pair)for(int sheet=0;sheet<2;++sheet) {
        auto a=std::make_unique<structure::Asset>(w,f);Triangle(*a,.06*sheet,sheet?1:2,sheet?btVector3(.1,0,-.5):btVector3(0,0,0));
        for(auto &n:a->nodes)n.x+=btVector3(3*pair,0,0);a->Build();
        if(!sheet)for(int i=0;i<3;++i)pins.push_back({a.get(),i});assets.push_back(a.get());storage.push_back(std::move(a));
    }
    persistent::Stepper stepper(w,assets,pins);Eigen::MatrixXd loads=Eigen::MatrixXd::Zero(6*pairs,3);
    for(int p=0;p<pairs;++p)for(int i=3;i<6;++i)loads(6*p+i,2)=-3;
    int peak=0,newton=0,retries=0,substeps=0;auto start=std::chrono::steady_clock::now();
    for(int tick=0;tick<120;++tick){auto s=stepper.Advance(.005,loads);peak=std::max(peak,s.peakContacts);newton+=s.newton;retries+=s.retries;substeps+=s.substeps;}
    double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();struct rusage usage{};getrusage(RUSAGE_SELF,&usage);
    std::printf("persistent benchmark pairs=%d nodes=%d members=%d triangles=%d fixed_nodes=%d steps=120 simulated_s=.6 wall_s=%.6f us_per_step=%.6f seconds_per_simulated_second=%.6f nonlinear_iterations=%d accepted_substeps=%d retries=%d peak_contact_stencils=%d process_peak_rss_bytes=%ld\n",pairs,6*pairs,6*pairs,2*pairs,3*pairs,elapsed,1e6*elapsed/120,elapsed/.6,newton,substeps,retries,peak,usage.ru_maxrss);
}
int main(int argc,char **argv){std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1&&std::string(argv[1])=="--benchmark"){Benchmark(1);Benchmark(4);return 0;}
    AnalyticBar();ExternalForce();LoadedPanels(false);LoadedPanels(true);SeveredMount();RejectWithoutPublication();
    TornSurface();FragmentRecontact();ChangedIsland();
    FastPoint(false);FastPoint(true);
    double coarse=AngularError(.02),fine=AngularError(.01),finer=AngularError(.005);
    Check("angular_integration_error_converges_without_correction",fine<.7*coarse&&finer<.7*fine&&finer>0);
    std::printf("persistent matrix: %d/%d passed\n",checked-failed,checked);return failed?1:0;}
