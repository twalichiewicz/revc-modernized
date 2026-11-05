// Independent axial-bar expectations; synthetic SI material, no alloy calibration.
#include "world-physics-material.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <limits>

int failures = 0, checks = 0;
void Check(const char *name, bool ok) {
    ++checks; failures += !ok;
    std::printf("material %s %s\n", name, ok ? "PASS" : "FAIL");
}
bool Near(double a, double b) { return std::abs(a - b) < 1e-9; }
int main() {
    // E=1000Pa, A=.1m², L=2m => k=50N/m, volume=.2m³.
    structure::Material m{1000, 10, 0, 0.05, 2, 0.5};
    structure::Bar b{2, .1, m};
    auto r = b.Sample(2.01);
    Check("elastic_force_and_energy", Near(r.force, .5) && Near(b.Stored(), .0025));
    b.Sample(2);
    Check("elastic_unload", Near(b.Stored(), 0) && Near(b.plasticStrain, 0) && Near(b.heat, 0));
    r = b.Sample(2.06); // strain .03, plastic .02, stress 10Pa
    Check("plastic_return", Near(r.force, 1) && Near(b.plasticStrain, .02) && Near(b.accumulated, .02));
    // .09J trial energy = .01 elastic + .02 reserve + .06 dissipation.
    Check("return_work_partition", Near(b.Stored(), .03) && Near(b.heat, .06) && Near(b.fractureWork, 0));
    b.Sample(2.04);
    Check("permanent_set_at_zero_force", Near(b.force, 0) && Near(b.plasticStrain, .02));
    const auto unchanged = b.heat;
    b.Sample(2.04);
    Check("repeat_sample_spends_nothing", Near(b.heat, unchanged));
    structure::Bar hard{2, .1, {1000, 10, 1000, 1, 100, .5}};
    hard.Sample(2.06);
    Check("isotropic_hardening", Near(hard.plasticStrain, .01) && Near(hard.force, 2) && Near(hard.Stored(), .06) && Near(hard.heat, .03));
    structure::Bar fracture{2, .1, m};
    fracture.Sample(2.2); // reserve .09 < Gc*A=.2J: cannot buy a fracture yet
    Check("fracture_needs_available_work", !fracture.broken && Near(fracture.accumulated, .09));
    fracture.Sample(2.44); // .21 plastic strain; .21 reserve+.01 elastic >= .2
    Check("fracture_spends_once", fracture.broken && Near(fracture.fractureWork, .2) && Near(fracture.Stored(), 0) && Near(fracture.force, 0));
    auto spent = fracture.heat;
    fracture.Sample(3);
    Check("broken_member_is_inert", Near(fracture.fractureWork, .2) && Near(fracture.heat, spent) && Near(fracture.force, 0));
    structure::Bar compression{2, .1, m};
    compression.Sample(1.94);
    Check("compression_plasticity", Near(compression.plasticStrain, -.02) && Near(compression.force, -1));
    compression.Sample(1.96);
    Check("compression_unload", Near(compression.force, 0));
    bool rejected = false;
    try { b.Sample(std::numeric_limits<double>::quiet_NaN()); } catch (const std::invalid_argument &) { rejected = true; }
    Check("reject_nonfinite_before_state_change", rejected && Near(b.heat, unchanged));
    rejected = false;
    try { structure::Bar invalid{0, .1, m}; } catch (const std::invalid_argument &) { rejected = true; }
    Check("reject_zero_reference_length", rejected);
    rejected = false;
    const double priorPlastic=b.plasticStrain,priorHeat=b.heat;
    try { b.Sample(1e308); } catch (const std::invalid_argument &) { rejected = true; }
    Check("reject_overflow_without_partial_state", rejected && Near(b.plasticStrain,priorPlastic) && Near(b.heat,priorHeat));
    std::printf("material matrix: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
