#pragma once
// Research-only 1D small-strain axial return map, SI units. Not a calibrated
// metal shell, buckling model, fatigue law or mesh-objective tearing model.
#include <cmath>
#include <initializer_list>

#include <stdexcept>
namespace structure {
struct Material {
    double modulus, yield, hardening, failureStrain, toughness, reserveFraction;
};
struct Result { double force; };
struct Bar {
    double length, area; Material material;
    double plasticStrain=0, accumulated=0, heat=0, fractureWork=0, force=0;
    double elastic=0, hardeningEnergy=0, reserve=0, projectionLoss=0;
    bool broken=false;
    Bar(double l, double a, Material m) : length(l), area(a), material(m) {
        for (auto v : {l, a, m.modulus, m.yield, m.hardening, m.failureStrain,
                       m.toughness, m.reserveFraction})
            if (!std::isfinite(v)) throw std::invalid_argument("Nonfinite bar property");
        if (l <= 0 || a <= 0 || m.modulus <= 0 || m.yield <= 0 || m.hardening < 0 ||
            m.failureStrain <= 0 || m.toughness <= 0 || m.reserveFraction < 0 || m.reserveFraction > 1)
            throw std::invalid_argument("Invalid bar property");
    }
    double Stored() const { return elastic + hardeningEnergy + reserve; }
    Result Sample(double currentLength) {
        Bar next=*this;
        const auto result=next.Advance(currentLength);
        for(auto value:{next.plasticStrain,next.accumulated,next.heat,next.fractureWork,
                        next.force,next.elastic,next.hardeningEnergy,next.reserve,
                        next.projectionLoss,next.Stored()})
            if(!std::isfinite(value))throw std::invalid_argument("Bar state overflow");
        *this=next;
        return result;
    }
  private:
    Result Advance(double currentLength) {
        if (!std::isfinite(currentLength) || currentLength <= 0)
            throw std::invalid_argument("Invalid current bar length");
        if (broken) return {0};
        const auto &m = material;
        const double strain = currentLength / length - 1, volume = area * length;
        const double trial = m.modulus * (strain - plasticStrain);
        const double excess = std::abs(trial) - (m.yield + m.hardening * accumulated);
        if (excess > 0) {
            const double increment = excess / (m.modulus + m.hardening);
            plasticStrain += std::copysign(increment, trial);
            accumulated += increment;
            const double plasticWork = m.yield * increment * volume;
            // Work released by return mapping at fixed total strain. Keep the
            // discrete projection loss separate from physical plastic work.
            const double loss = .5 * (m.modulus + m.hardening) * increment * increment * volume;
            projectionLoss += loss;
            reserve += m.reserveFraction * plasticWork;
            heat += (1 - m.reserveFraction) * plasticWork + loss;
        }
        const double elasticStrain = strain - plasticStrain;
        elastic = .5 * m.modulus * elasticStrain * elasticStrain * volume;
        hardeningEnergy = .5 * m.hardening * accumulated * accumulated * volume;
        force = m.modulus * elasticStrain * area;
        const double cost = m.toughness * area;
        if (accumulated >= m.failureStrain && Stored() >= cost) {
            // Dissipative fracture: spend existing stored/reserved work once.
            // No fragment kick and no second impact/damage energy budget.
            heat += Stored() - cost;
            fractureWork += cost;
            elastic = hardeningEnergy = reserve = force = 0;
            broken = true;
        }
        return {force};
    }
};
}
