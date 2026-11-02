// RED: the pristine instructions repeat 64 bounded passes for a NaN candidate.
// GREEN: the real public hook consumes those same native-shaped sectors and
// preserves ordinary native avoidance while finishing malformed queries.
#include "nativeweavearm.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <cstdarg>
#include <dlfcn.h>
static int failures;
static void Check(bool yes, const char *message) { if (!yes) { std::printf("FAIL: %s\n", message); ++failures; } }
static nativeweavetest::Fixture *fixture;
static nativeweavetest::Weave target;
static bool missingBinding, verified = true;
static unsigned logCount;
static unsigned comparisons;
static std::array<uint8_t, 0x58000> nativeBase;
namespace game { Api api{}; }
namespace hooks {
void *Install(const char *name, void *replacement) {
 Check(!std::strcmp(name, "_ZN8CCarCtrl30FindAngleToWeaveThroughTrafficEP8CVehicleP9CPhysicalff"), "public native float-return entry symbol");
 target = reinterpret_cast<nativeweavetest::Weave>(replacement); return reinterpret_cast<void *>(fixture->run ? fixture->run : target);
}
const void *ImageBase() { return verified ? nativeBase.data() : nullptr; }
}
void Log(const char *, ...) { ++logCount; }
static void *TestSymbol(void *, const char *name) {
 using F = nativeweavetest::Fixture;
 if (missingBinding) return nullptr;
 if (!std::strcmp(name, "_ZN6CWorld11ms_aSectorsE")) return fixture->sectors.data();
 if (!std::strcmp(name, "_ZN6CWorld19ms_nCurrentScanCodeE")) return fixture->scanPage.data() + 0x358;
 if (!std::strcmp(name, "_ZN6CWorld14ClearScanCodesEv")) return reinterpret_cast<void *>(F::Clear);
 if (!std::strcmp(name, "_ZN7CEntity14GetBoundCentreEv")) return reinterpret_cast<void *>(F::Centre);
 if (!std::strcmp(name, "_ZN8CCarCtrl16WeaveForOtherCarEP7CEntityP8CVehiclePfS4_")) return fixture->CarHelper();
 if (!std::strcmp(name, "_ZN8CCarCtrl14WeaveForObjectEP7CEntityP8CVehiclePfS4_")) return fixture->ObjectHelper();
 if (!std::strcmp(name, "_ZN8CCarCtrl11WeaveForPedEP7CEntityP8CVehiclePfS4_")) return fixture->PedHelper();
 if (!std::strcmp(name, "MI_TRAFFICLIGHTS")) return fixture->scanPage.data() + 0x6c8;
 if (!std::strcmp(name, "MI_SINGLESTREETLIGHTS1")) return fixture->scanPage.data() + 0x6d0;
 if (!std::strcmp(name, "MI_SINGLESTREETLIGHTS2")) return fixture->scanPage.data() + 0x6d2;
 if (!std::strcmp(name, "MI_SINGLESTREETLIGHTS3")) return fixture->scanPage.data() + 0x6d4;
 if (!std::strcmp(name, "MI_DOUBLESTREETLIGHTS")) return fixture->scanPage.data() + 0x6d6;
 Check(false, "known native dependency"); return nullptr;
}
#define dlsym TestSymbol
#include "../src/trafficweave.cpp"
#undef dlsym
using F = nativeweavetest::Fixture;
static uint8_t *Player() { return nullptr; }
static void Equivalent(int kind, float x, float y, float desired, float heading, int filter = 0)
{
 ++comparisons;
 auto prepare = [&] {
  fixture->Reset();
  F::Store(fixture->obstacle + 0x38, x); F::Store(fixture->obstacle + 0x3c, y);
  F::Store(fixture->obstacle + 0x30, 1.0f);
  if (filter == 1) F::Store(fixture->obstacle + 0x40, 10.0f);
  if (filter == 2) F::Store(fixture->obstacle + 0x60, uint32_t(0));
  if (filter == 3) { fixture->vehicle[0x298] = 0x20; fixture->obstacle[0x298] = 0x20; }
  if (filter == 4) F::Store(fixture->obstacle + 0x498, fixture->vehicle);
  if (filter == 5) F::Store(fixture->obstacle + 0x720, fixture->vehicle);
  if (filter == 6) F::Store(fixture->obstacle + 0x30, 0.8f);
  if (filter == 8) { fixture->vehicle[0x19e] = 3; }
 };
 nativeweavetest::Node node{fixture->obstacle}, duplicate{fixture->obstacle};
 auto lists = [&] { fixture->sectors[40 * 80 + 40].lists[kind] = &node; fixture->sectors[40 * 80 + 39].lists[kind + 1] = &duplicate; };
 prepare(); lists();
 const float expected = fixture->run(fixture->vehicle, filter == 7 ? fixture->obstacle : nullptr, desired, heading);
 const auto nativeCalls = fixture->helperCalls;
 prepare(); lists();
 const std::array<uint8_t, sizeof fixture->vehicle> saved = [&] { std::array<uint8_t, sizeof fixture->vehicle> out; std::memcpy(out.data(), fixture->vehicle, out.size()); return out; }();
 const float result = target(fixture->vehicle, filter == 7 ? fixture->obstacle : nullptr, desired, heading);
 if (result != expected)
  std::printf("DIFF kind=%d xy=%g,%g input=%g,%g filter=%d native=%g replacement=%g\n", kind, x, y, desired, heading, filter, expected, result);
 Check(result == expected, "finite car/ped/object gap exactly equals pristine native consumer");
 Check(fixture->helperCalls == nativeCalls, "native helper filtering/order/scan-code deduplication retained");
 Check(!std::memcmp(saved.data(), fixture->vehicle, saved.size()), "avoidance does not move or change the querying actor");
}
static void SectorEdge(float x, float y)
{
 ++comparisons;
 auto prepare = [&] {
  fixture->Reset();
  F::Store(fixture->vehicle + 0x38, x); F::Store(fixture->vehicle + 0x3c, y);
  F::Store(fixture->obstacle + 0x38, x); F::Store(fixture->obstacle + 0x3c, y + 1);
 };
 nativeweavetest::Node node{fixture->obstacle};
 const int row = std::clamp(int((y + 2000) / 50), 0, 79), col = std::clamp(int((x + 2400) / 50), 0, 79);
 prepare(); fixture->sectors[row * 80 + col].lists[4] = &node;
 const float expected = fixture->run(fixture->vehicle, nullptr, 0, 0);
 const auto count = fixture->helperCalls;
 prepare(); fixture->sectors[row * 80 + col].lists[4] = &node;
 const float actual = target(fixture->vehicle, nullptr, 0, 0);
 Check(actual == expected && fixture->helperCalls == count, "native truncation and empty-range semantics retained at every grid edge");
}
int main(int argc, char **argv)
{
 const bool native = argc == 2 && !std::strcmp(argv[1], "--native");
 if (argc != 1 && !native) { std::fprintf(stderr, "usage: test_trafficweave [--native]\n"); return 2; }
 if (native && !std::getenv("MACVC_NATIVE")) { std::fprintf(stderr, "--native requires explicit pristine MACVC_NATIVE\n"); return 2; }
 F local(false, native); fixture = &local;
 if (native) {
 const float normal = fixture->run(fixture->vehicle, nullptr, 0.0f, 0.0f);
 Check(fixture->passes == 1 && normal == 0.0f, "empty native traffic converges in one pass");
 fixture->Reset(); fixture->run(fixture->vehicle, nullptr, NAN, 0.0f);
 Check(fixture->passes == 64, "pristine NaN candidate reproduces the sampled unbounded sector pass");
 std::printf("Pristine NaN desired: %u bounded passes (native equality never converges)\n", fixture->passes);
 fixture->Reset(); nativeweavetest::Node car{}; fixture->Car(car); fixture->poisonHelper = true;
 fixture->run(fixture->vehicle, nullptr, 0.0f, 0.0f);
 Check(fixture->passes == 64 && fixture->helperCalls == 1, "pristine helper poison spins after one entity visit");
 std::printf("Pristine NaN helper: %u bounded passes, %u helper call\n", fixture->passes, fixture->helperCalls);
 }
 nativeweavetest::Node car{};
 for (auto word : {std::array<uint32_t, 2>{0x57224, 0x1e232020}, {0x57228, 0x1e200440}, {0x5729c, 0xf9401397}})
  F::Store(nativeBase.data() + word[0], native ? fixture->image.Word(0x100000000 + word[0]) : word[1]);
 game::api.modelInfoPtrs = reinterpret_cast<uint8_t **>(fixture->modelPage.data() + 0x50);
 game::api.FindPlayerPed = Player;
 Check(trafficweave::Install(), "real traffic avoidance replacement installs with verified native seam");
 fixture->Reset(); Check(target(fixture->vehicle, nullptr, NAN, 0.0f) == 0.0f, "NaN desired recovers finite current heading");
 fixture->Reset(); Check(target(fixture->vehicle, nullptr, 0.0f, NAN) == 0.0f, "NaN current recovers finite desired heading");
 fixture->Reset(); F::Store(fixture->vehicle + 0x18, 0.0f); F::Store(fixture->vehicle + 0x1c, 1.0f);
 Check(std::fabs(target(fixture->vehicle, nullptr, NAN, NAN) - 1.57079637f) < 0.000001f, "both invalid inputs use finite forward heading");
 fixture->Reset(); fixture->Car(car); fixture->poisonHelper = true;
 Check(target(fixture->vehicle, nullptr, 0.0f, 0.0f) == 0.0f && fixture->helperCalls == 1, "nonfinite native helper retains prior finite gap and terminates");
 for (float bad : {INFINITY, -INFINITY, 1e30f, -1e30f}) {
  fixture->Reset(); Check(std::isfinite(target(fixture->vehicle, nullptr, bad, 0.0f)), "unbounded input magnitude cannot enter native float-add loops");
 }
 if (native) {
 for (int kind : {2, 4, 6}) for (float desired : {-3.1f, -1.0f, 0.0f, 1.0f, 3.1f, 6.1f})
  for (float heading : {-3.0f, 0.0f, 3.0f}) for (auto point : {std::array<float, 2>{-395, 1}, {-405, 1}, {-400, -5}})
   Equivalent(kind, point[0], point[1], desired, heading);
 for (int kind : {2, 4, 6}) for (int filter = 1; filter <= 8; ++filter) Equivalent(kind, -395, 1, 0, 0, filter);
 Equivalent(4, -395, 1, -9999.900390625f, 0);
 for (auto point : {std::array<float, 2>{-2430, 0}, {-2400, 0}, {1590, 0}, {1630, 0}, {-400, -2030}, {-400, -2000}, {-400, 1990}, {-400, 2030}})
  SectorEdge(point[0], point[1]);
 for (auto point : {std::array<float, 2>{-395, 0.0f}, {-395, -0.0f}, {-405, 0}, {-400, 5}, {-400, -5}})
  for (float heading : {-2.8f, 0.0f, 2.8f}) Equivalent(6, point[0], point[1], 0, heading);
 }
 fixture->Reset(); fixture->Car(car);
 Check(target(fixture->vehicle, nullptr, 0, 0) == 0.5f && fixture->helperCalls == 1, "finite native-boundary helper retains avoidance gap");
 fixture->Reset(); nativeweavetest::Node foot{fixture->obstacle}; fixture->sectors[40 * 80 + 40].lists[6] = &foot;
 F::Store(fixture->obstacle + 0x38, -395.0f); F::Store(fixture->obstacle + 0x3c, 0.0f);
 Check(std::fabs(target(fixture->vehicle, nullptr, 0, 0) - 0.32f) < 0.000001f, "finite ped five meters ahead retains hand-derived 0.32 radian gap");
 fixture->Reset(); fixture->Car(car); Check(target(fixture->vehicle, fixture->obstacle, 0, 0) == 0 && fixture->helperCalls == 0, "ignored vehicle never contributes an avoidance gap");
 fixture->Reset(); fixture->Car(car); fixture->vehicle[0x298] = fixture->obstacle[0x298] = 0x20;
 Check(target(fixture->vehicle, nullptr, 0, 0) == 0 && fixture->helperCalls == 0, "two native boat flags retain their avoidance exemption");
 fixture->Reset(); nativeweavetest::Node object{fixture->obstacle}; fixture->sectors[40 * 80 + 40].lists[2] = &object;
 F::Store(fixture->obstacle + 0x3c, 0.0f); F::Store(fixture->obstacle + 0x30, 1.0f);
 F::Store(fixture->obstacle + 8, 0.0f); F::Store(fixture->obstacle + 0x1c, 0.0f);
 Check(target(fixture->vehicle, nullptr, 0, 0) == 0 && fixture->helperCalls == 0, "zero projected object distance is rejected before native callback normalization");
 for (int model = 0; model < 5; ++model) F::Store(fixture->scanPage.data() + std::array<int, 5>{0x6c8, 0x6d0, 0x6d2, 0x6d4, 0x6d6}[size_t(model)], uint16_t(10 + model));
 for (auto special : {std::array<float, 3>{10, 2.957f, .147f}, {11, .744f, 0}, {12, .043f, 0}, {13, 1.143f, .145f}, {14, 0, -.048f}}) {
  fixture->Reset(); fixture->sectors[40 * 80 + 40].lists[2] = &object;
  F::Store(fixture->obstacle + 0x6a, uint16_t(special[0])); F::Store(fixture->obstacle + 0x30, 1.0f);
  F::Store(fixture->obstacle + 0x38, -400.0f - special[1]); F::Store(fixture->obstacle + 0x3c, -special[2]);
  Check(target(fixture->vehicle, nullptr, 0, 0) == 0 && fixture->helperCalls == 0, "each native special-light projected overlap is bounded before callback");
 }
 fixture->Reset(); fixture->Car(car); F::Store(fixture->scanPage.data() + 0x358, uint16_t(65535)); F::Store(fixture->obstacle + 0x66, uint16_t(1));
 Check(target(fixture->vehicle, nullptr, 0, 0) == 0.5f && fixture->helperCalls == 1, "rollover clears old scan codes before new epoch");
 fixture->Reset(); fixture->Car(car); car.next = &car;
 Check(std::isfinite(target(fixture->vehicle, nullptr, 0, 0)) && fixture->helperCalls == 1, "cyclic node chain terminates within query budget");
 fixture->Reset(); fixture->Car(car); fixture->oscillatingHelper = true;
 Check(std::isfinite(target(fixture->vehicle, nullptr, 0, 0)) && fixture->helperCalls == 4, "callback scan mutation and finite oscillation terminate at the pass budget");
 for (float outside : {-4000.0f, 4000.0f, INFINITY, NAN}) {
  fixture->Reset(); F::Store(fixture->vehicle + 0x38, outside);
  Check(std::isfinite(target(fixture->vehicle, nullptr, 0.25f, 0.0f)) && fixture->helperCalls == 0, "invalid/off-world query never reads a sector");
 }
 // Execute the real pristine car/object helper bodies for large-COL overlap.
 // These finite dimensions represent the 22.6m-wide Learjet class rather than
 // narrowing the consumer to ordinary car-sized collision models.
 if (native) {
 F actual(true); fixture = &actual;
 game::api.modelInfoPtrs = reinterpret_cast<uint8_t **>(fixture->modelPage.data() + 0x50);
 F::Store(fixture->collision + 0xc, 16.0f); F::Store(fixture->collision + 0x1c, 11.3f); F::Store(fixture->collision + 0x20, 12.0f);
 Check(trafficweave::Install(), "real native car/object helper bodies bind in isolated process");
 for (int kind : {2, 4, 6}) for (float desired : {-3.1f, -0.5f, 0.0f, 0.5f, 3.1f})
  for (float heading : {-2.8f, 0.0f, 2.8f}) for (auto point : {std::array<float, 2>{-398, 1}, {-395, 1}, {-405, -1}, {-400, -12}})
   Equivalent(kind, point[0], point[1], desired, heading);
 for (float bad : {INFINITY, -INFINITY, 1e30f, -1e30f}) {
  fixture->Reset(); fixture->Car(car); F::Store(fixture->obstacle + 0x38, -398.0f);
  Check(std::isfinite(target(fixture->vehicle, nullptr, bad, 0)), "huge input is bounded before real native helper normalization");
 }
 fixture->Reset(); nativeweavetest::Node ped{fixture->obstacle}; fixture->sectors[40 * 80 + 40].lists[6] = &ped;
 F::Store(fixture->obstacle + 0x3c, 0.0f);
 Check(std::isfinite(target(fixture->vehicle, nullptr, 0, 0)), "exactly overlapping ped cannot divide by zero in inlined native-shaped avoidance");
 }
 Check(logCount == 4, "diagnostics stop after the first four malformed queries");
 verified = false; Check(!trafficweave::Install(), "unknown native seam refuses hook"); verified = true;
 missingBinding = true; Check(!trafficweave::Install(), "missing native callback refuses hook");
 if (!failures) std::printf("trafficweave %s consumer: all passed (%u native differential cases)\n", native ? "native" : "pure", comparisons);
 return failures ? 1 : 0;
}
