#pragma once
#include "materialmodel.h"
namespace projectilemodel {
// Compatibility interface for certified native projectile callers. The common
// material solver also owns collision work and all component absorption.
using materialmodel::kLayers;using materialmodel::Material;using materialmodel::Armored;using materialmodel::Surface;using materialmodel::Cost;
using materialmodel::Layer;using materialmodel::Ray;using materialmodel::Structure;using materialmodel::Result;
using materialmodel::Absorb;
Result Doses(const mechanicalmodel::Layout &,const Ray &);
}
