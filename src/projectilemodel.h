#pragma once
#include "mechanicalmodel.h"
#include <cstddef>
namespace projectilemodel {
constexpr size_t kLayers=64;
enum class Material {Body,Glass,Rubber};
struct Layer {float fraction=0,incidence=1;Material material=Material::Body;};
// Fractions refer to the original segment. Entry is the real accepted surface;
// limit is nearer external cover. Budgets use calibrated native damage units.
struct Ray {Vec3 from{},to{};float entry=0,limit=1,budget=0;const Layer *layers=nullptr;size_t count=0;};
struct Result {std::array<float,mechanicalmodel::kModules> doses{};float remaining=0,deposited=0,surfaceLoss=0;bool valid=false;};
Result Doses(const mechanicalmodel::Layout &,const Ray &);
}
