#pragma once
// Rate-independent axial return mapping as an incremental potential. Newton
// evaluations do not mutate history. Unlike the older explicit fixed-strain
// projection policy, the accepted reduced-potential solve adds no extra
// fixed-strain projection loss: its returned stress already includes yielding.
#include "world-physics-material.h"
#include <algorithm>
namespace persistent {
struct TrialState {
    double potential=0,force=0,tangent=0;
    double plastic=0,accumulated=0,elastic=0,hardening=0,plasticWork=0;
};
inline TrialState Trial(const structure::Bar &bar,double currentLength) {
    structure::Bar validate(bar.length,bar.area,bar.material);(void)validate;
    if(!std::isfinite(currentLength)||currentLength<=0||!std::isfinite(bar.plasticStrain)||
       !std::isfinite(bar.accumulated)||bar.accumulated<0)throw std::invalid_argument("Invalid constitutive trial");
    TrialState r;r.plastic=bar.plasticStrain;r.accumulated=bar.accumulated;
    if(bar.broken)return r;
    const auto &m=bar.material;double volume=bar.area*bar.length;
    double strain=currentLength/bar.length-1,stress=m.modulus*(strain-bar.plasticStrain);
    double excess=std::abs(stress)-(m.yield+m.hardening*bar.accumulated);
    double increment=std::max(0.,excess)/(m.modulus+m.hardening);
    r.plastic+=std::copysign(increment,stress);r.accumulated+=increment;
    double elasticStrain=strain-r.plastic;
    r.elastic=.5*m.modulus*elasticStrain*elasticStrain*volume;
    r.hardening=.5*m.hardening*r.accumulated*r.accumulated*volume;
    r.plasticWork=m.yield*increment*volume;
    r.potential=r.elastic+r.hardening+r.plasticWork;
    r.force=m.modulus*elasticStrain*bar.area;
    r.tangent=(excess>0?m.modulus*m.hardening/(m.modulus+m.hardening):m.modulus)*bar.area/bar.length;
    for(double value:{r.potential,r.force,r.tangent,r.plastic,r.accumulated,r.elastic,r.hardening,r.plasticWork})
        if(!std::isfinite(value))throw std::invalid_argument("Constitutive trial overflow");
    return r;
}
inline void Commit(structure::Bar &bar,double length) {
    auto r=Trial(bar,length);
    if(bar.broken)return;
    auto next=bar;
    next.plasticStrain=r.plastic;next.accumulated=r.accumulated;next.elastic=r.elastic;next.hardeningEnergy=r.hardening;
    next.reserve+=next.material.reserveFraction*r.plasticWork;
    next.heat+=(1-next.material.reserveFraction)*r.plasticWork;next.force=r.force;
    double cost=next.material.toughness*next.area;
    if(next.accumulated>=next.material.failureStrain&&next.Stored()>=cost) {
        next.heat+=next.Stored()-cost;next.fractureWork+=cost;
        next.elastic=next.hardeningEnergy=next.reserve=next.force=0;next.broken=true;
    }
    for(double value:{next.heat,next.reserve,next.Stored(),next.fractureWork})
        if(!std::isfinite(value)||value<0)throw std::invalid_argument("Material commit overflow");
    bar=next;
}
}
