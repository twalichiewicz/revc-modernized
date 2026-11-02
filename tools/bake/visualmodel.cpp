#include "visualmodel.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <tuple>

namespace visualmodel {
namespace {
struct Chunk { uint32_t type, version; size_t start, end; };
template<class T> T Get(const std::vector<uint8_t> &b,size_t p) { T v{};std::memcpy(&v,b.data()+p,sizeof v);return v; }
bool Children(const std::vector<uint8_t> &b,size_t a,size_t end,std::vector<Chunk> &out)
{
	if(end>b.size())return false;
	while(a<end) {
		if(end-a<12)return false;
		const uint32_t size=Get<uint32_t>(b,a+4);
		if(size>end-a-12)return false;
		out.push_back({Get<uint32_t>(b,a),Get<uint32_t>(b,a+8),a+12,a+12+size});a+=12+size;
	}
	return true;
}
uint32_t Library(uint32_t v) { return v&0xffff0000u?(((v>>14)&0x3ff00u)+0x30000u)|((v>>16)&0x3fu):v<<8; }
bool Finite(Vec3f p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<=1e6f&&std::fabs(p.y)<=1e6f&&std::fabs(p.z)<=1e6f; }
struct Frame { float m[12];int32_t parent; };
Vec3f Apply(const Frame &f,Vec3f p) {
	return {f.m[0]*p.x+f.m[3]*p.y+f.m[6]*p.z+f.m[9],
	        f.m[1]*p.x+f.m[4]*p.y+f.m[7]*p.z+f.m[10],
	        f.m[2]*p.x+f.m[5]*p.y+f.m[8]*p.z+f.m[11]};
}
bool Materials(const std::vector<uint8_t> &b,const Chunk &list,std::vector<Material> &out)
{
	std::vector<Chunk> chunks;if(!Children(b,list.start,list.end,chunks)||chunks.empty())return false;
	const Chunk &s=chunks[0];if(s.type!=1||s.end-s.start<4)return false;
	const uint32_t n=Get<uint32_t>(b,s.start);if(n>65535||s.end-s.start<4+size_t(n)*4)return false;
	size_t next=1;
	for(uint32_t i=0;i<n;i++) {
		const int32_t ref=Get<int32_t>(b,s.start+4+size_t(i)*4);
		if(ref>=0) {if(size_t(ref)>=out.size())return false;out.push_back(out[size_t(ref)]);continue;}
		while(next<chunks.size()&&chunks[next].type!=7)++next;
		if(next==chunks.size())return false;
		std::vector<Chunk> kids;if(!Children(b,chunks[next].start,chunks[next].end,kids))return false;++next;
		Material material;
		for(const Chunk &c:kids) {
			if(c.type==1&&c.end-c.start>=8)material.alpha=b[c.start+7];
			if(c.type==6) {
				std::vector<Chunk> tex;if(!Children(b,c.start,c.end,tex))return false;
				for(const Chunk &t:tex)if(t.type==2&&material.texture.empty())
					material.texture=Lower(std::string(reinterpret_cast<const char *>(b.data()+t.start),strnlen(reinterpret_cast<const char *>(b.data()+t.start),t.end-t.start)));
			}
		}
		out.push_back(std::move(material));
	}
	return true;
}
bool Geometry(const std::vector<uint8_t> &b,const Chunk &g,Mesh &mesh)
{
	std::vector<Chunk> children;if(!Children(b,g.start,g.end,children)||children.empty())return false;
	const Chunk &s=children[0];size_t p=s.start;if(s.type!=1||s.end-p<16)return false;
	const uint32_t flags=Get<uint32_t>(b,p),nt=Get<uint32_t>(b,p+4),nv=Get<uint32_t>(b,p+8),nm=Get<uint32_t>(b,p+12);
	if(flags&0x01000000u||nv>1000000||nt>2000000||!nv||!nm)return false;
	p+=16;if(Library(s.version)<0x34000)p+=12;
	if(flags&8)p+=size_t(nv)*4;
	const uint32_t uv=(flags>>16)&0xff?((flags>>16)&0xff):(flags&(4|128)?1:0);
	if(uv>8)return false;p+=size_t(nv)*uv*8;
	if(p>s.end||size_t(nt)*8>s.end-p)return false;
	mesh.faces.reserve(nt);
	for(uint32_t i=0;i<nt;i++,p+=8) {
		const uint32_t a=Get<uint16_t>(b,p+2),c=Get<uint16_t>(b,p),d=Get<uint16_t>(b,p+6),m=Get<uint16_t>(b,p+4);
		if(a>=nv||c>=nv||d>=nv)return false;
		mesh.faces.push_back({a,c,d,m});
	}
	if(p>s.end||s.end-p<24)return false;
	const bool vertices=Get<uint32_t>(b,p+16)!=0;p+=24;
	if(!vertices||size_t(nv)*12>s.end-p)return false;
	mesh.vertices.reserve(nv);
	for(uint32_t i=0;i<nv;i++,p+=12) {
		Vec3f v{Get<float>(b,p),Get<float>(b,p+4),Get<float>(b,p+8)};if(!Finite(v))return false;mesh.vertices.push_back(v);
	}
	for(const Chunk &c:children)if(c.type==8&&!Materials(b,c,mesh.materials))return false;
	if(mesh.materials.empty())mesh.materials.push_back({});
	for(const Face &f:mesh.faces)if(f.material>=mesh.materials.size())return false;
	return true;
}
}
bool Read(const std::vector<uint8_t> &b,Mesh &out,std::string &why)
{
	out={};why="invalid visual DFF";
	if(b.size()<12||Get<uint32_t>(b,0)!=16||Get<uint32_t>(b,4)>b.size()-12)return false;
	std::vector<Chunk> chunks;if(!Children(b,12,12+Get<uint32_t>(b,4),chunks))return false;
	std::vector<Frame> frames;std::vector<Mesh> geometries;struct Atomic {int32_t frame,geometry;};std::vector<Atomic> atomics;
	for(const Chunk &c:chunks) {
		std::vector<Chunk> kids;
		if(c.type!=14&&c.type!=26&&c.type!=20)continue;
		if(!Children(b,c.start,c.end,kids)||kids.empty())return false;
		if(c.type==14) {
			const Chunk &s=kids[0];if(s.type!=1||s.end-s.start<4)return false;
			const uint32_t n=Get<uint32_t>(b,s.start);if(n>65536||4+size_t(n)*56>s.end-s.start)return false;
			for(uint32_t i=0;i<n;i++) {
				const size_t p=s.start+4+size_t(i)*56;Frame f;std::memcpy(f.m,b.data()+p,48);f.parent=Get<int32_t>(b,p+48);
				for(float v:f.m)if(!std::isfinite(v))return false;
				if(f.parent<-1||f.parent>=int32_t(n))return false;frames.push_back(f);
			}
		} else if(c.type==26) {
			for(const Chunk &g:kids)if(g.type==15) {Mesh mesh;if(!Geometry(b,g,mesh))return false;geometries.push_back(std::move(mesh));}
		} else {
			const Chunk &s=kids[0];if(s.type!=1||s.end-s.start<16)return false;
			if(Get<uint32_t>(b,s.start+8)&4)atomics.push_back({Get<int32_t>(b,s.start),Get<int32_t>(b,s.start+4)});
		}
	}
	Mesh result;
	for(const Atomic &a:atomics) {
		if(a.frame<0||size_t(a.frame)>=frames.size()||a.geometry<0||size_t(a.geometry)>=geometries.size())return false;
		const Mesh &mesh=geometries[size_t(a.geometry)];const uint32_t base=uint32_t(result.vertices.size()),mat=uint32_t(result.materials.size());
		for(Vec3f p:mesh.vertices) {
			int32_t frame=a.frame;size_t depth=0;
			while(frame>=0) {if(depth++>=frames.size())return false;p=Apply(frames[size_t(frame)],p);frame=frames[size_t(frame)].parent;}
			if(!Finite(p))return false;result.vertices.push_back(p);
		}
		for(const Face &f:mesh.faces)result.faces.push_back({base+f.a,base+f.b,base+f.c,mat+f.material});
		result.materials.insert(result.materials.end(),mesh.materials.begin(),mesh.materials.end());
	}
	if(result.faces.empty())return false;
	out=std::move(result);why.clear();return true;
}
Vec3f Place(Vec3f p,const Instance &in)
{
	// This macVC build parses export scale but LoadObjectInstance applies only
	// its inverse quaternion and translation. Match the visible native world.
	const float x=-in.qx,y=-in.qy,z=-in.qz,w=in.qw;
	return {in.pos.x+(1-2*(y*y+z*z))*p.x+2*(x*y-z*w)*p.y+2*(x*z+y*w)*p.z,
	        in.pos.y+2*(x*y+z*w)*p.x+(1-2*(x*x+z*z))*p.y+2*(y*z-x*w)*p.z,
	        in.pos.z+2*(x*z-y*w)*p.x+2*(y*z+x*w)*p.y+(1-2*(x*x+y*y))*p.z};
}
bool Build(const Mesh &mesh,const Instance &in,float ground,mapdata::Building &out,bool bridge,float water)
{
	out={};if(!std::isfinite(ground))return false;
	mapdata::Building result;result.flags=mapdata::kVisualMesh|(bridge?mapdata::kBridge:0);
	const float floor=bridge?water-ground:0;
	std::vector<Vec3f> placed;placed.reserve(mesh.vertices.size());
	for(Vec3f p:mesh.vertices) {p=Place(p,in);p.z-=ground;if(!Finite(p))return false;placed.push_back(p);}
	std::map<std::tuple<int64_t,int64_t,int64_t>,uint16_t> weld;
	auto vertex=[&](Vec3f p) {
		const auto key=std::make_tuple(int64_t(std::llround(p.x*1000)),int64_t(std::llround(p.y*1000)),int64_t(std::llround(p.z*1000)));
		auto [at,added]=weld.try_emplace(key,uint16_t(result.verts.size()));
		if(added)result.verts.push_back({p.x,p.y,p.z});return at->second;
	};
	for(const Face &f:mesh.faces) {
		if(f.a>=placed.size()||f.b>=placed.size()||f.c>=placed.size()||f.material>=mesh.materials.size())return false;
		const auto &material=mesh.materials[f.material];
		if(material.alpha<64)continue;
		const Vec3f input[3]={placed[f.a],placed[f.b],placed[f.c]};
		const float ux=input[1].x-input[0].x,uy=input[1].y-input[0].y,uz=input[1].z-input[0].z;
		const float vx=input[2].x-input[0].x,vy=input[2].y-input[0].y,vz=input[2].z-input[0].z;
		const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
		const bool decal=material.texture.rfind("blackshadow",0)==0||material.texture.rfind("shad_",0)==0;
		if(decal&&std::fabs(nz)>.98f*std::sqrt(nx*nx+ny*ny+nz*nz))continue;
		std::vector<Vec3f> polygon;
		for(int i=0;i<3;i++) {
			const Vec3f a=input[(i+2)%3],b=input[i];const bool ai=a.z>=floor,bi=b.z>=floor;
			if(ai!=bi) {const float t=(floor-a.z)/(b.z-a.z);polygon.push_back({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,floor});}
			if(bi)polygon.push_back(b);
		}
		for(size_t i=1;i+1<polygon.size();i++) {
			const Vec3f a=polygon[0],b=polygon[i],c=polygon[i+1];
			const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
			const float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
			if(nx*nx+ny*ny+nz*nz<1e-8f)continue;
			if(result.verts.size()>65530||result.tris.size()/3>=65535)return false;
			result.tris.push_back(vertex(a));result.tris.push_back(vertex(b));result.tris.push_back(vertex(c));
		}
	}
	if(result.tris.empty())return false;
	result.lo=result.hi={result.verts[0].x,result.verts[0].y};
	for(const Vec3 &p:result.verts) {result.lo={std::min(result.lo.x,p.x),std::min(result.lo.y,p.y)};result.hi={std::max(result.hi.x,p.x),std::max(result.hi.y,p.y)};result.top=std::max(result.top,p.z);}
	out=std::move(result);return true;
}
bool ActiveAt(const Instance &in,int hour)
{
	if(in.timeOn<0||in.timeOff<0||in.timeOn==in.timeOff)return true;
	return in.timeOn<in.timeOff?hour>=in.timeOn&&hour<in.timeOff:hour>=in.timeOn||hour<in.timeOff;
}
void AlignBridges(std::vector<BridgePart> &parts)
{
	std::vector<size_t> parent(parts.size());for(size_t i=0;i<parent.size();i++)parent[i]=i;
	auto root=[&](size_t i){while(parent[i]!=i){i=parent[i]=parent[parent[i]];}return i;};
	for(size_t i=0;i<parts.size();i++)for(size_t j=0;j<i;j++) {
		const auto &a=parts[i],&b=parts[j];
		if(a.lo.x<=b.hi.x+2&&b.lo.x<=a.hi.x+2&&a.lo.y<=b.hi.y+2&&b.lo.y<=a.hi.y+2)
			parent[root(i)]=root(j);
	}
	std::map<size_t,float> deck,ground;
	for(size_t i=0;i<parts.size();i++) {
		const size_t r=root(i);const auto &p=parts[i];
		auto add=[&](std::map<size_t,float> &m){auto at=m.find(r);if(at==m.end())m[r]=p.ground;else at->second=std::min(at->second,p.ground);};
		add(ground);if(p.deck)add(deck);
	}
	for(size_t i=0;i<parts.size();i++){const size_t r=root(i);parts[i].ground=deck.count(r)?deck[r]:ground[r];}
}
}
