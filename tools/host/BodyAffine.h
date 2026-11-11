#pragma once
#include <array>
#include <cmath>
// RW frame columns may include nonuniform scale. Native CMatrix::Invert is
// rigid-only and must not be used for these local/model conversions.
namespace bodyaffine {
using Matrix=std::array<float,12>;
inline bool Inverse(const Matrix &m,Matrix &inverse){
 for(float v:m)if(!std::isfinite(v))return false;
 const double a=m[0],b=m[3],c=m[6],d=m[1],e=m[4],f=m[7],g=m[2],h=m[5],i=m[8];
 const double det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(!std::isfinite(det)||std::fabs(det)<1e-10)return false;
 Matrix out{{float((e*i-f*h)/det),float((f*g-d*i)/det),float((d*h-e*g)/det),float((c*h-b*i)/det),float((a*i-c*g)/det),float((b*g-a*h)/det),float((b*f-c*e)/det),float((c*d-a*f)/det),float((a*e-b*d)/det),0,0,0}};
 for(int row=0;row<3;++row)out[9+row]=-(out[row]*m[9]+out[3+row]*m[10]+out[6+row]*m[11]);for(float v:out)if(!std::isfinite(v))return false;inverse=out;return true;
}
}
