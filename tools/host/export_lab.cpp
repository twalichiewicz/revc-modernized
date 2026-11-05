// Local render/collision input; never distributes the user's original assets.
#include "../bake/visualmodel.h"
#include <cstdio>
#include <cmath>
int main(int argc,char **argv){
 if(argc!=3)return 2;ImgArchive archive;std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string why;
 if(!archive.Open(std::string(argv[1])+"/models/gta3.img")||!archive.Read("deaddodo.dff",bytes)||!visualmodel::Read(bytes,mesh,why))return 2;
 FILE *f=std::fopen(argv[2],"w");if(!f)return 2;std::fprintf(f,"%zu\n",mesh.faces.size());
 for(auto face:mesh.faces){auto a=mesh.vertices[face.a],b=mesh.vertices[face.b],c=mesh.vertices[face.c];
  bool wing=a.x>2&&b.x>2&&c.x>2&&std::fabs((a.y+b.y+c.y)/3)<2;
  std::fprintf(f,"%d\n",int(wing));for(auto p:{a,b,c})std::fprintf(f,"%.9g %.9g %.9g\n",p.x,p.y,p.z);
 }std::fclose(f);std::printf("aircraft render faces=%zu\n",mesh.faces.size());return 0;
}
