#include "../src/scene.cpp"
#include <cmath>
#include <cstdio>
struct Capture : Canvas {
	DepthMode mode=DepthMode::kOff;int mesh=0;bool finite=true;
	void SetTexture(void *) override {}
	void SetDepthMode(DepthMode m) override {mode=m;}
	void Flush() override {}
	void Tri(const Vtx &a,const Vtx &b,const Vtx &c,CRGBA,CRGBA,CRGBA) override {
		if(mode!=DepthMode::kTestWrite)return;++mesh;
		for(const Vtx &v:{a,b,c})finite=finite&&std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.depth)&&v.x>=-0.01f&&v.x<=310.01f&&v.y>=-0.01f&&v.y<=200.01f;
	}
};
int main()
{
	Projector p;p.Setup({},0,0,310,200);
	mapdata::Building bridge;bridge.flags=mapdata::kVisualMesh|mapdata::kBridge;
	bridge.verts={{-20,-800,5},{20,-800,5},{-20,40,8},{20,40,8}};bridge.tris={0,1,2,1,3,2};bridge.lo={-20,-800};bridge.hi={20,40};bridge.top=8;
	std::vector<mapdata::Building> buildings{bridge};SceneInput in;in.background=false;in.buildings=&buildings;
	Capture canvas;DrawScene(canvas,p,in);
	if(!canvas.mesh) {std::puts("FAIL: a long bridge crossing the camera plane must retain its visible span");return 1;}
	if(!canvas.finite){std::puts("FAIL: clipped visual mesh vertices must stay finite and inside the map");return 1;}
	// Below-deck geometry at the far edge still needs distinct inverse depths.
	auto farPoint=[&](float height) {
		float lo=0,hi=50;for(int i=0;i<50;i++){const float y=(lo+hi)*.5f;Vec2 pixel;p.Project({0,y},height,pixel);if(pixel.y>2)lo=y;else hi=y;}
		return Vec2{0,(lo+hi)*.5f};
	};
	const Vec2 upper=farPoint(-.16f),lower=farPoint(-.24f);
	buildings[0].verts.push_back({upper.x*in.range,upper.y*in.range,-.16f*in.range});
	buildings[0].verts.push_back({lower.x*in.range,lower.y*in.range,-.24f*in.range});
	buildings[0].hi.y=lower.y*in.range;
	const View view=MakeView(p,in);
	if(!(view.Depth01(upper,-.16f)<view.Depth01(lower,-.24f))){std::puts("FAIL: below-deck supports at the far map edge must retain depth ordering");return 1;}
	std::puts("mapvisual: all passed");
}
