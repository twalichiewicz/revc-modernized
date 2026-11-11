#define main LearjetModelFixtures
#include "test_airportjetmodel.cpp"
#undef main
Clump LoadDodo(){
 const char* env=std::getenv("GAMEDATA");const std::string data=env?env:"/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData";ImgArchive img;CHECK(img.Open(data+"/models/gta3.img"));std::vector<uint8_t>b;CHECK(img.Read("deaddodo.dff",b));
 std::vector<Frame*>frames;Clump clump{};
 for(auto c:Chunks(b,12,b.size()))if(c.id==14){int ext=0;for(auto s:Chunks(b,c.a,c.z))if(s.id==1){const auto n=Get<uint32_t>(b,s.a);for(unsigned i=0;i<n;++i){auto f=static_cast<Frame*>(FrameCreate());auto at=s.a+4+i*56;std::memcpy(f->matrix,b.data()+at,12);std::memcpy(f->matrix+4,b.data()+at+12,12);std::memcpy(f->matrix+8,b.data()+at+24,12);std::memcpy(f->matrix+12,b.data()+at+36,12);frames.push_back(f);}for(unsigned i=0;i<n;++i){int parent=Get<int>(b,s.a+4+i*56+48);if(parent>=0)FrameChild(frames[parent],frames[i]);else clump.root=frames[i];}}
 else if(s.id==3){for(auto name:Chunks(b,s.a,s.z))if(name.id==0x253f2fe){auto length=std::min<size_t>(23,name.z-name.a);std::memcpy(frames[ext]->name,b.data()+name.a,length);frames[ext]->name[length]=0;}++ext;}}
 for(auto c:Chunks(b,12,b.size()))if(c.id==20)for(auto s:Chunks(b,c.a,c.z))if(s.id==1){auto a=static_cast<Atomic*>(AtomicCreate());a->frame=frames[Get<int>(b,s.a)];a->geometry=Load("deaddodo.dff",Get<int>(b,s.a+4));a->flags=Get<unsigned>(b,s.a+8);clump.atomics.push_back(a);}return clump;
}
#ifndef DODO_MODEL_NO_MAIN
int main(){
 auto dodo=LoadDodo();auto donor=LoadCockpit();CHECK(airportjetmodel::Install());
 auto original=Part(dodo,"chassis_hi")->geometry;const auto fingerprint=GeometryFingerprint(*original);++original->refs;
 const bool prepared=airportjetmodel::Prepare(&dodo,&donor);CHECK(prepared);
 if(prepared){
  for(auto name:{"dodo_hull","dodo_cabin","dodo_yoke","dodo_tire_n","dodo_tire_l","dodo_tire_r","dodo_flap_l","dodo_flap_r","dodo_stab_l","dodo_stab_r","dodo_elevator_l","dodo_elevator_r","dodo_rudder"})CHECK(Part(dodo,name));
  CHECK(Find(dodo.root,"dodo_grip_l")&&Find(dodo.root,"dodo_grip_r"));
  CHECK(Part(dodo,"dodo_tire_n")->geometry->triangles.size()==92&&Part(dodo,"dodo_tire_l")->geometry->triangles.size()==92&&Part(dodo,"dodo_tire_r")->geometry->triangles.size()==92);
  auto cabin=Part(dodo,"dodo_cabin");int textured=0;for(auto m:cabin->geometry->materials)if(m->texture&&m->texture->name=="polmav8bit128")++textured;CHECK(textured>0&&cabin->geometry->triangles.size()>=300);
  for(int i=0;i<3;++i){auto tire=Part(dodo,i==0?"dodo_tire_n":(i==1?"dodo_tire_l":"dodo_tire_r"));float bottom=1e9f;for(auto p:tire->geometry->vertices)bottom=std::min(bottom,ModelPoint(tire->frame,p).z);CHECK(std::fabs(bottom+1.50155f)<.0001f);}
  airportjetmodel::Pose p;p.leftFlap=.3f;p.rightFlap=-.3f;p.elevator=.2f;p.rudder=.25f;p.wheels[0].spin=1;p.wheels[0].steer=.2f;airportjetmodel::Animate(&dodo,p);
  CHECK(std::fabs(Find(dodo.root,"dodo_flap_l")->matrix[6])>.1f&&std::fabs(Find(dodo.root,"dodo_elevator_l")->matrix[6])>.1f&&std::fabs(Find(dodo.root,"dodo_rudder")->matrix[1])>.1f);
  CHECK(std::fabs(Find(dodo.root,"dodo_yoke")->matrix[2])>.1f);
  CVector l,r,up;CHECK(airportjetmodel::VCSourceDodoGrips(&dodo,&l,&r,&up));CHECK(std::fabs(l.x-r.x)>.25f&&std::fabs(l.z-r.z)>.1f&&std::fabs(up.x)>.1f);
  p.materialWing=true;airportjetmodel::Animate(&dodo,p);CHECK(std::fabs(Find(dodo.root,"dodo_flap_r")->matrix[6])<1e-7f&&std::fabs(Find(dodo.root,"dodo_flap_l")->matrix[6])>.1f);
  const auto count=dodo.atomics.size();CHECK(airportjetmodel::Prepare(&dodo,&donor)&&dodo.atomics.size()==count);
 }
 CHECK(GeometryFingerprint(*original)==fingerprint);GeometryDestroy(original);Destroy(dodo);Destroy(donor);
 for(int offset:{1,8,30,70}){auto actor=LoadDodo(),cockpit=LoadCockpit();auto before=GeometryFingerprint(*Part(actor,"chassis_hi")->geometry);auto atoms=actor.atomics.size();failAt=allocation+offset;bool ok=airportjetmodel::Prepare(&actor,&cockpit);failAt=0;if(!ok){CHECK(actor.atomics.size()==atoms&&GeometryFingerprint(*Part(actor,"chassis_hi")->geometry)==before);for(auto a:actor.atomics)CHECK(a->flags&4);}Destroy(actor);Destroy(cockpit);}
 CHECK(liveGeometry==0&&liveMaterial==0&&liveFrame==0&&liveAtomic==0);
 if(!failures)std::puts("Dodo actual asset rig: all passed");return failures?1:0;
}
#endif
