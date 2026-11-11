// Private Dodo authoring, included by airportjetmodel.cpp after its shared
// native geometry/frame builders. Original model data is never mutated.
bool ReadPart(void *atomic,std::vector<Face> &faces,CVector offset={}){
 auto g=rw.geometry(atomic);if(!g)return false;const int nv=rw.vertexCount(g),nt=rw.triangleCount(g);
 const auto p=rw.vertices(rw.morph(g,0)),n=rw.normals(rw.morph(g,0));const auto uv=rw.uv(g,1);const auto colors=rw.colors(g);const auto triangles=rw.triangles(g);
 if(nv<3||nv>65535||nt<1||nt>20000||!p||!triangles)return false;
 for(int i=0;i<nt;++i){Face face{};face.material=rw.material(g,triangles[i].material);if(!face.material)return false;
  for(int j=0;j<3;++j){int k=triangles[i].v[j];if(k>=nv||!Finite(p[k]))return false;face.v[j]={Add(p[k],offset),n?n[k]:CVector{},uv?uv[k]:CVector2D{},colors?colors[k]:Color{255,255,255,255}};}faces.push_back(face);
 }return true;
}
constexpr CVector kDodoFlapPivot[2]={{-2.625f,-.1235f,1.015f},{2.625f,-.1235f,1.015f}};
constexpr CVector kDodoFlapAxis[2]={{1,-.035886f,.01016f},{1,.035886f,-.01016f}};
constexpr CVector kDodoElevator{0,-4.6513f,-.03f},kDodoRudder{0,-5.38f,0},kDodoYoke{-.32f,.72f,.41f};
void Tube(Mesh &mesh,CVector from,CVector to,float radius,void *material){
 const auto axis=Unit(Sub(to,from));const auto x=Unit(Cross(axis,std::fabs(axis.z)<.8f?CVector{0,0,1}:CVector{0,1,0})),y=Cross(axis,x);
 for(int i=0;i<12;++i){auto ring=[&](CVector center,int a){float angle=a*6.28318530718f/12;return Add(center,Add(Mul(x,radius*std::cos(angle)),Mul(y,radius*std::sin(angle))));};Quad(mesh,ring(from,i),ring(from,i+1),ring(to,i+1),ring(to,i),material);}
}
bool DodoComplete(void *root){return Find(root,"dodo_yoke")&&Find(root,"dodo_grip_l")&&Find(root,"dodo_grip_r")&&Find(root,"dodo_hull");}
bool PrepareDodo(void *clump,void *donor){
 auto root=rw.root(clump);if(!root)return false;if(DodoComplete(root))return true;
 auto source=FindAtomic(clump,"chassis_hi"),nose=FindAtomic(clump,"wheel_front"),rear=FindAtomic(clump,"wheel_rear");
 if(!source||!nose||!rear||rw.vertexCount(rw.geometry(source))!=717||rw.triangleCount(rw.geometry(source))!=561||rw.triangleCount(rw.geometry(nose))!=92||rw.triangleCount(rw.geometry(rear))!=184)return false;
 std::vector<Face> original,front,back;if(!ReadPart(source,original)||!ReadPart(nose,front,jetwheels::kDodoGear[0].center)||!ReadPart(rear,back,{0,-.432198f,-1.24355f}))return false;
 Materials materials;void *yoke=materials.Make({45,49,52,255});if(!yoke)return false;
 std::vector<Mesh> meshes(15);const char *names[]={"dodo_hull","dodo_glass","dodo_tire_n","dodo_tire_l","dodo_tire_r","dodo_flap_l","dodo_flap_r","dodo_stab_l","dodo_stab_r","dodo_elevator_l","dodo_elevator_r","dodo_rudder","dodo_cabin","dodo_dashboard","dodo_yoke"};
 for(size_t i=0;i<meshes.size();++i)meshes[i].name=names[i];
 meshes[2].faces=front;for(const auto &face:back){bool left=true,right=true;for(auto &v:face.v){left&=v.p.x<0;right&=v.p.x>0;}if(!left&&!right)return false;meshes[left?3:4].faces.push_back(face);}
 for(int i=0;i<3;++i){meshes[2+i].pivot=jetwheels::kDodoGear[i].center;meshes[2+i].wheel=i;}
 for(int i=0;i<2;++i){meshes[5+i].pivot=kDodoFlapPivot[i];meshes[9+i].pivot=kDodoElevator;}meshes[11].pivot=kDodoRudder;meshes[14].pivot=kDodoYoke;
 for(const auto &face:original){
  void *texture=rw.texture(face.material);const char *name=texture?rw.textureName(texture):nullptr;const auto color=rw.color(face.material);
  if(color&&color->a<255){meshes[1].faces.push_back(face);continue;}
  // Replace the old baked cockpit furniture/inner panels only. Preserve its
  // original exterior, window frames and every outboard wing source face.
  bool furniture=name&&!std::strcmp(name,"dodo644bit");for(const auto &v:face.v)furniture&=std::fabs(v.p.x)<.76f&&v.p.y>-.8f&&v.p.y<1.51f&&v.p.z>-.40f&&v.p.z<1.05f;if(furniture)continue;
  std::vector<std::vector<Vertex>> panels{{face.v[0],face.v[1],face.v[2]}};
  bool tail=true;for(auto &v:face.v)tail&=v.p.y< -4.1f&&v.p.z<.15f&&v.p.z>-.15f;
  if(tail){
   for(auto &panel:panels){std::vector<Vertex> left,right;Split(panel,{1,0,0,0},right,left);
    for(int side=0;side<2;++side){auto &part=side?right:left;for(auto &fixed:Extract(part,{{0,-1,0,-4.6513f}},meshes[9+side],face.material))Polygon(meshes[7+side],fixed,face.material);}
   }continue;
  }
  bool fin=true;for(auto &v:face.v)fin&=std::fabs(v.p.x)<.06f&&v.p.y< -4.4f&&v.p.z>=0;
  if(fin){for(auto &panel:panels)for(auto &fixed:Extract(panel,{{0,-1,-.52f,-5.38f}},meshes[11],face.material))Polygon(meshes[0],fixed,face.material);continue;}
  for(int side=0;side<2;++side){const float sign=side?1.f:-1.f;std::vector<std::vector<Vertex>> rest;for(auto &panel:panels){auto fixed=Extract(panel,{{sign,0,0,-2.625f},{-sign,0,0,4.8403f},{sign*.035886f,-1,0,-.2177f},{0,0,1,-.8f},{0,0,-1,1.2f}},meshes[5+side],face.material);rest.insert(rest.end(),fixed.begin(),fixed.end());}panels=std::move(rest);}
  for(auto &panel:panels)Polygon(meshes[0],panel,face.material);
 }
 Mesh unused;if(!Cockpit(donor,meshes[12],meshes[13],unused))return false;
 for(int i:{12,13})for(auto &face:meshes[i].faces)for(auto &v:face.v){v.p.y-=4.75f;v.p.z+=.70f;}
 Tube(meshes[14],Add(kDodoYoke,{0,0,0}),Add(kDodoYoke,{0,.27f,0}),.022f,yoke);
 Tube(meshes[14],Add(kDodoYoke,{-.16f,0,0}),Add(kDodoYoke,{.16f,0,0}),.018f,yoke);
 for(float side:{-.16f,.16f})Tube(meshes[14],Add(kDodoYoke,{side,0,-.015f}),Add(kDodoYoke,{side,0,.14f}),.019f,yoke);
 for(int i=2;i<12;++i)if(meshes[i].faces.empty())return false;
 std::vector<Built> built(meshes.size()+2);
 for(size_t i=0;i<meshes.size();++i){auto &part=built[i];part.geometry=Build(meshes[i]);part.atomic=rw.createAtomic();part.frame=rw.createFrame();if(!part.geometry||!part.atomic||!part.frame){Cleanup(built);return false;}
  Name(part.frame,meshes[i].name);if(meshes[i].wheel>=0){part.carrier=rw.createFrame();if(!part.carrier){Cleanup(built);return false;}const char *wheelNames[]={"dodo_wheel_n","dodo_wheel_l","dodo_wheel_r"};Name(part.carrier,wheelNames[meshes[i].wheel]);Matrix(part.carrier,meshes[i].pivot);Matrix(part.frame,{});rw.addChild(part.carrier,part.frame);}else Matrix(part.frame,meshes[i].pivot);
  rw.setGeometry(part.atomic,part.geometry,0);rw.setFrame(part.atomic,part.frame);
 }
 for(int i=0;i<2;++i){auto &grip=built[meshes.size()+i];grip.frame=rw.createFrame();if(!grip.frame){Cleanup(built);return false;}Name(grip.frame,i?"dodo_grip_r":"dodo_grip_l");Matrix(grip.frame,{i?.16f:-.16f,0,.07f});}
 // Commit after all allocations. The old atomics keep their original shared
 // geometry refs; only this actor's flags change, preventing LOD duplicates.
 rw.each(clump,[](void *a,void*)->void*{rw.setAtomicFlags(a,rw.atomicFlags(a)&~4u);return a;},nullptr);
 for(size_t i=0;i<meshes.size();++i){auto &part=built[i];rw.addChild(root,part.carrier?part.carrier:part.frame);rw.addAtomic(clump,part.atomic);rw.destroyGeometry(part.geometry);part.geometry=nullptr;}
 for(int i=0;i<2;++i)rw.addChild(built[14].frame,built[meshes.size()+i].frame);
 for(auto &part:built)part={};rw.update(root);return true;
}
void NeutralDodo(void *root){if(auto flap=Find(root,"dodo_flap_r"))Matrix(flap,kDodoFlapPivot[1]);}
void AnimateDodoSurfaces(void *root,const Pose &pose){
 auto safe=[](float v){return std::isfinite(v)?v:0.f;};
 const float flaps[2]={pose.leftFlap,pose.rightFlap};for(int side=0;side<2;++side)if(auto frame=Find(root,side?"dodo_flap_r":"dodo_flap_l"))Matrix(frame,kDodoFlapPivot[side],kDodoFlapAxis[side],side&&pose.materialWing?0:std::clamp(safe(flaps[side]),-.5f,.5f));
 for(auto name:{"dodo_elevator_l","dodo_elevator_r"})if(auto frame=Find(root,name))Matrix(frame,kDodoElevator,{1,0,0},std::clamp(safe(pose.elevator),-.4f,.4f));
 if(auto frame=Find(root,"dodo_rudder"))Matrix(frame,kDodoRudder,{0,-.52f,1},std::clamp(safe(pose.rudder),-.45f,.45f));
}
void AnimateDodo(void *root,const Pose &pose){
 auto safe=[](float v){return std::isfinite(v)?v:0.f;};
 if(!pose.bodyOwned)AnimateDodoSurfaces(root,pose);
 for(int i=0;i<3;++i){const char *tires[]={"dodo_tire_n","dodo_tire_l","dodo_tire_r"},*carriers[]={"dodo_wheel_n","dodo_wheel_l","dodo_wheel_r"};auto carrier=Find(root,carriers[i]),tire=Find(root,tires[i]);if(!carrier||!tire)continue;auto pivot=jetwheels::kDodoGear[i].center;pivot.z+=std::clamp(safe(pose.wheels[i].compression),-jetwheels::kTravel,jetwheels::kTravel);Matrix(carrier,pivot,{0,0,1},i?0:safe(pose.wheels[0].steer));Matrix(tire,{}, {1,0,0},safe(pose.wheels[i].spin));}
 if(auto frame=Find(root,"dodo_yoke")){auto pivot=kDodoYoke;pivot.y+=safe(pose.elevator)*.18f;Matrix(frame,pivot,{0,1,0},std::clamp(safe(pose.leftFlap-pose.rightFlap),-.7f,.7f));}rw.update(root);
}
