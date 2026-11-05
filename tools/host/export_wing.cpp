// Export a documented outboard Dodo wing region from the user's installed DFF.
// Generated vertex data stays in ignored build storage, never in this repo.
#include "../bake/visualmodel.h"
#include <cstdio>
#include <cmath>
int main(int argc,char **argv) {
 if(argc!=3)return 2;
 ImgArchive archive;std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string why;
 if(!archive.Open(std::string(argv[1])+"/models/gta3.img")||!archive.Read("deaddodo.dff",bytes)||!visualmodel::Read(bytes,mesh,why)){
  std::fprintf(stderr,"Dodo import failed: %s\n",why.c_str());return 2;
 }
 // Outboard starboard strip, away from the fuselage, tail and engine. This
 // is a geometrical test region, not an authored material/attachment model.
 std::vector<visualmodel::Face> wing;
 for(auto f:mesh.faces){auto a=mesh.vertices[f.a],b=mesh.vertices[f.b],c=mesh.vertices[f.c];
  if(a.x>2&&b.x>2&&c.x>2&&std::fabs((a.y+b.y+c.y)/3)<2)wing.push_back(f);
 }
 if(wing.size()<2||wing.size()>500)return 2;
 FILE *out=std::fopen(argv[2],"w");if(!out)return 2;
 std::fprintf(out,"%zu\n",wing.size());
 for(auto f:wing)for(auto i:{f.a,f.b,f.c}){const auto p=mesh.vertices[i];std::fprintf(out,"%.9g %.9g %.9g\n",p.x,p.y,p.z);}
 std::fclose(out);
 out=std::fopen((std::string(argv[2])+".dff").c_str(),"wb");if(!out)return 2;
 const bool saved=std::fwrite(bytes.data(),1,bytes.size(),out)==bytes.size();std::fclose(out);if(!saved)return 2;
 std::printf("deaddodo.dff model181 bytes=%zu whole_vertices=%zu whole_faces=%zu outboard_wing_faces=%zu\n",bytes.size(),mesh.vertices.size(),mesh.faces.size(),wing.size());return 0;
}
