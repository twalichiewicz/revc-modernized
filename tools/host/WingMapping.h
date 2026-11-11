#pragma once
#include <cmath>
// A clipped render vertex follows its original canonical triangle. No new
// solver node or independent animation owner is introduced by mesh authoring.
inline bool WingBarycentric(const double p[3],const double a[3],const double b[3],const double c[3],double w[3]){
 double e[3],f[3],d[3],n[3];for(int k=0;k<3;++k){e[k]=b[k]-a[k];f[k]=c[k]-a[k];d[k]=p[k]-a[k];}
 for(int k=0;k<3;++k)n[k]=e[(k+1)%3]*f[(k+2)%3]-e[(k+2)%3]*f[(k+1)%3];
 auto dot=[](const double*x,const double*y){return x[0]*y[0]+x[1]*y[1]+x[2]*y[2];};
 const double nn=dot(n,n),distance=dot(d,n);if(!std::isfinite(nn)||nn<1e-16||!std::isfinite(distance)||distance*distance>1e-8*nn)return false;
 const double ee=dot(e,e),ff=dot(f,f),ef=dot(e,f),de=dot(d,e),df=dot(d,f),denom=ee*ff-ef*ef;
 w[1]=(de*ff-df*ef)/denom;w[2]=(df*ee-de*ef)/denom;w[0]=1-w[1]-w[2];
 for(int k=0;k<3;++k)if(!std::isfinite(w[k])||w[k]<-1e-4||w[k]>1.0001)return false;return true;
}
