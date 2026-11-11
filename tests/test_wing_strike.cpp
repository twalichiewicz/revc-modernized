#include <algorithm>
#include "WingPhysics.h"
#include <cmath>
#include <cstdio>
#include <limits>
int checks=0,failures=0;
void Check(const char *name,bool pass){++checks;failures+=!pass;std::printf("wing-strike %s %s\n",name,pass?"PASS":"FAIL");}
double Norm(double *v){return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);}
WpState Run(const char *mesh,bool miss,double dt,bool oblique=false){
    char error[512]{};uint64_t id=0;int made=wp_create(mesh,6,miss,oblique,&id,error,sizeof error);Check("valid_actual_wing_initializes",made==0);if(made){std::puts(error);return {};}
    WpState start{},s{};wp_snapshot(id,&start);int steps=std::lround(.8/dt);bool complete=true;double maxDeformation=0,maxStep=0;int peakContacts=0;
    for(int i=0;i<steps;++i){if(wp_advance(id,dt,error,sizeof error)){std::printf("step%d rejected: %s\n",i,error);complete=false;break;}wp_snapshot(id,&s);maxDeformation=std::fmax(maxDeformation,s.deformation);maxStep=std::fmax(maxStep,s.stepMs);peakContacts=std::max(peakContacts,s.contacts);}
    Check("physical_intervals_resolve",complete&&std::abs(s.time-.8)<1e-10);
    Check("mass_is_not_lost_or_duplicated",std::abs(s.mass-228)<1e-9);
    double residual[3];for(int k=0;k<3;++k)residual[k]=s.momentum[k]+s.support[k]-start.momentum[k]-s.externalImpulse[k];Check("building_support_and_gravity_balance_momentum",Norm(residual)<1e-5);
    Check("canonical_faces_reference_actual_fragment_nodes",s.faces>0);for(int i=0;i<s.faces;++i)for(int j=0;j<3;++j){int n=s.face[i].node[j];if(n<0||n>=s.nodes||s.node[n].fragment!=s.face[i].fragment){Check("fragment_surface_binding",false);break;}}
    if(miss){Check("clear_miss_has_no_contact_or_fracture",peakContacts==0&&s.brokenMounts==0&&s.fragments==1&&s.fractureWork==0);Check("miss_preserves_forward_motion",std::abs(s.forwardSpeed-6)<1e-7);}
    else {Check("building_strike_transfers_force",peakContacts>0&&Norm(s.support)>1);Check("impact_deforms_and_separates_wing",maxDeformation>.01&&s.brokenMounts>0&&s.fragments>=2&&s.fractureWork>0);Check("assembly_loses_forward_speed",s.forwardSpeed<5.9);}
    std::printf("strike miss%d dt%.6g t%.3f speed%.6g parent%.6g fragments%d mounts%d faces%d contacts%d deform%.6g max_step_ms%.3f\n",miss,dt,s.time,s.forwardSpeed,s.parentSpeed,s.fragments,s.brokenMounts,s.faces,peakContacts,maxDeformation,maxStep);
    auto before=s;Check("nonfinite_step_rejected",wp_advance(id,std::numeric_limits<double>::quiet_NaN(),error,sizeof error)!=0);wp_snapshot(id,&s);Check("failed_step_keeps_revision_and_time",before.revision==s.revision&&before.time==s.time);
    Check("destroy_revokes_generation",wp_destroy(id)&&!wp_snapshot(id,&s)&&!wp_destroy(id));return before;
}
int main(int argc,char **argv){if(argc!=2)return 2;char error[512]{};uint64_t bad=0;Check("missing_input_rejected",wp_create("/missing-wing-file",6,0,0,&bad,error,sizeof error)!=0&&wp_live_sessions()==0);
    auto miss=Run(argv[1],true,.005);auto hit=Run(argv[1],false,.005);auto refined=Run(argv[1],false,.0025);Run(argv[1],false,.005,true);
    Check("refined_strike_retains_contact_separation_outcome",hit.fragments>=2&&refined.fragments>=2&&std::abs(hit.forwardSpeed-refined.forwardSpeed)<.5);
    for(int i=0;i<3;++i){uint64_t id=0;Check("reset_recreates_valid_island",wp_create(argv[1],6,1,0,&id,error,sizeof error)==0);Check("new_generation_is_unique",id>hit.session&&id>miss.session);wp_destroy(id);}
    Check("teardown_releases_all_sessions",wp_live_sessions()==0);std::printf("wing-strike checks=%d failures=%d\n",checks,failures);return failures?1:0;}
