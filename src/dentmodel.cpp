#include "dentmodel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace dentmodel {namespace {
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
float Length2(Vec3 p){return p.x*p.x+p.y*p.y+p.z*p.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
}
bool Make(Vec3 p,Vec3 n,float severity,Impact &out)
{
 if(!Finite(p)||!Finite(n)||!std::isfinite(severity)||severity<=3||
    std::fabs(p.x)>50||std::fabs(p.y)>50||std::fabs(p.z)>50)return false;
 const float len=std::sqrt(Length2(n));if(!std::isfinite(len)||len<1e-6f)return false;
 out={p,{n.x/len,n.y/len,n.z/len},std::min(.18f,(severity-3)*.018f),1.25f};return true;
}
Vec3 Move(Vec3 original,Vec3 current,const Impact &hit)
{
 const Vec3 d{original.x-hit.point.x,original.y-hit.point.y,original.z-hit.point.z};
 const float w=std::max(0.0f,1-Length2(d)/(hit.radius*hit.radius));
 Vec3 offset{current.x-original.x+hit.inward.x*hit.depth*w*w,
             current.y-original.y+hit.inward.y*hit.depth*w*w,
             current.z-original.z+hit.inward.z*hit.depth*w*w};
 const float len=std::sqrt(Length2(offset));const float scale=len>kCap?kCap/len:1;
 return {original.x+offset.x*scale,original.y+offset.y*scale,original.z+offset.z*scale};
}
int Region(Vec3 p,Vec3 n)
{
 if(std::fabs(n.z)>.7f)return 4;
 if(std::fabs(n.y)>=std::fabs(n.x))return p.y>=0?0:1;
 return p.x<0?2:3;
}
bool Body(const char *name)
{
 if(!name||std::strstr(name,"_lo")||std::strstr(name,"_vlo"))return false;
 if(!std::strcmp(name,"chassis_hi"))return true;
 for(auto prefix:{"door_","bonnet_","boot_","bump_","wing_"})
  if(!std::strncmp(name,prefix,std::strlen(prefix))&&std::strstr(name,"_hi"))return true;
 return false;
}
bool Local(const float *m,Vec3 p,bool direction,Vec3 &out)
{
 if(!m||!Finite(p))return false;
 const Vec3 a{m[0],m[1],m[2]},b{m[4],m[5],m[6]},c{m[8],m[9],m[10]},o{m[12],m[13],m[14]};
 if(!Finite(a)||!Finite(b)||!Finite(c)||!Finite(o))return false;
 const float det=Dot(a,Cross(b,c));if(!std::isfinite(det)||std::fabs(det)<1e-5f)return false;
 if(!direction)p={p.x-o.x,p.y-o.y,p.z-o.z};
 out={Dot(p,Cross(b,c))/det,Dot(p,Cross(c,a))/det,Dot(p,Cross(a,b))/det};return Finite(out);
}
}
