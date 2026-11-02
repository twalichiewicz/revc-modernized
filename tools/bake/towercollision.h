#pragma once
// Offline only: exact opaque upper faces from the four inventoried original
// building DFFs. Runtime collision never opens the archive or render geometry.
#include "gamefiles.h"
#include "visualmodel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace towerbake {
struct Face {Vec3f v[3];};
struct Model {int id;std::string name;float bottom;std::vector<Face> faces;};
inline bool Load(const std::string &root,std::vector<Model> &out,std::string &why){
 out.clear();why.clear();ImgArchive img;std::vector<uint8_t> col;
 if(!img.Open(root+"/models/gta3.img")){why="original collision archive unavailable";return false;}
 for(const auto &name:img.Names())if(name.size()>4&&name.substr(name.size()-4)==".col"){std::vector<uint8_t> bytes;if(!img.Read(name,bytes))return false;for(size_t at=0;at+8<=bytes.size()&&!std::memcmp(bytes.data()+at,"COLL",4);){uint32_t size=0;std::memcpy(&size,bytes.data()+at+4,4);const size_t end=at+8+size;if(end>bytes.size()||end<=at)return false;col.insert(col.end(),bytes.begin()+at,bytes.begin()+end);at=end;}}
 const std::pair<int,const char *> wanted[]={{1868,"doontoon22"},{1882,"doontoon39"},{1924,"doontoon56"},{1857,"doontoon_top07"}};
 auto get=[&](size_t at,auto &value){if(at>col.size()||sizeof value>col.size()-at)return false;std::memcpy(&value,col.data()+at,sizeof value);return true;};
 for(const auto &[id,name]:wanted){float top=-std::numeric_limits<float>::infinity();bool found=false;
  for(size_t start=0;start+80<=col.size();){uint32_t size=0;get(start+4,size);const size_t end=start+8+size;if(std::memcmp(col.data()+start,"COLL",4)||end>col.size()||end<=start)break;
   if(Lower(std::string(reinterpret_cast<const char *>(col.data()+start+8),strnlen(reinterpret_cast<const char *>(col.data()+start+8),22)))!=name){start=end;continue;}
   found=true;size_t p=start+72;uint32_t count=0;
   if(!get(p,count)||count>128){why="invalid original spheres";return false;}p+=4;
   for(uint32_t i=0;i<count;++i,p+=20){float radius=0,z=0;if(!get(p,radius)||!get(p+12,z))return false;top=std::max(top,z+radius);}
   if(!get(p,count)||count>128)return false;p+=4+size_t(count)*16;
   if(!get(p,count)||count>128)return false;p+=4;
   for(uint32_t i=0;i<count;++i,p+=28){float z=0;if(!get(p+20,z))return false;top=std::max(top,z);}
   uint32_t vertices=0;if(!get(p,vertices)||vertices>65535)return false;p+=4;const size_t base=p;p+=size_t(vertices)*12;
   if(!get(p,count)||count>65535)return false;p+=4;
   for(uint32_t i=0;i<count;++i,p+=16)for(int j=0;j<3;++j){uint32_t index=0;float z=0;if(!get(p+j*4,index)||index>=vertices||!get(base+size_t(index)*12+8,z))return false;top=std::max(top,z);}
   if(p>end)return false;break;
  }
  if(!found||!std::isfinite(top)){why=std::string("original primitive maximum unavailable: ")+name;return false;}
  std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string reason;
  if(!img.Read(std::string(name)+".dff",bytes)||!visualmodel::Read(bytes,mesh,reason)){why="original visual mesh unavailable";return false;}
  Model model{id,name,top+.01f,{}};
  for(const auto &f:mesh.faces){if(mesh.materials[f.material].alpha!=255)continue;const Vec3f input[]={mesh.vertices[f.a],mesh.vertices[f.b],mesh.vertices[f.c]};std::vector<Vec3f> polygon;
   Vec3f a=input[2];for(Vec3f b:input){const bool ai=a.z>=model.bottom,bi=b.z>=model.bottom;if(ai!=bi){const float t=(model.bottom-a.z)/(b.z-a.z);polygon.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,model.bottom});}if(bi)polygon.push_back(b);a=b;}
   for(size_t i=1;i+1<polygon.size();++i){const auto a=polygon[0],b=polygon[i],c=polygon[i+1];const float x=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),y=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),z=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(x*x+y*y+z*z>1e-8f)model.faces.push_back({{a,b,c}});}
  }
  if(model.faces.empty()||model.faces.size()>=600){why="upper geometry exceeds native triangle scratch bound";return false;}out.push_back(std::move(model));
 }
 return true;
}
}
