#pragma once
#include "aircraftconnectivity.h"
#include "airframemodel.h"
#include "vehiclebodylayout.h"
#include <array>
#include <cstdint>
#include <vector>

// Canonical vehicle material state, in model metres. Native adapters retain source
// attributes; every Fragment.source is an index into the complete input array.
namespace aircraftbodymodel {
enum class Role {Body,WingLeft,WingRight,Elevator,Rudder,Wheel,Door,Interior,Engine,Fuel,Electrical,MainRotor,TailRotor};
struct Surface {
 std::array<Vec3,3> points{};
 uint32_t group=0;
 bool structural=true,glass=false;
 Role role=Role::Body;
 // Laminated glazing (windscreens) keeps local perforations; tempered side and
 // rear panes are removed whole by a penetrating round.
 bool laminated=false;
};
// Source-host classification of captured RW atomic faces, in the actor's
// model metres. Shared with asset tests, which must exercise these exact rules.
Role ClassifyFace(const char *atomic,Vec3 centre,Kind kind,int model);
// Mod-prepared cabin, lining, trim and stair atomics stay native render-only
// geometry and are never admitted as material.
bool RenderOnlyAtomic(const char *atomic);
struct FaceClass {bool glass=false,laminated=false,structural=true;};
// translucent: the captured material's alpha<255 identity; alpha/texture as
// captured. Automobiles admit only cabin glazing; decals/lamps are sheet.
FaceClass ClassifyMaterial(Role role,Kind kind,const char *atomic,Vec3 centre,Vec3 normal,bool translucent,uint8_t alpha,const char *texture);
struct Result {bool valid=false,changed=false;float deposited=0,remaining=0;size_t holes=0;};
class Model {
 // Mechanical bodies accumulate plastic rotation as a vector, so a later
 // contact adds its own bend instead of re-aiming earlier deformation.
 struct Bend {Vec3 direction{0,0,-1};float elastic=0,velocity=0,plastic=0;Vec3 plasticVector{};};
 struct GroupPose {uint32_t group=0;std::array<float,12> affine{};};
 struct Episode {Vec3 point{};uint32_t last=0;float peak=0;bool seen=false;float bend=0;};
 struct Dent {Vec3 point{},normal{},direction{};float radius=0,depth=0;uint32_t group=0;bool yield=false;uint32_t yieldAt=0;float yieldPush=0;};
 struct PartEpisode {uint32_t group=0;Episode episode;};
 struct FlexAxis {float angle=0,velocity=0,plastic=0;};
 struct Section {
  std::array<float,2> at{},mass{},arm{},crackWork{};
  std::array<float,2> inertia{},extent{},intactInertia{};
  std::array<FlexAxis,2> flex{};
  // Spar/stringer allowance a skin-only station needs to carry its design load.
  std::array<float,2> reinforcement{{1,1}};
 };
 Definition definition_{};
 VehicleLayout layout_{};
 aircraftbodymodel::MaterialParameters material_{};
 mechanicalmodel::State mechanical_{};
 bool generic_=false;
 std::vector<Surface> surfaces_;
 std::vector<GroupPose> poses_;
 aircraftconnectivity::Mesh mesh_;
 std::array<float,airframemodel::Count> damage_{};
 std::array<bool,airframemodel::Count> originalComponents_{};
 std::array<Bend,2> bend_{};
 std::array<std::vector<uint32_t>,2> deformGroups_{};
 std::vector<uint32_t> tailGroups_;
 std::array<Episode,8> contacts_{};
 std::vector<PartEpisode> partContacts_;
 std::vector<Dent> dents_;
 // Twelve cells per wing/boom retain independent weak-section coordinates for
 // each bending axis. Their carried masses/lever arms use those coordinates.
 std::array<std::array<Section,12>,3> sections_{};
 uint64_t sectionRevision_=0;
 std::array<float,2> originalLift_{},originalControls_{};
 float mass_=0,fuel_=1,heat_=.2f;
 uint64_t deformationRevision_=0;
 bool initialized_=false;
 int Side(const Surface &)const;
 int Region(const Surface &)const;
 bool MechanicalKind()const;
 bool SheetComponent(int)const;
 float ComponentResistance(int)const;
 void ApplyDoses(const std::array<float,airframemodel::Count>&,Vec3 inward={},bool collision=false);
 bool InitializeBody(const std::vector<Surface>&,float,const std::vector<aircraftconnectivity::Anchor>&,const Definition*);
 float AttachedLift(int side)const;
 float AttachedControl(Role role)const;
 bool GroupAttached(uint32_t group)const;
 bool ComponentPresent(int module)const;
 bool ComponentVolume(int module,mechanicalmodel::Box&)const;
 Vec3 Pose(Vec3 point,uint32_t group)const;
 Vec3 RestDirection(Vec3 direction,uint32_t group)const;
 // crush scales internal component work by uncapped/capped accepted work.
 Result SpendContact(Vec3 point,Vec3 inward,float energy,float reach,uint32_t now,float dt,float crush=1);
 long NearestSurface(Vec3 point)const;
 static bool YieldPatch(const Dent&,uint32_t group,Vec3 point);
 void RememberSection(Vec3 point,int region);
 void RebuildSections();
 bool AdvanceSections(float dt,Vec3 apparentGravity);
public:
 bool Initialize(const std::vector<Surface> &,float mass,const std::vector<aircraftconnectivity::Anchor> &mounts={});
 bool Initialize(const std::vector<Surface>&,float mass,const Definition&,const std::vector<aircraftconnectivity::Anchor>&mounts={});
 const Definition& GetDefinition()const{return definition_;}
 const VehicleLayout& Layout()const{return layout_;}
 const aircraftbodymodel::MaterialParameters& MaterialParameters()const{return material_;}
 float SurfaceDensity()const{return material_.surfaceDensity;}
 const mechanicalmodel::State& Mechanical()const{return mechanical_;}
  // Compact RW columns: right, forward, up, translation, three floats each.
 bool SetGroupPose(uint32_t group,const std::array<float,12> &modelAffine);
 bool SetMass(float mass);
 Result Bullet(Vec3 from,Vec3 to,float budget,float limit=1);
 bool TraceComponent(Vec3 from,Vec3 to,float entry,float &closest,Vec3 &normal)const;
 Result Contact(Vec3 localPoint,Vec3 inward,float deltaV,uint32_t now,float dt);
 // Accepted native impulse work, in joules; rigid motion stays native-owned.
 Result ContactEnergy(Vec3 localPoint,Vec3 inward,float joules,uint32_t now,float dt);
 // Force (N) the struck material carries before it yields. A non-structural
 // sheet, or wheeled-body material over a quarter tyre radius below the wheel-
 // centre plane (valance, skirt, air dam, lip: no bumper beam), yields at its
 // plate-collapse // force; other structure at the body crush force. A crush stroke already
 // used up stands on the structure behind it.
 float YieldForce(size_t surface,Vec3 point)const;
 // 0 sheet, 1 structure; +2 when the patch's crush stroke is used up; +4 lower trim.
 int YieldZone(size_t surface,Vec3 point)const;
 float YieldForce(Vec3 point)const;
 // Plastic push of the struck patch by displacement (m) along inward, booking
 // force x the distance actually pushed. Contacts on one patch in one frame
 // (same now) are one penetration and push once.
 Result Yield(size_t surface,Vec3 point,Vec3 inward,float displacement,float force,uint32_t now);
 Result Yield(Vec3 point,Vec3 inward,float displacement,float force,uint32_t now);
 // Apparent acceleration in current model axes (m/s^2): gravity minus native
 // origin acceleration. Zero input gives no self-weight force in free fall.
 void Tick(float dt,bool running,Vec3 apparentGravity={0,0,-20});
 void Burn(float dt);
 // Transactional ownership transfer: source IDs remain stable; transferred
 // material cannot intercept another parent bullet or be emitted twice.
 bool TransferDetached(std::vector<aircraftconnectivity::Fragment> &out);
 airframemodel::Effects Effects()const;
 firemodel::Input Fire(bool running)const;
 // Visual ignition location in the same current model pose as surviving skin.
 Vec3 FireSource()const;
 const std::vector<aircraftconnectivity::Fragment> &Fragments()const{return mesh_.Fragments();}
 const std::vector<Surface> &Surfaces()const{return surfaces_;}
 Vec3 Deform(Vec3 restPosition,uint32_t group)const;
 uint64_t Revision()const{return mesh_.Revision()+deformationRevision_;}
 bool Initialized()const{return initialized_;}
 // Accumulated plastic beam rotation (rad) of one body half or wing.
 float BendPlastic(int side)const{return side>=0&&side<2?bend_[side].plastic:0;}
};
}
