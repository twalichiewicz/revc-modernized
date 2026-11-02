#include "trafficweave.h"
#include "game.h"
#include "hooks.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace trafficweave {
namespace {
constexpr const char *kSymbol = "_ZN8CCarCtrl30FindAngleToWeaveThroughTrafficEP8CVehicleP9CPhysicalff";
constexpr float kPi = 3.1415927410125732421875f, kTau = 6.283185482025146484375f;
constexpr float kNoAngle = -9999.900390625f;
struct Node { uint8_t *entity; Node *previous, *next; };
struct Sector { Node *lists[10]; };
static_assert(sizeof(Node) == 24 && sizeof(Sector) == 80);
Sector *gSectors;
uint16_t *gScan;
void (*gClearScan)();
void (*gCar)(uint8_t *, uint8_t *, float *, float *);
void (*gObject)(uint8_t *, uint8_t *, float *, float *);
CVector (*gCentre)(uint8_t *);
uint16_t *gObjectIds[5];
float (*gOriginal)(uint8_t *, uint8_t *, float, float);
unsigned gReports;
struct Diagnostic { uint8_t *vehicle; float desired, heading; };

template<class T> T Read(const uint8_t *entity, size_t at)
{
 T out; std::memcpy(&out, entity + at, sizeof out); return out;
}
template<class T> bool Bind(T &value, const char *symbol)
{
 value = reinterpret_cast<T>(dlsym(RTLD_MAIN_ONLY, symbol)); return value != nullptr;
}
float Wrap(float angle)
{
 if (!std::isfinite(angle)) return 0.0f;
 // Preserve native rounding for its ordinary one-turn inputs. fmod also
 // finishes for huge finite values where repeated float addition stops moving.
 if (std::fabs(angle) < 100.0f) {
  while (angle < -kPi) angle += kTau;
  while (angle > kPi) angle -= kTau;
  return angle;
 }
 angle = std::fmod(angle, kTau);
 if (angle < -kPi) angle += kTau;
 if (angle > kPi) angle -= kTau;
 return angle;
}
float PedLeft(float angle)
{
 // Native left candidates only wrap below -pi; a left value above +pi is
 // retained until the final gap comparison. Its numeric representation matters.
 if (angle >= -kPi) return angle;
 if (angle > -100.0f) { while (angle < -kPi) angle += kTau; return angle; }
 return Wrap(angle);
}
float PedRight(float angle)
{
 if (angle <= kPi) return angle;
 if (angle < 100.0f) { while (angle > kPi) angle -= kTau; return angle; }
 return Wrap(angle);
}
void Report(const Diagnostic &d, const char *reason)
{
 if (gReports < 4) {
  ++gReports;
  Log("trafficweave: bounded %s model=%d pos=%.3f,%.3f,%.3f input=%.6g,%.6g",
      reason, int(Read<int16_t>(d.vehicle, 0x6a)), Read<float>(d.vehicle, 0x38),
      Read<float>(d.vehicle, 0x3c), Read<float>(d.vehicle, 0x40), d.desired, d.heading);
 }
}
float Finite(float angle, float fallback, const Diagnostic &d, const char *reason)
{
 if (std::isfinite(angle)) return angle;
 Report(d, reason); return std::isfinite(fallback) ? Wrap(fallback) : 0.0f;
}
float Select(float desired, float heading, float left, float right)
{
 const float middle = Wrap(desired + 0.5f * Wrap(heading - desired));
 const float l = std::fabs(Wrap(left - middle)), r = std::fabs(Wrap(right - middle));
 if (l > kPi * 0.5f && r > kPi * 0.5f) return middle;
 if (std::fabs(l - r) < 0.08f) return right;
 return l < r ? left : right;
}
struct Bounds { float minX, maxX, minY, maxY; };
bool Inside(float x, float y, const Bounds &b)
{
 return std::isfinite(x) && std::isfinite(y) && x >= b.minX && x <= b.maxX && y >= b.minY && y <= b.maxY;
}
const uint8_t *Collision(uint8_t *vehicle)
{
 const int model = Read<int16_t>(vehicle, 0x6a);
 return model >= 0 && model < 6500 && game::api.modelInfoPtrs[model] ?
     Read<uint8_t *>(game::api.modelInfoPtrs[model], 0x20) : nullptr;
}
bool ObjectProjection(uint8_t *entity, uint8_t *vehicle, const Diagnostic &diagnostic)
{
 // These are the five offset light models in native WeaveForObject. Trees
 // and palms use the entity origin. Check its exact projected point before
 // native width/distance and repeated-add angle normalization can see zero.
 const uint16_t model = Read<uint16_t>(entity, 0x6a);
 float right = 0.0f, forward = 0.0f;
 if (model == *gObjectIds[0]) { right = 2.957f; forward = 0.147f; }
 else if (model == *gObjectIds[1]) right = 0.744f;
 else if (model == *gObjectIds[2]) right = 0.043f;
 else if (model == *gObjectIds[3]) { right = 1.143f; forward = 0.145f; }
 else if (model == *gObjectIds[4]) forward = -0.048f;
 const float rx = Read<float>(entity, 8) * right, ry = Read<float>(entity, 0xc) * right;
 const float px = rx + Read<float>(entity, 0x38), py = ry + Read<float>(entity, 0x3c);
 const float fx = Read<float>(entity, 0x18) * forward, fy = Read<float>(entity, 0x1c) * forward;
 const float dx = (px + fx) - Read<float>(vehicle, 0x38), dy = (py + fy) - Read<float>(vehicle, 0x3c);
 const float distance = std::sqrt(std::fma(dx, dx, dy * dy));
 const auto *collision = Collision(vehicle);
 const float width = collision ? Read<float>(collision, 0x1c) : NAN;
 const float half = (std::fma(width, 2.4f, 0.3f) / distance) * 0.5f;
 if (!(distance > 0.0f) || !std::isfinite(half) || std::fabs(half) > 10000.0f) {
  Report(diagnostic, "object-projection"); return false;
 }
 return true;
}
float PedAngle(float x, float y)
{
 // The inline native GetATanOfXY uses a ratio and fixed quadrant constants.
 // Keep that rounding (and its +tau representation on the positive X axis).
 const auto atan = [](float ratio) { return std::atan2(ratio, 1.0f); };
 if (x == 0.0f && y == 0.0f) return 0.0f;
 if (std::fabs(x) < std::fabs(y)) {
  if (y > 0.0f) return x > 0.0f ? kPi * 0.5f - atan(x / y) : kPi * 0.5f + atan(-x / y);
  return x > 0.0f ? kPi * 1.5f + atan(x / -y) : kPi * 1.5f - atan(x / y);
 }
 if (y > 0.0f) return x > 0.0f ? atan(y / x) : kPi - atan(y / -x);
 return x > 0.0f ? kTau - atan(-y / x) : kPi + atan(y / x);
}
void Ped(uint8_t *ped, uint8_t *vehicle, const Bounds &bounds, float &left, float &right, const Diagnostic &diagnostic)
{
 const float x = Read<float>(ped, 0x38), y = Read<float>(ped, 0x3c);
 if (!Inside(x, y, bounds) || !(std::fabs(Read<float>(ped, 0x40) - Read<float>(vehicle, 0x40)) < 4.0f) ||
     Read<uint8_t *>(ped, 0x498) == vehicle || Read<uint8_t *>(ped, 0x720) == vehicle) return;
 if (vehicle[0x19e] == 3 && game::api.FindPlayerPed() == ped) return;
 const float dx = x - Read<float>(vehicle, 0x38), dy = y - Read<float>(vehicle, 0x3c);
 const float distance = std::sqrt(std::fma(dx, dx, dy * dy));
 if (!(distance > 0.0f) || !std::isfinite(distance)) { Report(diagnostic, "ped-overlap"); return; }
 const auto *collision = Collision(vehicle);
 if (!collision) return;
 const float width = Read<float>(collision, 0x1c);
 const float half = (std::fma(width, 2.4f, 0.8f) / distance) * 0.5f;
 if (!std::isfinite(half)) { Report(diagnostic, "ped-width"); return; }
 const float angle = PedAngle(dx, dy);
 if (std::fabs(Wrap(angle - left)) < half) left = PedLeft(angle - half);
 if (std::fabs(Wrap(angle - right)) < half) right = PedRight(angle + half);
}

float Find(uint8_t *vehicle, uint8_t *ignored, float desired, float heading)
{
 const Diagnostic diagnostic{vehicle, desired, heading};
 if (!std::isfinite(desired) && !std::isfinite(heading)) {
  const float fx = Read<float>(vehicle, 0x18), fy = Read<float>(vehicle, 0x1c);
  heading = std::isfinite(fx) && std::isfinite(fy) ? std::atan2(fy, fx) : 0.0f;
 }
 desired = Finite(desired, heading, diagnostic, "input-desired");
 heading = Finite(heading, desired, diagnostic, "input-heading");
 if (desired != kNoAngle && std::fabs(desired) >= 100.0f) { Report(diagnostic, "input-magnitude"); desired = Wrap(desired); }
 if (std::fabs(heading) >= 100.0f) { Report(diagnostic, "heading-magnitude"); heading = Wrap(heading); }
 float left = desired, right = desired;
 const float x = Read<float>(vehicle, 0x38), y = Read<float>(vehicle, 0x3c);
 const float vx = Read<float>(vehicle, 0x84), vy = Read<float>(vehicle, 0x88);
 float speed = std::sqrt(std::fma(vx, vx, vy * vy));
 if (!std::isfinite(speed)) speed = 0.0f;
 const float radius = std::min(24.0f, (speed / 0.4f + 1.0f) * 12.0f);
 const Bounds bounds{x - radius, x + radius, y - radius, y + radius};
 uint16_t scan = uint16_t(*gScan + 1);
 *gScan = scan;
 if (!scan) { gClearScan(); scan = 1; *gScan = scan; }
 // Retain native truncation toward zero and its one-sided interval clamps,
 // including the first outside south/west strip. Saturating before integer
 // conversion also handles huge positions without undefined C++ conversion.
 if (!std::isfinite(x) || !std::isfinite(y)) return desired;
 const auto index = [](float value, float offset) { return int(std::clamp((value + offset) / 50.0f, -1000.0f, 1000.0f)); };
 const int minX = std::max(0, index(bounds.minX, 2400.0f)), maxX = std::min(79, index(bounds.maxX, 2400.0f));
 const int minY = std::max(0, index(bounds.minY, 2000.0f)), maxY = std::min(79, index(bounds.maxY, 2000.0f));
 if (minX > maxX || minY > maxY) return desired;
 if (desired == kNoAngle) return Select(desired, heading, left, right);
 // The native helpers only change these two candidates. Retain its fixed-point
 // pass and scan-code deduplication, with an explicit bound for unordered or
 // unexpectedly mutating callbacks. Ordinary native traffic needs <=2 passes.
 size_t remainingNodes = 16384;
 for (int pass = 0; pass < 4; ++pass) {
  const float previousLeft = left, previousRight = right;
  for (int row = minY; row <= maxY; ++row) for (int column = minX; column <= maxX; ++column) {
   auto &sector = gSectors[row * 80 + column];
   for (int list : {4, 5, 6, 7, 2, 3}) for (Node *node = sector.lists[list]; node;) {
    if (!remainingNodes--) { Report(diagnostic, "node-cycle-budget"); return Select(desired, heading, left, right); }
    uint8_t *entity = node->entity; node = node->next;
    if (Read<uint16_t>(entity, 0x66) == scan || !(Read<uint32_t>(entity, 0x60) & 0x100) || (list >= 4 && entity == ignored)) continue;
    std::memcpy(entity + 0x66, &scan, sizeof scan);
    if (list == 6 || list == 7) { Ped(entity, vehicle, bounds, left, right, diagnostic); continue; }
    if (list == 4 || list == 5) {
     const CVector centre = gCentre(entity);
     if (!Inside(centre.x, centre.y, bounds) || entity == vehicle ||
         !(std::fabs(Read<float>(entity, 0x40) - Read<float>(vehicle, 0x40)) < 4.0f) ||
         ((vehicle[0x298] & 0x20) && (entity[0x298] & 0x20))) continue;
    } else if (!Inside(Read<float>(entity, 0x38), Read<float>(entity, 0x3c), bounds) ||
               !(std::fabs(Read<float>(entity, 0x40) - Read<float>(vehicle, 0x40)) < 8.0f) ||
               !(Read<float>(entity, 0x30) > 0.9f)) continue;
    if (list < 4 && !ObjectProjection(entity, vehicle, diagnostic)) continue;
    const float savedLeft = left, savedRight = right;
    (list == 4 || list == 5 ? gCar : gObject)(entity, vehicle, &left, &right);
    // A malformed actor's helper may return NaN. Preserve every finite side
    // already found instead of feeding unordered values to the convergence test.
    left = Finite(left, savedLeft, diagnostic, list < 4 ? "object-left" : "car-left");
    right = Finite(right, savedRight, diagnostic, list < 4 ? "object-right" : "car-right");
   }
  }
  if (left == previousLeft && right == previousRight) return Select(desired, heading, left, right);
 }
 Report(diagnostic, "pass-budget");
 return Select(desired, heading, left, right);
}
bool Verified()
{
 const auto *base = static_cast<const uint8_t *>(hooks::ImageBase());
 return base && Read<uint32_t>(base, 0x57224) == 0x1e232020 &&
        Read<uint32_t>(base, 0x57228) == 0x1e200440 && Read<uint32_t>(base, 0x5729c) == 0xf9401397;
}
} // namespace

