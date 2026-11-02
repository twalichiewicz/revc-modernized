#include "reactimpact.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace reactimpact {
namespace {
using namespace reactphysics;
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
int Piece(Joint joint)
{
	if(joint>=ShoulderL&&joint<=HandL)return 2;
	if(joint>=ShoulderR&&joint<=HandR)return 3;
	if(joint>=HipL&&joint<=FootL)return 4;
	if(joint>=HipR&&joint<=FootR)return 5;
	return joint==Head?6:1;
}
}
Selection Resolve(const reactphysics::Pose &pose, Vec3 point, int piece)
{
	using namespace reactphysics;
	if(!Finite(point))return {};
	for(Vec3 joint:pose)if(!Finite(joint))return {};
	constexpr Joint parent[Count]={Pelvis,Pelvis,Chest,Chest,ShoulderL,ElbowL,Chest,ShoulderR,ElbowR,Pelvis,HipL,KneeL,Pelvis,HipR,KneeR};
	Selection best;
	float distance=std::numeric_limits<float>::infinity();int priority=-1;
	auto candidate=[&](Joint a,Joint b){
		const Vec3 line=Sub(pose[b],pose[a]);const float square=Dot(line,line);
		if(square<1e-8f)return;
		const float t=std::clamp(Dot(Sub(point,pose[a]),line)/square,0.0f,1.0f);
		const Vec3 delta{point.x-pose[a].x-line.x*t,point.y-pose[a].y-line.y*t,point.z-pose[a].z-line.z*t};
		const float d=Dot(delta,delta);const int rank=Piece(b)==piece?2:Piece(a)==piece?1:0;
		if(d<distance-1e-8f || (std::fabs(d-distance)<=1e-8f && rank>priority)){
			best={a,b,t,true};distance=d;priority=rank;
		}
	};
	for(int joint=1;joint<Count;++joint)candidate(parent[joint],Joint(joint));
	candidate(ShoulderL,ShoulderR);candidate(HipL,HipR);
	const Vec3 trunk=Sub(pose[Head],pose[Pelvis]);
	const float reach=std::clamp(std::sqrt(Dot(trunk,trunk))*.75f,.35f,1.0f);
	return distance<=reach*reach?best:Selection{};
}
} // namespace reactimpact
