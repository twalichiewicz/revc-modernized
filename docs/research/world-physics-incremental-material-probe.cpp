// Literal incremental-potential references; constitutive trial is read-only.
#include "world-physics-incremental-material.h"
#include <cmath>
#include <cstdio>
#include <limits>
int checked=0,failed=0;
void Check(const char *name,bool pass){++checked;failed+=!pass;std::printf("incremental-material %s %s\n",name,pass?"PASS":"FAIL");}
bool Near(double a,double b){return std::abs(a-b)<1e-9;}
int main(){
    structure::Bar bar{2,.1,{1000,10,0,.05,2,.5}};
    auto elastic=persistent::Trial(bar,2.01);
    Check("elastic_potential_force_tangent",Near(elastic.potential,.0025)&&Near(elastic.force,.5)&&Near(elastic.tangent,50));
    auto yielded=persistent::Trial(bar,2.06);
    Check("plastic_reduced_potential",Near(yielded.potential,.05)&&Near(yielded.force,1)&&Near(yielded.tangent,0));
    for(int i=0;i<10;++i)persistent::Trial(bar,2.2);
    Check("trial_does_not_mutate_history",Near(bar.plasticStrain,0)&&Near(bar.heat,0)&&!bar.broken);
    persistent::Commit(bar,2.06);
    Check("accepted_plastic_state",Near(bar.plasticStrain,.02)&&Near(bar.accumulated,.02)&&Near(bar.force,1));
    Check("no_fixed_strain_projection_loss_double_count",Near(bar.Stored(),.03)&&Near(bar.heat,.02)&&Near(bar.projectionLoss,0)&&Near(bar.Stored()+bar.heat,.05));
    persistent::Commit(bar,2.04);
    Check("permanent_set_unloads_to_zero_force",Near(bar.force,0)&&Near(bar.plasticStrain,.02));
    auto heat=bar.heat;persistent::Commit(bar,2.04);
    Check("repeated_commit_does_not_spend_work",Near(bar.heat,heat));
    structure::Bar hard{2,.1,{1000,10,1000,1,100,.5}};
    auto trial=persistent::Trial(hard,2.06);persistent::Commit(hard,2.06);
    Check("hardening_potential_and_tangent",Near(trial.potential,.07)&&Near(trial.force,2)&&Near(trial.tangent,25)&&Near(hard.Stored()+hard.heat,.07));
    structure::Bar fracture{2,.1,{1000,10,0,.05,2,.5}};
    persistent::Commit(fracture,2.2);
    Check("fracture_cannot_spend_dissipated_work",!fracture.broken&&Near(fracture.reserve,.09));
    persistent::Commit(fracture,2.44);
    Check("fracture_spends_once_after_commit",fracture.broken&&Near(fracture.fractureWork,.2)&&Near(fracture.Stored(),0)&&Near(fracture.force,0));
    heat=fracture.heat;persistent::Commit(fracture,3);
    Check("broken_member_cannot_repeat_work",Near(fracture.fractureWork,.2)&&Near(fracture.heat,heat));
    bool rejected=false;heat=bar.heat;
    try{persistent::Commit(bar,std::numeric_limits<double>::quiet_NaN());}catch(const std::invalid_argument&){rejected=true;}
    Check("invalid_trial_cannot_mutate_state",rejected&&Near(bar.heat,heat)&&Near(bar.plasticStrain,.02));
    std::printf("incremental material matrix: %d/%d passed\n",checked-failed,checked);return failed?1:0;
}
