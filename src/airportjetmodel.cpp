#include "airportjetmodel.h"
#include "jetlights.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <string>
#include <vector>
namespace airportjetmodel {
namespace {
struct Color {uint8_t r,g,b,a;};
struct Triangle {uint16_t v[3],material;};
struct Vertex {CVector p,n;CVector2D uv;Color color;};
struct Face {Vertex v[3];void *material;};
struct Mesh {std::vector<Face> faces;CVector pivot{};const char *name=nullptr;int wheel=-1,gear=-1,knee=-1;};
struct RW {
 void *(*each)(void *,void *(*)(void *,void *),void *),*(*geometry)(const void *),*(*atomicFrame)(const void *),*(*root)(const void *);
 int (*vertexCount)(const void *),(*triangleCount)(const void *);
 void *(*morph)(const void *,int);const CVector *(*vertices)(const void *),*(*normals)(const void *);
 const CVector2D *(*uv)(const void *,int);const Color *(*colors)(const void *);const Triangle *(*triangles)(const void *);
 void *(*material)(const void *,int),*(*texture)(const void *);const char *(*textureName)(void *);
 void *(*createGeometry)(int,int,uint32_t);bool (*destroyGeometry)(void *);void *(*lock)(void *,int),*(*unlock)(void *),*(*setTriangleMaterial)(void *,Triangle *,void *);
 uint32_t (*flags)(const void *);void (*setFlags)(void *,uint32_t);
 void *(*createMaterial)();bool (*destroyMaterial)(void *);void (*setColor)(void *,const Color *);const Color *(*color)(const void *);
 uint32_t (*atomicFlags)(const void *);void *(*setAtomicFlags)(void *,uint32_t);
 void *(*createAtomic)();bool (*destroyAtomic)(void *);void *(*setGeometry)(void *,void *,uint32_t),*(*setFrame)(void *,void *),*(*addAtomic)(void *,void *);
 void *(*createFrame)(),*(*addChild)(void *,void *),*(*children)(void *,void *(*)(void *,void *),void *),*(*update)(void *);bool (*destroyFrame)(void *);
 float *(*matrix)(void *);const float *(*ltm)(void *);char *(*name)(void *);void (*hierarchyId)(void *,intptr_t);
 void (*calcSphere)(const void *,void *),(*setSphere)(void *,const void *);
} rw{};
bool ready;
constexpr CVector kFlapPivot[2]={{-1.8f,-2.7308f,-1.04948f},{1.8f,-2.7308f,-1.04948f}};
constexpr CVector kFlapAxis[2]={{1,-.094f,-.05313f},{1,.094f,.05313f}};
constexpr CVector kElevatorPivot{0,-8.08f,1.765f},kRudderPivot{0,-7.65f,0},kDoorPivot{-.64f,3.7f,-1.02f};
constexpr CVector kGearHinge[3]={{0,6.10f,-.75f},{-1.225f,-.9803f,-.95f},{1.225f,-.9803f,-.95f}};
// Legacy frame names retain the fixed structural-COL sampling contract.
// These source-skin patches are visibility-only final seals.
constexpr const char *kBayNames[8]={"jet_baydoor_nl","jet_baydoor_nr","jet_baydoor_ml","jet_baydoor_mr","jet_baydoor_mli","jet_baydoor_mri","jet_baydoor_mli_b","jet_baydoor_mri_b"};
constexpr CVector kKneePivot[2]={{-1.1778f,-.9008f,-1.4775f},{1.1778f,-.9008f,-1.4775f}};
constexpr const char *kKneeNames[2]={"jet_knee_l","jet_knee_r"};
constexpr const char *kGearNames[3]={"jet_gear_n","jet_gear_l","jet_gear_r"};
constexpr const char *kStrutNames[3]={"jet_strut_n","jet_strut_l","jet_strut_r"};
constexpr const char *kTireNames[3]={"jet_tire_n","jet_tire_l","jet_tire_r"};
constexpr const char *kWheelNames[3]={"jet_wheel_n","jet_wheel_l","jet_wheel_r"};
// A measured source triangle cannot span both cabin sides; floor plus the
// four planes of one side need at most20 retained triangle slots.
constexpr int kClipFaces=20;
constexpr const char *kPackingNames[8]={"jet_tire_l","jet_tire_r","jet_strut_l","jet_strut_r","jet_link_l","jet_link_r","jet_fairing_l","jet_fairing_r"};
constexpr const char *kPackingBasis[8]={"jet_tire_l_basis","jet_tire_r_basis","jet_strut_l_basis","jet_strut_r_basis","jet_link_l_basis","jet_link_r_basis","jet_fairing_l_basis","jet_fairing_r_basis"};
CVector Add(CVector a,CVector b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
CVector Sub(CVector a,CVector b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
CVector Mul(CVector a,float k){return {a.x*k,a.y*k,a.z*k};}
float Dot(CVector a,CVector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
CVector Cross(CVector a,CVector b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float Length(CVector a){return std::sqrt(Dot(a,a));}
CVector Unit(CVector a){const float length=Length(a);return length>.00001f?Mul(a,1/length):CVector{0,0,1};}
bool Finite(CVector a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
template<class T>bool Bind(T &value,const char *symbol){value=reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY,symbol));return value!=nullptr;}
Vertex Mix(const Vertex &a,const Vertex &b,float t){Vertex v;v.p=Add(a.p,Mul(Sub(b.p,a.p),t));v.n=Unit(Add(a.n,Mul(Sub(b.n,a.n),t)));v.uv={a.uv.x+(b.uv.x-a.uv.x)*t,a.uv.y+(b.uv.y-a.uv.y)*t};
 const uint8_t *x=reinterpret_cast<const uint8_t *>(&a.color),*y=reinterpret_cast<const uint8_t *>(&b.color);auto *z=reinterpret_cast<uint8_t *>(&v.color);for(int i=0;i<4;++i)z[i]=uint8_t(std::clamp(std::lround(x[i]+(y[i]-x[i])*t),0l,255l));return v;}
void Polygon(Mesh &mesh,const std::vector<Vertex> &polygon,void *material){for(size_t i=1;i+1<polygon.size();++i){const Vertex a=polygon[0],b=polygon[i],c=polygon[i+1];if(Length(Cross(Sub(b.p,a.p),Sub(c.p,a.p)))>.0000001f)mesh.faces.push_back({{a,b,c},material});}}
using Plane=std::array<float,4>;
void Split(const std::vector<Vertex> &input,const Plane &plane,std::vector<Vertex> &inside,std::vector<Vertex> &outside){
 inside.clear();outside.clear();if(input.empty())return;
 auto distance=[&](const Vertex &v){return plane[0]*v.p.x+plane[1]*v.p.y+plane[2]*v.p.z+plane[3];};
 Vertex a=input.back();float da=distance(a);
 for(const Vertex &b:input){const float db=distance(b);if((da>=0)!=(db>=0)){const Vertex cut=Mix(a,b,da/(da-db));inside.push_back(cut);outside.push_back(cut);}if(db>=0)inside.push_back(b);else outside.push_back(b);a=b;da=db;}
}
std::vector<std::vector<Vertex>> Extract(const std::vector<Vertex> &input,const std::vector<Plane> &planes,Mesh &part,void *material){
 for(const auto &plane:planes){bool enters=false;for(const auto &v:input)enters=enters||(plane[0]*v.p.x+plane[1]*v.p.y+plane[2]*v.p.z+plane[3]>=0);if(!enters)return {input};}
 std::vector<std::vector<Vertex>> remainder;auto current=input;
 for(const auto &plane:planes){std::vector<Vertex> in,out;Split(current,plane,in,out);if(out.size()>=3)remainder.push_back(std::move(out));current=std::move(in);if(current.size()<3)break;}
 if(current.size()>=3)Polygon(part,current,material);return remainder;
}
std::vector<std::vector<Vertex>> Exterior(const std::vector<Vertex> &input,bool bottom,const std::vector<Face> &source,Mesh &buried,void *material,float clearance=-.0002f){
 std::vector<std::vector<Vertex>> pieces{input};
 for(const auto &face:source){const CVector a=face.v[0].p,e=Sub(face.v[1].p,a),f=Sub(face.v[2].p,a),n=Cross(e,f);if(std::fabs(n.z)<.00001f||(bottom?n.z>=0:n.z<=0))continue;
  const float sign=n.z>0?1.f:-1.f;std::vector<Plane> planes;
  for(int j=0;j<3;++j){const CVector p=face.v[j].p,q=face.v[(j+1)%3].p,d=Sub(q,p);planes.push_back({-sign*d.y,sign*d.x,0,sign*(d.y*p.x-d.x*p.y)});}
  const float direction=bottom?1.f:-1.f;planes.push_back({direction*n.x/n.z,direction*n.y/n.z,direction,-direction*Dot(n,a)/n.z+clearance});
  std::vector<std::vector<Vertex>> next;for(const auto &p:pieces){auto rest=Extract(p,planes,buried,material);next.insert(next.end(),rest.begin(),rest.end());}pieces=std::move(next);if(pieces.empty())break;
 }return pieces;
}
std::vector<Plane> WheelBay(int side){
 std::vector<Plane> cut={{float(side),0,0,0},{0,0,-1,-.98f}};const float cx=side*.37f,cy=-.785f;for(int i=0;i<32;++i){const float a=i*6.283185307f/32,x=std::cos(a)/.38f,y=std::sin(a)/.435f;cut.push_back({-x,-y,0,1+x*cx+y*cy});}return cut;
}
std::vector<std::vector<Vertex>> Flap(const std::vector<Vertex> &input,int side,Mesh &flap,void *material){
 const float sign=side?1.0f:-1.0f;return Extract(input,{{sign,0,0,-1.8f},{-sign,0,0,8.9f},{.094f*sign,-1,0,-2.90f},{-.094f*sign,1,0,3.67f},{0,0,1,1.5f},{0,0,-1,-.2f}},flap,material);
}
// These are the five measured window islands in the original shared atlas.
// Their rounded Y/Z footprint cuts the shell behind the overlay as well.
constexpr float kWindowY[5]={2.5504f,1.6413f,.7167f,-.1395f,-1.0239f};
bool SideGlass(const Face &face){
 for(const auto &v:face.v)if(std::fabs(v.p.x)<.44f||std::fabs(v.p.x)>.93f||v.p.y< -1.025f||v.p.y>2.974f||v.p.z<-.387f||v.p.z>.260f||v.uv.x<.75f||v.uv.y<-.25f||v.uv.y>0)return false;
 return true;
}
// Preserve shared source positions at hard edges. The inside shell reverses
// winding instead of moving duplicate seam vertices along different normals.
std::vector<CVector> Hull2(std::vector<CVector> points,int axis){
 auto u=[&](CVector p){return axis==1?p.y:p.x;};auto v=[&](CVector p){return axis==1?p.z:(axis==2?p.y:p.z);};
 std::sort(points.begin(),points.end(),[&](CVector a,CVector b){return u(a)<u(b)||(u(a)==u(b)&&v(a)<v(b));});
 points.erase(std::unique(points.begin(),points.end(),[&](CVector a,CVector b){return std::fabs(u(a)-u(b))<.000001f&&std::fabs(v(a)-v(b))<.000001f;}),points.end());if(points.size()<3)return {};
 auto turn=[&](CVector a,CVector b,CVector c){return double(u(b)-u(a))*(v(c)-v(a))-double(v(b)-v(a))*(u(c)-u(a));};std::vector<CVector> hull;
 for(CVector p:points){while(hull.size()>1&&turn(hull[hull.size()-2],hull.back(),p)<=.0000001)hull.pop_back();hull.push_back(p);}const size_t lower=hull.size();
 for(auto i=points.rbegin()+1;i!=points.rend();++i){while(hull.size()>lower&&turn(hull[hull.size()-2],hull.back(),*i)<=.0000001)hull.pop_back();hull.push_back(*i);}hull.pop_back();return hull;
}
using PointKey=std::array<int,3>;
PointKey Key(CVector p){return {int(std::lround(p.x*100000)),int(std::lround(p.y*100000)),int(std::lround(p.z*100000))};}
struct WindowIsland {std::vector<CVector> pane;std::vector<std::pair<CVector,CVector>> edges;std::vector<Plane> planes;Mesh backing;};
std::vector<WindowIsland> Windows(const std::vector<Face> &source){
 std::vector<WindowIsland> result;for(int side:{-1,1})for(float y:kWindowY){std::vector<CVector> points;
  struct Boundary {CVector a,b;int count=0;};std::map<std::pair<PointKey,PointKey>,Boundary> edges;
  for(const auto &face:source)if(SideGlass(face)){float middle=0;for(const auto &v:face.v)middle+=v.p.y/3;if(face.v[0].p.x*side>0&&middle>=y-.001f&&middle<=y+.43f){for(const auto &v:face.v)points.push_back(v.p);for(int i=0;i<3;++i){const CVector a=face.v[i].p,b=face.v[(i+1)%3].p;const PointKey ka=Key(a),kb=Key(b);auto key=std::minmax(ka,kb);auto &edge=edges[{key.first,key.second}];edge.a=a;edge.b=b;++edge.count;}}}
  // The YZ hull classifies the cut, but must not replace the true3D
  // boundary: collinear YZ pane vertices still carry a5cm X curvature.
  WindowIsland window;for(const auto &entry:edges)if(entry.second.count==1)window.edges.push_back({entry.second.a,entry.second.b});window.pane=Hull2(std::move(points),1);window.planes.push_back({float(side),0,0,-.2f});
  for(size_t i=0;i<window.pane.size();++i){const CVector a=window.pane[i],d=Sub(window.pane[(i+1)%window.pane.size()],a);window.planes.push_back({0,-d.z,d.y,d.z*a.y-d.y*a.z});}result.push_back(std::move(window));
 }
 return result;
}
bool BodyFace(size_t i){return i<=35||(i>=49&&i<=50)||(i>=348&&i<=349)||(i>=735&&i<=737)||(i>=38&&i<=44)||(i>=46&&i<=48)||(i>=299&&i<=334)||(i>=337&&i<=343)||(i>=345&&i<=347)||(i>=214&&i<=224)||(i>=563&&i<=576)||(i>=257&&i<=258)||(i>=693&&i<=694)||(i>=207&&i<=208)||(i>=550&&i<=554)||(i>=1112&&i<=1158);}
int Strut(const Face &face){
 for(int wheel=0;wheel<3;++wheel){bool selected=true;for(const auto &v:face.v){const CVector d=Sub(v.p,jetwheels::kGear[size_t(wheel)].center);selected=selected&&std::fabs(d.x)<.36f&&std::fabs(d.y)<.42f&&v.p.z< -1.02f&&v.uv.x>.26f&&v.uv.x<.50f&&v.uv.y<-.26f&&v.uv.y>-.38f;}if(selected)return wheel;}
 // Main-gear fairings belong to the assembly, including their three long
 // source triangles that the previous low-Z tire-only extraction missed.
 for(int side:{-1,1}){bool selected=true;for(const auto &v:face.v)selected=selected&&std::fabs(v.p.x-side*1.4203f)<.006f&&v.p.y> -1.28f&&v.p.y<-.68f&&v.p.z< -1.11f&&v.p.z> -2.02f;if(selected)return side<0?1:2;}
 return -1;
}
bool Glass(const Face &face){
 // The actual14 glass faces have their own UV island inside the shared
 // opaque ap_learjet atlas. Narrow surrounding pillar/frame strips use a
 // different island and remain in the opaque hull.
 for(const auto &v:face.v)if(!Finite(v.p)||v.p.y<4.3f||v.p.y>6.8f||v.p.z<-.3f||v.p.z>.5f||std::fabs(v.p.x)>.9f||v.uv.x<.76f||v.uv.x>.97f||v.uv.y<-.22f||v.uv.y>-.04f)return false;
 return true;
}
bool Wheels(const std::vector<Face> &faces,std::vector<int> &parts){
 std::map<PointKey,std::vector<int>> adjacent;std::vector<bool> low(faces.size()),visited(faces.size());parts.assign(faces.size(),-1);
 for(size_t i=0;i<faces.size();++i){low[i]=true;for(const auto &v:faces[i].v)low[i]=low[i]&&v.p.z< -1.5f;if(low[i])for(const auto &v:faces[i].v)adjacent[Key(v.p)].push_back(int(i));}
 int found[3]{};
 for(size_t i=0;i<faces.size();++i)if(low[i]&&!visited[i]){std::vector<int> component{int(i)};visited[i]=true;CVector lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
  for(size_t at=0;at<component.size();++at)for(const auto &v:faces[size_t(component[at])].v){lo={std::min(lo.x,v.p.x),std::min(lo.y,v.p.y),std::min(lo.z,v.p.z)};hi={std::max(hi.x,v.p.x),std::max(hi.y,v.p.y),std::max(hi.z,v.p.z)};for(int next:adjacent[Key(v.p)])if(!visited[size_t(next)]){visited[size_t(next)]=true;component.push_back(next);}}
  if(component.size()!=92)continue;const CVector centre=Mul(Add(lo,hi),.5f);
  for(int wheel=0;wheel<3;++wheel)if(Length(Sub(centre,jetwheels::kGear[size_t(wheel)].center))<.005f&&std::fabs((hi.z-lo.z)*.5f-jetwheels::kGear[size_t(wheel)].radius)<.005f){if(found[wheel]++)return false;for(int face:component)parts[size_t(face)]=wheel;}
 }
 return found[0]==1&&found[1]==1&&found[2]==1;
}
struct Source {void *atomic=nullptr;int count=0;};
void *First(void *atomic,void *context){auto &out=*static_cast<Source *>(context);if(!out.count)out.atomic=atomic;++out.count;return atomic;}
bool Read(void *atomic,std::vector<Face> &out){
 void *g=rw.geometry(atomic);if(!g||rw.vertexCount(g)!=1303||rw.triangleCount(g)!=1212)return false;
 const CVector *p=rw.vertices(rw.morph(g,0)),*normal=rw.normals(rw.morph(g,0));const CVector2D *uv=rw.uv(g,1);const Color *color=rw.colors(g);const Triangle *triangles=rw.triangles(g);if(!p||!uv||!triangles)return false;
 void *material0=rw.material(g,0);void *texture=material0?rw.texture(material0):nullptr;const char *name=texture?rw.textureName(texture):nullptr;if(!name||std::strcmp(name,"ap_learjet"))return false;
 out.reserve(1212);for(int i=0;i<1212;++i){Face face{};face.material=rw.material(g,triangles[i].material);if(!face.material)return false;
  for(int j=0;j<3;++j){const int k=triangles[i].v[j];if(k>=1303||!Finite(p[k])||!std::isfinite(uv[k].x)||!std::isfinite(uv[k].y))return false;face.v[j]={p[k],normal?normal[k]:CVector{},uv[k],color?color[k]:Color{255,255,255,255}};}
  const CVector flat=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));for(auto &v:face.v)if(Length(v.n)<.1f)v.n=flat;out.push_back(face);
 }return true;
}
// The static asset bakes black64 into the original gear shadows. Once these
// become closing nose doors, use their adjacent native belly UV mapping
// and retain the original prelight. Main shadows are overlays, removed below.
// Shared source materials and tire/turbine black64 are never edited.
bool RepaintShadow(Face &face,const Face &paint){
 void *texture=rw.texture(face.material);const char *name=texture?rw.textureName(texture):nullptr;if(!name||std::strcmp(name,"black64"))return false;
 const Vertex a=paint.v[0],b=paint.v[1],c=paint.v[2];const double ex=b.p.x-a.p.x,ey=b.p.y-a.p.y,fx=c.p.x-a.p.x,fy=c.p.y-a.p.y,det=ex*fy-ey*fx;
 face.material=paint.material;for(auto &v:face.v){const double x=std::fabs(v.p.x)-a.p.x,y=v.p.y-a.p.y,u=(x*fy-y*fx)/det,w=(ex*y-ey*x)/det;v.uv={float(a.uv.x+u*(b.uv.x-a.uv.x)+w*(c.uv.x-a.uv.x)),float(a.uv.y+u*(b.uv.y-a.uv.y)+w*(c.uv.y-a.uv.y))};}return true;
}
struct CockpitSource {void *atomic=nullptr;int count=0;};
void *CockpitAtomic(void *atomic,void *data){auto &source=*static_cast<CockpitSource *>(data);void *frame=rw.atomicFrame(atomic);const char *name=frame?rw.name(frame):nullptr;if(name&&!std::strcmp(name,"chassis_hi")){source.atomic=atomic;++source.count;}return atomic;}
bool Cockpit(void *clump,Mesh &cabin,Mesh &dashboard,Mesh &rear){
 if(!clump)return false;CockpitSource source;rw.each(clump,CockpitAtomic,&source);if(source.count!=1)return false;
 void *g=rw.geometry(source.atomic);if(!g||rw.vertexCount(g)!=2076||rw.triangleCount(g)!=2079)return false;
 const CVector *positions=rw.vertices(rw.morph(g,0)),*normals=rw.normals(rw.morph(g,0));const CVector2D *uv=rw.uv(g,1);const Color *colors=rw.colors(g);const Triangle *triangles=rw.triangles(g);const float *matrix=rw.ltm(rw.atomicFrame(source.atomic));if(!positions||!uv||!triangles||!matrix)return false;
 auto point=[&](CVector p){return CVector{matrix[0]*p.x+matrix[4]*p.y+matrix[8]*p.z+matrix[12],matrix[1]*p.x+matrix[5]*p.y+matrix[9]*p.z+matrix[13],matrix[2]*p.x+matrix[6]*p.y+matrix[10]*p.z+matrix[14]};};
 // The actual cabin atlas includes instrument panel, cyclic/pedals, floor and
 // two58-face front seats. Exterior paint, glazing, skids and rotors use other
 // material islands or lie outside this measured front-cabin volume.
 for(int i=0;i<2079;++i){const auto &triangle=triangles[i];void *material=rw.material(g,triangle.material),*texture=material?rw.texture(material):nullptr;const char *name=texture?rw.textureName(texture):nullptr;if(!name||std::strcmp(name,"polmav8bit128"))continue;
  if((i>=42&&i<=57)||(i>=579&&i<=595)||(i>=1180&&i<=1196))continue;
  Face face{};face.material=material;bool selected=true;
  for(int j=0;j<3;++j){const unsigned k=triangle.v[j];if(k>=2076)return false;const CVector p=point(positions[k]);if(!Finite(p)||std::fabs(p.x)>.84f||p.y<.7f||p.y>2.95f||p.z<-.55f||p.z>.6f){selected=false;break;}
   if(!std::isfinite(uv[k].x)||!std::isfinite(uv[k].y))return false;
   // Match the actual Polmav front-seat dummy to the measured Learjet pilot
   // root. Nonuniform scale fits its narrow shell; normals use inverse scale.
   CVector target{p.x*.76f,5.0f+(p.y-1.476f)*.68f,-.65f+(p.z+.09952587f)*.85f};
   const bool pedal=(i>=567&&i<=578)||(i>=1168&&i<=1179);if(pedal)target.z+=.15f;
   CVector normal=normals?normals[k]:CVector{};normal=Unit({(matrix[0]*normal.x+matrix[4]*normal.y+matrix[8]*normal.z)/.76f,(matrix[1]*normal.x+matrix[5]*normal.y+matrix[9]*normal.z)/.68f,(matrix[2]*normal.x+matrix[6]*normal.y+matrix[10]*normal.z)/.85f});
   face.v[j]={target,normal,uv[k],colors?colors[k]:Color{255,255,255,255}};
  }if(selected){cabin.faces.push_back(face);
   // Reuse each actual upholstered donor seat with its atlas and shape. The
   // narrower cabin leaves a half-metre aisle and a clear door vestibule.
   if(i>=1963)for(float row:{2.2f,.7f,-.8f}){Face seat=face;for(auto &v:seat.v){v.p.x+=v.p.x<0?-.12f:.12f;v.p.y+=row-4.983525f;v.p.z-=.024597f;}rear.faces.push_back(seat);}
  }
 }
 if(cabin.faces.size()!=320)return false;void *instrument=rw.material(g,1);if(!instrument)return false;
 auto plain=[&](CVector a,CVector b,CVector c,CVector d){const CVector n=Unit(Cross(Sub(b,a),Sub(c,a)));const Vertex v[4]={{a,n,{.2f,.2f},{255,255,255,255}},{b,n,{.2f,.2f},{255,255,255,255}},{c,n,{.2f,.2f},{255,255,255,255}},{d,n,{.2f,.2f},{255,255,255,255}}};Polygon(dashboard,{v[0],v[1],v[2],v[3]},instrument);};
 constexpr float x=.59f,front=5.48f,back=5.64f,top=-.09f,bottom=-.38f;
 plain({-x,front,bottom},{x,front,bottom},{x,back,top},{-x,back,top});
 plain({-x,back,top},{x,back,top},{x,5.83f,-.13f},{-x,5.83f,-.13f});
 plain({-x,front,bottom},{-x,back,top},{-x,5.83f,-.13f},{-x,5.70f,-.38f});
 plain({x,5.70f,-.38f},{x,5.83f,-.13f},{x,back,top},{x,front,bottom});
 plain({-x,5.83f,-.13f},{x,5.83f,-.13f},{x,5.70f,-.38f},{-x,5.70f,-.38f});
 auto base=[&](CVector raw){const CVector p=point(raw);return CVector{p.x*.76f,5+(p.y-1.476f)*.68f,-.65f+(p.z+.09952587f)*.85f};};
 auto panelY=[&](float z){return front+(z-bottom)/(top-bottom)*(back-front)-.003f;};
 // Four copies of the six real folded upper instrument/control faces, not a
 // stretched tall pedestal or invented instrument artwork.
 for(float centre:{-.43f,-.18f,.18f,.43f})for(int index:{45,48,588,589,1189,1190}){const Triangle &triangle=triangles[index];Face face{};face.material=rw.material(g,triangle.material);
  for(int j=0;j<3;++j){const unsigned k=triangle.v[j];if(k>=2076)return false;const CVector source=base(positions[k]);const float fraction=(source.z+.514744f)/.308188f,z=-.35f+.24f*fraction,oldPlane=5.732515f+.077171f*fraction;face.v[j]={{centre+source.x*(.1175f/.148288f),panelY(z)+(source.y-oldPlane)*.8f,z},{},uv[k],colors?colors[k]:Color{255,255,255,255}};}
  const CVector n=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));for(auto &v:face.v)v.n=n;dashboard.faces.push_back(face);
 }
 for(int index:{46,47}){const Triangle &triangle=triangles[index];Face face{};face.material=rw.material(g,triangle.material);
  for(int j=0;j<3;++j){const unsigned k=triangle.v[j];if(k>=2076)return false;const CVector source=base(positions[k]);const float z=-.34f+(source.z+.954682f)/(.954682f-.527374f)*.22f;face.v[j]={{source.x*(.054f/.081508f),panelY(z)-.004f,z},{},uv[k],colors?colors[k]:Color{255,255,255,255}};}
  std::swap(face.v[1],face.v[2]);const CVector n=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));for(auto &v:face.v)v.n=n;dashboard.faces.push_back(face);
 }
 return true;
}
Vertex V(CVector p,CVector normal={0,0,1}){return {p,normal,{},Color{255,255,255,255}};}
void Quad(Mesh &mesh,CVector a,CVector b,CVector c,CVector d,void *material){const CVector n=Unit(Cross(Sub(b,a),Sub(c,a)));Polygon(mesh,{V(a,n),V(b,n),V(c,n),V(d,n)},material);}
// A shared affine inset gives the fuselage a real3–5mm inner skin. Native
// vehicle rendering uses CULLNONE, so reversed coplanar faces fight the paint.
// Coincident source vertices use this same map, independent of hard normals.
struct ShellInset {
 // A single affine map preserves every clipped source T-junction. Computing
 // a separate centre from each Y section moved interpolated seam vertices
 // off their neighbours' edges and opened sub-pixel holes in the lining.
 // This measured centreline lies inside every occupied cross-section.
 CVector operator()(CVector p)const{return {p.x*.995f,p.y,p.z*.995f-p.y*.0005f};}
};
// Fit a horizontal/sloped floor or transverse closure to the real shell
// intersection, including the wide cabin sides that a rectangular floor misses.
void Section(const std::vector<Face> &shell,Mesh &mesh,const std::vector<Plane> &bounds,Plane plane,bool transverse,bool reverse,void *material,bool atlas=false){
 std::vector<CVector> points;for(const Face &face:shell){std::vector<Vertex> polygon{face.v[0],face.v[1],face.v[2]},in,out;for(const auto &bound:bounds){Split(polygon,bound,in,out);polygon=std::move(in);if(polygon.empty())break;}if(polygon.empty())continue;
  Vertex a=polygon.back();auto distance=[&](CVector p){return plane[0]*p.x+plane[1]*p.y+plane[2]*p.z+plane[3];};float da=distance(a.p);for(const auto &b:polygon){const float db=distance(b.p);if(std::fabs(da)<.000001f)points.push_back(a.p);if((da>=0)!=(db>=0))points.push_back(Mix(a,b,da/(da-db)).p);a=b;da=db;}
 }auto hull=Hull2(std::move(points),transverse?0:2);if(reverse)std::reverse(hull.begin(),hull.end());std::vector<Vertex> polygon;for(CVector p:hull){Vertex v=V(p);v.uv=atlas?CVector2D{.2f,.2f}:CVector2D{p.x,p.y};polygon.push_back(v);}if(polygon.size()<3)return;const CVector normal=Unit(Cross(Sub(polygon[1].p,polygon[0].p),Sub(polygon[2].p,polygon[0].p)));for(auto &v:polygon)v.n=normal;Polygon(mesh,polygon,material);
}
void Lining(const std::vector<Face> &faces,Mesh &lining,void *wall,const ShellInset &inward,bool cabin=true){
 std::vector<Face> shell=faces;for(auto &face:shell)for(auto &v:face.v){v.p=inward(v.p);v.n=Unit({v.n.x/.995f,v.n.y+.0005f*v.n.z/.995f,v.n.z/.995f});}
 Mesh hidden;for(const Face &face:shell){auto polygon=std::vector<Vertex>{face.v[0],face.v[1],face.v[2]};std::vector<Vertex> in,out;
  for(const Plane &plane:std::vector<Plane>{{0,1,0,2.5f},{0,-1,0,8.5f}}){Split(polygon,plane,in,out);polygon=std::move(in);}if(polygon.size()<3)continue;
  const auto visible=cabin?Extract(polygon,{{0,-1,0,6.f},{0,0,-1,-.93f}},hidden,wall):std::vector<std::vector<Vertex>>{polygon};
  for(auto panel:visible){std::reverse(panel.begin(),panel.end());for(auto &v:panel){v.n=Mul(v.n,-1);v.uv={v.p.y*.4f,v.p.z*.4f};v.color={255,255,255,255};}Polygon(lining,panel,wall);}
 }if(!cabin)return;
 Section(shell,lining,{{0,0,1,.93f}},{0,1,0,2.5f},true,true,wall);

}
// Preserve concave cabin cross-sections: one convex hull over several metres
// bridges the wing-root narrowing and protrudes through the original shell.
void CabinFloor(const std::vector<Face> &source,Mesh &mesh,void *material,const ShellInset &inward){
 struct Edge {CVector a,b;};std::vector<Edge> edges;std::vector<float> cuts{-2.5f,6.f};constexpr float z=-.93f;
 for(size_t i=0;i<source.size();++i)if(BodyFace(i)){std::vector<CVector> points;for(int j=0;j<3;++j){const CVector a=inward(source[i].v[j].p),b=inward(source[i].v[(j+1)%3].p);if((a.z<z)!=(b.z<z))points.push_back(Add(a,Mul(Sub(b,a),(z-a.z)/(b.z-a.z))));}if(points.size()!=2||std::fabs(points[1].y-points[0].y)<.000001f)continue;edges.push_back({points[0],points[1]});for(auto p:points)if(p.y>-2.5f&&p.y<6.f)cuts.push_back(p.y);}
 auto x=[](const Edge &e,float y){return e.a.x+(e.b.x-e.a.x)*(y-e.a.y)/(e.b.y-e.a.y);};
 // Include envelope switches between line segments, not just source vertices.
 for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j){const auto &a=edges[i],&b=edges[j];const float lo=std::max({-2.5f,std::min(a.a.y,a.b.y),std::min(b.a.y,b.b.y)}),hi=std::min({6.f,std::max(a.a.y,a.b.y),std::max(b.a.y,b.b.y)});if(hi-lo<.000001f)continue;const float d=x(a,lo)-x(b,lo),e=x(a,hi)-x(b,hi);if(d*e<0)cuts.push_back(lo+(hi-lo)*d/(d-e));}
 std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end(),[](float a,float b){return std::fabs(a-b)<.00001f;}),cuts.end());
 struct Width {float lo=1e9f,hi=-1e9f;bool valid()const{return lo<-.1f&&hi>.1f;}};std::vector<Width> widths(cuts.size());
 for(size_t i=0;i<cuts.size();++i)for(const auto &edge:edges)if(cuts[i]>=std::min(edge.a.y,edge.b.y)-.00001f&&cuts[i]<=std::max(edge.a.y,edge.b.y)+.00001f){const float value=x(edge,cuts[i]);widths[i].lo=std::min(widths[i].lo,value);widths[i].hi=std::max(widths[i].hi,value);}
 // The original lower fuselage opens into the wing centre section here.
 // Bridge only that internal gap; the original wing/belly exterior stays.
 for(size_t i=0;i<cuts.size();++i)if(!widths[i].valid()){size_t first=i,last=i;while(first>0&&!widths[first].valid())--first;while(last+1<cuts.size()&&!widths[last].valid())++last;if(!widths[first].valid()||!widths[last].valid())continue;const float t=(cuts[i]-cuts[first])/(cuts[last]-cuts[first]);widths[i]={widths[first].lo+(widths[last].lo-widths[first].lo)*t,widths[first].hi+(widths[last].hi-widths[first].hi)*t};}
 for(size_t i=1;i<cuts.size();++i)if(widths[i-1].valid()&&widths[i].valid()){const auto a=widths[i-1],b=widths[i];Quad(mesh,{a.lo,cuts[i-1],z},{a.hi,cuts[i-1],z},{b.hi,cuts[i],z},{b.lo,cuts[i],z},material);}

}
std::array<Plane,8> CabinPlanes(const std::vector<Face> &source,const ShellInset &inward){
 std::array<Plane,8> planes{};const int indices[8]={19,21,30,31,318,320,329,330};for(size_t i=0;i<8;++i){const auto &f=source[size_t(indices[i])];const auto a=inward(f.v[0].p),b=inward(f.v[1].p),c=inward(f.v[2].p);CVector n=Unit(Cross(Sub(b,a),Sub(c,a)));if(Dot(n,Sub({0,-.6f,-.8f},a))<0)n=Mul(n,-1);planes[i]={n.x,n.y,n.z,-Dot(n,a)};}return planes;
}
void LowerCabinClosure(const std::vector<Face> &source,Mesh &lining,Mesh &guides,void *wall,void *floor,const ShellInset &inward){
 const auto planes=CabinPlanes(source,inward);const int indices[8]={19,21,30,31,318,320,329,330};for(int index:indices){Face f=source[size_t(index)];for(auto &v:f.v)v.p=inward(v.p);guides.faces.push_back(f);}
 constexpr float back=-1.301994f,front=-.0271291f,bottom=-.93f,top=-.70f;
 // The stock fuselage opens into the wing centre. Complete the actual cabin
 // below its original side faces; these inward walls do not add outer skin.
 for(size_t i=0;i<planes.size();++i){const auto &p=planes[i];auto point=[&](float y,float z){return CVector{-(p[1]*y+p[2]*z+p[3])/p[0],y,z};};std::vector<Vertex> polygon{V(point(back,bottom)),V(point(front,bottom)),V(point(front,top)),V(point(back,top))};for(size_t j=0;j<planes.size();++j)if(i!=j){std::vector<Vertex> in,out;Split(polygon,planes[j],in,out);polygon=std::move(in);}if(polygon.size()<3)continue;const auto n=Unit(Cross(Sub(polygon[1].p,polygon[0].p),Sub(polygon[2].p,polygon[0].p)));if(Dot(n,{p[0],p[1],p[2]})<0)std::reverse(polygon.begin(),polygon.end());for(auto &v:polygon)v.n={p[0],p[1],p[2]};Polygon(lining,polygon,wall);}
 std::vector<Vertex> deck{V({-1,back,bottom}),V({1,back,bottom}),V({1,front,bottom}),V({-1,front,bottom})};for(const auto &p:planes){std::vector<Vertex> in,out;Split(deck,p,in,out);deck=std::move(in);}Polygon(lining,deck,floor);
}
void WindowReveals(const std::vector<WindowIsland> &windows,Mesh &trim,void *material,const ShellInset &inward){
 for(const auto &window:windows){CVector centre{};for(CVector p:window.pane)centre=Add(centre,Mul(p,1.f/window.pane.size()));centre.x=0;
  for(const auto &face:window.backing.faces)for(int i=0;i<3;++i){const CVector a=face.v[i].p,b=face.v[(i+1)%3].p;
   for(const auto &edge:window.edges){const CVector p=edge.first,d=Sub(edge.second,p);const float length=d.y*d.y+d.z*d.z;if(length<.000001f)continue;
    auto on=[&](CVector v){return std::fabs(d.y*(v.z-p.z)-d.z*(v.y-p.y))<.000002f;};if(!on(a)||!on(b))continue;
    const float ta=((a.y-p.y)*d.y+(a.z-p.z)*d.z)/length,tb=((b.y-p.y)*d.y+(b.z-p.z)*d.z)/length,span=tb-ta;if(std::fabs(span)<.000001f)continue;
    // A single clipped shell edge can span several collinear pane edges.
    // Stitch each overlap separately to preserve every actual X bend.
    const float first=std::max(0.f,std::min(-ta/span,(1-ta)/span)),last=std::min(1.f,std::max(-ta/span,(1-ta)/span));if(last-first<.000001f)continue;
    const CVector u=Add(a,Mul(Sub(b,a),first)),v=Add(a,Mul(Sub(b,a),last)),x=Add(p,Mul(d,std::clamp(ta+span*first,0.f,1.f))),y=Add(p,Mul(d,std::clamp(ta+span*last,0.f,1.f))),normal=Cross(Sub(v,u),Sub(y,u));
    (void)normal; // One continuous inner-to-pane reveal also spans the outer shell.
    // A second outer-to-pane strip overlaps this surface under CULLNONE.
    const CVector innerU=inward(u),innerV=inward(v),innerNormal=Cross(Sub(innerV,innerU),Sub(y,innerU));if(Dot(innerNormal,Sub(centre,innerU))>0)Quad(trim,innerU,innerV,y,x,material);else Quad(trim,x,y,innerV,innerU,material);
   }
  }
 }
 for(auto &face:trim.faces){const CVector n=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));for(auto &v:face.v)v.n=n;}
}
void FrontReveals(const std::vector<Face> &source,Mesh &trim,void *material,const ShellInset &inward){
 struct Edge {CVector a,b;int count=0;};std::map<std::pair<PointKey,PointKey>,Edge> edges;for(const auto &face:source)if(Glass(face))for(int i=0;i<3;++i){const CVector a=face.v[i].p,b=face.v[(i+1)%3].p;const PointKey ka=Key(a),kb=Key(b);auto key=std::minmax(ka,kb);auto &edge=edges[{key.first,key.second}];edge.a=a;edge.b=b;++edge.count;}
 for(const auto &entry:edges){const auto &e=entry.second;if(e.count!=1)continue;const CVector a=e.a,b=e.b,c=inward(b),d=inward(a),normal=Cross(Sub(b,a),Sub(c,a));if(Dot(normal,Sub({0,5,-.1f},a))>0)Quad(trim,a,b,c,d,material);else Quad(trim,d,c,b,a,material);}
}
void DoorReveals(const std::vector<Face> &faces,Mesh &moving,void *material,const ShellInset &inward){
 for(const auto &face:faces)for(int i=0;i<3;++i){const CVector a=face.v[i].p,b=face.v[(i+1)%3].p;bool rim=false;for(float y:{3.25f,4.15f})rim=rim||(std::fabs(a.y-y)<.0001f&&std::fabs(b.y-y)<.0001f);for(float z:{-1.02f,.29f})rim=rim||(std::fabs(a.z-z)<.0001f&&std::fabs(b.z-z)<.0001f);if(rim)Quad(moving,inward(a),inward(b),b,a,material);}
}
// Sculpt the inside skin itself into three treads. At full opening the main
// strips are horizontal; the bevels return continuously to the curved door.
// There is no second ladder, box back face or self-levelling carrier.
void DoorTreads(Mesh &inside,std::array<Mesh *,3> treads,void *material){
 const auto raw=std::move(inside.faces);constexpr float slope=.839099631f;
 const float starts[3]={-.85f,-.45f,.02f};float intercept[3]={-1e9f,-1e9f,-1e9f};
 for(int step=0;step<3;++step)for(const auto &face:raw){Mesh band;Extract({face.v[0],face.v[1],face.v[2]},{{0,0,1,-starts[step]},{0,0,-1,starts[step]+.16f},{0,1,0,-3.32f},{0,-1,0,4.08f}},band,material);for(const auto &f:band.faces)for(const auto &v:f.v)intercept[step]=std::max(intercept[step],v.p.x-slope*(v.p.z-starts[step])+.008f);}
 const float heights[3]={-1.28f,-1.60f,-1.93f};for(int step=0;step<3;++step){const float x=kDoorPivot.x+(heights[step]-kDoorPivot.z+.642787610f*(starts[step]-kDoorPivot.z))/.766044443f;intercept[step]=std::max(intercept[step],x);}
 for(const auto &face:raw){std::vector<std::vector<Vertex>> panels{{face.v[0],face.v[1],face.v[2]}};
  std::vector<Plane> cuts={{0,1,0,-3.32f},{0,1,0,-3.39f},{0,1,0,-4.01f},{0,1,0,-4.08f}};for(float start:starts)for(float z:{start-.05f,start,start+.16f,start+.21f})cuts.push_back({0,0,1,-z});
  for(const auto &cut:cuts){std::vector<std::vector<Vertex>> next;for(const auto &panel:panels){std::vector<Vertex> in,out;Split(panel,cut,in,out);if(in.size()>=3)next.push_back(std::move(in));if(out.size()>=3)next.push_back(std::move(out));}panels=std::move(next);}
  for(auto &panel:panels){float z=0,y=0;for(const auto &v:panel){z+=v.p.z/panel.size();y+=v.p.y/panel.size();}int tread=-1;for(int step=0;step<3;++step)if(z>=starts[step]&&z<=starts[step]+.16f&&y>=3.39f&&y<=4.01f)tread=step;
   for(auto &v:panel){const float across=std::clamp((v.p.y-3.32f)/.07f,0.f,1.f)*std::clamp((4.08f-v.p.y)/.07f,0.f,1.f);for(int step=0;step<3;++step){const float along=std::clamp((v.p.z-starts[step]+.05f)/.05f,0.f,1.f)*std::clamp((starts[step]+.21f-v.p.z)/.05f,0.f,1.f);if(along>0)v.p.x+=(intercept[step]+slope*(v.p.z-starts[step])-v.p.x)*along*across;}}
   const CVector n=Unit(Cross(Sub(panel[1].p,panel[0].p),Sub(panel[2].p,panel[0].p)));for(auto &v:panel)v.n=n;Polygon(tread>=0?*treads[size_t(tread)]:inside,panel,tread>=0?material:face.material);
  }
 }
}
// Shared source hard edges can leave sub-millimetre overlapping strips after
// shell clipping. Emit their union once, including within a single atomic.
void UniqueSurface(Mesh &mesh){
 std::vector<Face> unique;Mesh discarded;
 for(const Face &face:mesh.faces){std::vector<std::vector<Vertex>> panels{{face.v[0],face.v[1],face.v[2]}};
  const CVector normal=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));
  for(const Face &old:unique){const CVector n=Unit(Cross(Sub(old.v[1].p,old.v[0].p),Sub(old.v[2].p,old.v[0].p)));if(std::fabs(Dot(normal,n))<.99999f)continue;bool near=true;for(const auto &v:face.v)near=near&&std::fabs(Dot(n,Sub(v.p,old.v[0].p)))<.001f;if(!near)continue;
   std::vector<Plane> edges;for(int i=0;i<3;++i){const CVector p=old.v[i].p,edge=Unit(Cross(n,Sub(old.v[(i+1)%3].p,p)));edges.push_back({edge.x,edge.y,edge.z,-Dot(edge,p)});}std::vector<std::vector<Vertex>> next;
   for(const auto &panel:panels){auto rest=Extract(panel,edges,discarded,face.material);next.insert(next.end(),rest.begin(),rest.end());}panels=std::move(next);discarded.faces.clear();if(panels.empty())break;
  }
  Mesh result;for(const auto &panel:panels)Polygon(result,panel,face.material);unique.insert(unique.end(),result.faces.begin(),result.faces.end());
 }mesh.faces=std::move(unique);
}
void Lens(Mesh &mesh,Vec3 p,float radius,void *material){const CVector c{p.x,p.y,p.z};const CVector a[6]={Add(c,{radius,0,0}),Add(c,{-radius,0,0}),Add(c,{0,radius,0}),Add(c,{0,-radius,0}),Add(c,{0,0,radius}),Add(c,{0,0,-radius})};
 for(auto q:std::array<std::array<int,3>,8>{{{{0,2,4}},{{2,1,4}},{{1,3,4}},{{3,0,4}},{{2,0,5}},{{1,2,5}},{{3,1,5}},{{0,3,5}}}}){Face face{{V(a[q[0]]),V(a[q[1]]),V(a[q[2]])},material};const CVector normal=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));for(auto &v:face.v)v.n=normal;mesh.faces.push_back(face);}
}
struct Materials {std::vector<void *> owned;~Materials(){for(void *m:owned)rw.destroyMaterial(m);}void *Make(Color color){void *m=rw.createMaterial();if(m){rw.setColor(m,&color);owned.push_back(m);}return m;}};
void Matrix(void *frame,CVector pivot,CVector axis={1,0,0},float angle=0){float *m=rw.matrix(frame);if(!m)return;axis=Unit(axis);const float c=std::cos(angle),s=std::sin(angle),t=1-c,x=axis.x,y=axis.y,z=axis.z;
 const float values[16]={t*x*x+c,t*x*y+s*z,t*x*z-s*y,0,t*x*y-s*z,t*y*y+c,t*y*z+s*x,0,t*x*z+s*y,t*y*z-s*x,t*z*z+c,0,pivot.x,pivot.y,pivot.z,1};std::memcpy(m,values,sizeof values);const uint32_t flags=3;std::memcpy(m+3,&flags,4);rw.update(frame);
}
void Name(void *frame,const char *name,bool initialize=true){char *out=rw.name(frame);if(out){std::memset(out,0,24);std::strncpy(out,name,23);}if(initialize)rw.hierarchyId(frame,0);}
struct Built {void *geometry=nullptr,*atomic=nullptr,*frame=nullptr,*carrier=nullptr;};
void Cleanup(std::vector<Built> &parts){for(auto &part:parts){if(part.atomic)rw.destroyAtomic(part.atomic);if(part.geometry)rw.destroyGeometry(part.geometry);if(part.frame)rw.destroyFrame(part.frame);if(part.carrier)rw.destroyFrame(part.carrier);}parts.clear();}
void *Build(const Mesh &mesh){
 if(mesh.faces.empty()||mesh.faces.size()>20000)return nullptr;const size_t vertices=mesh.faces.size()*3;if(vertices>65535)return nullptr;
 void *g=rw.createGeometry(int(vertices),int(mesh.faces.size()),0x1007eu);if(!g)return nullptr;
 if(!rw.lock(g,0x7f)){rw.destroyGeometry(g);return nullptr;}
 void *morph=rw.morph(g,0);auto *p=const_cast<CVector *>(rw.vertices(morph)),*normal=const_cast<CVector *>(rw.normals(morph));auto *uv=const_cast<CVector2D *>(rw.uv(g,1));auto *colors=const_cast<Color *>(rw.colors(g));auto *triangles=const_cast<Triangle *>(rw.triangles(g));
 if(!p||!normal||!uv||!colors||!triangles){rw.destroyGeometry(g);return nullptr;}
 for(size_t i=0;i<mesh.faces.size();++i){for(int j=0;j<3;++j){const size_t k=i*3+size_t(j);const Vertex &v=mesh.faces[i].v[j];p[k]=Sub(v.p,mesh.pivot);normal[k]=Unit(v.n);uv[k]=v.uv;colors[k]=v.color;triangles[i].v[j]=uint16_t(k);}if(!rw.setTriangleMaterial(g,triangles+i,mesh.faces[i].material)){rw.destroyGeometry(g);return nullptr;}}
 alignas(16) float sphere[4]{};rw.calcSphere(morph,sphere);rw.setSphere(morph,sphere);if(!rw.unlock(g)){rw.destroyGeometry(g);return nullptr;}return g;
}
struct FindContext {const char *name;void *frame=nullptr;};
void *FindChild(void *frame,void *data){auto &find=*static_cast<FindContext *>(data);const char *name=rw.name(frame);if(name&&!std::strcmp(name,find.name))find.frame=frame;if(!find.frame)rw.children(frame,FindChild,data);return frame;}
void *Find(void *root,const char *name){FindContext find{name};rw.children(root,FindChild,&find);return find.frame;}
struct AtomicSearch {const char *name;void *atomic=nullptr;};
void *NamedAtomic(void *atomic,void *data){auto &search=*static_cast<AtomicSearch *>(data);void *frame=rw.atomicFrame(atomic);const char *name=frame?rw.name(frame):nullptr;if(name&&!std::strcmp(name,search.name))search.atomic=atomic;return atomic;}
void *FindAtomic(void *clump,const char *name){AtomicSearch search{name};rw.each(clump,NamedAtomic,&search);return search.atomic;}
// Native clump clones share geometry. Each actor gets its own fixed-capacity
// clipping buffers once; immutable hidden basis atomics own the source refs.
// The transaction leaves every existing geometry untouched on allocation fail.
bool PackingBuffers(void *clump){
 std::array<void *,8> allocated{},atomics{};
 for(size_t part=0;part<8;++part){void *atomic=FindAtomic(clump,kPackingNames[part]),*basis=FindAtomic(clump,kPackingBasis[part]);if(!atomic||!basis)continue;void *original=rw.geometry(basis),*current=rw.geometry(atomic);const int count=rw.triangleCount(original);if(count<=0||count>1000)continue;atomics[part]=atomic;if(rw.triangleCount(current)==count*kClipFaces&&rw.vertexCount(current)==count*kClipFaces*3)continue;
  const auto *triangles=rw.triangles(original);const auto *p=rw.vertices(rw.morph(original,0)),*n=rw.normals(rw.morph(original,0));const auto *uv=rw.uv(original,1);const auto *colors=rw.colors(original);Mesh mesh;
  for(int i=0;i<count;++i){Face face{};face.material=rw.material(original,triangles[i].material);for(int j=0;j<3;++j){const auto k=triangles[i].v[j];face.v[j]={p[k],n[k],uv[k],colors[k]};}mesh.faces.push_back(face);face.v[1]=face.v[2]=face.v[0];for(int pad=1;pad<kClipFaces;++pad)mesh.faces.push_back(face);}
  allocated[part]=Build(mesh);if(!allocated[part]){for(void *geometry:allocated)if(geometry)rw.destroyGeometry(geometry);return false;}
 }
 for(size_t part=0;part<8;++part)if(allocated[part]){rw.setGeometry(atomics[part],allocated[part],0);rw.destroyGeometry(allocated[part]);}return true;
}
void ClipPackedGear(void *clump,void *root){
 if(!PackingBuffers(clump))return;const float *body=rw.ltm(root);if(!body)return;const CVector origin{body[12],body[13],body[14]};
 for(size_t part=0;part<8;++part){void *atomic=FindAtomic(clump,kPackingNames[part]),*basis=FindAtomic(clump,kPackingBasis[part]);if(!atomic||!basis)continue;void *source=rw.geometry(basis),*target=rw.geometry(atomic);const int count=rw.triangleCount(source);if(rw.triangleCount(target)!=count*kClipFaces)continue;const float *matrix=rw.ltm(rw.atomicFrame(atomic));if(!matrix)continue;
  auto local=[&](Plane plane){const CVector x{body[0],body[1],body[2]},y{body[4],body[5],body[6]},z{body[8],body[9],body[10]},n=Add(Add(Mul(x,plane[0]),Mul(y,plane[1])),Mul(z,plane[2]));return Plane{Dot(n,{matrix[0],matrix[1],matrix[2]}),Dot(n,{matrix[4],matrix[5],matrix[6]}),Dot(n,{matrix[8],matrix[9],matrix[10]}),plane[3]+Dot(n,Sub({matrix[12],matrix[13],matrix[14]},origin))};};
  // Clip only the enclosed cabin above its actual floor. The original side
  // planes preserve every outboard wheel/cover strip during visible travel.
  std::array<Plane,9> cavity{};cavity[0]=local({0,0,1,.93f});void *guideAtomic=FindAtomic(clump,"jet_cabin_basis");if(!guideAtomic)continue;void *guide=rw.geometry(guideAtomic);const auto *gp=rw.vertices(rw.morph(guide,0));const auto *gt=rw.triangles(guide);for(size_t i=0;i<8;++i){const auto a=gp[gt[i].v[0]],b=gp[gt[i].v[1]],c=gp[gt[i].v[2]];CVector n=Unit(Cross(Sub(b,a),Sub(c,a)));if(Dot(n,Sub({0,-.6f,-.8f},a))<0)n=Mul(n,-1);cavity[i+1]=local({n.x,n.y,n.z,-Dot(n,a)});}
  const auto *triangles=rw.triangles(source);const auto *p=rw.vertices(rw.morph(source,0)),*n=rw.normals(rw.morph(source,0));const auto *uv=rw.uv(source,1);const auto *colors=rw.colors(source);if(!rw.lock(target,0x1e))continue;
  auto *outP=const_cast<CVector *>(rw.vertices(rw.morph(target,0))),*outN=const_cast<CVector *>(rw.normals(rw.morph(target,0)));auto *outUV=const_cast<CVector2D *>(rw.uv(target,1));auto *outColor=const_cast<Color *>(rw.colors(target));
  for(int i=0;i<count;++i){std::array<Vertex,16> current{};for(int j=0;j<3;++j){const auto k=triangles[i].v[j];current[size_t(j)]={p[k],n[k],uv[k],colors[k]};}const Vertex pad=current[0];size_t size=3,slot=0;
   auto write=[&](const Vertex &a,const Vertex &b,const Vertex &c){if(slot>=size_t(kClipFaces))return;const Vertex v[3]={a,b,c};for(int j=0;j<3;++j){const size_t k=(size_t(i)*kClipFaces+slot)*3+size_t(j);outP[k]=v[j].p;outN[k]=v[j].n;outUV[k]=v[j].uv;outColor[k]=v[j].color;}++slot;};
   for(const auto &plane:cavity){if(!size)break;std::array<Vertex,16> inside{},outside{};size_t ni=0,no=0;auto distance=[&](const Vertex &v){return plane[0]*v.p.x+plane[1]*v.p.y+plane[2]*v.p.z+plane[3];};Vertex a=current[size-1];float da=distance(a);
    for(size_t vertex=0;vertex<size;++vertex){const auto &b=current[vertex];const float db=distance(b);if((da>=0)!=(db>=0)){const auto cut=Mix(a,b,da/(da-db));inside[ni++]=cut;outside[no++]=cut;}if(db>=0)inside[ni++]=b;else outside[no++]=b;a=b;da=db;}
    for(size_t triangle=1;triangle+1<no;++triangle)if(Length(Cross(Sub(outside[triangle].p,outside[0].p),Sub(outside[triangle+1].p,outside[0].p)))>.00000001f)write(outside[0],outside[triangle],outside[triangle+1]);current=inside;size=ni;
   }
   while(slot<size_t(kClipFaces))write(pad,pad,pad);
  }rw.unlock(target);
 }
}
bool Complete(void *root){if(!Find(root,"jet_cabin_basis"))return false;for(const char *name:kPackingBasis)if(!Find(root,name))return false;for(const char *name:{"jet_baydoor_nl","jet_baydoor_nr","jet_baydoor_ml","jet_baydoor_mr","jet_baydoor_mli","jet_baydoor_mri","jet_baydoor_mli_b","jet_baydoor_mri_b","jet_fairing_l","jet_fairing_r","jet_link_l","jet_link_r","jet_knee_l","jet_knee_r","jet_gear_n","jet_gear_l","jet_gear_r","jet_wheel_n","jet_wheel_l","jet_wheel_r","jet_glass","jet_tire_n","jet_tire_l","jet_tire_r","jet_flap_l","jet_flap_r","jet_cabin","jet_dashboard","jet_window_trim","jet_lenses","jet_elevator_l","jet_elevator_r","jet_rudder","jet_strut_n","jet_strut_l","jet_strut_r","jet_door","jet_lining","jet_steps","jet_step_1","jet_step_2","jet_step_3","jet_cabin_rear"})if(!Find(root,name))return false;return true;}
}
bool Install(){
#define B(field,symbol) Bind(rw.field,symbol)
 ready=B(each,"_Z20RpClumpForAllAtomicsPN2rw5ClumpEPFPNS_6AtomicES3_PvES4_")&&B(geometry,"_Z19RpAtomicGetGeometryPKN2rw6AtomicE")&&B(atomicFrame,"_Z16RpAtomicGetFramePKN2rw6AtomicE")&&B(root,"_Z15RpClumpGetFramePKN2rw5ClumpE")&&
 B(vertexCount,"_Z24RpGeometryGetNumVerticesPKN2rw8GeometryE")&&B(triangleCount,"_Z25RpGeometryGetNumTrianglesPKN2rw8GeometryE")&&B(morph,"_Z24RpGeometryGetMorphTargetPKN2rw8GeometryEi")&&B(vertices,"_Z24RpMorphTargetGetVerticesPKN2rw11MorphTargetE")&&B(normals,"_Z29RpMorphTargetGetVertexNormalsPKN2rw11MorphTargetE")&&B(uv,"_Z28RpGeometryGetVertexTexCoordsPKN2rw8GeometryE24RwTextureCoordinateIndex")&&B(colors,"_Z27RpGeometryGetPreLightColorsPKN2rw8GeometryE")&&B(triangles,"_Z22RpGeometryGetTrianglesPKN2rw8GeometryE")&&B(material,"_Z21RpGeometryGetMaterialPKN2rw8GeometryEi")&&B(texture,"_Z20RpMaterialGetTexturePKN2rw8MaterialE")&&B(textureName,"_Z16RwTextureGetNamePN2rw7TextureE")&&
 B(createGeometry,"_Z16RpGeometryCreateiij")&&B(destroyGeometry,"_Z17RpGeometryDestroyPN2rw8GeometryE")&&B(lock,"_Z14RpGeometryLockPN2rw8GeometryEi")&&B(unlock,"_Z16RpGeometryUnlockPN2rw8GeometryE")&&B(setTriangleMaterial,"_Z29RpGeometryTriangleSetMaterialPN2rw8GeometryEPNS_8TriangleEPNS_8MaterialE")&&B(flags,"_Z18RpGeometryGetFlagsPKN2rw8GeometryE")&&B(setFlags,"_Z18RpGeometrySetFlagsPN2rw8GeometryEj")&&
 B(createMaterial,"_Z16RpMaterialCreatev")&&B(destroyMaterial,"_Z17RpMaterialDestroyPN2rw8MaterialE")&&B(setColor,"_Z18RpMaterialSetColorPN2rw8MaterialEPKNS_4RGBAE")&&B(color,"_Z18RpMaterialGetColorPKN2rw8MaterialE")&&B(atomicFlags,"_Z16RpAtomicGetFlagsPKN2rw6AtomicE")&&B(setAtomicFlags,"_Z16RpAtomicSetFlagsPN2rw6AtomicEj")&&B(createAtomic,"_Z14RpAtomicCreatev")&&B(destroyAtomic,"_Z15RpAtomicDestroyPN2rw6AtomicE")&&B(setGeometry,"_Z19RpAtomicSetGeometryPN2rw6AtomicEPNS_8GeometryEj")&&B(setFrame,"_Z16RpAtomicSetFramePN2rw6AtomicEPNS_5FrameE")&&B(addAtomic,"_Z16RpClumpAddAtomicPN2rw5ClumpEPNS_6AtomicE")&&
 B(createFrame,"_Z13RwFrameCreatev")&&B(destroyFrame,"_Z14RwFrameDestroyPN2rw5FrameE")&&B(addChild,"_Z15RwFrameAddChildPN2rw5FrameES1_")&&B(children,"_Z21RwFrameForAllChildrenPN2rw5FrameEPFS1_S1_PvES2_")&&B(update,"_Z20RwFrameUpdateObjectsPN2rw5FrameE")&&B(matrix,"_Z16RwFrameGetMatrixPN2rw5FrameE")&&B(ltm,"_Z13RwFrameGetLTMPN2rw5FrameE")&&B(name,"_Z16GetFrameNodeNamePN2rw5FrameE")&&B(hierarchyId,"_ZN18CVisibilityPlugins19SetFrameHierarchyIdEPN2rw5FrameEl")&&B(calcSphere,"_Z31RpMorphTargetCalcBoundingSpherePKN2rw11MorphTargetEPNS_6SphereE")&&B(setSphere,"_Z30RpMorphTargetSetBoundingSpherePN2rw11MorphTargetEPKNS_6SphereE");
#undef B
 return ready;
}
bool Prepare(void *clump,void *policeMaverickClump){
 if(!ready||!clump)return false;void *root=rw.root(clump);if(!root)return false;if(Complete(root))return true;
 Source input;rw.each(clump,First,&input);if(input.count!=1||!input.atomic)return false;void *source=input.atomic;std::vector<Face> original;if(!Read(source,original))return false;
 std::vector<int> wheels;if(!Wheels(original,wheels))return false;
 Materials materials;void *glass=materials.Make({170,195,210,28});if(!glass)return false;
 void *wall=materials.Make({74,82,88,255}),*floor=materials.Make({54,59,64,255}),*step=materials.Make({102,109,115,255});if(!wall||!floor||!step)return false;
 std::vector<Mesh> meshes(38);meshes[37].name="jet_cabin_rear";meshes[0].name="jet_hull";meshes[1].name="jet_glass";
 for(int i=0;i<3;++i){meshes[size_t(i+2)].name=kTireNames[i];meshes[size_t(i+2)].pivot=jetwheels::kGear[size_t(i)].center;meshes[size_t(i+2)].wheel=i;meshes[size_t(i+2)].gear=i;meshes[size_t(i+2)].knee=i?i-1:-1;
  meshes[size_t(i+12)].name=kStrutNames[i];meshes[size_t(i+12)].gear=i;}
 for(int i=0;i<2;++i){meshes[size_t(i+5)].name=i?"jet_flap_r":"jet_flap_l";meshes[size_t(i+5)].pivot=kFlapPivot[i];meshes[size_t(i+9)].name=i?"jet_elevator_r":"jet_elevator_l";meshes[size_t(i+9)].pivot=kElevatorPivot;}
 meshes[7].name="jet_cabin";meshes[8].name="jet_lenses";meshes[11].name="jet_rudder";meshes[11].pivot=kRudderPivot;
 meshes[15].name="jet_door";meshes[15].pivot=kDoorPivot;meshes[16].name="jet_lining";meshes[17].name="jet_steps";meshes[17].pivot=kDoorPivot;
 const char *stepNames[3]={"jet_step_1","jet_step_2","jet_step_3"};for(int i=0;i<3;++i){auto &tread=meshes[size_t(18+i)];tread.name=stepNames[i];tread.pivot=kDoorPivot;}
 for(int i=0;i<8;++i)meshes[size_t(i<6?21+i:30+i-6)].name=kBayNames[i];meshes[28].name="jet_fairing_l";meshes[28].gear=1;meshes[28].knee=0;meshes[29].name="jet_fairing_r";meshes[29].gear=2;meshes[29].knee=1;meshes[32].name="jet_link_l";meshes[32].gear=1;meshes[32].knee=0;meshes[33].name="jet_link_r";meshes[33].gear=2;meshes[33].knee=1;meshes[35].name="jet_dashboard";meshes[36].name="jet_window_trim";
 std::vector<Face> innerShell,airframe;auto windows=Windows(original);for(const auto &window:windows)if(window.pane.size()<3)return false;Mesh buried;for(size_t i=0;i<original.size();++i)if(wheels[i]<0&&Strut(original[i])<0&&!(i>=1104&&i<=1111))airframe.push_back(original[i]);

 const std::vector<Plane> door={{-1,0,0,-.2f},{0,1,0,-3.25f},{0,-1,0,4.15f},{0,0,1,1.02f},{0,0,-1,.29f}};
 for(size_t i=0;i<original.size();++i){if(i>=1104&&i<=1111)continue;Face face=original[i];RepaintShadow(face,original[16]);if(wheels[i]>=0){meshes[size_t(wheels[i]+2)].faces.push_back(face);continue;}const int strut=Strut(face);if(strut>=0){bool fairing=strut>0;for(const auto &v:face.v)fairing=fairing&&std::fabs(std::fabs(v.p.x)-1.4203f)<.006f;const bool upper=strut==0||(strut==1?i<=818:i<=845);if(fairing)meshes[size_t(strut+27)].faces.push_back(face);else if(strut>0&&upper)meshes[size_t(strut+12)].faces.push_back(face);else meshes[size_t(!upper?strut+31:strut+12)].faces.push_back(face);continue;}
  if(Glass(face)||SideGlass(face)){Face g=face;g.material=glass;meshes[1].faces.push_back(g);continue;}
  std::vector<std::vector<Vertex>> polygons{{face.v[0],face.v[1],face.v[2]}};
  const CVector outward=Unit(Cross(Sub(face.v[1].p,face.v[0].p),Sub(face.v[2].p,face.v[0].p)));
  if(i>=557&&i<=559&&outward.z>.02f){std::vector<std::vector<Vertex>> next;for(const auto &p:polygons){auto exterior=Exterior(p,false,airframe,buried,face.material);next.insert(next.end(),exterior.begin(),exterior.end());}polygons=std::move(next);}
  if(outward.z<-.02f||i==209||i==210||i==555||i==556)for(int bay=0;bay<8;++bay){
   const bool nose=bay<2,left=(bay%2)==0;std::vector<Plane> cut;
   if(nose)cut={{1,0,0,.35f},{-1,0,0,.35f},{0,1,0,-5.9f},{0,-1,0,7.62f},{0,0,-1,-1.0f},{left?-1.f:1.f,0,0,0}};
   else if(bay<4)cut={{left?1.f:-1.f,0,0,1.25f},{left?-1.f:1.f,0,0,-.65f},{0,1,0,1.08f},{0,-1,0,-.30f},{0,0,-1,-.98f}};
   else{cut=WheelBay(left?-1:1);cut.push_back({0,bay<6?1.f:-1.f,0,bay<6?.785f:-.785f});}
   std::vector<std::vector<Vertex>> next;for(const auto &polygon:polygons){auto rest=Extract(polygon,cut,meshes[size_t(bay<6?21+bay:30+bay-6)],face.material);next.insert(next.end(),rest.begin(),rest.end());}polygons=std::move(next);
  }

  if(BodyFace(i)){
   for(auto &window:windows){std::vector<std::vector<Vertex>> next;for(const auto &polygon:polygons){auto rest=Extract(polygon,window.planes,window.backing,face.material);next.insert(next.end(),rest.begin(),rest.end());}polygons=std::move(next);}
   std::vector<std::vector<Vertex>> next;for(const auto &polygon:polygons){auto rest=Extract(polygon,door,meshes[15],face.material);next.insert(next.end(),rest.begin(),rest.end());}polygons=std::move(next);
   Mesh shell;for(const auto &polygon:polygons)Polygon(shell,polygon,face.material);innerShell.insert(innerShell.end(),shell.faces.begin(),shell.faces.end());
  }
  for(const auto &polygon:polygons){
   // Tailplane trailing portions use the source skin and a swept span hinge.
   bool horizontal=true,vertical=true;for(const auto &v:polygon){horizontal=horizontal&&v.p.y< -7.4f&&v.p.z>1.65f&&v.p.z<1.9f;vertical=vertical&&v.p.y< -4.5f&&std::fabs(v.p.x)<.14f&&v.p.z>.30f;}
   if(horizontal){auto rest=Extract(polygon,{{-1,0,0,0},{.2f,-1,0,-8.08f}},meshes[9],face.material);for(const auto &left:rest)for(const auto &right:Extract(left,{{1,0,0,0},{-.2f,-1,0,-8.08f}},meshes[10],face.material))Polygon(meshes[0],right,face.material);}
   else if(vertical){for(const auto &rest:Extract(polygon,{{0,-1,-.30f,-7.65f},{0,0,1,-.45f}},meshes[11],face.material))Polygon(meshes[0],rest,face.material);}
   else for(const auto &right:Flap(polygon,1,meshes[6],face.material))for(const auto &left:Flap(right,0,meshes[5],face.material))Polygon(meshes[0],left,face.material);
  }
 }
 if(meshes[1].faces.size()!=94||meshes[5].faces.empty()||meshes[6].faces.empty()||meshes[9].faces.empty()||meshes[10].faces.empty()||meshes[11].faces.empty()||meshes[15].faces.empty())return false;
 for(int i=0;i<3;++i)if(i?meshes[size_t(12+i)].faces.empty():meshes[12].faces.size()!=27u)return false;
 for(int i:std::array<int,8>{21,22,23,24,25,26,30,31}){auto raw=std::move(meshes[size_t(i)].faces);for(const auto &f:raw)for(const auto &p:Exterior({f.v[0],f.v[1],f.v[2]},true,airframe,buried,f.material))Polygon(meshes[size_t(i)],p,f.material);}
 if(!Cockpit(policeMaverickClump,meshes[7],meshes[35],meshes[37]))return false;
 const ShellInset inward;Lining(innerShell,meshes[16],wall,inward);CabinFloor(original,meshes[16],floor,inward);Mesh guides;guides.name="jet_cabin_basis";LowerCabinClosure(original,meshes[16],guides,wall,floor,inward);WindowReveals(windows,meshes[36],step,inward);FrontReveals(original,meshes[36],step,inward);
 void *instrument=meshes[35].faces.front().material;
 Section(innerShell,meshes[35],{{0,1,0,-5.48f},{0,-1,0,5.70f}},{0,0,1,.38f},false,false,instrument,true);
 Section(innerShell,meshes[35],{{0,0,1,.82f},{0,0,-1,-.38f}},{0,1,0,-5.70f},true,false,instrument,true);

 // Closed door's independent inward skin follows the real clipped exterior;
 // sculpted inner-face treads rotate rigidly with this same lower hinge.
 Lining(meshes[15].faces,meshes[17],wall,inward,false);DoorTreads(meshes[17],{{&meshes[18],&meshes[19],&meshes[20]}},step);DoorReveals(meshes[15].faces,meshes[17],step,inward);
 for(size_t i=0;i<jetlights::kLenses.size();++i){const auto &lens=jetlights::kLenses[i];void *material=materials.Make({lens.red,lens.green,lens.blue,255});if(!material)return false;
  if(i!=3){Lens(meshes[8],lens.position,lens.radius,material);continue;}
  // The top emitter is correctly anchored, but its full lower octahedron
  // protruded4cm into the ceiling. Trim only the buried cap to the actual
  // source roof plus1mm; emitter, radius and all other lenses stay exact.
  Mesh cap,hidden;Lens(cap,lens.position,lens.radius,material);for(const auto &face:cap.faces)for(const auto &p:Exterior({face.v[0],face.v[1],face.v[2]},false,airframe,hidden,material,.001f))Polygon(meshes[8],p,material);
 }
 for(size_t part:{size_t(16),size_t(17),size_t(18),size_t(19),size_t(20),size_t(36)})UniqueSurface(meshes[part]);
 for(size_t part=0;part<8;++part){const int indices[8]={3,4,13,14,32,33,28,29};Mesh basis=meshes[size_t(indices[part])];basis.name=kPackingBasis[part];basis.wheel=basis.gear=basis.knee=-1;meshes.push_back(std::move(basis));}
 meshes.push_back(std::move(guides));
 std::vector<Built> built(meshes.size()+5);
 for(int i=0;i<3;++i){auto &part=built[meshes.size()+size_t(i)];part.frame=rw.createFrame();if(!part.frame){Cleanup(built);return false;}Name(part.frame,kGearNames[i]);Matrix(part.frame,{});}
 for(int i=0;i<2;++i){auto &part=built[meshes.size()+3+size_t(i)];part.frame=rw.createFrame();if(!part.frame){Cleanup(built);return false;}Name(part.frame,kKneeNames[i]);Matrix(part.frame,{});}
 for(size_t i=0;i<meshes.size();++i){if(meshes[i].faces.empty())continue;auto &part=built[i];part.geometry=Build(meshes[i]);if(!part.geometry){Cleanup(built);return false;}if(i==0)continue;
  part.atomic=rw.createAtomic();part.frame=rw.createFrame();if(!part.atomic||!part.frame){Cleanup(built);return false;}Name(part.frame,meshes[i].name);
  if(meshes[i].wheel>=0){part.carrier=rw.createFrame();if(!part.carrier){Cleanup(built);return false;}Name(part.carrier,kWheelNames[meshes[i].wheel]);Matrix(part.carrier,meshes[i].pivot);Matrix(part.frame,{});rw.addChild(part.carrier,part.frame);}else Matrix(part.frame,meshes[i].pivot);
  rw.setGeometry(part.atomic,part.geometry,0);rw.setFrame(part.atomic,part.frame);if(!std::strncmp(meshes[i].name,"jet_baydoor_",12)||std::strstr(meshes[i].name,"_basis"))rw.setAtomicFlags(part.atomic,rw.atomicFlags(part.atomic)&~4u);
 }
 // Commit only after every geometry/material/atomic/frame allocation succeeds.
 rw.setGeometry(source,built[0].geometry,0);Name(rw.atomicFrame(source),"jet_hull",rw.atomicFrame(source)!=root);
 for(int i=0;i<3;++i)rw.addChild(root,built[meshes.size()+size_t(i)].frame);
 for(int i=0;i<2;++i)rw.addChild(built[meshes.size()+size_t(i+1)].frame,built[meshes.size()+3+size_t(i)].frame);
 for(size_t i=1;i<meshes.size();++i){auto &part=built[i];if(!part.atomic)continue;void *parent=meshes[i].knee>=0?built[meshes.size()+3+size_t(meshes[i].knee)].frame:(meshes[i].gear>=0?built[meshes.size()+size_t(meshes[i].gear)].frame:root);rw.addChild(parent,part.carrier?part.carrier:part.frame);rw.addAtomic(clump,part.atomic);}
 for(auto &part:built){if(part.geometry)rw.destroyGeometry(part.geometry);part={};}rw.update(root);return true;
}
void Animate(void *clump,const Pose &pose){
 if(!ready||!clump)return;void *root=rw.root(clump);if(!root)return;
 auto safe=[](float a){return std::isfinite(a)?a:0.0f;};
 const float down=std::isfinite(pose.gearDown)?std::clamp(pose.gearDown,0.0f,1.0f):1.0f,extension=std::clamp((down-.18f)/.82f,0.0f,1.0f),retract=1-extension;
 // Original belly patches seal the openings when extension reaches zero.
 // They never become additional deployed doors, rims or mechanisms.
 for(const char *name:kBayNames)if(void *frame=Find(root,name))Matrix(frame,{});
 struct Visibility {float down;};Visibility visibility{down};
 rw.each(clump,[](void *atomic,void *data)->void *{const auto &v=*static_cast<Visibility *>(data);void *frame=rw.atomicFrame(atomic);const char *name=frame?rw.name(frame):nullptr;if(!name)return atomic;bool owned=false,visible=true;
  if(std::strstr(name,"_basis")){owned=true;visible=false;}
  else if(!std::strncmp(name,"jet_baydoor_",12)){owned=true;visible=v.down<=.18f;}
  else if(!std::strncmp(name,"jet_tire_",9)||!std::strncmp(name,"jet_strut_",10)||!std::strncmp(name,"jet_link_",9)||!std::strncmp(name,"jet_fairing_",12)){owned=true;const bool nose=!std::strcmp(name,"jet_tire_n")||!std::strcmp(name,"jet_strut_n");visible=!nose||v.down>=.04f;}
  if(owned){const uint32_t flags=rw.atomicFlags(atomic);rw.setAtomicFlags(atomic,visible?flags|4u:flags&~4u);}return atomic;
 },&visibility);

 for(int side=0;side<2;++side)if(void *knee=Find(root,kKneeNames[side])){const CVector p=kKneePivot[side];Matrix(knee,{}, {0,side?1.f:-1.f,0},retract*.523598776f);float *m=rw.matrix(knee);m[12]=p.x-(m[0]*p.x+m[4]*p.y+m[8]*p.z);m[13]=p.y-(m[1]*p.x+m[5]*p.y+m[9]*p.z);m[14]=p.z-(m[2]*p.x+m[6]*p.y+m[10]*p.z);rw.update(knee);}
 for(int side=0;side<2;++side)if(void *fairing=Find(root,side?"jet_fairing_r":"jet_fairing_l"))Matrix(fairing,{(side?-1.f:1.f)*.265f*retract,0,0});
 for(size_t i=0;i<3;++i){void *gear=Find(root,kGearNames[i]),*carrier=Find(root,kWheelNames[i]),*tire=Find(root,kTireNames[i]);if(!carrier||!tire||!gear)continue;
  const CVector hinge=kGearHinge[i];const CVector axis=i?CVector{0,i==1?-1.f:1.f,0}:CVector{1,0,0};Matrix(gear,{},axis,retract*(i?1.483529864f:1.570796327f));float *g=rw.matrix(gear);
  g[12]=hinge.x-(g[0]*hinge.x+g[4]*hinge.y+g[8]*hinge.z);g[13]=hinge.y-(g[1]*hinge.x+g[5]*hinge.y+g[9]*hinge.z)+(i?.35f*retract:0);g[14]=hinge.z-(g[2]*hinge.x+g[6]*hinge.y+g[10]*hinge.z);rw.update(gear);
  CVector pivot=jetwheels::kGear[i].center;pivot.z+=std::clamp(safe(pose.wheels[i].compression),-jetwheels::kTravel,jetwheels::kTravel)*extension;
  if(i){pivot.z+=.12f*std::sin(3.141592654f*retract)-.045f*retract;const float t=std::clamp(retract/.8f,0.f,1.f),wheelAngle=1.570796327f*t*t*(3-2*t);Matrix(carrier,pivot,{0,i==1?1.f:-1.f,0},retract*2.007128640f-wheelAngle);}else Matrix(carrier,pivot,{0,0,1},safe(pose.wheels[i].steer)*extension);Matrix(tire,{}, {1,0,0},std::fmod(safe(pose.wheels[i].spin),6.283185307f));
 }
 const float flaps[2]={pose.leftFlap,pose.rightFlap};for(int i=0;i<2;++i)if(void *frame=Find(root,i?"jet_flap_r":"jet_flap_l"))Matrix(frame,kFlapPivot[i],kFlapAxis[i],std::clamp(safe(flaps[i]),-.6f,.6f));
 for(int i=0;i<2;++i)if(void *frame=Find(root,i?"jet_elevator_r":"jet_elevator_l"))Matrix(frame,kElevatorPivot,{1,i?-.2f:.2f,0},std::clamp(safe(pose.elevator),-.5f,.5f));
 if(void *frame=Find(root,"jet_rudder"))Matrix(frame,kRudderPivot,{0,-.30f,1},std::clamp(safe(pose.rudder),-.55f,.55f));
 const float door=std::clamp(safe(pose.doorOpen),0.0f,1.0f),angle=-2.268928028f*door;for(const char *name:{"jet_door","jet_steps","jet_step_1","jet_step_2","jet_step_3"})if(void *frame=Find(root,name))Matrix(frame,kDoorPivot,{0,1,0},angle);

 rw.update(root);ClipPackedGear(clump,root);
}
void Animate(void *clump,const std::array<jetwheels::WheelPose,3> &wheels,float leftFlap,float rightFlap){Pose pose;pose.wheels=wheels;pose.leftFlap=leftFlap;pose.rightFlap=rightFlap;Animate(clump,pose);}
}
