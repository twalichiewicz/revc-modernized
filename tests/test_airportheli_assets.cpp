#include "../tools/bake/visualmodel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
int main(){const char *env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive img;std::vector<uint8_t> bytes;visualmodel::Mesh mesh;std::string why;if(!img.Open(data+"/models/gta3.img")||!img.Read("helipad0.dff",bytes)||!visualmodel::Read(bytes,mesh,why))return 2;
 Vec3f lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};for(auto p:mesh.vertices){lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
 Vec3f markLo{1e9f,1e9f,1e9f},markHi{-1e9f,-1e9f,-1e9f};int marks=0;
 for(auto f:mesh.faces)if(mesh.materials[f.material].texture=="yellow"){++marks;for(auto k:{f.a,f.b,f.c}){auto p=mesh.vertices[k];markLo={std::min(markLo.x,p.x),std::min(markLo.y,p.y),std::min(markLo.z,p.z)};markHi={std::max(markHi.x,p.x),std::max(markHi.y,p.y),std::max(markHi.z,p.z)};}}
 const Vec3f marker{(markLo.x+markHi.x)*.5f,(markLo.y+markHi.y)*.5f,(markLo.z+markHi.z)*.5f};
 if(marks!=2||std::fabs(marker.x-10.02f)>.02f||std::fabs(marker.y+2.03f)>.02f||std::fabs(marker.z-1.29f)>.02f)return 1;
 std::ifstream placements(data+"/data/maps/airport/airport.ipl");int count=0;for(std::string line;std::getline(placements,line);){std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);int id,interior;std::string name;Vec3f at;if(in>>id>>name>>interior>>at.x>>at.y>>at.z)if(id==876&&Lower(name)=="helipad0"&&interior==0){++count;if(std::fabs(at.x+1359.9109f)>.05f||std::fabs(at.y+1251.8196f)>.05f)return 1;}}
 std::printf("airport Helipad0: %d placement, bounds %.2f %.2f %.2f .. %.2f %.2f %.2f\n",count,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);return count==1&&hi.x-lo.x>8&&hi.y-lo.y>8?0:1;}
