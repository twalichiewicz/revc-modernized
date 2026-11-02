// Original opaque meshes -> baked upper faces -> the aircraft collision data.
#include "../tools/bake/gamefiles.h"
#include "../tools/bake/visualmodel.h"
#ifndef TOWERCOLLISION_BASELINE
#include "../tools/bake/towercollision.h"
#include "../src/towercollisiondata.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
static int failures;
#define CHECK(x) do{if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
Vec3f Sub(Vec3f a,Vec3f b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Dot(Vec3f a,Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3f Cross(Vec3f a,Vec3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool Hit(Vec3f a,Vec3f b,Vec3f x,Vec3f y,Vec3f z,float &t){auto d=Sub(b,a),e=Sub(y,x),f=Sub(z,x),h=Cross(d,f);float det=Dot(e,h);if(std::fabs(det)<1e-7f)return false;auto s=Sub(a,x);float u=Dot(s,h)/det;if(u<0||u>1)return false;auto q=Cross(s,e);float v=Dot(d,q)/det;if(v<0||u+v>1)return false;float at=Dot(f,q)/det;if(at<0||at>=t)return false;t=at;return true;}
bool Original(const ColModel &col,const Instance &in,Vec3f from,Vec3f to){float t=1;for(auto f:col.faces)Hit(from,to,visualmodel::Place(col.verts[f.a],in),visualmodel::Place(col.verts[f.b],in),visualmodel::Place(col.verts[f.c],in),t);
 for(auto box:col.boxes){float near=0,far=1;float a[]={from.x-in.pos.x,from.y-in.pos.y,from.z-in.pos.z},d[]={to.x-from.x,to.y-from.y,to.z-from.z},lo[]={box.min.x,box.min.y,box.min.z},hi[]={box.max.x,box.max.y,box.max.z};bool valid=true;for(int k=0;k<3;++k){if(std::fabs(d[k])<1e-7f){if(a[k]<lo[k]||a[k]>hi[k])valid=false;continue;}float l=(lo[k]-a[k])/d[k],h=(hi[k]-a[k])/d[k];if(l>h)std::swap(l,h);near=std::max(near,l);far=std::min(far,h);}if(valid&&near<=far&&far>=0&&near<1)t=std::min(t,near);}
 return t<1;
}
bool Upper(const Instance &in,Vec3f from,Vec3f to,float &t){
#ifndef TOWERCOLLISION_BASELINE
 for(const auto &model:towercollisiondata::kModels)if(in.model==model.name)for(int i=0;i<model.triangleCount;++i){const auto &f=model.triangles[i];auto point=[&](int k){const auto p=model.vertices[f.v[k]];return visualmodel::Place({p.x,p.y,p.z},in);};Hit(from,to,point(0),point(1),point(2),t);}
#else
 (void)in;(void)from;(void)to;(void)t;
#endif
 return t<1;
}
#ifndef TOWERCOLLISION_BASELINE
// Independently inspect the original render inputs. Material alpha alone is
// insufficient: an opaque material may carry an invisible cutout atlas.
bool LocalUpper(const towercollisiondata::Model &model,Vec3f a,Vec3f b){float t=1;for(int i=0;i<model.triangleCount;++i){const auto &f=model.triangles[i];auto p=[&](int k){const auto v=model.vertices[f.v[k]];return Vec3f{v.x,v.y,v.z};};Hit(a,b,p(0),p(1),p(2),t);}return t<1;}
void SurfaceCoverage(ImgArchive &archive,const towercollisiondata::Model &model){
 std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string why;CHECK(archive.Read(std::string(model.name)+".dff",bytes));CHECK(visualmodel::Read(bytes,mesh,why));int checked=0,missed=0;
 for(const auto &f:mesh.faces){if(mesh.materials[f.material].alpha!=255)continue;const auto a=mesh.vertices[f.a],b=mesh.vertices[f.b],c=mesh.vertices[f.c];auto normal=Cross(Sub(b,a),Sub(c,a));const float length=std::sqrt(Dot(normal,normal));if(length<.0001f)continue;normal={normal.x/length*.1f,normal.y/length*.1f,normal.z/length*.1f};
  for(int i=1;i<10;++i)for(int j=1;j<10-i;++j){const float u=i*.1f,v=j*.1f,w=1-u-v;const Vec3f p{a.x*u+b.x*v+c.x*w,a.y*u+b.y*v+c.y*w,a.z*u+b.z*v+c.z*w};if(p.z<=model.bottom+.02f)continue;++checked;missed+=!LocalUpper(model,{p.x-normal.x,p.y-normal.y,p.z-normal.z},{p.x+normal.x,p.y+normal.y,p.z+normal.z});}
 }CHECK(checked>0&&missed==0);std::printf("upper original surface %d: %d/%d missed\n",model.id,missed,checked);
 bool found=false;for(const auto &entry:archive.Names())if(entry.size()>4&&entry.substr(entry.size()-4)==".col"){CHECK(archive.Read(entry,bytes));for(size_t start=0;start+80<=bytes.size()&&!std::memcmp(bytes.data()+start,"COLL",4);){uint32_t size=0;std::memcpy(&size,bytes.data()+start+4,4);const size_t end=start+8+size;if(end>bytes.size()||end<=start)break;
   const std::string name=Lower(std::string(reinterpret_cast<const char *>(bytes.data()+start+8),strnlen(reinterpret_cast<const char *>(bytes.data()+start+8),22)));if(name==model.name){found=true;float radius=0;Vec3f centre{},lo{},hi{};std::memcpy(&radius,bytes.data()+start+32,4);std::memcpy(&centre,bytes.data()+start+36,12);std::memcpy(&lo,bytes.data()+start+48,12);std::memcpy(&hi,bytes.data()+start+60,12);
    for(int k=0;k<model.vertexCount;++k){const auto p=model.vertices[k];CHECK(p.x>=lo.x-.02f&&p.x<=hi.x+.02f&&p.y>=lo.y-.02f&&p.y<=hi.y+.02f&&p.z>=lo.z-.02f&&p.z<=hi.z+.02f);const Vec3f d{p.x-centre.x,p.y-centre.y,p.z-centre.z};CHECK(Dot(d,d)<(radius+.02f)*(radius+.02f));}break;
   }start=end;
  }if(found)break;
 }CHECK(found);
}
void Opacity(ImgArchive &archive,const towercollisiondata::Model &model){
 const char *txd=model.id==1868?"dt_shops1":model.id==1882?"dt_qbert":model.id==1924?"dt_scyscrap2":"dt_newtest2";
 std::vector<uint8_t> dff,texture;CHECK(archive.Read(std::string(model.name)+".dff",dff));CHECK(archive.Read(std::string(txd)+".txd",texture));visualmodel::Mesh mesh;std::string why;CHECK(visualmodel::Read(dff,mesh,why));
 std::map<std::string,bool> inspected;int mast=0;
 for(const auto &f:mesh.faces){if(mesh.materials[f.material].alpha!=255||std::max({mesh.vertices[f.a].z,mesh.vertices[f.b].z,mesh.vertices[f.c].z})<model.bottom)continue;
  const auto &name=mesh.materials[f.material].texture;if(model.id==1882&&std::max({mesh.vertices[f.a].z,mesh.vertices[f.b].z,mesh.vertices[f.c].z})>400)++mast;if(inspected[name])continue;inspected[name]=true;
  int width=0,height=0;std::vector<uint8_t> rgba;CHECK(DecodeTxdTexture(texture,name,width,height,rgba));CHECK(!rgba.empty());for(size_t i=3;i<rgba.size();i+=4)CHECK(rgba[i]==255);
 }
 CHECK(inspected.size()==size_t(model.id==1868?5:model.id==1857?1:2));if(model.id==1882)CHECK(mast==4);
 auto word=[&](size_t p){uint32_t value=0;CHECK(p+4<=dff.size());if(p+4<=dff.size())std::memcpy(&value,dff.data()+p,4);return value;};
 for(size_t p=12;p+12<=dff.size()&&p<12+word(4);){const size_t end=p+12+word(p+4);CHECK(end<=dff.size());if(end>dff.size())break;
  if(word(p)==26)for(size_t g=p+12;g+12<=end;){const size_t stop=g+12+word(g+4);CHECK(stop<=end);if(stop>end)break;if(word(g)==15){const size_t st=g+24;const uint32_t flags=word(st),vertices=word(st+8),version=word(g+20);const uint32_t library=version&0xffff0000u?(((version>>14)&0x3ff00u)+0x30000u)|((version>>16)&0x3fu):version<<8;const size_t colors=st+16+(library<0x34000?12:0);if(flags&8){CHECK(colors+size_t(vertices)*4<=stop);for(uint32_t i=0;i<vertices&&colors+size_t(i)*4+3<dff.size();++i)CHECK(dff[colors+size_t(i)*4+3]==255);}}g=stop;}
  p=end;
 }
}
#endif
int main(){const char *env=std::getenv("GAMEDATA");const std::string root=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive archive;CHECK(archive.Open(root+"/models/gta3.img"));std::vector<uint8_t> bytes;std::map<std::string,ColModel> cols;CHECK(archive.Read("downtown.col",bytes));ParseColArchive(bytes,cols);
 int cases=0;for(const auto &in:LoadInstances(root))if(in.model=="doontoon22"||in.model=="doontoon39"){
  const float height=in.model=="doontoon22"?250:300;Vec3f from{in.pos.x-150,in.pos.y,height},to{in.pos.x+150,in.pos.y,height};float t=1;CHECK(!Original(cols[in.model],in,from,to));CHECK(Upper(in,from,to,t));
  if(t<1){const float x=from.x+(to.x-from.x)*t;CHECK(std::fabs(x-(in.model=="doontoon22"?-562.971f:-400.440f))<.02f);}
  from.z=to.z=180;CHECK(Original(cols[in.model],in,from,to));float below=1;CHECK(!Upper(in,from,to,below));++cases;
 }CHECK(cases==2);
#ifndef TOWERCOLLISION_BASELINE
 std::vector<towerbake::Model> regenerated;std::string why;CHECK(towerbake::Load(root,regenerated,why));if(!why.empty())std::printf("bake: %s\n",why.c_str());CHECK(regenerated.size()==4&&std::size(towercollisiondata::kModels)==4);
 const int ids[]={1868,1882,1924,1857};size_t triangles=0;
 for(size_t i=0;i<regenerated.size()&&i<std::size(towercollisiondata::kModels);++i){const auto &fresh=regenerated[i];const auto &baked=towercollisiondata::kModels[i];CHECK(baked.id==ids[i]&&fresh.id==baked.id&&fresh.name==baked.name);CHECK(fresh.faces.size()==baked.triangleCount&&baked.triangleCount>0&&baked.triangleCount<600&&baked.vertexCount>0&&baked.vertexCount<=3*baked.triangleCount);triangles+=baked.triangleCount;Opacity(archive,baked);SurfaceCoverage(archive,baked);
  for(size_t k=0;k<fresh.faces.size()&&k<baked.triangleCount;++k)for(int v=0;v<3;++v){const auto &p=fresh.faces[k].v[v];const auto q=baked.vertices[baked.triangles[k].v[v]];CHECK(p.x==q.x&&p.y==q.y&&p.z==q.z);CHECK(q.z>=baked.bottom-.0001f);}
 }
 std::printf("tower upper assets: four original models, %zu clipped opaque triangles; both native-missing facade rays accepted, lower native contacts preserved\n",triangles);
#endif
 if(!failures)std::puts("tower collision assets: all passed");return failures?1:0;
}
