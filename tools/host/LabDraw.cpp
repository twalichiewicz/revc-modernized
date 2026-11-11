#include "LabDraw.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
namespace labdraw {
rw::Atomic *Mesh(const std::vector<Face> &source){
 if(source.empty())return nil;
 auto g=rw::Geometry::create(source.size()*3,source.size(),rw::Geometry::POSITIONS|rw::Geometry::PRELIT|rw::Geometry::MODULATE);
 if(!g)return nil;auto mat=rw::Material::create();g->matList.appendMaterial(mat);mat->destroy();
 for(unsigned j=0;j<source.size();++j){auto &f=source[j];auto n=CrossProduct(f.p[1]-f.p[0],f.p[2]-f.p[0]);float shade=.45f+.55f*std::fabs(n.z/std::max(.001f,n.Magnitude()));
 for(int k=0;k<3;++k){g->morphTargets[0].vertices[3*j+k]=rw::makeV3d(f.p[k].x,f.p[k].y,f.p[k].z);g->colors[3*j+k]=rw::makeRGBA(f.colour.red*shade,f.colour.green*shade,f.colour.blue*shade,f.colour.alpha);g->triangles[j].v[k]=3*j+k;}g->triangles[j].matId=0;}
 g->calculateBoundingSphere();g->unlock();auto a=rw::Atomic::create();a->setGeometry(g,0);g->destroy();a->setFrame(rw::Frame::create());return a;
}
void Destroy(rw::Atomic *a){if(a){auto f=a->getFrame();a->destroy();f->destroy();}}
void Text(rw::Charset *font,const char *text,float x,float y,float scale,rw::RGBA colour){
 using V=rw::RWDEVICE::Im2DVertex;std::vector<V> v;std::vector<uint16> ix;float z=rw::im2d::GetNearZ(),rz=1/rw::engine->currentCamera->nearPlane;
 for(const unsigned char *s=(const unsigned char*)text;*s;++s){unsigned c=*s;if(c>=unsigned(font->desc.count))continue;float u=float(c%font->desc.tileWidth)*font->desc.width_internal/font->raster->width,t=float(c/font->desc.tileWidth)*font->desc.height_internal/font->raster->height;
  float du=float(font->desc.width_internal)/font->raster->width,dv=float(font->desc.height_internal)/font->raster->height,w=font->desc.width_internal*scale,h=font->desc.height_internal*scale;unsigned at=v.size();
  for(int k=0;k<4;++k){V q;q.setScreenX(x+(k&1)*w);q.setScreenY(y+(k/2)*h);q.setScreenZ(z);q.setCameraZ(1/rz);q.setRecipCameraZ(rz);q.setColor(colour.red,colour.green,colour.blue,colour.alpha);q.setU(u+(k&1)*du,rz);q.setV(t+(k/2)*dv,rz);v.push_back(q);}
  for(unsigned k:{0u,1u,2u,2u,1u,3u})ix.push_back(at+k);x+=font->desc.width*scale;
 }rw::SetRenderState(rw::ZTESTENABLE,0);rw::SetRenderState(rw::ZWRITEENABLE,0);rw::SetRenderState(rw::CULLMODE,rw::CULLNONE);rw::SetRenderState(rw::VERTEXALPHA,1);rw::SetRenderStatePtr(rw::TEXTURERASTER,font->raster);rw::SetRenderState(rw::TEXTUREFILTER,rw::Texture::NEAREST);
 if(!v.empty())rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST,v.data(),v.size(),ix.data(),ix.size());
}
bool Capture(const char *path,int w,int h){
 std::vector<unsigned char> pixels(size_t(w)*h*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());FILE *out=std::fopen(path,"wb");if(!out)return false;std::fprintf(out,"P6\n%d %d\n255\n",w,h);for(int y=h-1;y>=0;--y)std::fwrite(pixels.data()+size_t(y)*w*3,1,size_t(w)*3,out);return std::fclose(out)==0;
}
}