bool Install()
{
 const bool ready = Verified() && game::api.modelInfoPtrs && game::api.FindPlayerPed &&
     Bind(gSectors, "_ZN6CWorld11ms_aSectorsE") && Bind(gScan, "_ZN6CWorld19ms_nCurrentScanCodeE") &&
     Bind(gClearScan, "_ZN6CWorld14ClearScanCodesEv") && Bind(gCentre, "_ZN7CEntity14GetBoundCentreEv") &&
     Bind(gCar, "_ZN8CCarCtrl16WeaveForOtherCarEP7CEntityP8CVehiclePfS4_") &&
     Bind(gObject, "_ZN8CCarCtrl14WeaveForObjectEP7CEntityP8CVehiclePfS4_") &&
     Bind(gObjectIds[0], "MI_TRAFFICLIGHTS") && Bind(gObjectIds[1], "MI_SINGLESTREETLIGHTS1") &&
     Bind(gObjectIds[2], "MI_SINGLESTREETLIGHTS2") && Bind(gObjectIds[3], "MI_SINGLESTREETLIGHTS3") &&
     Bind(gObjectIds[4], "MI_DOUBLESTREETLIGHTS");
 if (!ready || !hooks::Install(kSymbol, Find, gOriginal)) {
  Log("trafficweave: verified native avoidance hook missing; boat/car convergence remains native"); return false;
 }
 return true;
}
} // namespace trafficweave
