#pragma once
#include "mechanicalmodel.h"
#include <cstddef>
#include <algorithm>
#include <cmath>
namespace projectilemodel {
constexpr size_t kLayers=64;
enum class Material {Body,Glass,Rubber,Armor,ArmoredGlass,Skin};
inline bool Armored(int model){return model==157||model==158;}
inline Material Surface(int model,Material m){return Armored(model)?m==Material::Body?Material::Armor:m==Material::Glass?Material::ArmoredGlass:m:m;}
inline float Cost(Material m,float incidence){const float costs[]={12,4,6,100,65,3};const int i=int(m);return i>=0&&i<6?costs[i]/std::max(.05f,incidence):1000.f;}
struct Layer {float fraction=0,incidence=1;Material material=Material::Body;};
// Fractions refer to the original segment. Entry is the real accepted surface;
// limit is nearer external cover. Budgets use calibrated native damage units.
struct Ray {Vec3 from{},to{};float entry=0,limit=1,budget=0;const Layer *layers=nullptr;size_t count=0;};
struct Structure {std::array<mechanicalmodel::Box,3> volumes{};std::array<float,3> resistance{},capacity{};};
struct Result {std::array<float,3> structuralDoses{};std::array<float,mechanicalmodel::kModules> doses{};std::array<float,kLayers> layerRemaining{};float remaining=0,deposited=0,surfaceLoss=0;bool valid=false;};
Result Absorb(const mechanicalmodel::Box &body,const std::array<mechanicalmodel::Box,mechanicalmodel::kModules> &volumes,const float *resistance,const float *capacity,const Ray &,const Structure *structure=nullptr);
Result Doses(const mechanicalmodel::Layout &,const Ray &);
}
